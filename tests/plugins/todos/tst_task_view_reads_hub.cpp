// Sub-project D Task 6: TaskView is reader-driven.
//
// Seeds 2 VTODO records into a GenericSqliteBackend "palm:todo"
// collection, constructs a HubTodoReader over it, instantiates
// TaskView, and verifies the table model row count grows. Also
// pins the hide-edit-affordances behavior: the New/Delete/Toggle-Done
// QActions are not visible to the user.

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QTableView>
#include <QAbstractItemModel>
#include <QAction>
#include <memory>

#include "plugins/todos/taskview.h"
#include "plugins/todos/hubtodoreader.h"

#include <kalburator/universal/genericsqlitebackend.h>
#include <kalburator/types/backendrecord.h>
#include <kalburator/types/collectioninfo.h>
#include <kalburator/shape/shape.h>

using Kalburator::Sinks::GenericSqliteBackend;
using Kalburator::Sync::BackendRecord;
using Kalburator::Sync::CollectionInfo;
using Kalburator::Shape::DomainId;
using Kalburator::Shape::EncodingId;
using Kalburator::Shape::Shape;

namespace {

const Shape kTestShape{ DomainId{"todo"}, EncodingId{"ical-vtodo"} };

std::unique_ptr<GenericSqliteBackend> makeHub(QTemporaryDir &dir)
{
    auto hub = std::make_unique<GenericSqliteBackend>(
        dir.path() + QStringLiteral("/hub.sqlite"));
    CollectionInfo info;
    info.id   = QStringLiteral("palm:todo");
    info.name = QStringLiteral("Tasks");
    info.type = QStringLiteral("todo");
    hub->createCollection(info, kTestShape);
    return hub;
}

void seedTodo(GenericSqliteBackend *hub, const QString &id, const QString &uid)
{
    const QByteArray bytes = QStringLiteral(
        "BEGIN:VCALENDAR\r\n"
        "VERSION:2.0\r\n"
        "PRODID:-//WildPalms//TEST//EN\r\n"
        "BEGIN:VTODO\r\n"
        "UID:%1\r\n"
        "SUMMARY:%1\r\n"
        "END:VTODO\r\n"
        "END:VCALENDAR\r\n").arg(uid).toUtf8();

    BackendRecord r;
    r.id = id;
    r.type = QStringLiteral("todo");
    r.data = bytes;
    r.contentHash = QStringLiteral("dummy-hash");
    r.lastModified = QDateTime::currentDateTimeUtc();
    hub->createRecord(QStringLiteral("palm:todo"), r);
}

} // namespace

class TstTaskViewReadsHub : public QObject
{
    Q_OBJECT
private slots:
    void populatesModelFromReader();
    void hidesEditAffordances();
};

void TstTaskViewReadsHub::populatesModelFromReader()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto hub = makeHub(tmp);
    seedTodo(hub.get(), QStringLiteral("t-1"), QStringLiteral("todo-1"));
    seedTodo(hub.get(), QStringLiteral("t-2"), QStringLiteral("todo-2"));

    WildPalms::TodoPlugin::HubTodoReader reader(
        hub.get(), QStringLiteral("palm:todo"));
    TaskView view;
    view.setHubReader(&reader);
    view.loadFromPath(tmp.path());

    auto *table = view.findChild<QTableView*>();
    QVERIFY(table);
    QAbstractItemModel *model = table->model();
    QVERIFY(model);
    QCOMPARE(model->rowCount(), 2);
}

void TstTaskViewReadsHub::hidesEditAffordances()
{
    TaskView view;
    const auto actions = view.findChildren<QAction*>();
    bool anyEditVisible = false;
    for (auto *a : actions) {
        const QString t = a->text();
        if (t.contains(QStringLiteral("New"))
            || t.contains(QStringLiteral("Delete"))
            || t.contains(QStringLiteral("Toggle"))) {
            if (a->isVisible()) anyEditVisible = true;
        }
    }
    QVERIFY(!anyEditVisible);
}

QTEST_MAIN(TstTaskViewReadsHub)
#include "tst_task_view_reads_hub.moc"
