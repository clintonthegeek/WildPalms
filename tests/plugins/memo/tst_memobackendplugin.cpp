#include <QtTest/QtTest>
#include <QWidget>

#include "plugins/memo/memobackendplugin.h"
#include "plugins/memo/memoblobbackend.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "palm/codecs/memocodec.h"
#include "runtime/palmdeviceaccess.h"

#include <kalburator/blob/iblobbackend.h>
#include <kalburator/conflict/conflictrecord.h>   // Kalburator::Conflict::RecordSnapshot

using WildPalms::Memo::MemoPlugin;
using WildPalms::Memo::MemoBlobBackend;
using WildPalms::Runtime::PalmDeviceAccess;

class TestMemoBackendPlugin : public QObject {
    Q_OBJECT
private slots:
    void metadataStatics();
    void createPalmBackendReturnsMemoBlobBackend();
    void viewHooksReportMemoSurface();
    void conflictHtmlRendersMemoTitleAndBody();
    void enrichSnapshotDecodesPalmBytesOnSourceSide();
};

void TestMemoBackendPlugin::metadataStatics()
{
    MemoPlugin p;
    QCOMPARE(p.pluginId(), QStringLiteral("memo"));
    QCOMPARE(p.displayName(), QStringLiteral("Memos"));
    QCOMPARE(p.description(),
             QStringLiteral("Synchronizes Palm MemoDB with Markdown files"));
    QCOMPARE(p.version(), QStringLiteral("2.0"));
}

void TestMemoBackendPlugin::createPalmBackendReturnsMemoBlobBackend()
{
    MemoPlugin p;
    auto mock = std::make_unique<WildPalms::PalmSync::MockPalmDatabaseAccess>();
    PalmDeviceAccess dev(std::move(mock));

    auto backend = p.createPalmBackend(&dev);
    QVERIFY(backend != nullptr);
    QCOMPARE(backend->backendId(), QStringLiteral("palm-memo"));
}

void TestMemoBackendPlugin::viewHooksReportMemoSurface()
{
    MemoPlugin p;
    QCOMPARE(p.hasMainView(), true);
    QCOMPARE(p.mainViewName(), QStringLiteral("Memos"));

    QWidget parent;
    QWidget *view = p.createMainView(&parent);
    QVERIFY(view != nullptr);
    delete view;
}

void TestMemoBackendPlugin::conflictHtmlRendersMemoTitleAndBody()
{
    MemoPlugin p;
    Kalburator::Conflict::RecordSnapshot snap;
    snap.content = QByteArrayLiteral("Grocery list\n- milk\n- eggs");
    snap.metadata[QStringLiteral("title")] = QStringLiteral("Grocery list");
    snap.contentType = QStringLiteral("text/plain");

    const QString html = p.formatConflictRecordHtml(snap);
    QVERIFY(html.contains(QStringLiteral("<h3>Grocery list</h3>")));
    QVERIFY(html.contains(QStringLiteral("- milk")));
}

void TestMemoBackendPlugin::enrichSnapshotDecodesPalmBytesOnSourceSide()
{
    MemoPlugin p;
    Kalburator::Conflict::RecordSnapshot snap;
    const auto palm = WildPalms::PalmCodecs::encodeMemo(
        {QStringLiteral("Shopping list\nfirst line extends"), false});
    snap.content = palm;

    p.enrichConflictSnapshot(snap, /*isSourceSide=*/true);
    QCOMPARE(QString::fromUtf8(snap.content),
             QStringLiteral("Shopping list\nfirst line extends"));
    QCOMPARE(snap.contentType, QStringLiteral("text/plain"));
    QCOMPARE(snap.metadata.value(QStringLiteral("title")).toString(),
             QStringLiteral("Shopping list"));
}

QTEST_MAIN(TestMemoBackendPlugin)
#include "tst_memobackendplugin.moc"
