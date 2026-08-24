#include "palmdevicemonitor.h"

#include <QDebug>
#include <QDir>
#include <QSocketNotifier>
#include <QTimer>

#include <libudev.h>

// Palm, Inc. USB vendor ID
static const char *PALM_VENDOR_ID = "0830";

PalmDeviceMonitor::PalmDeviceMonitor(QObject *parent)
    : QObject(parent)
{
}

PalmDeviceMonitor::~PalmDeviceMonitor()
{
    stop();
}

bool PalmDeviceMonitor::start()
{
    if (m_running) {
        return true;
    }

    // Initialize udev
    m_udev = udev_new();
    if (!m_udev) {
        Q_EMIT monitorError(QStringLiteral("Failed to create udev context"));
        return false;
    }

    // Create monitor for tty subsystem events from udev (post-processing)
    m_monitor = udev_monitor_new_from_netlink(m_udev, "udev");
    if (!m_monitor) {
        Q_EMIT monitorError(QStringLiteral("Failed to create udev monitor"));
        udev_unref(m_udev);
        m_udev = nullptr;
        return false;
    }

    // Filter to tty subsystem only - we care about ttyUSB port creation/removal
    if (udev_monitor_filter_add_match_subsystem_devtype(m_monitor, "tty", nullptr) < 0) {
        Q_EMIT monitorError(QStringLiteral("Failed to add udev filter for tty subsystem"));
        udev_monitor_unref(m_monitor);
        m_monitor = nullptr;
        udev_unref(m_udev);
        m_udev = nullptr;
        return false;
    }

    if (udev_monitor_enable_receiving(m_monitor) < 0) {
        Q_EMIT monitorError(QStringLiteral("Failed to enable udev monitor"));
        udev_monitor_unref(m_monitor);
        m_monitor = nullptr;
        udev_unref(m_udev);
        m_udev = nullptr;
        return false;
    }

    int fd = udev_monitor_get_fd(m_monitor);
    if (fd < 0) {
        Q_EMIT monitorError(QStringLiteral("Failed to get udev monitor file descriptor"));
        udev_monitor_unref(m_monitor);
        m_monitor = nullptr;
        udev_unref(m_udev);
        m_udev = nullptr;
        return false;
    }

    m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &PalmDeviceMonitor::onUdevEvent);

    m_running = true;
    qDebug() << "PalmDeviceMonitor: started monitoring for Palm USB devices";

    // Shakedown F7: udev only reports events from now on. A Palm that was
    // already plugged in before launch would never be detected. Scan what
    // is already present.
    enumerateExistingDevices();

    return true;
}

void PalmDeviceMonitor::enumerateExistingDevices()
{
    if (!m_udev) {
        return;
    }

    struct udev_enumerate *enumerate = udev_enumerate_new(m_udev);
    if (!enumerate) {
        return;
    }

    udev_enumerate_add_match_subsystem(enumerate, "tty");
    udev_enumerate_scan_devices(enumerate);

    // Group tty ports by their owning Palm USB device (syspath -> serial).
    QMap<QString, QPair<QString, QStringList>> found;
    struct udev_list_entry *entry;
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumerate)) {
        const char *entryPath = udev_list_entry_get_name(entry);
        struct udev_device *ttyDev = udev_device_new_from_syspath(m_udev, entryPath);
        if (!ttyDev) {
            continue;
        }

        struct udev_device *parent = ttyDev;
        while (parent) {
            const char *parentSubsystem = udev_device_get_subsystem(parent);
            if (parentSubsystem && qstrcmp(parentSubsystem, "usb") == 0) {
                const char *devtype = udev_device_get_devtype(parent);
                if (devtype && qstrcmp(devtype, "usb_device") == 0) {
                    const char *vendor = udev_device_get_sysattr_value(parent, "idVendor");
                    if (vendor && qstrcmp(vendor, PALM_VENDOR_ID) == 0) {
                        const QString syspath =
                            QString::fromLatin1(udev_device_get_syspath(parent));
                        const char *serial = udev_device_get_sysattr_value(parent, "serial");
                        const QString serialStr =
                            serial ? QString::fromLatin1(serial) : QString();
                        const char *devnode = udev_device_get_devnode(ttyDev);
                        if (devnode) {
                            found[syspath].first = serialStr;
                            found[syspath].second.append(
                                QString::fromLatin1(devnode));
                        }
                    }
                    break;
                }
            }
            parent = udev_device_get_parent(parent);
        }

        udev_device_unref(ttyDev);
    }

    udev_enumerate_unref(enumerate);

    for (auto it = found.constBegin(); it != found.constEnd(); ++it) {
        if (m_detectedDevices.contains(it.key())) {
            continue;   // already tracked via a live udev event
        }
        m_detectedDevices.insert(it.key(), it.value().first);
        QStringList ports = it.value().second;
        ports.sort();
        qDebug() << "PalmDeviceMonitor: pre-existing Palm device at" << it.key()
                 << "ports:" << ports << "serial:" << it.value().first;
        Q_EMIT palmDetected(ports, it.value().first);
    }
}

void PalmDeviceMonitor::stop()
{
    if (!m_running) {
        return;
    }

    m_running = false;

    delete m_notifier;
    m_notifier = nullptr;

    if (m_monitor) {
        udev_monitor_unref(m_monitor);
        m_monitor = nullptr;
    }

    if (m_udev) {
        udev_unref(m_udev);
        m_udev = nullptr;
    }

    m_detectedDevices.clear();

    qDebug() << "PalmDeviceMonitor: stopped";
}

void PalmDeviceMonitor::onUdevEvent()
{
    struct udev_device *dev = udev_monitor_receive_device(m_monitor);
    if (!dev) {
        return;
    }

    const char *action = udev_device_get_action(dev);
    const char *subsystem = udev_device_get_subsystem(dev);
    const char *devnode = udev_device_get_devnode(dev);

    if (!action || !subsystem) {
        udev_device_unref(dev);
        return;
    }

    const QString actionStr = QString::fromLatin1(action);
    const QString devnodeStr = devnode ? QString::fromLatin1(devnode) : QString();

    qDebug() << "PalmDeviceMonitor: udev event:" << actionStr
             << "subsystem:" << QString::fromLatin1(subsystem)
             << "devnode:" << devnodeStr;

    if (actionStr == QLatin1String("add")) {
        // Walk parent devices to find the USB device with Palm vendor ID
        struct udev_device *parent = dev;
        struct udev_device *usbDevice = nullptr;

        while (parent) {
            const char *parentSubsystem = udev_device_get_subsystem(parent);
            if (parentSubsystem && qstrcmp(parentSubsystem, "usb") == 0) {
                const char *devtype = udev_device_get_devtype(parent);
                if (devtype && qstrcmp(devtype, "usb_device") == 0) {
                    const char *vendor = udev_device_get_sysattr_value(parent, "idVendor");
                    if (vendor && qstrcmp(vendor, PALM_VENDOR_ID) == 0) {
                        usbDevice = parent;
                        break;
                    }
                }
            }
            parent = udev_device_get_parent(parent);
        }

        if (usbDevice) {
            const char *syspath = udev_device_get_syspath(usbDevice);
            const char *serial = udev_device_get_sysattr_value(usbDevice, "serial");

            const QString usbSyspath = QString::fromLatin1(syspath);
            const QString usbSerial = serial ? QString::fromLatin1(serial) : QString();

            qDebug() << "PalmDeviceMonitor: Palm device detected at" << usbSyspath
                     << "serial:" << usbSerial;

            // If we haven't already started collecting ports for this device,
            // schedule a delayed collection. The visor driver creates two ttyUSB
            // ports in separate udev events, so we wait briefly for both to appear.
            if (!m_detectedDevices.contains(usbSyspath)) {
                m_detectedDevices.insert(usbSyspath, usbSerial);

                // Capture values for the lambda
                const QString capturedSyspath = usbSyspath;
                const QString capturedSerial = usbSerial;

                QTimer::singleShot(100, this, [this, capturedSyspath, capturedSerial]() {
                    if (!m_running) {
                        return;
                    }

                    QStringList ports;
                    collectPalmPorts(capturedSyspath, ports);

                    if (!ports.isEmpty()) {
                        ports.sort();
                        qDebug() << "PalmDeviceMonitor: Palm ports ready:" << ports
                                 << "serial:" << capturedSerial;
                        Q_EMIT palmDetected(ports, capturedSerial);
                    } else {
                        qWarning() << "PalmDeviceMonitor: no ttyUSB ports found for"
                                   << capturedSyspath;
                    }
                });
            }
        }
    } else if (actionStr == QLatin1String("remove")) {
        // For remove events, we need to check if this ttyUSB port belongs to
        // a tracked Palm device. Since the device is being removed, we can't
        // walk the parent tree - instead check our tracking map.
        //
        // We check by seeing if the removed device's syspath starts with
        // any of our tracked USB device syspaths.
        const char *syspath = udev_device_get_syspath(dev);
        const QString removedSyspath = syspath ? QString::fromLatin1(syspath) : QString();

        for (auto it = m_detectedDevices.begin(); it != m_detectedDevices.end(); ++it) {
            if (removedSyspath.startsWith(it.key())) {
                const QString serial = it.value();
                qDebug() << "PalmDeviceMonitor: Palm device disconnected, serial:" << serial;
                m_detectedDevices.erase(it);
                Q_EMIT palmDisconnected(serial);
                break;
            }
        }
    }

    udev_device_unref(dev);
}

void PalmDeviceMonitor::collectPalmPorts(const QString &syspath, QStringList &ports)
{
    // Enumerate all tty devices and find those whose parent USB device
    // matches the given syspath.
    struct udev_enumerate *enumerate = udev_enumerate_new(m_udev);
    if (!enumerate) {
        return;
    }

    udev_enumerate_add_match_subsystem(enumerate, "tty");
    udev_enumerate_scan_devices(enumerate);

    struct udev_list_entry *entry;
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumerate)) {
        const char *entryPath = udev_list_entry_get_name(entry);
        struct udev_device *ttyDev = udev_device_new_from_syspath(m_udev, entryPath);
        if (!ttyDev) {
            continue;
        }

        // Walk up to find the USB device parent
        struct udev_device *parent = ttyDev;
        while (parent) {
            const char *parentSubsystem = udev_device_get_subsystem(parent);
            if (parentSubsystem && qstrcmp(parentSubsystem, "usb") == 0) {
                const char *devtype = udev_device_get_devtype(parent);
                if (devtype && qstrcmp(devtype, "usb_device") == 0) {
                    const char *parentSyspath = udev_device_get_syspath(parent);
                    if (parentSyspath && syspath == QString::fromLatin1(parentSyspath)) {
                        const char *devnode = udev_device_get_devnode(ttyDev);
                        if (devnode) {
                            ports.append(QString::fromLatin1(devnode));
                        }
                    }
                    break;
                }
            }
            parent = udev_device_get_parent(parent);
        }

        udev_device_unref(ttyDev);
    }

    udev_enumerate_unref(enumerate);
}
