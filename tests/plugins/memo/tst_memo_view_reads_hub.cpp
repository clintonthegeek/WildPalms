// Sub-project D Task 6: MemoView is reader-driven.
//
// Seeds 2 records into a GenericSqliteBackend "palm:memo" collection,
// constructs a HubMemoReader over it, instantiates MemoView, and
// verifies the list widget populates from the reader. Also pins the
// hide-edit-affordances behavior: the New/Save/Delete QActions are
// not visible to the user.

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QListWidget>
#include <QAction>
#include <memory>

#include "plugins/memo/memoview.h"
#include "plugins/memo/hubmemoreader.h"

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

const Shape kTestShape{ DomainId{"note"}, EncodingId{"markdown"} };

std::unique_ptr<GenericSqliteBackend> makeHub(QTemporaryDir &dir)
{
    auto hub = std::make_unique<GenericSqliteBackend>(
        dir.path() + QStringLiteral("/hub.sqlite"));
    CollectionInfo info;
    info.id   = QStringLiteral("palm:memo");
    info.name = QStringLiteral("Memos");
    info.type = QStringLiteral("note");
    hub->createCollection(info, kTestShape);
    return hub;
}

void seedMemo(GenericSqliteBackend *hub, const QString &id, const QString &title)
{
    const QByteArray bytes = QStringLiteral(
        "---\nid: 1\ncategory: Unfiled\n---\n\n%1\n").arg(title).toUtf8();
    BackendRecord r;
    r.id = id;
    r.type = QStringLiteral("memo");
    r.data = bytes;
    r.contentHash = QStringLiteral("dummy-hash");
    r.lastModified = QDateTime::currentDateTimeUtc();
    hub->createRecord(QStringLiteral("palm:memo"), r);
}

} // namespace

class TstMemoViewReadsHub : public QObject
{
    Q_OBJECT
private slots:
    void populatesListFromReader();
    void emptyReaderRendersEmptyState();
    void hidesEditAffordances();
};

void TstMemoViewReadsHub::populatesListFromReader()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto hub = makeHub(tmp);
    seedMemo(hub.get(), QStringLiteral("m-1"), QStringLiteral("First"));
    seedMemo(hub.get(), QStringLiteral("m-2"), QStringLiteral("Second"));

    WildPalms::Memo::HubMemoReader reader(hub.get(),
                                          QStringLiteral("palm:memo"));
    MemoView view;
    view.setHubReader(&reader);
    view.loadFromPath(tmp.path());

    auto *list = view.findChild<QListWidget*>();
    QVERIFY(list);
    QCOMPARE(list->count(), 2);
}

void TstMemoViewReadsHub::emptyReaderRendersEmptyState()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto hub = makeHub(tmp);
    WildPalms::Memo::HubMemoReader reader(hub.get(),
                                          QStringLiteral("palm:memo"));
    MemoView view;
    view.setHubReader(&reader);
    view.loadFromPath(tmp.path());

    auto *list = view.findChild<QListWidget*>();
    QVERIFY(list);
    // Empty reader -> view's applyFilter() inserts the "No memos found"
    // placeholder row.
    QCOMPARE(list->count(), 1);
}

void TstMemoViewReadsHub::hidesEditAffordances()
{
    MemoView view;
    const auto actions = view.findChildren<QAction*>();
    bool anyEditVisible = false;
    for (auto *a : actions) {
        const QString t = a->text();
        if (t.contains(QStringLiteral("New"))
            || t.contains(QStringLiteral("Delete"))
            || t.contains(QStringLiteral("Save"))) {
            if (a->isVisible()) anyEditVisible = true;
        }
    }
    QVERIFY(!anyEditVisible);
}

QTEST_MAIN(TstMemoViewReadsHub)
#include "tst_memo_view_reads_hub.moc"
