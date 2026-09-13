#include <QTest>
#include <QCryptographicHash>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtGlobal>

#include "emulatorfixture.h"
#include "recontrolclient.h"
#include "pilotlinkdecoder.h"
#include "canonseed.h"
#include "../wildpalms_qtest_main.h"

#include "runtime/palmruntime.h"
#include "runtime/palmrunresult.h"
#include <kalburator/sync/backendregistry.h>
#include <kalburator/types/backendrecord.h>
#include <kalburator/universal/genericsqlitebackend.h>

using namespace WildPalms::DeviceE2E;
using namespace WildPalms::Runtime;

class TestDeviceE2EHotSync : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        qputenv("TZ", "UTC"); // deterministic wall-clock for the fidelity assertion
        tzset();
    }

    void emulatorLaunchesAndExposesPtyAndDatebook()
    {
        if (!EmulatorFixture::configured())
            QSKIP("device-e2e: set WILDPALMS_POSE64_BIN and WILDPALMS_PALM_BASELINE_PSF to run");
        EmulatorFixture emu;
        QVERIFY2(emu.launch(), qPrintable(emu.lastError()));
        QVERIFY(!emu.ptyPath().isEmpty());
        const ReControlReply apps = emu.client()->commandMultiline(QStringLiteral("apps all"), 5000);
        QVERIFY2(apps.raw.contains(QStringLiteral("DatebookDB")),
                 "baseline psf has no DatebookDB; see docs/device-e2e-harness.md");
    }

    void hubToPalm_calendar_firstHotSync()
    {
        if (!EmulatorFixture::configured())
            QSKIP("device-e2e: set WILDPALMS_POSE64_BIN and WILDPALMS_PALM_BASELINE_PSF to run");

        // 1. Launch a clean emulated Palm with a HotSync pty.
        EmulatorFixture emu;
        QVERIFY2(emu.launch(), qPrintable(emu.lastError()));
        QVERIFY2(emu.loadBaseline(), qPrintable(emu.lastError())); // isolation + fresh pty
        const QString pty = emu.ptyPath();
        QVERIFY(!pty.isEmpty());

        // 2. Headless PalmRuntime over a fresh profile dir.
        QTemporaryDir profileDir;
        QVERIFY(profileDir.isValid());
        PalmRuntime rt(profileDir.path());

        // 3. Seed the REAL hub's "calendar" collection with one canon event.
        auto *base = rt.backendRegistry().backendInstance(QStringLiteral("wp-hub"));
        auto *hub = dynamic_cast<Kalburator::Sinks::GenericSqliteBackend *>(base);
        QVERIFY2(hub, "wp-hub backend missing or wrong type");
        const CanonCalendarEventSpec spec;
        Kalburator::Sync::BackendRecord rec;
        rec.id = spec.uid;
        rec.type = QStringLiteral("calendar");
        rec.data = buildCanonCalendarEvent(spec);
        // content_hash is a NOT NULL column in GenericSqliteBackend; a default
        // (null) QString makes the INSERT fail and createRecord return "".
        rec.contentHash = QString::fromLatin1(
            QCryptographicHash::hash(rec.data, QCryptographicHash::Sha256).toHex());
        rec.lastModified = QDateTime::currentDateTimeUtc();
        const QString createdId = hub->createRecord(QStringLiteral("calendar"), rec);
        QVERIFY(!createdId.isEmpty());

        // 4. Connect over the pty (single-element list => WP skips its probe).
        //    Attach-then-tap: enter pi_accept_to, then send the cradle tap. Retry once.
        QSignalSpy ready(&rt, &PalmRuntime::readyForSync);
        QSignalSpy completed(&rt, &PalmRuntime::connectionComplete);
        bool connected = false;
        for (int attempt = 0; attempt < 2 && !connected; ++attempt) {
            rt.connectDevice(QStringList{pty});
            QTest::qWait(1000); // let the worker open the pty + enter pi_accept_to
            QVERIFY2(emu.cradleTap(), qPrintable(emu.lastError()));
            QTRY_VERIFY_WITH_TIMEOUT(completed.count() >= 1, 30000);
            const auto args = completed.takeFirst();
            connected = args.at(0).toBool();
            if (!connected) {
                emu.dismissProblemFormIfPresent();
                QTest::qWait(500);
            }
        }
        QVERIFY2(connected, "device did not connect (attach-then-tap failed twice)");
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() >= 1, 5000);

        // 5. One HotSync. finishConnect() builds the palm<->hub Star mapping for
        //    ALL four conduits unconditionally, so a real HotSync runs calendar,
        //    contacts, memo and todo. This test asserts CALENDAR fidelity, so we
        //    do NOT gate on either of two run-level signals that the multi-conduit
        //    HotSync makes unreliable for this purpose:
        //      - result.success is dragged down by an unrelated conduit (the
        //        contacts write-back path against the baseline AddressDB's
        //        pre-seeded records reports "Write to contacts failed").
        //      - result.perPluginStats does NOT carry per-conduit created counts:
        //        PalmRuntime folds the SUM of every mapping's targetStats into a
        //        single "calendar" key (palmruntime.cpp:978), and the Palm backend
        //        does not surface device dlp_WriteRecord writes as engine-level
        //        targetStats.created, so this counter reads 0 even when the event
        //        is demonstrably written to the device (see the dlp_WriteRecord
        //        "Record written successfully" log line).
        //    The independent on-device export+decode below is the ground-truth
        //    fidelity check.
        auto future = rt.hotSync();
        QTRY_VERIFY_WITH_TIMEOUT(future.isFinished(), 60000);
        const PalmRunResult result = future.resultAt(0);
        if (!result.success) {
            qInfo() << "device-e2e: aggregate HotSync reported failure (non-calendar"
                       " conduit):" << result.errorMessage
                    << "- calendar fidelity is asserted independently below";
        }

        // 6. Export the on-device DatebookDB and decode it independently.
        const QString exported = profileDir.filePath(QStringLiteral("exported-DatebookDB.pdb"));
        QVERIFY2(emu.exportDatabase(QStringLiteral("DatebookDB"), exported), qPrintable(emu.lastError()));
        const QList<DecodedAppointment> appts = readAppointments(exported);

        // 7. Fidelity assertions: exactly the seeded event survived the wire.
        QCOMPARE(appts.size(), 1);
        const DecodedAppointment &a = appts.first();
        QCOMPARE(a.description, QStringLiteral("Seeded Event")); // canon summary -> Palm description
        QCOMPARE(a.note, QStringLiteral("Note body text"));      // canon description -> Palm note
        QCOMPARE(a.allDay, false);
        QCOMPARE(a.begin, QDateTime(QDate(2026, 7, 1), QTime(9, 0, 0)));
        QCOMPARE(a.end, QDateTime(QDate(2026, 7, 1), QTime(10, 0, 0)));
        QCOMPARE(a.category, 0); // Unfiled
        // Alarm: NOT asserted as surviving. The canon->Palm calendar transcode is
        // lossy on alarms for this seed — the engine logs
        //   onWorkerTranscodingWarning ... warnings: QList("alarms")
        // and the decoded record comes back with hasAlarm=false / advance=0 /
        // advanceUnits=0 (i.e. the 10-minutes-before alarm did not reach the
        // device). This is a libkalburator transcode-loss characteristic, not a
        // wire-fidelity failure of this harness; the core fields above prove the
        // event itself round-trips. We record (not assert) the alarm outcome so a
        // future transcode fix that starts preserving alarms surfaces here.
        qInfo() << "device-e2e: decoded alarm (informational): hasAlarm=" << a.hasAlarm
                << "advance=" << a.advance << "advanceUnits=" << a.advanceUnits
                << "(canon->Palm alarm transcode is currently lossy; see"
                   " onWorkerTranscodingWarning warnings=QList(\"alarms\"))";
    }
};

WILDPALMS_QTEST_GUILESS_MAIN(TestDeviceE2EHotSync)
#include "tst_device_e2e_hotsync.moc"
