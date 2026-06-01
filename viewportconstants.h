#ifndef VIEWPORTCONSTANTS_H
#define VIEWPORTCONSTANTS_H

#include <QColor>

namespace ViewportConstants {

constexpr float kFocalLength = 420.0f;
constexpr float kAxisPickLength = 36.0f;
constexpr float kRotationRingRadius = 48.0f;
constexpr int kRingSegments = 72;
constexpr float kRingStepDegrees = 5.0f;

const QColor kAxisXColor(255, 95, 120);
const QColor kAxisYColor(105, 245, 145);
const QColor kAxisZColor(105, 180, 255);

const QColor kRotateXColor(235, 80, 80);
const QColor kRotateYColor(80, 210, 120);
const QColor kRotateZColor(90, 155, 245);

const QColor kGridAxisXColor(210, 80, 80);
const QColor kGridAxisYColor(80, 180, 110);
const QColor kGridAxisZColor(90, 150, 230);

const QColor kSubtractColor(225, 95, 95);
const QColor kIntersectColor(150, 115, 240);
const QColor kDefaultMeshColor(80, 160, 255);

constexpr int kGridExtent = 120;
constexpr int kGridStep = 20;

constexpr float kEpsilon = 0.0001f;

} // namespace ViewportConstants

#endif
