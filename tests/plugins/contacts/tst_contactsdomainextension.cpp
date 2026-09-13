#include <QTest>

#include "contactsdomainextension.h"

#include <kalburator/shape/transformationregistry.h>

using WildPalms::ContactsPlugin::ContactsPalmShapes;
using namespace Kalburator::Shape;

namespace {

// O7: apply the plugin's ShapeContribution into a registry the way
// PluginManager::applyPlugin does — register peer shapes, then edges.
void applyPalmShapes(TransformationRegistry &reg)
{
    ContactsPalmShapes c;
    for (const auto &[shape, cat] : c.peerShapes())
        reg.registerShape(shape, cat);
    for (const auto &edge : c.edges())
        reg.registerEdge(edge);
}

} // namespace

class TestContactsDomainExtension : public QObject {
    Q_OBJECT
private slots:
    void registersPalmShape()
    {
        // O7: use a local registry instead of the deleted ::instance() global.
        TransformationRegistry reg;
        // vcard4 endpoint must exist before edge registration (the libkalburator
        // contacts plugin registers it; here we stub it). Without it, registerEdge
        // asserts "to-shape not registered".
        const Shape v4{ DomainId{"contacts"}, EncodingId{"vcard4"} };
        reg.registerShape(v4, {});

        applyPalmShapes(reg);

        const Shape palm{ DomainId{"contacts"}, EncodingId{"palm"} };
        QVERIFY(reg.catalogueFor(palm) != nullptr);
    }

    void registersBothEdges()
    {
        TransformationRegistry reg;
        const Shape v4{ DomainId{"contacts"}, EncodingId{"vcard4"} };
        reg.registerShape(v4, {});

        applyPalmShapes(reg);

        const Shape palm{ DomainId{"contacts"}, EncodingId{"palm"} };

        const auto edgesFromPalm = reg.edgesFrom(palm);
        QVERIFY2(std::any_of(edgesFromPalm.begin(), edgesFromPalm.end(),
                             [&](const auto &e) { return e.to == v4; }),
                 "expected palm -> vcard4 edge");

        const auto edgesFromV4 = reg.edgesFrom(v4);
        QVERIFY2(std::any_of(edgesFromV4.begin(), edgesFromV4.end(),
                             [&](const auto &e) { return e.to == palm; }),
                 "expected vcard4 -> palm edge");
    }

    void idempotentReregister()
    {
        TransformationRegistry reg;
        const Shape v4{ DomainId{"contacts"}, EncodingId{"vcard4"} };
        reg.registerShape(v4, {});
        applyPalmShapes(reg);
        applyPalmShapes(reg);   // identical re-registration must not assert
    }
};

QTEST_GUILESS_MAIN(TestContactsDomainExtension)
#include "tst_contactsdomainextension.moc"
