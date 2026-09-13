#pragma once

#include <kalburator/shape/transformationedge.h>

namespace WildPalms::Palm::Transformations {

/**
 * @brief Transforms Palm AddressDB raw bytes to vCard 3.0 text.
 *
 * G.7 Task 53. Stub implementation: passes bytes through unchanged.
 * Real decoding (Palm address structure → vCard fields) is a G.10 item
 * that requires linking WildPalmsCodecs. Registered with
 * TransformationRegistry by WildPalmsDomainExtension at start-up.
 */
class PalmAddressToVCard : public Kalburator::Shape::TransformationStage
{
public:
    QByteArray transform(const QByteArray &sourceBytes) const override;
};

} // namespace WildPalms::Palm::Transformations
