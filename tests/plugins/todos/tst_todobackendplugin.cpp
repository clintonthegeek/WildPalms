#include <QtTest/QtTest>

#include <cstring>

#include <pi-appinfo.h>

#include "plugins/todos/todobackendplugin.h"
#include "plugins/todos/todoblobbackend.h"
#include "plugins/todos/todoconflicthandler.h"

#include "palm/calendar/categorymappingstore.h"
#include "palm/sync/mockpalmdatabaseaccess.h"
#include "runtime/palmdeviceaccess.h"

// K.8b T13: core/ibackendplugin_v2.h deleted along with the V2 plugin ABI.

// Complete-type includes for libkalburator pointers (delete on a forward
// decl is UB; ConflictHandler is needed for dynamic_cast).
#include <kalburator/calendar/syncbackend.h>      // Kalburator::Sync::SyncBackend (calendar-typed)
#include <kalburator/blob/iblobbackend.h>
#include <kalburator/conflict/conflictpolicy.h>

using WildPalms::TodoPlugin::TodoBackendPlugin;
using WildPalms::TodoPlugin::TodoBlobBackend;
using WildPalms::TodoPlugin::TodoConflictHandler;
using WildPalms::PalmSync::MockPalmDatabaseAccess;
using WildPalms::Runtime::PalmDeviceAccess;

namespace {

QByteArray buildAppInfoTwoSlots()
{
    CategoryAppInfo_t info;
    std::memset(&info, 0, sizeof(info));
    const QStringList names{ QStringLiteral("Unfiled"),
                             QStringLiteral("Personal"),
                             QStringLiteral("Business") };
    for (int i = 0; i < names.size(); ++i) {
        const QByteArray utf = names[i].toUtf8().left(15);
        std::memcpy(info.name[i], utf.constData(), utf.size());
        info.name[i][utf.size()] = '\0';
        info.ID[i] = static_cast<unsigned char>(i);
    }
    info.lastUniqueID = 15;

    QByteArray buf(4096, '\0');
    const int written = pack_CategoryAppInfo(
        &info,
        reinterpret_cast<unsigned char *>(buf.data()),
        buf.size());
    if (written < 0) return {};
    buf.resize(written);
    return buf;
}

} // namespace

class TestTodoBackendPlugin : public QObject
{
    Q_OBJECT
private slots:
    void metadataValues();
    void claimedDatabasesIsToDoDB();
    void createBackendsPopulatesCategoryStoreFromAppInfo();
    void createConflictHandlerReturnsTodoHandler();
    void createMainViewReturnsTaskView();
};

void TestTodoBackendPlugin::metadataValues()
{
    TodoBackendPlugin p;
    QCOMPARE(p.pluginId(),    QStringLiteral("todo"));
    QCOMPARE(p.displayName(), QStringLiteral("Tasks"));
    QVERIFY(!p.description().isEmpty());
    QCOMPARE(p.version(),     QStringLiteral("2.0"));
}

void TestTodoBackendPlugin::claimedDatabasesIsToDoDB()
{
    TodoBackendPlugin p;
    auto claims = p.claimedDatabases();
    QCOMPARE(claims.size(), 1);
    QCOMPARE(claims[0], QStringLiteral("ToDoDB"));
}

void TestTodoBackendPlugin::createBackendsPopulatesCategoryStoreFromAppInfo()
{
    auto mock = std::make_unique<MockPalmDatabaseAccess>();
    mock->setAppBlock(QStringLiteral("ToDoDB"), buildAppInfoTwoSlots());
    PalmDeviceAccess dev(std::move(mock));

    TodoBackendPlugin p;
    auto blobPtr = p.createPalmBackend(&dev);
    QVERIFY(blobPtr != nullptr);

    auto *blob = static_cast<TodoBlobBackend *>(blobPtr.get());
    QVERIFY(blob);
    auto cols = blob->availableCollections();
    QCOMPARE(cols.size(), 4);  // palm:todo (domain) + Unfiled + Personal + Business

    // The domain-level collection must be present (C, Task 9).
    const QStringList ids = [&] {
        QStringList l;
        for (const auto &c : cols) l << c.id;
        return l;
    }();
    QVERIFY(ids.contains(QStringLiteral("palm:todo")));

    QCOMPARE(cols[2].name, QStringLiteral("Personal"));
    QCOMPARE(cols[3].name, QStringLiteral("Business"));
}

void TestTodoBackendPlugin::createConflictHandlerReturnsTodoHandler()
{
    auto mock = std::make_unique<MockPalmDatabaseAccess>();
    PalmDeviceAccess dev(std::move(mock));

    TodoBackendPlugin p;
    auto blobPtr = p.createPalmBackend(&dev);   // primes m_device
    Q_UNUSED(blobPtr)

    auto *handler = p.createConflictHandler();
    QVERIFY(handler != nullptr);
    QVERIFY(dynamic_cast<TodoConflictHandler *>(handler) != nullptr);
    delete handler;
}

void TestTodoBackendPlugin::createMainViewReturnsTaskView()
{
    TodoBackendPlugin p;
    QVERIFY(p.hasMainView());
    QWidget *w = p.createMainView(nullptr);
    QVERIFY(w);
    QCOMPARE(QString::fromLatin1(w->metaObject()->className()),
             QStringLiteral("TaskView"));
    delete w;
}

QTEST_MAIN(TestTodoBackendPlugin)
#include "tst_todobackendplugin.moc"
