#ifndef PALMDEVICEMONITOR_H
#define PALMDEVICEMONITOR_H

#include <QObject>
#include <QStringList>
#include <QMap>

struct udev;
struct udev_monitor;
class QSocketNotifier;

/**
 * @brief Monitors udev for Palm USB device attach/detach events.
 *
 * Watches for USB devices with idVendor=0830 (Palm, Inc.).
 * When the visor driver creates ttyUSB ports, emits palmDetected()
 * with the list of port paths.
 */
class PalmDeviceMonitor : public QObject
{
    Q_OBJECT

public:
    explicit PalmDeviceMonitor(QObject *parent = nullptr);
    ~PalmDeviceMonitor() override;

    bool start();
    void stop();
    bool isRunning() const { return m_running; }

    /// Shakedown F7: a device plugged in BEFORE start() produces no udev
    /// "add" event, so it was never detected. This scans already-present
    /// tty devices for Palm hardware and emits palmDetected() per hit.
    void enumerateExistingDevices();

Q_SIGNALS:
    /** Emitted when a Palm USB device is detected. Ports are all
     *  ttyUSB paths created for this device (typically 2). */
    void palmDetected(const QStringList &ports, const QString &usbSerial);

    /** Emitted when the Palm USB device is disconnected. */
    void palmDisconnected(const QString &usbSerial);

    /** Emitted on monitor errors. */
    void monitorError(const QString &error);

private Q_SLOTS:
    void onUdevEvent();

private:
    void collectPalmPorts(const QString &syspath, QStringList &ports);

    struct udev *m_udev = nullptr;
    struct udev_monitor *m_monitor = nullptr;
    QSocketNotifier *m_notifier = nullptr;
    bool m_running = false;

    // Track detected devices: USB syspath -> serial number
    QMap<QString, QString> m_detectedDevices;
};

#endif // PALMDEVICEMONITOR_H
