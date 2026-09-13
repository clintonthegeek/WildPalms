#pragma once

#include <kalburator/shape/transformationedge.h>

namespace WildPalms::Palm::Transformations {

/**
 * @brief Transforms Palm ToDoDB raw bytes to VTODO iCal text.
 * G.7 Task 53 stub — pass-through. Real decoding is a G.10 item.
 */
class PalmToDoToVToDo : public Kalburator::Shape::TransformationStage
{
public:
    QByteArray transform(const QByteArray &sourceBytes) const override;
};

} // namespace WildPalms::Palm::Transformations
