// Sub-project D Task 6: ContactView is reader-driven.
//
// Seeds 2 VCARD records into a GenericSqliteBackend "palm:contacts"
// collection, constructs a HubContactsReader over it, instantiates
// ContactView, and verifies the contact list is populated.

#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QListWidget>
#include <memory>

#include "plugins/contacts/contactview.h"
#include "plugins/contacts/hubcontactsreader.h"

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

const Shape kTestShape{ DomainId{"contacts"}, EncodingId{"vcard4"} };

std::unique_ptr<GenericSqliteBackend> makeHub(QTemporaryDir &dir)
{
    auto hub = std::make_unique<GenericSqliteBackend>(
        dir.path() + QStringLiteral("/hub.sqlite"));
    CollectionInfo info;
    info.id   = QStringLiteral("palm:contacts");
    info.name = QStringLiteral("Contacts");
    info.type = QStringLiteral("contacts");
    hub->createCollection(info, kTestShape);
    return hub;
}

void seedContact(GenericSqliteBackend *hub, const QString &id, const QString &fn)
{
    const QByteArray bytes = QStringLiteral(
        "BEGIN:VCARD\r\n"
        "VERSION:4.0\r\n"
        "FN:%1\r\n"
        "END:VCARD\r\n").arg(fn).toUtf8();

    BackendRecord r;
    r.id = id;
    r.type = QStringLiteral("contact");
    r.data = bytes;
    r.contentHash = QStringLiteral("dummy-hash");
    r.lastModified = QDateTime::currentDateTimeUtc();
    hub->createRecord(QStringLiteral("palm:contacts"), r);
}

} // namespace

class TstContactViewReadsHub : public QObject
{
    Q_OBJECT
private slots:
    void populatesListFromReader();
};

void TstContactViewReadsHub::populatesListFromReader()
{
    QTemporaryDir tmp; QVERIFY(tmp.isValid());
    auto hub = makeHub(tmp);
    seedContact(hub.get(), QStringLiteral("c-1"), QStringLiteral("Alice"));
    seedContact(hub.get(), QStringLiteral("c-2"), QStringLiteral("Bob"));

    WildPalms::ContactsPlugin::HubContactsReader reader(
        hub.get(), QStringLiteral("palm:contacts"));
    ContactView view;
    view.setHubReader(&reader);
    view.loadFromPath(tmp.path());

    auto *list = view.findChild<QListWidget*>();
    QVERIFY(list);
    QCOMPARE(list->count(), 2);
}

QTEST_MAIN(TstContactViewReadsHub)
#include "tst_contact_view_reads_hub.moc"
