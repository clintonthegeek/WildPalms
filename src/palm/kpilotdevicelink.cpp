#include "kpilotdevicelink.h"
#include "pilotrecord.h"

// pilot-link headers
#include <pi-source.h>
#include <pi-socket.h>
#include <pi-dlp.h>
#include <pi-file.h>
#include <pi-buffer.h>
#include <pi-debug.h>

#include <QDebug>
#include <QFile>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <cstring>

// POSIX — for raw port probing before handing off to pilot-link
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <termios.h>
#include <sys/ioctl.h>

// ============================================================================
// ConnectionWorker - runs blocking pilot-link calls in a separate thread
//
// Two-phase connection:
//   Phase 1 (probe): Opens all ttyUSB ports simultaneously with raw POSIX
//     and polls for incoming data.  Only the HotSync port receives CMP
//     wakeup data from the Palm; the debug/console port stays silent.
//     No protocol bytes are sent, so we can't crash the Palm.
//   Phase 2 (connect): Uses pilot-link pi_accept_to() on the identified
//     HotSync port only.  The wakeup packet is safe on the HotSync port.
// ============================================================================

ConnectionWorker::ConnectionWorker(const QStringList &devicePaths,
                                   int timeoutSeconds,
                                   QObject *parent)
    : QObject(parent)
    , m_devicePaths(devicePaths)
    , m_timeoutSeconds(timeoutSeconds)
    , m_cancelRequested(false)
{
    qDebug() << "[ConnectionWorker] Created for ports:" << devicePaths
             << "timeout:" << timeoutSeconds << "s";
}

ConnectionWorker::~ConnectionWorker()
{
    qDebug() << "[ConnectionWorker] Destroyed";
}

void ConnectionWorker::requestCancel()
{
    qDebug() << "[ConnectionWorker] Cancel requested";
    m_cancelRequested = true;
}

/**
 * Phase 1: Probe all ports simultaneously to find the HotSync port.
 *
 * Opens every ttyUSB port with raw POSIX, configures serial parameters
 * to match pilot-link (raw, 9600 baud, 8N1, CLOCAL), and polls for
 * incoming data.  Only the HotSync port receives CMP data from the
 * Palm; the debug/console port stays silent.
 *
 * All ports are opened simultaneously because the Palm's USB controller
 * may require all endpoints to be active before starting the protocol.
 *
 * No protocol bytes are written, so this cannot crash the Palm.
 *
 * Returns the device path that has data, or empty if none responded.
 */
ConnectionWorker::ProbeResult ConnectionWorker::probeForActivePort()
{
    qDebug() << "[ConnectionWorker] Probing" << m_devicePaths.size()
             << "port(s) for HotSync data (timeout" << m_timeoutSeconds << "s)";

    struct ProbePort {
        int fd;
        QString path;
    };

    QVector<ProbePort> probes;
    QVector<struct pollfd> pfds;

    // Open all ports at once and configure serial like pilot-link's s_open()
    for (const QString &port : m_devicePaths) {
        int fd = ::open(port.toUtf8().constData(), O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            qDebug() << "[ConnectionWorker] Cannot open" << port
                     << "for probing (errno:" << errno << ")";
            continue;
        }

        // Match pilot-link's serial configuration:
        // raw mode, 9600 baud, 8N1, CLOCAL (ignore modem control)
        struct termios tcn;
        if (tcgetattr(fd, &tcn) == 0) {
            tcn.c_oflag = 0;
            tcn.c_iflag = IGNBRK | IGNPAR;
            tcn.c_cflag = CREAD | CLOCAL | CS8;
            cfsetspeed(&tcn, B9600);
            tcn.c_lflag = NOFLSH;
            cfmakeraw(&tcn);
            for (int i = 0; i < NCCS; i++)
                tcn.c_cc[i] = 0;
            tcn.c_cc[VMIN] = 1;
            tcn.c_cc[VTIME] = 0;
            tcsetattr(fd, TCSANOW, &tcn);
        }

        struct pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfds.append(pfd);
        probes.append({fd, port});

        qDebug() << "[ConnectionWorker] Opened" << port << "for probing (fd:" << fd << ")";
    }

    if (probes.isEmpty()) {
        qWarning() << "[ConnectionWorker] Could not open any ports for probing";
        return {};
    }

    emit statusUpdate(QString("Probing %1 port(s)...").arg(probes.size()));

    // Poll all ports simultaneously.  Check for cancellation every 250ms.
    ProbeResult result;
    int totalMs = m_timeoutSeconds * 1000;
    int elapsed = 0;
    const int pollInterval = 250;

    while (elapsed < totalMs && !m_cancelRequested && result.port.isEmpty()) {
        int remaining = qMin(pollInterval, totalMs - elapsed);
        int ret = ::poll(pfds.data(), pfds.size(), remaining);

        if (ret > 0) {
            for (int i = 0; i < pfds.size(); ++i) {
                if (pfds[i].revents & POLLIN) {
                    result.port = probes[i].path;
                    result.fd = probes[i].fd;
                    break;
                }
            }
        }
        elapsed += remaining;
    }

    // Close ONLY non-active probe fds.  The active port's fd stays open
    // so its CMP data remains in the kernel tty buffer — two fds to the
    // same tty share the input buffer, and closing the LAST fd flushes it.
    // pilot-link's pi_bind() will open its own fd, then we close ours.
    for (auto &p : probes) {
        if (p.fd != result.fd) {
            ::close(p.fd);
        }
    }

    if (!result.port.isEmpty()) {
        qDebug() << "[ConnectionWorker] HotSync data detected on" << result.port
                 << "after" << elapsed << "ms (keeping probe fd" << result.fd << "open)";
    } else if (m_cancelRequested) {
        qDebug() << "[ConnectionWorker] Probe cancelled";
    } else {
        qDebug() << "[ConnectionWorker] No HotSync data on any port after"
                 << totalMs << "ms";
    }

    return result;
}

/**
 * Phase 2: Connect via pilot-link on the identified HotSync port.
 */
void ConnectionWorker::doConnect()
{
    qDebug() << "[ConnectionWorker] doConnect() starting on thread:" << QThread::currentThread();
    qDebug() << "[ConnectionWorker] Ports to try:" << m_devicePaths;

    // Phase 1: Probe all ports to find the HotSync port.
    // The probe keeps the active port's fd open so CMP data stays in the
    // kernel tty buffer.  We close it after pi_bind() opens its own fd.
    QString activePort;
    int probeFd = -1;  // Probe fd to close after pi_bind

    if (m_devicePaths.size() == 1) {
        activePort = m_devicePaths.first();
        qDebug() << "[ConnectionWorker] Single port, skipping probe:" << activePort;
    } else {
        ProbeResult probe = probeForActivePort();
        activePort = probe.port;
        probeFd = probe.fd;
    }

    if (activePort.isEmpty()) {
        if (m_cancelRequested) {
            emit connectionFailed("Connection cancelled");
        } else {
            // Shakedown F6: plugging USB without pressing the HotSync
            // button lands here — tell the user what to do about it.
            QString error = QString(
                "No HotSync data detected on any of %1 port(s). "
                "Press the HotSync button on your Palm, then try again.")
                .arg(m_devicePaths.size());
            qWarning() << "[ConnectionWorker]" << error;
            emit connectionFailed(error);
        }
        return;
    }

    if (m_cancelRequested) {
        if (probeFd >= 0) ::close(probeFd);
        emit connectionFailed("Connection cancelled");
        return;
    }

    // Phase 2: Connect via pilot-link on the identified HotSync port.
    // pi_accept_to() with timeout is safe here — the wakeup packet goes
    // to the HotSync port (which expects it), not the debug port.
    qDebug() << "[ConnectionWorker] Connecting on HotSync port:" << activePort;
    emit statusUpdate(QString("Connecting on %1...").arg(activePort));

    // Close the probe fd BEFORE pilot-link opens the tty.  USB serial
    // drivers (visor, usb-serial) don't handle two concurrent fds to the
    // same port during CMP handshake.  Closing flushes the CMP wakeup
    // data, but pi_serial_accept() has a Linux workaround that sends a
    // wakeup packet when no data is available within 1 second.
    if (probeFd >= 0) {
        qDebug() << "[ConnectionWorker] Closing probe fd" << probeFd
                 << "before pilot-link takes over";
        ::close(probeFd);
        probeFd = -1;
    }

    int sock = pi_socket(PI_AF_PILOT, PI_SOCK_STREAM, PI_PF_DLP);
    if (sock < 0) {
        QString error = QString("Failed to create pilot-link socket (errno: %1)").arg(errno);
        qWarning() << "[ConnectionWorker]" << error;
        emit connectionFailed(error);
        return;
    }

    int bindResult = pi_bind(sock, activePort.toUtf8().constData());

    if (bindResult < 0) {
        QString error = QString("Failed to bind to %1 (result: %2)")
            .arg(activePort).arg(bindResult);
        qWarning() << "[ConnectionWorker]" << error;
        pi_close(sock);
        emit connectionFailed(error);
        return;
    }

    int listenResult = pi_listen(sock, 1);
    if (listenResult < 0) {
        QString error = QString("Failed to listen on %1 (result: %2)")
            .arg(activePort).arg(listenResult);
        qWarning() << "[ConnectionWorker]" << error;
        pi_close(sock);
        emit connectionFailed(error);
        return;
    }

    qDebug() << "[ConnectionWorker] Calling pi_accept_to() on" << activePort
             << "with timeout" << m_timeoutSeconds << "s";

    int acceptResult = pi_accept_to(sock, nullptr, nullptr, m_timeoutSeconds);

    if (acceptResult >= 0) {
        if (m_cancelRequested) {
            qDebug() << "[ConnectionWorker] Connected but cancel was requested, closing";
            pi_close(acceptResult);
            emit connectionFailed("Connection cancelled");
            return;
        }

        qDebug() << "[ConnectionWorker] Connected on" << activePort
                 << "accept result:" << acceptResult;
        emit statusUpdate("Device connected!");

        // Perform initial DLP reads on the same thread as pi_accept_to(),
        // matching how pilot-xfer (plu_connect) does it.  This avoids
        // cross-thread socket usage for the first protocol exchanges and
        // keeps timing tight (no event-loop hop between CMP and DLP).
        HandshakeResult result;
        result.socket = acceptResult;

        // Read user info first (needed for device identification)
        struct PilotUser pilotUser;
        memset(&pilotUser, 0, sizeof(pilotUser));
        if (dlp_ReadUserInfo(acceptResult, &pilotUser) >= 0) {
            result.userInfoValid = true;
            result.userName = QString::fromLatin1(pilotUser.username);
            result.userId = pilotUser.userID;
            qDebug() << "[ConnectionWorker] ReadUserInfo OK - user:" << result.userName
                     << "id:" << result.userId;
        } else {
            qDebug() << "[ConnectionWorker] ReadUserInfo failed (non-fatal)";
        }

        // Read system info
        struct SysInfo sysInfo;
        memset(&sysInfo, 0, sizeof(sysInfo));
        if (dlp_ReadSysInfo(acceptResult, &sysInfo) >= 0) {
            result.sysInfoValid = true;
            result.romVersion = sysInfo.romVersion;
            result.productId = QString::fromLatin1(sysInfo.prodID);
            qDebug() << "[ConnectionWorker] ReadSysInfo OK - romVersion:" << result.romVersion;
        } else {
            qDebug() << "[ConnectionWorker] ReadSysInfo failed (non-fatal)";
        }

        // Read storage info (card 0 = internal storage)
        struct CardInfo cardInfo;
        memset(&cardInfo, 0, sizeof(cardInfo));
        if (dlp_ReadStorageInfo(acceptResult, 0, &cardInfo) >= 0) {
            result.storageInfoValid = true;
            result.cardName = QString::fromLatin1(cardInfo.name);
            result.manufacturer = QString::fromLatin1(cardInfo.manufacturer);
            result.romSize = cardInfo.romSize;
            result.ramSize = cardInfo.ramSize;
            result.ramFree = cardInfo.ramFree;
            qDebug() << "[ConnectionWorker] ReadStorageInfo OK - name:" << result.cardName
                     << "manufacturer:" << result.manufacturer
                     << "ROM:" << result.romSize << "RAM:" << result.ramSize
                     << "free:" << result.ramFree;
        } else {
            qDebug() << "[ConnectionWorker] ReadStorageInfo failed (non-fatal)";
        }

        // NOTE: OpenConduit is NOT called here. It belongs in PalmRuntime's
        // sync flow (hotSync/fullSync/etc.) which handles it before each
        // sync operation.

        emit connectionEstablished(result);
        return;
    }

    // pi_accept_to() auto-closes the socket on failure
    if (m_cancelRequested) {
        emit connectionFailed("Connection cancelled");
    } else {
        QString error = QString("Handshake failed on %1 (result: %2)")
            .arg(activePort).arg(acceptResult);
        qWarning() << "[ConnectionWorker]" << error;
        emit connectionFailed(error);
    }
}

// ============================================================================
// KPilotDeviceLink
// ============================================================================

KPilotDeviceLink::KPilotDeviceLink(const QStringList &devicePaths, QObject *parent)
    : KPilotLink(parent)
    , m_devicePaths(devicePaths)
    , m_socket(-1)
    , m_isConnected(false)
    , m_workerThread(nullptr)
    , m_worker(nullptr)
{
    qDebug() << "[KPilotDeviceLink] Initialized for ports:" << devicePaths;
    emit logMessage(QString("Initialized device link for: %1").arg(devicePaths.join(QStringLiteral(", "))));
}

KPilotDeviceLink::~KPilotDeviceLink()
{
    qDebug() << "[KPilotDeviceLink] Destructor called";
    closeConnection();
}

void KPilotDeviceLink::cleanupWorker()
{
    qDebug() << "[KPilotDeviceLink] cleanupWorker() called";

    if (m_worker) {
        qDebug() << "[KPilotDeviceLink] Requesting worker cancellation";
        m_worker->requestCancel();
    }

    if (m_workerThread) {
        qDebug() << "[KPilotDeviceLink] Waiting for worker thread to finish...";
        m_workerThread->quit();
        // pi_accept_to() uses a bounded timeout (default 5s per port), so
        // the thread will exit on its own.  Allow generous headroom.
        if (!m_workerThread->wait(20000)) {
            qWarning() << "[KPilotDeviceLink] Worker thread did not finish in 20s, terminating";
            m_workerThread->terminate();
            m_workerThread->wait(2000);
        }
        qDebug() << "[KPilotDeviceLink] Worker thread finished";

        delete m_workerThread;
        m_workerThread = nullptr;
    }

    // Worker is deleted by thread's finished signal via deleteLater
    m_worker = nullptr;
}

void KPilotDeviceLink::cancelConnection()
{
    qDebug() << "[KPilotDeviceLink] cancelConnection() called";

    if (!m_worker) {
        qDebug() << "[KPilotDeviceLink] No active connection attempt to cancel";
        return;
    }

    emit logMessage("Cancelling connection attempt...");

    // Set the cancel flag.  The worker thread checks this between port
    // attempts and exits within at most one pi_accept_to() timeout period
    // (default 1 second).  No need to force-close the socket — the bounded
    // timeout guarantees the thread becomes responsive.
    cleanupWorker();
    setStatus(Init);
    emit logMessage("Connection cancelled");
}

bool KPilotDeviceLink::openConnection()
{
    qDebug() << "[KPilotDeviceLink] openConnection() called";

    if (m_isConnected) {
        qDebug() << "[KPilotDeviceLink] Already connected, returning true";
        emit logMessage("Already connected");
        return true;
    }

    // Clean up any existing worker
    cleanupWorker();

    emit logMessage(QString("Opening connection on %1 port(s)...").arg(m_devicePaths.size()));
    setStatus(WaitingForDevice);

    // Create worker thread — tries ports sequentially with bounded timeout
    qDebug() << "[KPilotDeviceLink] Creating worker thread for ports:" << m_devicePaths;
    m_workerThread = new QThread(this);
    m_worker = new ConnectionWorker(m_devicePaths);
    m_worker->moveToThread(m_workerThread);

    // Register metatype for cross-thread signal
    qRegisterMetaType<HandshakeResult>("HandshakeResult");

    // Connect signals
    connect(m_workerThread, &QThread::started, m_worker, &ConnectionWorker::doConnect);
    connect(m_worker, &ConnectionWorker::connectionEstablished,
            this, &KPilotDeviceLink::onConnectionEstablished);
    connect(m_worker, &ConnectionWorker::connectionFailed,
            this, &KPilotDeviceLink::onConnectionFailed);
    connect(m_worker, &ConnectionWorker::statusUpdate,
            this, &KPilotDeviceLink::onWorkerStatus);

    // Clean up worker when thread finishes
    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);

    // Start the worker thread
    qDebug() << "[KPilotDeviceLink] Starting worker thread";
    m_workerThread->start();

    emit logMessage("Connecting — trying ports sequentially");
    qDebug() << "[KPilotDeviceLink] openConnection() returning true (async)";

    return true;  // Connection started successfully (but not yet complete)
}

void KPilotDeviceLink::onConnectionEstablished(const HandshakeResult &result)
{
    qDebug() << "[KPilotDeviceLink] onConnectionEstablished() socket:" << result.socket;

    m_socket = result.socket;
    m_isConnected = true;
    m_handshake = result;

    setStatus(AcceptedDevice);
    emit logMessage("Device connected successfully!");

    // Emit deviceReady with user info (previously done inside readUserInfo)
    if (result.userInfoValid) {
        emit deviceReady(result.userName, QStringLiteral("Palm Device"));
    }

    emit connectionComplete(true);

    qDebug() << "[KPilotDeviceLink] Connection established, m_isConnected = true";
}

void KPilotDeviceLink::onConnectionFailed(const QString &error)
{
    qWarning() << "[KPilotDeviceLink] onConnectionFailed():" << error;

    m_socket = -1;
    m_isConnected = false;
    setStatus(PilotLinkError);
    setError(error);

    emit connectionComplete(false);
}

void KPilotDeviceLink::onWorkerStatus(const QString &status)
{
    qDebug() << "[KPilotDeviceLink] Worker status:" << status;
    emit logMessage(status);
}

void KPilotDeviceLink::closeConnection()
{
    qDebug() << "[KPilotDeviceLink] closeConnection() called, m_isConnected:" << m_isConnected;

    // First clean up any pending worker
    cleanupWorker();

    if (m_socket >= 0) {
        qDebug() << "[KPilotDeviceLink] Closing socket:" << m_socket;
        emit logMessage("Closing connection...");

        // Set a short timeout so pi_close()'s internal dlp_EndOfSync()
        // doesn't block forever if the device is already gone (e.g. a
        // PTY whose master side was closed).
        int timeout = 2000; // milliseconds
        size_t len = sizeof(timeout);
        pi_setsockopt(m_socket, PI_LEVEL_DEV, PI_DEV_TIMEOUT,
                       &timeout, &len);

        pi_close(m_socket);
        m_socket = -1;
    }

    m_isConnected = false;
    setStatus(Init);
    qDebug() << "[KPilotDeviceLink] Connection closed";
}

bool KPilotDeviceLink::readUserInfo(struct PilotUser &user)
{
    qDebug() << "[KPilotDeviceLink] readUserInfo() called";

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] readUserInfo() - not connected";
        setError("Not connected");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Calling dlp_ReadUserInfo() on socket" << m_socket
             << "connected:" << pi_socket_connected(m_socket);
    struct PilotUser pilotUser;
    int result = dlp_ReadUserInfo(m_socket, &pilotUser);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_ReadUserInfo() failed, result:" << result
                    << "pi_error:" << pi_error(m_socket)
                    << "palmos_error:" << pi_palmos_error(m_socket);
        setError("Failed to read user info");
        return false;
    }

    user = pilotUser;
    qDebug() << "[KPilotDeviceLink] User info read successfully:"
             << "username=" << user.username
             << "userID=" << user.userID;

    emit logMessage(QString("User: %1 (ID: %2)")
                   .arg(user.username)
                   .arg(user.userID));

    emit deviceReady(QString::fromUtf8(user.username), QString("Palm Device"));

    return true;
}

bool KPilotDeviceLink::writeUserInfo(const struct PilotUser &user)
{
    qDebug() << "[KPilotDeviceLink] writeUserInfo() called for user:" << user.username;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] writeUserInfo() - not connected";
        setError("Not connected");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Calling dlp_WriteUserInfo()";
    int result = dlp_WriteUserInfo(m_socket, const_cast<struct PilotUser*>(&user));
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_WriteUserInfo() failed, result:" << result;
        setError("Failed to write user info");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] User info written successfully";
    emit logMessage(QString("User info updated: %1").arg(user.username));
    return true;
}

bool KPilotDeviceLink::readSysInfo(struct SysInfo &sysInfo)
{
    qDebug() << "[KPilotDeviceLink] readSysInfo() called";

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] readSysInfo() - not connected";
        setError("Not connected");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Calling dlp_ReadSysInfo() on socket" << m_socket;
    struct SysInfo info;
    int result = dlp_ReadSysInfo(m_socket, &info);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_ReadSysInfo() failed, result:" << result
                    << "pi_error:" << pi_error(m_socket)
                    << "palmos_error:" << pi_palmos_error(m_socket);
        setError("Failed to read system info");
        return false;
    }

    sysInfo = info;
    qDebug() << "[KPilotDeviceLink] System info read:"
             << "romVersion=0x" << Qt::hex << sysInfo.romVersion
             << "prodID=" << sysInfo.prodID;

    emit logMessage(QString("ROM Version: %1.%2")
                   .arg((sysInfo.romVersion >> 16) & 0xFF)
                   .arg((sysInfo.romVersion >> 8) & 0xFF));

    return true;
}

bool KPilotDeviceLink::readStorageInfo(int cardNo, struct CardInfo &cardInfo)
{
    qDebug() << "[KPilotDeviceLink] readStorageInfo() called for card:" << cardNo;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] readStorageInfo() - not connected";
        setError("Not connected");
        return false;
    }

    struct CardInfo info;
    memset(&info, 0, sizeof(info));
    int result = dlp_ReadStorageInfo(m_socket, cardNo, &info);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_ReadStorageInfo() failed, result:" << result
                    << "pi_error:" << pi_error(m_socket)
                    << "palmos_error:" << pi_palmos_error(m_socket);
        setError("Failed to read storage info");
        return false;
    }

    cardInfo = info;
    qDebug() << "[KPilotDeviceLink] Storage info read:"
             << "name=" << info.name
             << "manufacturer=" << info.manufacturer
             << "romSize=" << info.romSize
             << "ramSize=" << info.ramSize
             << "ramFree=" << info.ramFree;

    return true;
}

int KPilotDeviceLink::openDatabase(const QString &dbName, bool readWrite)
{
    qDebug() << "[KPilotDeviceLink] openDatabase() called for:" << dbName
             << "readWrite:" << readWrite;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] openDatabase() - not connected";
        setError("Not connected");
        return -1;
    }

    int dbHandle = 0;
    int mode = readWrite ? (dlpOpenRead | dlpOpenWrite) : dlpOpenRead;

    qDebug() << "[KPilotDeviceLink] Calling dlp_OpenDB() mode:" << mode;
    emit logMessage(QString("Opening database: %1 (%2)")
                   .arg(dbName, readWrite ? "read-write" : "read-only"));

    int result = dlp_OpenDB(m_socket, 0, mode, dbName.toUtf8().constData(), &dbHandle);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_OpenDB() failed, result:" << result;
        // Socket/protocol-level errors (≤ -200) mean the DLP session is
        // unrecoverable.  Mark as disconnected so subsequent calls fast-fail
        // instead of retrying hundreds of times.
        if (result <= -200)
            m_isConnected = false;
        setError(QString("Failed to open database: %1").arg(dbName));
        return -1;
    }

    qDebug() << "[KPilotDeviceLink] Database opened, handle:" << dbHandle;
    emit logMessage(QString("Database opened with handle: %1").arg(dbHandle));
    return dbHandle;
}

bool KPilotDeviceLink::closeDatabase(int handle)
{
    qDebug() << "[KPilotDeviceLink] closeDatabase() called for handle:" << handle;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] closeDatabase() - not connected";
        setError("Not connected");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Calling dlp_CloseDB()";
    int result = dlp_CloseDB(m_socket, handle);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_CloseDB() failed, result:" << result;
        setError(QString("Failed to close database handle: %1").arg(handle));
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Database closed successfully";
    emit logMessage(QString("Database closed: %1").arg(handle));
    return true;
}

QStringList KPilotDeviceLink::listDatabases()
{
    qDebug() << "[KPilotDeviceLink] listDatabases() called";
    QStringList databases;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] listDatabases() - not connected";
        setError("Not connected");
        return databases;
    }

    emit logMessage("Listing databases...");

    pi_buffer_t *buffer = pi_buffer_new(0xffff);
    int dbIndex = 0;
    int flags = dlpDBListRAM;  // List databases in RAM

    qDebug() << "[KPilotDeviceLink] Starting database enumeration";

    while (true) {
        struct DBInfo info;
        int result = dlp_ReadDBList(m_socket, 0, flags, dbIndex, buffer);
        if (result < 0) {
            qDebug() << "[KPilotDeviceLink] dlp_ReadDBList() ended at index:" << dbIndex;
            break;
        }

        // Parse database info from buffer
        memcpy(&info, buffer->data, sizeof(info));

        QString dbName = QString::fromLatin1(info.name);
        databases.append(dbName);

        qDebug() << "[KPilotDeviceLink] Found database:" << dbName;
        emit logMessage(QString("  Found: %1").arg(dbName));
        dbIndex++;
    }

    pi_buffer_free(buffer);

    qDebug() << "[KPilotDeviceLink] Total databases found:" << databases.size();
    emit logMessage(QString("Found %1 databases").arg(databases.size()));
    return databases;
}

QList<PilotRecord*> KPilotDeviceLink::readAllRecords(int dbHandle)
{
    qDebug() << "[KPilotDeviceLink] readAllRecords() called for handle:" << dbHandle;
    QList<PilotRecord*> records;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] readAllRecords() - not connected";
        setError("Not connected");
        return records;
    }

    emit logMessage(QString("Reading all records from database handle %1...").arg(dbHandle));

    pi_buffer_t *buffer = pi_buffer_new(0xffff);
    int index = 0;

    while (m_isConnected) {
        recordid_t id = 0;
        int attr = 0;
        int category = 0;

        int result = dlp_ReadRecordByIndex(m_socket, dbHandle, index,
                                          buffer, &id, &attr, &category);

        if (result < 0) {
            // dlp_ReadRecordByIndex returns PI_ERR_DLP_PALMOS with
            // pi_palmos_error() == dlpErrNotFound to signal "no record at
            // this index" — i.e. end of database. That is *also* the
            // response at index 0 for an empty database, so we cannot
            // use index == 0 as a disconnect heuristic (it would tear
            // down the whole sync on the first empty Palm DB).
            if (pi_palmos_error(m_socket) == dlpErrNotFound) {
                qDebug() << "[KPilotDeviceLink] End of records at index:" << index;
            } else {
                qWarning() << "[KPilotDeviceLink] dlp_ReadRecordByIndex() failed at index" << index
                           << "result:" << result
                           << "pi_error:" << pi_error(m_socket)
                           << "palmos_error:" << pi_palmos_error(m_socket);
                setError("Failed to read records");
                setStatus(PilotLinkError);
                if (result <= -200)
                    m_isConnected = false;
            }
            break;
        }

        QByteArray data(reinterpret_cast<const char*>(buffer->data), buffer->used);
        PilotRecord *record = new PilotRecord(id, category, attr, data);
        records.append(record);

        if (index % 50 == 0 && index > 0) {
            qDebug() << "[KPilotDeviceLink] Read" << index << "records so far...";
        }

        index++;
    }

    pi_buffer_free(buffer);

    qDebug() << "[KPilotDeviceLink] Total records read:" << records.size();
    emit logMessage(QString("Read %1 records").arg(records.size()));
    return records;
}

PilotRecord* KPilotDeviceLink::readRecordByIndex(int dbHandle, int index)
{
    qDebug() << "[KPilotDeviceLink] readRecordByIndex() handle:" << dbHandle << "index:" << index;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] readRecordByIndex() - not connected";
        setError("Not connected");
        return nullptr;
    }

    pi_buffer_t *buffer = pi_buffer_new(0xffff);
    recordid_t id = 0;
    int attr = 0;
    int category = 0;

    int result = dlp_ReadRecordByIndex(m_socket, dbHandle, index,
                                      buffer, &id, &attr, &category);

    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_ReadRecordByIndex() failed, result:" << result;
        pi_buffer_free(buffer);
        setError(QString("Failed to read record at index: %1").arg(index));
        return nullptr;
    }

    QByteArray data(reinterpret_cast<const char*>(buffer->data), buffer->used);
    PilotRecord *record = new PilotRecord(id, category, attr, data);

    pi_buffer_free(buffer);
    qDebug() << "[KPilotDeviceLink] Record read successfully, id:" << id;
    return record;
}

PilotRecord* KPilotDeviceLink::readRecordById(int dbHandle, int recordId)
{
    qDebug() << "[KPilotDeviceLink] readRecordById() handle:" << dbHandle << "id:" << recordId;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] readRecordById() - not connected";
        setError("Not connected");
        return nullptr;
    }

    pi_buffer_t *buffer = pi_buffer_new(0xffff);
    int attr = 0;
    int category = 0;
    int index = 0;

    int result = dlp_ReadRecordById(m_socket, dbHandle, recordId, buffer,
                                   &index, &attr, &category);

    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_ReadRecordById() failed, result:" << result;
        pi_buffer_free(buffer);
        setError(QString("Failed to read record by ID: %1").arg(recordId));
        return nullptr;
    }

    QByteArray data(reinterpret_cast<const char*>(buffer->data), buffer->used);
    PilotRecord *record = new PilotRecord(recordId, category, attr, data);

    pi_buffer_free(buffer);
    qDebug() << "[KPilotDeviceLink] Record read successfully by id:" << recordId;
    return record;
}

bool KPilotDeviceLink::writeRecord(int dbHandle, PilotRecord *record)
{
    qDebug() << "[KPilotDeviceLink] writeRecord() called for handle:" << dbHandle
             << "recordId:" << record->id() << "category:" << record->category();

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] writeRecord() - not connected";
        setError("Not connected");
        return false;
    }

    if (!record) {
        qWarning() << "[KPilotDeviceLink] writeRecord() - null record";
        setError("Cannot write null record");
        return false;
    }

    const QByteArray &data = record->data();
    recordid_t newRecordId = 0;

    // flags: 0 = normal write
    // recuid: 0 = create new record, otherwise update existing
    recordid_t recuid = record->id();

    qDebug() << "[KPilotDeviceLink] Calling dlp_WriteRecord() size:" << data.size()
             << "category:" << record->category() << "recuid:" << recuid;

    int result = dlp_WriteRecord(m_socket, dbHandle, 0, recuid,
                                 record->category(),
                                 reinterpret_cast<const void*>(data.constData()),
                                 data.size(), &newRecordId);

    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_WriteRecord() failed, result:" << result;
        setError(QString("Failed to write record: error %1").arg(result));
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Record written successfully, newRecordId:" << newRecordId;

    // Update record with new ID if it was a create operation
    if (recuid == 0 && newRecordId != 0) {
        record->setId(newRecordId);
    }

    emit logMessage(QString("Record written (ID: %1, size: %2 bytes)")
                   .arg(newRecordId).arg(data.size()));
    return true;
}

bool KPilotDeviceLink::deleteRecord(int dbHandle, int recordId)
{
    qDebug() << "[KPilotDeviceLink] deleteRecord() handle:" << dbHandle << "recordId:" << recordId;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] deleteRecord() - not connected";
        setError("Not connected");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Calling dlp_DeleteRecord()";
    int result = dlp_DeleteRecord(m_socket, dbHandle, 0, recordId);

    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_DeleteRecord() failed, result:" << result;
        setError(QString("Failed to delete record %1: error %2").arg(recordId).arg(result));
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Record deleted successfully";
    emit logMessage(QString("Record deleted (ID: %1)").arg(recordId));
    return true;
}

bool KPilotDeviceLink::resetDBIndex(int dbHandle)
{
    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] resetDBIndex() - not connected";
        setError("Not connected");
        return false;
    }

    int result = dlp_ResetDBIndex(m_socket, dbHandle);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_ResetDBIndex() failed, result:" << result;
        setError("Failed to reset database index");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Database index reset for handle:" << dbHandle;
    return true;
}

QList<PilotRecord*> KPilotDeviceLink::readModifiedRecords(int dbHandle)
{
    qDebug() << "[KPilotDeviceLink] readModifiedRecords() called for handle:" << dbHandle;
    QList<PilotRecord*> records;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] readModifiedRecords() - not connected";
        setError("Not connected");
        return records;
    }

    if (!resetDBIndex(dbHandle)) {
        return records;
    }

    QElapsedTimer timer;
    timer.start();

    pi_buffer_t *buffer = pi_buffer_new(0xffff);

    while (m_isConnected) {
        recordid_t id = 0;
        int recindex = 0;
        int attr = 0;
        int category = 0;

        int result = dlp_ReadNextModifiedRec(m_socket, dbHandle,
                                             buffer, &id, &recindex,
                                             &attr, &category);

        if (result < 0) {
            // End of modified records: PI_ERR_DLP_PALMOS with dlpErrNotFound
            if (pi_palmos_error(m_socket) == dlpErrNotFound) {
                qDebug() << "[KPilotDeviceLink] readModifiedRecords: end of modified records";
            } else {
                qWarning() << "[KPilotDeviceLink] dlp_ReadNextModifiedRec() failed, result:" << result
                           << "pi_error:" << pi_error(m_socket)
                           << "palmos_error:" << pi_palmos_error(m_socket);
            }
            break;
        }

        QByteArray data(reinterpret_cast<const char*>(buffer->data), buffer->used);
        PilotRecord *record = new PilotRecord(id, category, attr, data);
        records.append(record);

        qDebug() << "[KPilotDeviceLink] readModifiedRecords: id=" << id
                 << "attr=0x" << Qt::hex << attr << Qt::dec
                 << "dirty=" << (attr & 0x40 ? "yes" : "no")
                 << "deleted=" << (attr & 0x80 ? "yes" : "no")
                 << "category=" << category
                 << "size=" << buffer->used;
    }

    pi_buffer_free(buffer);

    qDebug() << "[KPilotDeviceLink] readModifiedRecords: found" << records.size()
             << "records in" << timer.elapsed() << "ms";
    emit logMessage(QString("Read %1 modified records").arg(records.size()));
    return records;
}

bool KPilotDeviceLink::readAppBlock(int dbHandle, unsigned char *buffer, size_t *size)
{
    qDebug() << "[KPilotDeviceLink] readAppBlock() called for handle:" << dbHandle;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] readAppBlock() - not connected";
        setError("Not connected");
        return false;
    }

    pi_buffer_t *buf = pi_buffer_new(0xffff);

    qDebug() << "[KPilotDeviceLink] Calling dlp_ReadAppBlock()";
    int result = dlp_ReadAppBlock(m_socket, dbHandle, 0, -1, buf);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_ReadAppBlock() failed, result:" << result;
        pi_buffer_free(buf);
        setError("Failed to read AppInfo block");
        return false;
    }

    *size = buf->used;
    memcpy(buffer, buf->data, buf->used);

    pi_buffer_free(buf);
    qDebug() << "[KPilotDeviceLink] AppInfo block read," << *size << "bytes";
    emit logMessage(QString("Read AppInfo block (%1 bytes)").arg(*size));

    return true;
}

bool KPilotDeviceLink::writeAppBlock(int dbHandle, const unsigned char *buffer, size_t size)
{
    qDebug() << "[KPilotDeviceLink] writeAppBlock() called for handle:" << dbHandle
             << "size:" << size;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] writeAppBlock() - not connected";
        setError("Not connected");
        return false;
    }

    if (!buffer || size == 0) {
        qWarning() << "[KPilotDeviceLink] writeAppBlock() - invalid buffer";
        setError("Invalid buffer");
        return false;
    }

    int result = dlp_WriteAppBlock(m_socket, dbHandle,
                                   reinterpret_cast<const void*>(buffer), size);

    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_WriteAppBlock() failed, result:" << result;
        setError(QString("Failed to write AppInfo block: error %1").arg(result));
        return false;
    }

    qDebug() << "[KPilotDeviceLink] AppInfo block written successfully";
    emit logMessage(QString("AppInfo block written (%1 bytes)").arg(size));
    return true;
}

bool KPilotDeviceLink::beginSync()
{
    qDebug() << "[KPilotDeviceLink] beginSync() called";

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] beginSync() - not connected";
        setError("Not connected");
        return false;
    }

    emit logMessage("Beginning sync...");

    qDebug() << "[KPilotDeviceLink] Calling dlp_OpenConduit()";
    int result = dlp_OpenConduit(m_socket);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_OpenConduit() failed, result:" << result;
        setError("Failed to open sync conduit");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Sync conduit opened";
    return true;
}

bool KPilotDeviceLink::endSync()
{
    qDebug() << "[KPilotDeviceLink] endSync() called";

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] endSync() - not connected";
        setError("Not connected");
        return false;
    }

    emit logMessage("Ending sync...");

    char logEntry[] = "Sync completed by Wild Palms.\n";
    qDebug() << "[KPilotDeviceLink] Adding sync log entry";
    if (dlp_AddSyncLogEntry(m_socket, logEntry) < 0) {
        qWarning() << "[KPilotDeviceLink] Failed to add sync log entry (non-fatal)";
    }

    qDebug() << "[KPilotDeviceLink] Calling dlp_EndOfSync()";
    int result = dlp_EndOfSync(m_socket, 0);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_EndOfSync() failed, result:" << result;
        setError("Failed to end sync");
        return false;
    }

    setStatus(SyncDone);
    qDebug() << "[KPilotDeviceLink] Sync complete!";
    emit logMessage("Sync complete!");

    return true;
}

bool KPilotDeviceLink::cleanUpDatabase(int dbHandle)
{
    qDebug() << "[KPilotDeviceLink] cleanUpDatabase() called for handle:" << dbHandle;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] cleanUpDatabase() - not connected";
        setError("Not connected");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Calling dlp_CleanUpDatabase()";
    int result = dlp_CleanUpDatabase(m_socket, dbHandle);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_CleanUpDatabase() failed, result:" << result;
        setError("Failed to clean up database");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Database cleanup complete";
    return true;
}

bool KPilotDeviceLink::resetSyncFlags(int dbHandle)
{
    qDebug() << "[KPilotDeviceLink] resetSyncFlags() called for handle:" << dbHandle;

    if (!m_isConnected) {
        qWarning() << "[KPilotDeviceLink] resetSyncFlags() - not connected";
        setError("Not connected");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Calling dlp_ResetSyncFlags()";
    int result = dlp_ResetSyncFlags(m_socket, dbHandle);
    if (result < 0) {
        qWarning() << "[KPilotDeviceLink] dlp_ResetSyncFlags() failed, result:" << result;
        setError("Failed to reset sync flags");
        return false;
    }

    qDebug() << "[KPilotDeviceLink] Sync flags reset complete";
    return true;
}

bool KPilotDeviceLink::installFile(const QString &filePath)
{
    if (!m_isConnected || m_socket < 0) {
        qWarning() << "[DeviceLink] Cannot install file — not connected";
        return false;
    }

    pi_file_t *pf = pi_file_open(filePath.toLocal8Bit().constData());
    if (!pf) {
        qWarning() << "[DeviceLink] Failed to open file for install:" << filePath;
        return false;
    }

    struct DBInfo dbInfo;
    pi_file_get_info(pf, &dbInfo);
    qDebug() << "[DeviceLink] Installing:" << dbInfo.name << "from" << filePath;

    int rc = pi_file_install(pf, m_socket, 0, nullptr);
    pi_file_close(pf);

    if (rc < 0) {
        qWarning() << "[DeviceLink] pi_file_install failed:" << rc;
        return false;
    }

    qDebug() << "[DeviceLink] Installed successfully:" << dbInfo.name;
    return true;
}

bool KPilotDeviceLink::retrieveDatabase(const QString &dbName, const QString &destPath)
{
    if (!m_isConnected || m_socket < 0) {
        qWarning() << "[DeviceLink] Cannot retrieve — not connected";
        return false;
    }
    struct DBInfo info;
    int rc = dlp_FindDBInfo(m_socket, 0, 0,
                             dbName.toLocal8Bit().constData(),
                             0, 0, &info);
    if (rc < 0) {
        qWarning() << "[DeviceLink] dlp_FindDBInfo failed for" << dbName << "rc:" << rc;
        return false;
    }
    // Skip databases the OS has marked as non-transferable.
    if (info.flags & dlpDBFlagCopyPrevention) {
        qDebug() << "[DeviceLink] Skipping copy-prevented database:" << dbName;
        return true;
    }
    // Skip streaming file databases (not real record DBs; pi_file_retrieve
    // would fail on them).
    if (info.flags & dlpDBFlagStream) {
        qDebug() << "[DeviceLink] Skipping stream database:" << dbName;
        return true;
    }
    pi_file_t *pf = pi_file_create(destPath.toLocal8Bit().constData(), &info);
    if (!pf) {
        qWarning() << "[DeviceLink] pi_file_create failed for" << destPath;
        return false;
    }
    rc = pi_file_retrieve(pf, m_socket, 0, nullptr);
    pi_file_close(pf);
    if (rc < 0) {
        qWarning() << "[DeviceLink] pi_file_retrieve failed for" << dbName << "rc:" << rc;
        QFile::remove(destPath);
        return false;
    }
    qDebug() << "[DeviceLink] Retrieved database:" << dbName << "->" << destPath;
    return true;
}

void KPilotDeviceLink::pauseTickle()
{
    emit ticklePauseRequested();
}

void KPilotDeviceLink::resumeTickle()
{
    emit tickleResumeRequested();
}

bool KPilotDeviceLink::findDatabase(const QString &dbName)
{
    if (!m_isConnected || m_socket < 0) {
        return false;
    }

    struct DBInfo info;
    int rc = dlp_FindDBInfo(m_socket, 0, 0,
                             dbName.toLocal8Bit().constData(),
                             0, 0, &info);
    return (rc >= 0);
}

qint64 KPilotDeviceLink::databaseModnum(const QString &dbName)
{
    if (!m_isConnected || m_socket < 0)
        return -1;

    struct DBInfo info;
    int rc = dlp_FindDBInfo(m_socket, 0, 0,
                            dbName.toUtf8().constData(),
                            0, 0, &info);
    if (rc < 0)
        return -1;
    return static_cast<qint64>(info.modnum);
}
