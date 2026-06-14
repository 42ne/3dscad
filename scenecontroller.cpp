#include "scenecontroller.h"

#include "expression.h"
#include "openscadparser.h"
#include "scenecommands.h"
#include "scenetreegraphicshelpers.h"
#include "scenestringutils.h"

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QSet>
#include <QUndoStack>

#include <functional>
#include <cmath>
#include <limits>

// ────────────────────────────────────────────────────────────────────────────
// File-local helpers (moved here from mainwindow.cpp)
// ────────────────────────────────────────────────────────────────────────────


static float normalizedRotationDegrees(float value)
{
    while (value >  180.0f) value -= 360.0f;
    while (value < -180.0f) value += 360.0f;
    return value;
}

static QVector3D normalizedRotation(const QVector3D &r)
{
    return QVector3D(normalizedRotationDegrees(r.x()),
                     normalizedRotationDegrees(r.y()),
                     normalizedRotationDegrees(r.z()));
}

static void collectVariableValues(const SceneDocument::TreeNode &node, QHash<QString, qreal> *values)
{
    if (!values)
        return;
    if (node.type == SceneDocument::TreeNode::Variable) {
        (*values)[node.variableName] = node.variableValue;
        return;
    }
    for (const SceneDocument::TreeNode &child : node.children)
        collectVariableValues(child, values);
}

static QString formatNumber(qreal val)
{
    QString s = QString::number(val, 'f', 3);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s.chop(1);
    }
    if (s == QStringLiteral("-0"))
        s = QStringLiteral("0");
    return s;
}

static QString offsetExpression(const QString &expression, qreal delta)
{
    if (expression.isEmpty())
        return {};

    const QString trimmed = expression.trimmed();

    bool isPlainNumber = false;
    qreal numVal = trimmed.toDouble(&isPlainNumber);
    if (isPlainNumber) {
        qreal newVal = numVal + delta;
        return formatNumber(newVal);
    }

    static const QRegularExpression trailingConst(
        QStringLiteral(R"(^(.*?)([+-]\s*\d+(?:\.\d+)?)\s*$)"));

    QRegularExpressionMatch m = trailingConst.match(trimmed);
    if (m.hasMatch()) {
        QString prefix = m.capturedView(1).trimmed().toString();
        QString constStr = m.capturedView(2).toString();
        qreal constVal = constStr.remove(QLatin1Char(' ')).toDouble();
        qreal newConst = constVal + delta;

        const QString formatted = formatNumber(newConst);
        if (prefix.isEmpty())
            return formatted;
        if (newConst >= 0.0)
            return prefix + QStringLiteral(" + ") + formatted;
        else
            return prefix + QStringLiteral(" - ") + formatNumber(-newConst);
    }

    if (delta >= 0.0)
        return trimmed + QStringLiteral(" + ") + formatNumber(delta);
    else
        return trimmed + QStringLiteral(" - ") + formatNumber(-delta);
}

static QStringList dragExpressionsWithChangedComponentsCleared(const QStringList &startExpressions,
                                                                const QVector3D &delta)
{
    QStringList expressions = startExpressions;
    const float components[] = { delta.x(), delta.y(), delta.z() };
    for (int axis = 0; axis < 3 && axis < expressions.size(); ++axis) {
        if (qFuzzyIsNull(components[axis]))
            continue;
        if (startExpressions[axis].isEmpty()) {
            expressions[axis].clear();
        } else {
            expressions[axis] = offsetExpression(startExpressions[axis],
                                                  static_cast<qreal>(components[axis]));
        }
    }
    return expressions;
}

static bool isStandaloneNumericToken(const QString &expression, int start, int length)
{
    if (start < 0 || length <= 0 || start + length > expression.size())
        return false;
    return expression.mid(start, length) == expression.trimmed();
}

static QString adjustedNumericToken(const QString &expression,
                                    int    start,
                                    int    length,
                                    qreal  delta,
                                    qreal  step,
                                    qreal  minimumValue,
                                    bool   clampMagnitude)
{
    const QString numberText = expression.mid(start, length);
    bool ok = false;
    const qreal value = numberText.toDouble(&ok);
    if (!ok) return QString();

    const int   decimalPoint = numberText.indexOf(QLatin1Char('.'));
    const int   precision    = decimalPoint >= 0
                                   ? qMin(3, numberText.size() - decimalPoint - 1)
                                   : 0;
    const qreal adjusted  = value + delta * step;
    const qreal newValue  = clampMagnitude ? qMax(minimumValue, adjusted) : adjusted;
    QString replacement   = QString::number(newValue, 'f', precision);
    if (precision == 0 && replacement == QStringLiteral("-0"))
        replacement = QStringLiteral("0");
    return replacement;
}

static QString adjustedNumberReplacement(const QString &expression,
                                          int start, int length,
                                          qreal delta, qreal minValue)
{
    const QString numberText = expression.mid(start, length);
    if (numberText.isEmpty()) return QString();

    const int decimalPoint = numberText.indexOf(QLatin1Char('.'));
    const int precision = decimalPoint >= 0
                              ? qMin(3, numberText.size() - decimalPoint - 1) : 0;
    const qreal step = precision > 0 ? 0.1 : 1.0;
    const bool standalone = isStandaloneNumericToken(expression, start, length);
    return adjustedNumericToken(expression, start, length,
                                delta, step, minValue, !standalone);
}

// ── Convex hull helper ──────────────────────────────────────────────────────
// Brute-force O(n⁴) convex hull for small point sets (<100 points typical).
// Returns faces as CCW-ordered vertex indices, oriented outward.
static QVector<QVector<int>> computeConvexHullFaces(const QVector<QVector3D> &pts)
{
    const int n = pts.size();
    if (n < 4) return {};

    const qreal eps = 1e-8f;
    QVector3D centroid;
    for (const auto &p : pts) centroid += p;
    centroid /= static_cast<qreal>(n);

    // Step 1: collect all valid face triangles
    struct TriFace { int i, j, k; QVector3D normal; };
    QVector<TriFace> triFaces;

    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            for (int k = j + 1; k < n; ++k) {
                const QVector3D &a = pts[i], &b = pts[j], &c = pts[k];
                QVector3D normal = QVector3D::crossProduct(b - a, c - a);
                const qreal len = normal.length();
                if (len < eps) continue;
                normal /= len;

                bool allFront = true, allBack = true;
                for (int l = 0; l < n; ++l) {
                    if (l == i || l == j || l == k) continue;
                    const qreal d = QVector3D::dotProduct(pts[l] - a, normal);
                    if (d > eps) allBack = false;
                    else if (d < -eps) allFront = false;
                    if (!allFront && !allBack) break;
                }

                if (allFront || allBack) {
                    QVector3D triCenter = (a + b + c) / 3.0f;
                    if (QVector3D::dotProduct(normal, centroid - triCenter) > 0)
                        normal = -normal;
                    triFaces.append({i, j, k, normal});
                }
            }
        }
    }

    if (triFaces.isEmpty()) return {};

    // Step 2: group coplanar triangles into face groups
    struct FaceGroup { QVector<int> indices; QVector3D normal; };
    QVector<FaceGroup> groups;

    for (const auto &tri : triFaces) {
        const qreal d = QVector3D::dotProduct(tri.normal, pts[tri.i]);
        bool found = false;
        for (auto &g : groups) {
            if ((g.normal - tri.normal).length() > eps * 100) continue;
            const qreal gd = QVector3D::dotProduct(g.normal, pts[g.indices[0]]);
            if (qAbs(d - gd) > eps * 100) continue;
            for (int idx : {tri.i, tri.j, tri.k})
                if (!g.indices.contains(idx))
                    g.indices.append(idx);
            found = true;
            break;
        }
        if (!found)
            groups.append({{tri.i, tri.j, tri.k}, tri.normal});
    }

    // Step 3: sort each face's vertices into CCW order around the face normal
    QVector<QVector<int>> result;
    for (auto &g : groups) {
        if (g.indices.size() < 3) continue;

        // Use face centroid as origin so all vertices have well-defined angles
        QVector3D centroid;
        for (int idx : g.indices)
            centroid += pts[idx];
        centroid /= static_cast<float>(g.indices.size());

        const QVector3D ref = qAbs(g.normal.x()) < 0.9f
                                  ? QVector3D(1, 0, 0) : QVector3D(0, 1, 0);
        const QVector3D basis1 = QVector3D::crossProduct(g.normal, ref).normalized();
        const QVector3D basis2 = QVector3D::crossProduct(g.normal, basis1).normalized();

        std::sort(g.indices.begin(), g.indices.end(),
                  [&](int a, int b) {
                      const QVector3D da = pts[a] - centroid;
                      const QVector3D db = pts[b] - centroid;
                      const qreal aa = std::atan2(QVector3D::dotProduct(da, basis2),
                                                  QVector3D::dotProduct(da, basis1));
                      const qreal ab = std::atan2(QVector3D::dotProduct(db, basis2),
                                                  QVector3D::dotProduct(db, basis1));
                      return aa < ab;
                  });
        result.append(g.indices);
    }
    return result;
}

// ── Non-convex polyhedron face builder ──────────────────────────────────────
// Used when points lie on exactly two parallel Z-planes (extrusion profile).

// Signed area of triangle (a,b,c) projected to XY (positive = CCW).
static float cross2DXY(const QVector3D &a, const QVector3D &b, const QVector3D &c)
{
    return (b.x()-a.x())*(c.y()-a.y()) - (b.y()-a.y())*(c.x()-a.x());
}

// True if segments (a,b) and (c,d) properly intersect (not touching at shared endpoints).
static bool segsCross2DXY(const QVector3D &a, const QVector3D &b,
                          const QVector3D &c, const QVector3D &d)
{
    const float d1 = cross2DXY(c, d, a), d2 = cross2DXY(c, d, b);
    const float d3 = cross2DXY(a, b, c), d4 = cross2DXY(a, b, d);
    return ((d1>0.0f&&d2<0.0f)||(d1<0.0f&&d2>0.0f)) &&
           ((d3>0.0f&&d4<0.0f)||(d3<0.0f&&d4>0.0f));
}

// True if the polygon (index list) has no self-intersecting edges in XY.
static bool polygonValidXY(const QVector<int> &ring, const QVector<QVector3D> &pts)
{
    const int n = ring.size();
    for (int i = 0; i < n; ++i)
        for (int j = i+2; j < n; ++j) {
            if (i==0 && j==n-1) continue;
            if (segsCross2DXY(pts[ring[i]], pts[ring[(i+1)%n]],
                              pts[ring[j]], pts[ring[(j+1)%n]])) return false;
        }
    return true;
}

// Sort index set into a CCW simple polygon order using angle from centroid.
// Falls back to insertion order if the centroid-sort still produces crossings.
static QVector<int> sortPolygonCCW(const QVector<int> &indices, const QVector<QVector3D> &pts)
{
    // Prefer insertion order if it's already valid (typical for templates).
    if (polygonValidXY(indices, pts)) {
        // Ensure CCW
        float area = 0.0f;
        const int n = indices.size();
        for (int i = 0; i < n; ++i) {
            const QVector3D &a = pts[indices[i]], &b = pts[indices[(i+1)%n]];
            area += a.x()*b.y() - b.x()*a.y();
        }
        if (area >= 0.0f) return indices;
        QVector<int> rev = indices;
        std::reverse(rev.begin(), rev.end());
        return rev;
    }

    // Angle-sort around centroid
    float cx = 0, cy = 0;
    for (int i : indices) { cx += pts[i].x(); cy += pts[i].y(); }
    cx /= indices.size(); cy /= indices.size();

    QVector<int> sorted = indices;
    std::sort(sorted.begin(), sorted.end(), [&](int a, int b) {
        return std::atan2(pts[a].y()-cy, pts[a].x()-cx) <
               std::atan2(pts[b].y()-cy, pts[b].x()-cx);
    });

    // Ensure CCW
    float area = 0.0f;
    const int n = sorted.size();
    for (int i = 0; i < n; ++i) {
        const QVector3D &a = pts[sorted[i]], &b = pts[sorted[(i+1)%n]];
        area += a.x()*b.y() - b.x()*a.y();
    }
    if (area < 0.0f) std::reverse(sorted.begin(), sorted.end());
    return sorted;
}

// Build all faces for a 2-layer extrusion: bottom cap + top cap + side quads.
static QVector<QVector<int>> buildExtrusionFaces(const QVector<int> &bottomIdx,
                                                  const QVector<int> &topIdx,
                                                  const QVector<QVector3D> &pts)
{
    const QVector<int> bottomPoly = sortPolygonCCW(bottomIdx, pts);
    const int N = bottomPoly.size();

    // Match each bottom vertex to its corresponding top vertex (nearest XY).
    QVector<int> topPoly(N);
    for (int i = 0; i < N; ++i) {
        float best = std::numeric_limits<float>::max();
        topPoly[i] = topIdx[0];
        for (int t : topIdx) {
            const float dx = pts[t].x()-pts[bottomPoly[i]].x();
            const float dy = pts[t].y()-pts[bottomPoly[i]].y();
            const float d = dx*dx + dy*dy;
            if (d < best) { best = d; topPoly[i] = t; }
        }
    }

    QVector<QVector<int>> faces;

    // Bottom cap: reverse polygon winding for outward normal -Z.
    {
        QVector<int> bottomCap = bottomPoly;
        std::reverse(bottomCap.begin(), bottomCap.end());
        faces.append(bottomCap);
    }
    // Top cap: CCW polygon winding for outward normal +Z.
    {
        faces.append(topPoly);
    }
    // Side quads
    for (int i = 0; i < N; ++i) {
        const int b0=bottomPoly[i], b1=bottomPoly[(i+1)%N];
        const int t0=topPoly[i],    t1=topPoly[(i+1)%N];
        faces.append({b0, b1, t1, t0});
    }
    return faces;
}

static qreal cross2D(const QVector3D &origin, const QVector3D &a, const QVector3D &b)
{
    return (a.x() - origin.x()) * (b.y() - origin.y())
           - (a.y() - origin.y()) * (b.x() - origin.x());
}

static QVector<int> convexHullXY(QVector<int> indices, const QVector<QVector3D> &pts)
{
    if (indices.size() < 3)
        return {};

    std::sort(indices.begin(), indices.end(), [&](int left, int right) {
        if (!qFuzzyCompare(pts[left].x(), pts[right].x()))
            return pts[left].x() < pts[right].x();
        if (!qFuzzyCompare(pts[left].y(), pts[right].y()))
            return pts[left].y() < pts[right].y();
        return left < right;
    });

    QVector<int> hull;
    hull.reserve(indices.size() * 2);
    constexpr qreal eps = 1.0e-6;

    for (int idx : indices) {
        while (hull.size() >= 2
               && cross2D(pts[hull[hull.size() - 2]], pts[hull.last()], pts[idx]) <= eps)
            hull.removeLast();
        hull.append(idx);
    }

    const int lowerSize = hull.size();
    for (int i = indices.size() - 2; i >= 0; --i) {
        const int idx = indices[i];
        while (hull.size() > lowerSize
               && cross2D(pts[hull[hull.size() - 2]], pts[hull.last()], pts[idx]) <= eps)
            hull.removeLast();
        hull.append(idx);
    }

    if (!hull.isEmpty())
        hull.removeLast();
    return hull.size() >= 3 ? hull : QVector<int>();
}

static QVector<int> mapTopLoopFromBottom(const QVector<int> &bottomLoop,
                                         const QVector<int> &topIdx,
                                         const QVector<QVector3D> &pts)
{
    QVector<int> topLoop;
    topLoop.reserve(bottomLoop.size());
    QSet<int> usedTop;

    for (int bottom : bottomLoop) {
        float best = std::numeric_limits<float>::max();
        int bestTop = -1;
        for (int top : topIdx) {
            if (usedTop.contains(top))
                continue;
            const float dx = pts[top].x() - pts[bottom].x();
            const float dy = pts[top].y() - pts[bottom].y();
            const float d = dx * dx + dy * dy;
            if (d < best) {
                best = d;
                bestTop = top;
            }
        }
        if (bestTop < 0)
            return {};
        usedTop.insert(bestTop);
        topLoop.append(bestTop);
    }

    return topLoop;
}

static bool pointOnSegmentXY(const QVector3D &point,
                             const QVector3D &a,
                             const QVector3D &b)
{
    constexpr qreal eps = 1.0e-5;
    if (qAbs(cross2D(a, b, point)) > eps)
        return false;

    return point.x() >= qMin(a.x(), b.x()) - eps
           && point.x() <= qMax(a.x(), b.x()) + eps
           && point.y() >= qMin(a.y(), b.y()) - eps
           && point.y() <= qMax(a.y(), b.y()) + eps;
}

static bool allPointsStrictlyInsideHullXY(const QVector<int> &points,
                                          const QVector<int> &hull,
                                          const QVector<QVector3D> &pts)
{
    if (points.isEmpty() || hull.size() < 3)
        return false;

    for (int idx : points) {
        for (int i = 0; i < hull.size(); ++i) {
            const QVector3D &a = pts[hull[i]];
            const QVector3D &b = pts[hull[(i + 1) % hull.size()]];
            if (pointOnSegmentXY(pts[idx], a, b))
                return false;
        }
    }

    return true;
}

static QVector<QVector<int>> buildSingleHoleExtrusionFaces(const QVector<int> &bottomIdx,
                                                           const QVector<int> &topIdx,
                                                           const QVector<QVector3D> &pts)
{
    if (bottomIdx.size() != topIdx.size() || bottomIdx.size() < 6)
        return {};

    const QVector<int> outer = convexHullXY(bottomIdx, pts);
    if (outer.size() < 3 || outer.size() == bottomIdx.size())
        return {};

    QVector<int> inner;
    for (int idx : bottomIdx)
        if (!outer.contains(idx))
            inner.append(idx);
    if (inner.size() < 3 || inner.size() != outer.size())
        return {};
    if (!allPointsStrictlyInsideHullXY(inner, outer, pts))
        return {};

    QVector<int> innerLoop = sortPolygonCCW(inner, pts);
    if (!polygonValidXY(innerLoop, pts))
        return {};

    const QVector<int> outerTop = mapTopLoopFromBottom(outer, topIdx, pts);
    const QVector<int> innerTop = mapTopLoopFromBottom(innerLoop, topIdx, pts);
    if (outerTop.size() != outer.size() || innerTop.size() != innerLoop.size())
        return {};

    QVector<QVector<int>> faces;
    faces.reserve(outer.size() * 4);
    const int n = outer.size();
    for (int i = 0; i < n; ++i) {
        const int next = (i + 1) % n;
        faces.append({outer[i], innerLoop[i], innerLoop[next], outer[next]});
    }
    for (int i = 0; i < n; ++i) {
        const int next = (i + 1) % n;
        faces.append({outerTop[i], outerTop[next], innerTop[next], innerTop[i]});
    }
    for (int i = 0; i < n; ++i) {
        const int next = (i + 1) % n;
        faces.append({outer[i], outer[next], outerTop[next], outerTop[i]});
    }
    for (int i = 0; i < n; ++i) {
        const int next = (i + 1) % n;
        faces.append({innerLoop[next], innerLoop[i], innerTop[i], innerTop[next]});
    }
    return faces;
}

static QVector<QVector<QVector<int>>> computePolyhedronFaceVariants(const QVector<QVector3D> &pts)
{
    QVector<QVector<QVector<int>>> variants;
    const int n = pts.size();
    if (n < 4)
        return variants;

    QElapsedTimer timer;
    timer.start();

    float zMin = pts[0].z(), zMax = pts[0].z();
    for (const auto &p : pts) {
        zMin = qMin(zMin, p.z());
        zMax = qMax(zMax, p.z());
    }

    const float zEps = 0.01f;
    if (zMax - zMin > zEps) {
        QVector<int> bottom, top, other;
        for (int i = 0; i < n; ++i) {
            if      (qAbs(pts[i].z() - zMin) <= zEps) bottom.append(i);
            else if (qAbs(pts[i].z() - zMax) <= zEps) top.append(i);
            else    other.append(i);
        }
        if (other.isEmpty() && bottom.size() == top.size() && bottom.size() >= 3) {
            const QVector<QVector<int>> holeFaces = buildSingleHoleExtrusionFaces(bottom, top, pts);
            if (!holeFaces.isEmpty())
                variants.append(holeFaces);

            if (variants.isEmpty()) {
                const QVector<QVector<int>> extrusionFaces = buildExtrusionFaces(bottom, top, pts);
                if (!extrusionFaces.isEmpty())
                    variants.append(extrusionFaces);
            }
        }
    }

    if (variants.isEmpty() && timer.elapsed() < 150) {
        const QVector<QVector<int>> hullFaces = computeConvexHullFaces(pts);
        if (!hullFaces.isEmpty())
            variants.append(hullFaces);
    }

    return variants;
}

// ─────────────────────────────────────────────────────────────────────────────

static ShapeNode makeShapeForTool(const QString &toolName, int shapeNumber)
{
    auto displayName = [](const QString &tool) -> QString {
        if (tool == QLatin1String("point_3d")) return QStringLiteral("Point");
        if (tool == QLatin1String("face_3d"))  return QStringLiteral("Face");
        return tool.left(1).toUpper() + tool.mid(1);
    };

    ShapeNode shape;
    shape.name = QString("%1 %2").arg(displayName(toolName)).arg(shapeNumber);

    if (toolName == QStringLiteral("circle")) {
        shape.type   = ShapeNode::Circle;
        shape.radius = 10.0f;
    } else if (toolName == QStringLiteral("square")) {
        shape.type = ShapeNode::Square;
        shape.size = QVector3D(20, 20, 0.1f);
    } else if (toolName == QStringLiteral("polygon")) {
        shape.type = ShapeNode::Polygon2D;
        shape.polyhedronPoints = {
            QVector3D(0.0f, 12.0f, 0.0f),
            QVector3D(-10.0f, -8.0f, 0.0f),
            QVector3D(10.0f, -8.0f, 0.0f)
        };
    } else if (toolName == QStringLiteral("sphere")) {
        shape.type   = ShapeNode::Sphere;
        shape.radius = 10.0f;
    } else if (toolName == QStringLiteral("cylinder")) {
        shape.type   = ShapeNode::Cylinder;
        shape.radius = 10.0f;
        shape.height = 30.0f;
    } else if (toolName == QStringLiteral("cone")) {
        shape.type    = ShapeNode::Cone;
        shape.radius  = 10.0f;
        shape.radius2 = 0.0f;
        shape.height  = 30.0f;
    } else if (toolName == QStringLiteral("point_3d")) {
        shape.type = ShapeNode::Point3D;
        shape.position = QVector3D(0, 0, 0);
    } else if (toolName == QStringLiteral("face_3d")) {
        shape.type = ShapeNode::Face3D;
        QVector<int> f; f << 0 << 1 << 2;
        shape.polyhedronFaces.append(f);
    } else if (toolName == QStringLiteral("text")) {
        shape.type = ShapeNode::Text;
        shape.textValue = QStringLiteral("Text");
        shape.textSize = 10.0f;
        shape.parameterExpressions = QStringList({QStringLiteral("10"), QStringLiteral("1")});
    } else {
        shape.type = ShapeNode::Cube;
        shape.size = QVector3D(20, 20, 20);
    }
    return shape;
}

static bool isVariableTool(const QString &toolName)
{
    return toolName == QStringLiteral("var") || toolName == QStringLiteral("variable");
}

// ────────────────────────────────────────────────────────────────────────────
// SceneController
// ────────────────────────────────────────────────────────────────────────────

SceneController::SceneController(QObject *parent)
    : QObject(parent)
{
    m_undoStack  = new QUndoStack(this);
    m_undoAction = m_undoStack->createUndoAction(this, QStringLiteral("Undo"));
    m_redoAction = m_undoStack->createRedoAction(this, QStringLiteral("Redo"));
    m_undoAction->setShortcut(QKeySequence::Undo);
    m_redoAction->setShortcut(QKeySequence::Redo);
}

// ── Helpers ──────────────────────────────────────────────────────────────────

void SceneController::pushCommandIfValid(QUndoCommand *command)
{
    // Commands with isValid() == false are discarded to avoid touching the stack.
    // Commands that don't implement isValid() are always pushed.
    m_undoStack->push(command);
}

void SceneController::setCtrlHighlight(int nodeId, const QString &contextPrefix,
                                        const QString &expression,
                                        int start, int replacementSize)
{
    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        m_ctrlHighlight.active        = true;
        m_ctrlHighlight.nodeId        = nodeId;
        m_ctrlHighlight.contextPrefix = contextPrefix;
        m_ctrlHighlight.expression    = expression;
        m_ctrlHighlight.numberStart   = start;
        m_ctrlHighlight.numberLength  = replacementSize;
    } else {
        m_ctrlHighlight.active = false;
    }
}

void SceneController::selectShapeInternal(int shapeId)
{
    m_scene.setSelectedShapeId(shapeId);

    int nodeId = 0;
    if (shapeId >= 0) {
        std::function<int(const SceneDocument::TreeNode &)> find =
            [&](const SceneDocument::TreeNode &n) -> int {
            if (n.type == SceneDocument::TreeNode::Primitive && n.shapeId == shapeId)
                return n.id;
            for (const auto &child : n.children) {
                const int found = find(child);
                if (found > 0) return found;
            }
            return 0;
        };
        nodeId = find(m_scene.treeRoot());
    }
    m_selectedTreeNodeId = nodeId;
    emit selectionChanged(nodeId);
}

int SceneController::selectedTreeGroupId() const
{
    if (m_selectedTreeNodeId <= 0)
        return 0;

    const SceneDocument::TreeNode *node = m_scene.treeNodeById(m_selectedTreeNodeId);
    if (!node) return 0;

    if (node->type == SceneDocument::TreeNode::Group)
        return m_selectedTreeNodeId;

    std::function<int(const SceneDocument::TreeNode &)> findParent =
        [&](const SceneDocument::TreeNode &parent) -> int {
        for (const auto &child : parent.children) {
            if (child.id == m_selectedTreeNodeId) return parent.id;
            const int found = findParent(child);
            if (found > 0) return found;
        }
        return 0;
    };
    return findParent(m_scene.treeRoot());
}

int SceneController::selectedDirectGroupId() const
{
    if (m_selectedTreeNodeId <= 0) return 0;
    const SceneDocument::TreeNode *node = m_scene.treeNodeById(m_selectedTreeNodeId);
    if (!node || node->type != SceneDocument::TreeNode::Group) return 0;
    return m_selectedTreeNodeId;
}

// ── Selection ─────────────────────────────────────────────────────────────────

void SceneController::selectShape(int shapeId)
{
    selectShapeInternal(shapeId);
}

void SceneController::selectTreeNode(int treeNodeId)
{
    m_selectedTreeNodeId = treeNodeId;
    emit selectionChanged(treeNodeId);
}

void SceneController::clearSelection()
{
    m_scene.setSelectedShapeId(-1);
    m_ctrlHighlight = CtrlParamHighlight();
    m_selectedTreeNodeId = 0;
    emit selectionChanged(0);
    emit ctrlHighlightChanged();
}

// ── Scene mutations ───────────────────────────────────────────────────────────

void SceneController::addCube()
{
    ShapeNode s;
    s.type = ShapeNode::Cube;
    s.name = QString("Cube %1").arg(m_scene.shapeCount() + 1);
    s.size = QVector3D(20, 20, 20);
    m_undoStack->push(new AddShapeCommand(&m_scene, s, [this]() { emit sceneChanged(); }));
}

void SceneController::addSphere()
{
    ShapeNode s;
    s.type   = ShapeNode::Sphere;
    s.name   = QString("Sphere %1").arg(m_scene.shapeCount() + 1);
    s.radius = 10;
    m_undoStack->push(new AddShapeCommand(&m_scene, s, [this]() { emit sceneChanged(); }));
}

void SceneController::addCylinder()
{
    ShapeNode s;
    s.type   = ShapeNode::Cylinder;
    s.name   = QString("Cylinder %1").arg(m_scene.shapeCount() + 1);
    s.radius = 10;
    s.height = 30;
    m_undoStack->push(new AddShapeCommand(&m_scene, s, [this]() { emit sceneChanged(); }));
}

void SceneController::addCone()
{
    ShapeNode s;
    s.type    = ShapeNode::Cone;
    s.name    = QString("Cone %1").arg(m_scene.shapeCount() + 1);
    s.radius  = 10;  // r1 bottom
    s.radius2 = 0;   // r2 top → true cone
    s.height  = 30;
    m_undoStack->push(new AddShapeCommand(&m_scene, s, [this]() { emit sceneChanged(); }));
}

void SceneController::addGroup(SceneDocument::TreeNode::Operation operation)
{
    auto *command = new AddGroupCommand(&m_scene, operation,
                                        selectedTreeGroupId(), -1,
                                        [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

void SceneController::moveTreeNodeToGroup(int nodeId, int parentGroupId, int insertIndex, bool isParameterZone)
{
    const bool moduleParameterZone = isParameterZone;
    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);

    // Safety guard: only Module and Scene groups may become direct root children.
    // The widget filters these at drop time; this is a last-resort check.
    const int rootId = m_scene.treeRoot().id;
    if (parentGroupId == rootId && node) {
        const bool rootEligible = node->type == SceneDocument::TreeNode::Group
            && (node->operation == SceneDocument::TreeNode::Module
             || node->operation == SceneDocument::TreeNode::Scene);
        if (!rootEligible)
            return;
    }

    if (node && node->type == SceneDocument::TreeNode::Variable && parentGroupId > 0) {
        const SceneDocument::TreeNode *parentNode = m_scene.treeNodeById(parentGroupId);
        const bool targetIsRoot   = (parentGroupId == m_scene.treeRoot().id);
        const bool targetIsModule = parentNode
                                    && parentNode->type == SceneDocument::TreeNode::Group
                                    && parentNode->operation == SceneDocument::TreeNode::Module;
        if (!targetIsRoot && !targetIsModule) return;
    }
    if (node && node->type == SceneDocument::TreeNode::ModuleCall) {
        const SceneDocument::TreeNode *parentNode = m_scene.treeNodeById(parentGroupId);
        if (!parentNode || parentNode->type != SceneDocument::TreeNode::Group) return;
    }

    auto *command = new MoveTreeNodeCommand(&m_scene, nodeId, parentGroupId, insertIndex,
                                             [this]() { emit sceneChanged(); },
                                             moduleParameterZone);
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

bool SceneController::applyCode(const QString &code,
                                 QString *errorMessage,
                                 int     *errorLine)
{
    SceneDocument::Snapshot snapshot;
    QString localError;
    int     localLine = -1;

    if (!OpenScadParser::parseScene(code, &snapshot, &localError, &localLine)) {
        if (errorMessage) *errorMessage = localError;
        if (errorLine)    *errorLine    = localLine;
        return false;
    }

    auto *command = new ReplaceSceneCommand(&m_scene, snapshot,
                                             [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return true; }
    m_undoStack->push(command);
    return true;
}

// ── Viewport drag: shape ──────────────────────────────────────────────────────

void SceneController::handleShapeDragStarted(int index)
{
    m_scene.setSelectedIndex(index);
    selectShapeInternal(m_scene.selectedShapeId());

    const ShapeNode *shape = m_scene.selectedShape();
    if (!shape) return;
    m_shapeDragStartShape = *shape;
    m_shapeDragActive = true;
}

void SceneController::handleShapeDragged(int index, const QVector3D &delta)
{
    if (!m_shapeDragActive || m_scene.selectedIndex() != index) return;
    ShapeNode *shape = m_scene.selectedShape();
    if (!shape) return;
    *shape = m_shapeDragStartShape;
    shape->position = m_shapeDragStartShape.position + delta;
    emit liveViewportUpdate();
}

void SceneController::handleShapeDragFinished(int index)
{
    if (!m_shapeDragActive || m_scene.selectedIndex() != index) return;
    m_shapeDragActive = false;

    const ShapeNode *shape = m_scene.selectedShape();
    if (!shape) return;

    auto *command = new UpdateShapeCommand(&m_scene,
                                           m_shapeDragStartShape, *shape,
                                           [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

void SceneController::handleShapeRotationDragStarted(int index)
{
    handleShapeDragStarted(index);
}

void SceneController::handleShapeRotated(int index, const QVector3D &deltaDegrees)
{
    if (!m_shapeDragActive || m_scene.selectedIndex() != index) return;
    ShapeNode *shape = m_scene.selectedShape();
    if (!shape) return;
    *shape = m_shapeDragStartShape;
    shape->rotation = normalizedRotation(m_shapeDragStartShape.rotation + deltaDegrees);
    emit liveViewportUpdate();
}

void SceneController::handleShapeRotationDragFinished(int index)
{
    handleShapeDragFinished(index);
}

// ── Viewport drag: group ─────────────────────────────────────────────────────

void SceneController::handleGroupDragStarted(int groupId)
{
    selectTreeNode(groupId);

    const SceneDocument::TreeNode *group = m_scene.treeNodeById(groupId);
    if (!group || group->type != SceneDocument::TreeNode::Group) return;

    m_groupDragId         = groupId;
    m_groupDragStartPos   = group->position;
    m_groupDragStartRot   = group->rotation;
    m_groupDragStartScale = group->scale;
    m_groupDragStartExpressions = group->transformExpressions;
    m_groupDragActive     = true;
}

void SceneController::handleGroupDragged(int groupId, const QVector3D &delta)
{
    if (!m_groupDragActive || m_groupDragId != groupId) return;
    m_scene.updateGroupTransform(groupId,
                                 m_groupDragStartPos + delta,
                                 m_groupDragStartRot,
                                 m_groupDragStartScale,
                                 dragExpressionsWithChangedComponentsCleared(m_groupDragStartExpressions, delta));
    emit liveViewportUpdate();
}

void SceneController::handleGroupDragFinished(int groupId)
{
    if (!m_groupDragActive || m_groupDragId != groupId) return;
    m_groupDragActive = false;

    const SceneDocument::TreeNode *group = m_scene.treeNodeById(groupId);
    if (!group || group->type != SceneDocument::TreeNode::Group) return;

    const QVector3D finalPos   = group->position;
    const QVector3D finalRot   = group->rotation;
    const QVector3D finalScale = group->scale;
    if (finalPos == m_groupDragStartPos
        && finalRot == m_groupDragStartRot
        && finalScale == m_groupDragStartScale)
        return;

    const SceneDocument::Snapshot newSnapshot = m_scene.snapshot();
    m_scene.updateGroupTransform(groupId,
                                 m_groupDragStartPos,
                                 m_groupDragStartRot,
                                 m_groupDragStartScale,
                                 m_groupDragStartExpressions);
    const SceneDocument::Snapshot oldSnapshot = m_scene.snapshot();
    m_scene.restoreSnapshot(newSnapshot);

    auto *command = new UpdateGroupTransformCommand(&m_scene, oldSnapshot, newSnapshot,
                                                    [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

void SceneController::handleGroupRotationDragStarted(int groupId)
{
    handleGroupDragStarted(groupId);
}

void SceneController::handleGroupRotated(int groupId, const QVector3D &deltaDegrees)
{
    if (!m_groupDragActive || m_groupDragId != groupId) return;
    m_scene.updateGroupTransform(groupId,
                                 m_groupDragStartPos,
                                 normalizedRotation(m_groupDragStartRot + deltaDegrees),
                                 m_groupDragStartScale,
                                 dragExpressionsWithChangedComponentsCleared(m_groupDragStartExpressions, deltaDegrees));
    emit liveViewportUpdate();
}

void SceneController::handleGroupRotationDragFinished(int groupId)
{
    handleGroupDragFinished(groupId);
}

static QVector<int> pointShapeIdsForPolyhedronElements(const SceneDocument &scene,
                                                       const QVector<int> &elementNodeIds)
{
    QVector<int> pointShapeIds;
    auto appendUniqueShapeId = [&](int shapeId) {
        if (shapeId > 0 && !pointShapeIds.contains(shapeId))
            pointShapeIds.append(shapeId);
    };

    for (int nodeId : elementNodeIds) {
        const SceneDocument::TreeNode *node = scene.treeNodeById(nodeId);
        if (!node || node->type != SceneDocument::TreeNode::Primitive)
            continue;

        const ShapeNode *shape = scene.shapeById(node->shapeId);
        if (!shape)
            continue;

        if (shape->type == ShapeNode::Point3D) {
            appendUniqueShapeId(shape->id);
            continue;
        }

        if (shape->type != ShapeNode::Face3D || shape->polyhedronFaces.isEmpty())
            continue;

        int parentGroupId = 0;
        if (!SceneDocument::findChildParent(scene.treeRoot(), nodeId, &parentGroupId, nullptr))
            continue;
        const SceneDocument::TreeNode *parent = scene.treeNodeById(parentGroupId);
        if (!parent || parent->operation != SceneDocument::TreeNode::Polyhedron)
            continue;

        QVector<int> groupPointShapeIds;
        for (const SceneDocument::TreeNode &child : parent->children) {
            if (child.type != SceneDocument::TreeNode::Primitive)
                continue;
            const ShapeNode *pointShape = scene.shapeById(child.shapeId);
            if (pointShape && pointShape->type == ShapeNode::Point3D)
                groupPointShapeIds.append(pointShape->id);
        }

        for (int pointIndex : shape->polyhedronFaces.first()) {
            if (pointIndex >= 0 && pointIndex < groupPointShapeIds.size())
                appendUniqueShapeId(groupPointShapeIds[pointIndex]);
        }
    }

    return pointShapeIds;
}

void SceneController::handlePolyhedronElementsDragStarted(const QVector<int> &elementNodeIds)
{
    m_polyhedronElementsDragStartPoints.clear();
    const QVector<int> pointShapeIds = pointShapeIdsForPolyhedronElements(m_scene, elementNodeIds);
    for (int shapeId : pointShapeIds) {
        const ShapeNode *shape = m_scene.shapeById(shapeId);
        if (shape && shape->type == ShapeNode::Point3D)
            m_polyhedronElementsDragStartPoints.append(*shape);
    }

    if (m_polyhedronElementsDragStartPoints.isEmpty()) {
        m_polyhedronElementsDragActive = false;
        return;
    }

    m_polyhedronElementsDragStartSnapshot = m_scene.snapshot();
    m_polyhedronElementsDragActive = true;
}

void SceneController::handlePolyhedronElementsDragged(const QVector3D &delta)
{
    if (!m_polyhedronElementsDragActive)
        return;

    for (const ShapeNode &startShape : m_polyhedronElementsDragStartPoints) {
        ShapeNode updated = startShape;
        updated.position = startShape.position + delta;
        if (ShapeNode *shape = m_scene.shapeById(startShape.id))
            *shape = updated;
    }
    emit liveViewportUpdate();
}

void SceneController::handlePolyhedronElementsDragFinished()
{
    if (!m_polyhedronElementsDragActive)
        return;
    m_polyhedronElementsDragActive = false;

    bool changed = false;
    for (const ShapeNode &startShape : m_polyhedronElementsDragStartPoints) {
        const ShapeNode *shape = m_scene.shapeById(startShape.id);
        if (shape && shape->position != startShape.position) {
            changed = true;
            break;
        }
    }
    if (!changed) {
        m_polyhedronElementsDragStartPoints.clear();
        return;
    }

    const SceneDocument::Snapshot newSnapshot = m_scene.snapshot();
    m_scene.restoreSnapshot(m_polyhedronElementsDragStartSnapshot);

    auto *command = new ReplaceSceneCommand(&m_scene, newSnapshot,
                                            [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
    m_polyhedronElementsDragStartPoints.clear();
}

// ── Graphics-tree: tool drop ──────────────────────────────────────────────────

void SceneController::handleToolDrop(const QString &toolName, int parentGroupId, int insertIndex, bool isParameterZone)
{
    const bool moduleParameterZone = isParameterZone;

    if (isVariableTool(toolName)) {
        const int rootId  = m_scene.treeRoot().id;
        const int sceneId = m_scene.sceneNodeId();

        const SceneDocument::TreeNode *parentNode =
            parentGroupId > 0 ? m_scene.treeNodeById(parentGroupId) : nullptr;
        const bool inModule = parentNode
                              && parentNode->type == SceneDocument::TreeNode::Group
                              && parentNode->operation == SceneDocument::TreeNode::Module;

        if (parentGroupId > 0 && !inModule && parentGroupId != rootId && parentGroupId != sceneId)
            return;

        if (inModule) {
            struct AddModuleParamCommand : public QUndoCommand {
                SceneDocument *scene; int moduleId; int insertIdx; bool parameter;
                std::function<void()> refresh; int addedId = 0;
                AddModuleParamCommand(SceneDocument *s, int mid, int idx, bool isParam,
                                      std::function<void()> r)
                    : scene(s), moduleId(mid), insertIdx(idx), parameter(isParam), refresh(r) {}
                void redo() override { addedId = scene->addVariableToModule(moduleId, parameter, insertIdx); if (refresh) refresh(); }
                void undo() override { if (addedId > 0) { scene->removeVariableById(addedId); if (refresh) refresh(); } }
                bool isValid() const { return scene && moduleId > 0; }
            };
            auto *cmd = new AddModuleParamCommand(&m_scene, parentGroupId, insertIndex,
                                                   moduleParameterZone,
                                                   [this]() { emit sceneChanged(); });
            if (!cmd->isValid()) { delete cmd; return; }
            m_undoStack->push(cmd);
            return;
        }

        auto *command = new AddVariableCommand(&m_scene, insertIndex,
                                               [this]() { emit sceneChanged(); });
        if (!command->isValid()) { delete command; return; }
        m_undoStack->push(command);
        return;
    }

    if (toolName == QLatin1String("polyhedron")) {
        if (parentGroupId <= 0 || parentGroupId == m_scene.treeRoot().id)
            parentGroupId = m_scene.sceneNodeId();
        auto *cmd = new AddPolyhedronGroupCommand(&m_scene, parentGroupId, insertIndex,
                                                   [this]() { emit sceneChanged(); });
        if (!cmd->isValid()) { delete cmd; return; }
        m_undoStack->push(cmd);
        return;
    }

    SceneDocument::TreeNode::Operation operation;
    if (SceneTreeGraphics::operationForToolName(toolName, &operation)) {
        if (operation == SceneDocument::TreeNode::Module) {
            if (parentGroupId > 0 && parentGroupId != m_scene.treeRoot().id)
                return;
            parentGroupId = 0;
        } else if (parentGroupId <= 0 || parentGroupId == m_scene.treeRoot().id) {
            parentGroupId = m_scene.sceneNodeId();
        }

        if (operation == SceneDocument::TreeNode::RawCode) {
            auto *command = new AddRawCodeCommand(&m_scene,
                                                  QStringLiteral("// raw OpenSCAD"),
                                                  parentGroupId,
                                                  insertIndex,
                                                  [this]() { emit sceneChanged(); });
            if (!command->isValid()) { delete command; return; }
            m_undoStack->push(command);
            return;
        }

        auto *command = new AddGroupCommand(&m_scene, operation, parentGroupId, insertIndex,
                                            [this]() { emit sceneChanged(); });
        if (!command->isValid()) { delete command; return; }
        m_undoStack->push(command);
        return;
    }

    if (!ShapeNode::isPrimitiveTool(toolName))
        return;

    ShapeNode shape = makeShapeForTool(toolName, m_scene.shapeCount() + 1);
    m_undoStack->push(new AddShapeCommand(&m_scene, shape, parentGroupId, insertIndex,
                                          [this]() { emit sceneChanged(); }));
}

// ── Graphics-tree: module call drop ──────────────────────────────────────────

void SceneController::handleModuleCallDrop(int moduleGroupId, int parentGroupId, int insertIndex)
{
    auto *command = new AddModuleCallCommand(&m_scene, moduleGroupId, parentGroupId, insertIndex,
                                             [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

// ── Graphics-tree: node selected ─────────────────────────────────────────────

void SceneController::handleNodeSelected(int nodeId)
{
    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node) {
        clearSelection();
        return;
    }

    if (node->type == SceneDocument::TreeNode::Primitive) {
        m_scene.setSelectedShapeId(node->shapeId);
        selectShapeInternal(node->shapeId);
    } else {
        m_scene.setSelectedShapeId(-1);
        m_selectedTreeNodeId = node->id;
        emit selectionChanged(node->id);
    }
}

// ── Graphics-tree: node delete ────────────────────────────────────────────────

void SceneController::handleNodeDeleteRequested(int nodeId)
{
    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node) return;

    if (node->type == SceneDocument::TreeNode::ModuleCall) {
        auto *command = new RemoveModuleCallCommand(&m_scene, node->id,
                                                    [this]() { emit sceneChanged(); });
        if (!command->isValid()) { delete command; return; }
        m_undoStack->push(command);
        return;
    }
    if (node->type == SceneDocument::TreeNode::Primitive) {
        const ShapeNode *shape = m_scene.shapeById(node->shapeId);
        if (shape && (shape->type == ShapeNode::Point3D || shape->type == ShapeNode::Face3D)) {
            int parentId = 0;
            int childIndex = -1;
            if (SceneDocument::findChildParent(m_scene.treeRoot(), nodeId, &parentId, &childIndex)) {
                const auto *parent = m_scene.treeNodeById(parentId);
                if (parent && parent->operation == SceneDocument::TreeNode::Polyhedron) {
                    const int pointChildIndex = shape->type == ShapeNode::Point3D ? childIndex : -1;
                    auto *command = new RemovePolyhedronElementCommand(&m_scene,
                                                                       node->shapeId,
                                                                       parentId,
                                                                       pointChildIndex,
                                                                       [this]() { emit sceneChanged(); });
                    if (!command->isValid()) { delete command; return; }
                    m_undoStack->push(command);
                    return;
                }
            }
        }

        auto *command = new DeleteShapeCommand(&m_scene, node->shapeId,
                                               [this]() { emit sceneChanged(); });
        if (!command->isValid()) { delete command; return; }
        m_undoStack->push(command);
        return;
    }
    if (node->type == SceneDocument::TreeNode::Variable) {
        auto *command = new RemoveVariableCommand(&m_scene, node->id,
                                                  [this]() { emit sceneChanged(); });
        if (!command->isValid()) { delete command; return; }
        m_undoStack->push(command);
        return;
    }
    if (node->id == m_scene.treeRoot().id) return;

    if (node->type == SceneDocument::TreeNode::Group
        && node->operation == SceneDocument::TreeNode::Polyhedron) {
        auto *command = new RemovePolyhedronGroupCommand(&m_scene, node->id,
                                                         [this]() { emit sceneChanged(); });
        if (!command->isValid()) { delete command; return; }
        m_undoStack->push(command);
        return;
    }

    auto *command = new RemoveGroupCommand(&m_scene, node->id,
                                           [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

// ── Graphics-tree: transform value adjusted ───────────────────────────────────

void SceneController::handleTransformValueAdjusted(int groupId, int axis,
                                                    int numberStart, int numberLength,
                                                    qreal delta)
{
    if (axis < 0 || qFuzzyIsNull(delta)) return;

    const SceneDocument::TreeNode *group = m_scene.treeNodeById(groupId);
    if (!group || group->type != SceneDocument::TreeNode::Group) return;

    const bool isLinExtr = group->operation == SceneDocument::TreeNode::LinearExtrude;
    const bool isRotExtr = group->operation == SceneDocument::TreeNode::RotateExtrude;

    if (!isLinExtr && !isRotExtr && (axis > 2)) return;
    if (isLinExtr && (axis > 4)) return;
    if (isRotExtr && (axis > 0)) return;
    // Center (axis 1) is boolean — not scrollable
    if (isLinExtr && axis == 1) return;

    const QString currentExpr = (isLinExtr && axis > 2)
        ? QString()
        : SceneTreeGraphics::transformAxisExpression(*group, axis);
    QStringList newExpressions = group->transformExpressions;
    while (newExpressions.size() <= axis) newExpressions.append(QString());

    QVector3D position = group->position;
    QVector3D rotation = group->rotation;
    QVector3D scale    = group->scale;
    float newTwist   = group->linearExtrudeTwist;
    int   newSlices  = group->linearExtrudeSlices;
    float newScaleVal= group->linearExtrudeScaleVal;

    // ── LinearExtrude numeric adjustment ──────────────────────────────────
    if (isLinExtr) {
        const qreal step = (axis == 3) ? 1.0          // slices (integer)
                         : (axis == 4) ? 0.1          // scale value
                         :                1.0;         // height / twist
        auto adjustVal = [&](float current, qreal minV) -> float {
            return float(qMax(minV, double(current) + delta * step));
        };

        if (axis == 0) {
            const qreal minVal = 0.01;
            const float newVal = adjustVal(scale.x(), minVal);
            scale.setX(newVal);
            newExpressions[0] = QString::number(newVal, 'f', 1);
        } else if (axis == 2) {
            newTwist = adjustVal(newTwist, -1e9);
            newExpressions[1] = QString::number(newTwist, 'g');
        } else if (axis == 3) {
            newSlices = int(adjustVal(float(newSlices), 0.0));
            newExpressions[2] = QString::number(newSlices);
        } else if (axis == 4) {
            newScaleVal = adjustVal(newScaleVal, 0.01);
            newExpressions[3] = QString::number(newScaleVal, 'g');
        }

        const SceneDocument::Snapshot oldSnapshot = m_scene.snapshot();
        if (!m_scene.updateGroupLinearExtrudeParams(groupId, scale,
                                                    newTwist, newSlices, newScaleVal,
                                                    newExpressions))
            return;
        const SceneDocument::Snapshot newSnapshot = m_scene.snapshot();
        m_scene.restoreSnapshot(oldSnapshot);
        auto *command = new UpdateGroupTransformCommand(&m_scene, oldSnapshot, newSnapshot,
                                                        [this]() { emit sceneChanged(); });
        m_undoStack->push(command);
        m_ctrlHighlight.active = false;
        emit ctrlHighlightChanged();
        return;
    }

    // ── RotateExtrude numeric adjustment ─────────────────────────────────
    if (isRotExtr) {
        const qreal step = 1.0;
        const float newAngle = float(qMax(-1e9, double(group->scale.x()) + delta * step));
        QVector3D newScale = group->scale;
        newScale.setX(newAngle);
        newExpressions[0] = QString::number(newAngle, 'f', 0);

        const SceneDocument::Snapshot oldSnapshot = m_scene.snapshot();
        if (!m_scene.updateGroupTransform(groupId, position, rotation, newScale, newExpressions))
            return;
        const SceneDocument::Snapshot newSnapshot = m_scene.snapshot();
        m_scene.restoreSnapshot(oldSnapshot);
        auto *command = new UpdateGroupTransformCommand(&m_scene, oldSnapshot, newSnapshot,
                                                        [this]() { emit sceneChanged(); });
        m_undoStack->push(command);
        m_ctrlHighlight.active = false;
        emit ctrlHighlightChanged();
        return;
    }

    // ── Standard transform adjustment ─────────────────────────────────────
    if (axis > 2) return;

    if (numberStart >= 0 && numberLength > 0
        && numberStart + numberLength <= currentExpr.size()) {

        const bool  isScale     = (group->operation == SceneDocument::TreeNode::Scale
                                   || group->operation == SceneDocument::TreeNode::Resize);
        const qreal minVal      = isScale ? 0.01 : -1e9;
        const QString numberText = currentExpr.mid(numberStart, numberLength);
        const int   decimalPoint = numberText.indexOf(QLatin1Char('.'));
        const int   precision    = decimalPoint >= 0
                                       ? qMin(3, numberText.size() - decimalPoint - 1) : 0;
        const qreal step         = precision > 0 ? 0.1 : 1.0;
        const bool  standalone   = isStandaloneNumericToken(currentExpr, numberStart, numberLength);
        const qreal tokenMin     = isScale ? 0.01 : (standalone ? -1e9 : 0.0);
        QString replacement = adjustedNumericToken(currentExpr, numberStart, numberLength,
                                                   delta, step, tokenMin,
                                                   isScale || !standalone);
        if (replacement.isEmpty()) return;
        if (isScale && precision == 0 && !replacement.contains(QLatin1Char('.')))
            replacement = QString::number(replacement.toDouble(), 'f', 1);

        const QString newExpr = currentExpr.left(numberStart)
                                + replacement
                                + currentExpr.mid(numberStart + numberLength);
        newExpressions[axis] = newExpr;

        if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
            QString contextPrefix = QStringLiteral("[");
            for (int i = 0; i < axis; ++i)
                contextPrefix += SceneTreeGraphics::transformAxisExpression(*group, i)
                                 + QStringLiteral(", ");
            m_ctrlHighlight.active        = true;
            m_ctrlHighlight.nodeId        = groupId;
            m_ctrlHighlight.contextPrefix = contextPrefix;
            m_ctrlHighlight.expression    = newExpr;
            m_ctrlHighlight.numberStart   = numberStart;
            m_ctrlHighlight.numberLength  = int(replacement.size());
        } else {
            m_ctrlHighlight.active = false;
        }

        QHash<QString, qreal> varValues;
        collectVariableValues(m_scene.treeRoot(), &varValues);

        qreal newNumeric = replacement.toDouble();
        ExpressionSyntax::evaluate(newExpr, varValues, &newNumeric);
        newNumeric = qMax(minVal, newNumeric);

        const bool isTranslateOrMirror = (group->operation == SceneDocument::TreeNode::Translate
                                        || group->operation == SceneDocument::TreeNode::Mirror);
        if (axis == 0) {
            if (isTranslateOrMirror) position.setX(float(newNumeric));
            else if (group->operation == SceneDocument::TreeNode::Rotate) rotation.setX(float(newNumeric));
            else scale.setX(float(newNumeric));
        } else if (axis == 1) {
            if (isTranslateOrMirror) position.setY(float(newNumeric));
            else if (group->operation == SceneDocument::TreeNode::Rotate) rotation.setY(float(newNumeric));
            else scale.setY(float(newNumeric));
        } else {
            if (isTranslateOrMirror) position.setZ(float(newNumeric));
            else if (group->operation == SceneDocument::TreeNode::Rotate) rotation.setZ(float(newNumeric));
            else scale.setZ(float(newNumeric));
        }
    } else {
        const bool  isScale = (group->operation == SceneDocument::TreeNode::Scale
                            || group->operation == SceneDocument::TreeNode::Resize);
        const qreal step    = group->operation == SceneDocument::TreeNode::Rotate ? 5.0
                            : isScale ? 0.1 : 1.0;
        QVector3D *target   = (group->operation == SceneDocument::TreeNode::Translate
                             || group->operation == SceneDocument::TreeNode::Mirror) ? &position
                            : group->operation == SceneDocument::TreeNode::Rotate    ? &rotation
                                                                                     : &scale;
        auto adjustAxis = [&](float current) -> float {
            return float(isScale ? qMax(0.01, double(current) + delta * step)
                                 : double(current) + delta * step);
        };
        if (axis == 0)      target->setX(adjustAxis(target->x()));
        else if (axis == 1) target->setY(adjustAxis(target->y()));
        else                target->setZ(adjustAxis(target->z()));
        newExpressions[axis].clear();
        m_ctrlHighlight.active = false;
    }

    const SceneDocument::Snapshot oldSnapshot = m_scene.snapshot();
    if (!m_scene.updateGroupTransform(groupId, position, rotation, scale, newExpressions))
        return;
    const SceneDocument::Snapshot newSnapshot = m_scene.snapshot();
    m_scene.restoreSnapshot(oldSnapshot);

    auto *command = new UpdateGroupTransformCommand(&m_scene, oldSnapshot, newSnapshot,
                                                    [this]() { emit sceneChanged(); });
    m_undoStack->push(command);
    emit ctrlHighlightChanged();
}

// ── Graphics-tree: module rename ──────────────────────────────────────────────

void SceneController::handleTransformExpressionEdited(int groupId, int axis, const QString &expression)
{
    const SceneDocument::TreeNode *group = m_scene.treeNodeById(groupId);
    if (!group || group->type != SceneDocument::TreeNode::Group)
        return;

    const bool isLinExtr = group->operation == SceneDocument::TreeNode::LinearExtrude;
    const bool isRotExtr = group->operation == SceneDocument::TreeNode::RotateExtrude;

    // RotateExtrude does not support full-expression mode
    if (isRotExtr && axis < 0)
        return;

    // LinearExtrude full expression: "h=<height>, c=<center>, t=<twist>, sl=<slices>, sc=<scale>"
    if (axis == -1 && isLinExtr) {
        // Parse comma-separated key=value pairs
        const auto parseVal = [&](const QString &src, const QString &key, const QString &altKey) -> QString {
            for (const QString &part : src.split(QLatin1Char(','))) {
                const int eq = part.indexOf(QLatin1Char('='));
                if (eq < 0) continue;
                const QString k = part.left(eq).trimmed().toLower();
                if (k == key || k == altKey)
                    return part.mid(eq + 1).trimmed();
            }
            return QString();
        };
        const QString hStr  = parseVal(expression, QStringLiteral("h"),  QStringLiteral("height"));
        const QString cStr  = parseVal(expression, QStringLiteral("c"),  QStringLiteral("center"));
        const QString tStr  = parseVal(expression, QStringLiteral("t"),  QStringLiteral("twist"));
        const QString slStr = parseVal(expression, QStringLiteral("sl"), QStringLiteral("slices"));
        const QString scStr = parseVal(expression, QStringLiteral("sc"), QStringLiteral("scale"));

        QHash<QString, qreal> varValues;
        collectVariableValues(m_scene.treeRoot(), &varValues);

        QVector3D scale = group->scale;
        float newTwist   = group->linearExtrudeTwist;
        int   newSlices  = group->linearExtrudeSlices;
        float newScaleVal= group->linearExtrudeScaleVal;
        bool  newCenter  = group->linearExtrudeCenter;
        QStringList newExpressions = group->transformExpressions;
        while (newExpressions.size() < 4) newExpressions.append(QString());

        if (!hStr.isEmpty()) {
            qreal v = 0.0;
            if (ExpressionSyntax::evaluate(hStr, varValues, &v)) {
                scale.setX(float(qMax<qreal>(0.01, v)));
                newExpressions[0] = hStr;
            }
        }
        if (!cStr.isEmpty()) {
            const QString cl = cStr.toLower();
            newCenter = (cl == QLatin1String("true") || cl == QLatin1String("1"));
        }
        if (!tStr.isEmpty()) {
            qreal v = 0.0;
            if (ExpressionSyntax::evaluate(tStr, varValues, &v)) {
                newTwist = float(v);
                newExpressions[1] = tStr;
            }
        }
        if (!slStr.isEmpty()) {
            bool ok = false;
            int iv = slStr.toInt(&ok);
            if (ok) { newSlices = qMax(0, iv); newExpressions[2] = slStr; }
        }
        if (!scStr.isEmpty()) {
            qreal v = 0.0;
            if (ExpressionSyntax::evaluate(scStr, varValues, &v)) {
                newScaleVal = float(qMax<qreal>(0.01, v));
                newExpressions[3] = scStr;
            }
        }

        const SceneDocument::Snapshot oldSnapshot = m_scene.snapshot();
        {
            SceneDocument::TreeNode *mut = m_scene.treeNodeById(groupId);
            if (mut) mut->linearExtrudeCenter = newCenter;
        }
        if (!m_scene.updateGroupLinearExtrudeParams(groupId, scale, newTwist, newSlices, newScaleVal, newExpressions)) {
            m_scene.restoreSnapshot(oldSnapshot);
            return;
        }
        const SceneDocument::Snapshot newSnapshot = m_scene.snapshot();
        m_scene.restoreSnapshot(oldSnapshot);
        auto *command = new UpdateGroupTransformCommand(&m_scene, oldSnapshot, newSnapshot,
                                                        [this]() { emit sceneChanged(); });
        m_undoStack->push(command);
        m_ctrlHighlight.active = false;
        emit ctrlHighlightChanged();
        return;
    }

    if (axis < 0 || axis > 4)
        return;
    const QString trimmed = expression.trimmed();

    QStringList newExpressions = group->transformExpressions;
    while (newExpressions.size() <= axis)
        newExpressions.append(QString());

    if (isLinExtr) {
        QVector3D scale = group->scale;
        float newTwist   = group->linearExtrudeTwist;
        int   newSlices  = group->linearExtrudeSlices;
        float newScaleVal= group->linearExtrudeScaleVal;

        if (axis == 0) { // height
            QHash<QString, qreal> varValues;
            collectVariableValues(m_scene.treeRoot(), &varValues);
            qreal v = 0.0;
            if (!ExpressionSyntax::evaluate(trimmed, varValues, &v))
                return;
            v = qMax<qreal>(0.01, v);
            scale.setX(float(v));
            newExpressions[0] = trimmed;
        } else if (axis == 1) { // center — toggle
            const bool newCenter = trimmed.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0
                                  || trimmed == QLatin1String("1");
            if (newCenter == group->linearExtrudeCenter)
                return;
            newExpressions[1].clear(); // center doesn't use transformExpressions
            // Toggle: update the node directly via snapshot cycle
            const SceneDocument::Snapshot oldSnapshot = m_scene.snapshot();
            {
                SceneDocument::TreeNode *mutableNode = m_scene.treeNodeById(groupId);
                if (mutableNode)
                    mutableNode->linearExtrudeCenter = newCenter;
            }
            const SceneDocument::Snapshot newSnapshot = m_scene.snapshot();
            m_scene.restoreSnapshot(oldSnapshot);
            auto *command = new UpdateGroupTransformCommand(&m_scene, oldSnapshot, newSnapshot,
                                                            [this]() { emit sceneChanged(); });
            m_undoStack->push(command);
            m_ctrlHighlight.active = false;
            emit ctrlHighlightChanged();
            return;
        } else if (axis == 2) { // twist
            QHash<QString, qreal> varValues;
            collectVariableValues(m_scene.treeRoot(), &varValues);
            qreal v = 0.0;
            if (!ExpressionSyntax::evaluate(trimmed, varValues, &v))
                return;
            newTwist = float(v);
            newExpressions[1] = trimmed;
        } else if (axis == 3) { // slices
            bool ok = false;
            int iv = trimmed.toInt(&ok);
            if (!ok) return;
            iv = qMax(0, iv);
            newSlices = iv;
            newExpressions[2] = trimmed;
        } else if (axis == 4) { // scale
            QHash<QString, qreal> varValues;
            collectVariableValues(m_scene.treeRoot(), &varValues);
            qreal v = 0.0;
            if (!ExpressionSyntax::evaluate(trimmed, varValues, &v))
                return;
            v = qMax<qreal>(0.01, v);
            newScaleVal = float(v);
            newExpressions[3] = trimmed;
        }

        // Map axis to transformExpressions index:
        // axis 0→0, axis 1→N/A, axis 2→1, axis 3→2, axis 4→3
        const int exprIdx = (axis >= 2) ? axis - 1 : axis;
        if (axis != 1
            && newExpressions[exprIdx] == group->transformExpressions.value(exprIdx)
            && scale == group->scale
            && qFuzzyCompare(newTwist, group->linearExtrudeTwist)
            && newSlices == group->linearExtrudeSlices
            && qFuzzyCompare(newScaleVal, group->linearExtrudeScaleVal))
            return;

        const SceneDocument::Snapshot oldSnapshot = m_scene.snapshot();
        if (!m_scene.updateGroupLinearExtrudeParams(groupId, scale,
                                                    newTwist, newSlices, newScaleVal,
                                                    newExpressions))
            return;
        const SceneDocument::Snapshot newSnapshot = m_scene.snapshot();
        m_scene.restoreSnapshot(oldSnapshot);
        auto *command = new UpdateGroupTransformCommand(&m_scene, oldSnapshot, newSnapshot,
                                                        [this]() { emit sceneChanged(); });
        m_undoStack->push(command);
        m_ctrlHighlight.active = false;
        emit ctrlHighlightChanged();
        return;
    }

    // ── RotateExtrude single-angle edit ────────────────────────────────────
    if (isRotExtr && axis == 0) {
        QHash<QString, qreal> varValues;
        collectVariableValues(m_scene.treeRoot(), &varValues);
        qreal v = 0.0;
        if (!ExpressionSyntax::evaluate(trimmed, varValues, &v))
            return;
        newExpressions[0] = trimmed;
        QVector3D newScale = group->scale;
        newScale.setX(float(v));

        if (newExpressions[0] == group->transformExpressions.value(0)
            && qFuzzyCompare(newScale.x(), group->scale.x()))
            return;

        const SceneDocument::Snapshot oldSnapshot = m_scene.snapshot();
        if (!m_scene.updateGroupTransform(groupId, group->position, group->rotation,
                                           newScale, newExpressions))
            return;
        const SceneDocument::Snapshot newSnapshot = m_scene.snapshot();
        m_scene.restoreSnapshot(oldSnapshot);
        auto *command = new UpdateGroupTransformCommand(&m_scene, oldSnapshot, newSnapshot,
                                                        [this]() { emit sceneChanged(); });
        m_undoStack->push(command);
        m_ctrlHighlight.active = false;
        emit ctrlHighlightChanged();
        return;
    }

    // ── Standard transform expression edit ─────────────────────────────────
    if (axis > 2) return;

    QHash<QString, qreal> varValues;
    collectVariableValues(m_scene.treeRoot(), &varValues);

    qreal numericValue = 0.0;
    if (!ExpressionSyntax::evaluate(trimmed, varValues, &numericValue))
        return;

    if (newExpressions[axis] == trimmed)
        return;
    newExpressions[axis] = trimmed;

    QVector3D position = group->position;
    QVector3D rotation = group->rotation;
    QVector3D scale = group->scale;
    if (group->operation == SceneDocument::TreeNode::Scale
        || group->operation == SceneDocument::TreeNode::Resize)
        numericValue = qMax<qreal>(0.01, numericValue);

    const bool usePosition = group->operation == SceneDocument::TreeNode::Translate
                             || group->operation == SceneDocument::TreeNode::Mirror;
    QVector3D *target = usePosition ? &position
                       : group->operation == SceneDocument::TreeNode::Rotate ? &rotation
                                                                              : &scale;
    if (axis == 0)
        target->setX(float(numericValue));
    else if (axis == 1)
        target->setY(float(numericValue));
    else
        target->setZ(float(numericValue));

    const SceneDocument::Snapshot oldSnapshot = m_scene.snapshot();
    if (!m_scene.updateGroupTransform(groupId, position, rotation, scale, newExpressions))
        return;
    const SceneDocument::Snapshot newSnapshot = m_scene.snapshot();
    m_scene.restoreSnapshot(oldSnapshot);

    auto *command = new UpdateGroupTransformCommand(&m_scene, oldSnapshot, newSnapshot,
                                                    [this]() { emit sceneChanged(); });
    m_undoStack->push(command);
    m_ctrlHighlight.active = false;
    emit ctrlHighlightChanged();
}

void SceneController::handleColorChannelAdjusted(int groupId, int channel, qreal delta)
{
    if (channel < 0 || channel > 2 || qFuzzyIsNull(delta))
        return;

    const SceneDocument::TreeNode *group = m_scene.treeNodeById(groupId);
    if (!group || group->type != SceneDocument::TreeNode::Group
        || group->operation != SceneDocument::TreeNode::Color) {
        return;
    }

    QColor color = group->color.isValid() ? group->color : QColor(79, 163, 255);
    const int step = (QApplication::keyboardModifiers() & Qt::ShiftModifier) ? 10 : 5;
    int channels[3] = {color.red(), color.green(), color.blue()};
    channels[channel] = qBound(0, channels[channel] + int(delta) * step, 255);
    color.setRgb(channels[0], channels[1], channels[2], color.alpha());

    auto *command = new UpdateGroupColorCommand(&m_scene, groupId, color,
                                                [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
    m_ctrlHighlight.active = false;
    emit ctrlHighlightChanged();
}

void SceneController::handleModuleRenameRequested(int groupId, const QString &newName)
{
    auto *command = new RenameModuleCommand(&m_scene, groupId, newName,
                                            [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

// ── Graphics-tree: variable rename ───────────────────────────────────────────

void SceneController::handleVariableRenameRequested(int variableId, const QString &newName)
{
    auto *command = new RenameVariableCommand(&m_scene, variableId, newName,
                                              [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

// ── Graphics-tree: shape parameter adjusted ───────────────────────────────────

void SceneController::handleShapeParameterAdjusted(int nodeId, int paramIndex,
                                                    int numberStart, int numberLength,
                                                    qreal delta)
{
    if (nodeId <= 0 || paramIndex < 0 || numberStart < 0 || numberLength <= 0
        || qFuzzyIsNull(delta))
        return;

    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node || node->type != SceneDocument::TreeNode::Primitive) return;

    const ShapeNode *shape = m_scene.shapeById(node->shapeId);
    if (!shape) return;

    const QVector<SceneTreeGraphics::ShapeParameterControl> controls =
        SceneTreeGraphics::shapeParameterControls(*shape);
    if (paramIndex >= controls.size()) return;

    const QString &currentExpr = controls[paramIndex].expression;
    if (numberStart + numberLength > currentExpr.size()) return;

    const QString replacement = adjustedNumberReplacement(currentExpr, numberStart, numberLength,
                                                          delta, 0.1);
    if (replacement.isEmpty()) return;

    const QString newExpr = currentExpr.left(numberStart)
                            + replacement
                            + currentExpr.mid(numberStart + numberLength);

    QString contextPrefix;
    if (shape->type == ShapeNode::Cylinder) {
        contextPrefix = (paramIndex == 0) ? QStringLiteral(", r=") : QStringLiteral("h=");
    } else if (shape->type == ShapeNode::Sphere) {
        contextPrefix = QStringLiteral("r=");
    } else {
        contextPrefix = QStringLiteral("[");
        for (int i = 0; i < paramIndex && i < controls.size(); ++i)
            contextPrefix += controls[i].expression + QStringLiteral(", ");
    }
    setCtrlHighlight(nodeId, contextPrefix, newExpr, numberStart, int(replacement.size()));

    QHash<QString, qreal> varValues;
    collectVariableValues(m_scene.treeRoot(), &varValues);

    qreal newNumericValue = replacement.toDouble();
    ExpressionSyntax::evaluate(newExpr, varValues, &newNumericValue);

    ShapeNode updatedShape = *shape;
    while (updatedShape.parameterExpressions.size() < controls.size())
        updatedShape.parameterExpressions.append(QString());
    updatedShape.parameterExpressions[paramIndex] = newExpr;
    updatedShape.applyParameterValue(paramIndex, newNumericValue);

    auto *command = new UpdateShapeCommand(&m_scene, *shape, updatedShape,
                                           [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
    emit ctrlHighlightChanged();
}

// ── Graphics-tree: variable number adjusted ───────────────────────────────────

void SceneController::handleVariableNumberAdjusted(int nodeId, int start, int length,
                                                    qreal delta)
{
    if (nodeId <= 0 || start < 0 || length <= 0 || qFuzzyIsNull(delta)) return;

    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node || node->type != SceneDocument::TreeNode::Variable) return;

    QString expression = node->variableExpression;
    if (start + length > expression.size()) return;

    const QString replacement = adjustedNumberReplacement(expression, start, length,
                                                          delta, 0.0);
    if (replacement.isEmpty()) return;

    expression.replace(start, length, replacement);

    setCtrlHighlight(nodeId, QString(), expression, start, int(replacement.size()));

    auto *command = new UpdateVariableExpressionCommand(&m_scene, nodeId, expression,
                                                        [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
    emit ctrlHighlightChanged();
}

// ── Graphics-tree: module call argument adjusted ──────────────────────────────

void SceneController::handleShapeParameterExpressionEdited(int nodeId,
                                                           int paramIndex,
                                                           const QString &expression)
{
    if (nodeId <= 0 || paramIndex < 0)
        return;

    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node || node->type != SceneDocument::TreeNode::Primitive)
        return;

    const ShapeNode *shape = m_scene.shapeById(node->shapeId);
    if (!shape)
        return;

    const QVector<SceneTreeGraphics::ShapeParameterControl> controls =
        SceneTreeGraphics::shapeParameterControls(*shape);
    if (paramIndex >= controls.size())
        return;

    const QString trimmed = expression.trimmed();
    QHash<QString, qreal> varValues;
    collectVariableValues(m_scene.treeRoot(), &varValues);

    qreal numericValue = 0.0;
    if (!ExpressionSyntax::evaluate(trimmed, varValues, &numericValue))
        return;

    ShapeNode updatedShape = *shape;
    while (updatedShape.parameterExpressions.size() < controls.size())
        updatedShape.parameterExpressions.append(QString());
    if (updatedShape.parameterExpressions[paramIndex] == trimmed)
        return;
    updatedShape.parameterExpressions[paramIndex] = trimmed;
    updatedShape.applyParameterValue(paramIndex, numericValue);

    auto *command = new UpdateShapeCommand(&m_scene, *shape, updatedShape,
                                           [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
    m_ctrlHighlight.active = false;
    emit ctrlHighlightChanged();
}

void SceneController::handleShapeCenterToggled(int nodeId, int shapeId, bool center)
{
    Q_UNUSED(nodeId);
    if (shapeId <= 0) return;

    const ShapeNode *shape = m_scene.shapeById(shapeId);
    if (!shape) return;

    ShapeNode updatedShape = *shape;
    updatedShape.center = center;

    auto *command = new UpdateShapeCommand(&m_scene, *shape, updatedShape,
                                           [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

static ShapeNode *polygon2DShapeForNode(SceneDocument *scene, int nodeId)
{
    if (!scene || nodeId <= 0)
        return nullptr;
    const SceneDocument::TreeNode *node = scene->treeNodeById(nodeId);
    if (!node || node->type != SceneDocument::TreeNode::Primitive)
        return nullptr;
    ShapeNode *shape = scene->shapeById(node->shapeId);
    if (!shape || shape->type != ShapeNode::Polygon2D)
        return nullptr;
    return shape;
}

void SceneController::handlePolygon2DAddPoint(int nodeId)
{
    const ShapeNode *shape = polygon2DShapeForNode(&m_scene, nodeId);
    if (!shape)
        return;

    ShapeNode updated = *shape;
    QVector3D newPoint(0.0f, 0.0f, 0.0f);
    if (!updated.polyhedronPoints.isEmpty())
        newPoint = updated.polyhedronPoints.last() + QVector3D(10.0f, 0.0f, 0.0f);
    updated.polyhedronPoints.append(newPoint);

    auto *command = new UpdateShapeCommand(&m_scene, *shape, updated,
                                           [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

void SceneController::handlePolygon2DRemovePoint(int nodeId, int pointIndex)
{
    const ShapeNode *shape = polygon2DShapeForNode(&m_scene, nodeId);
    if (!shape || pointIndex < 0 || pointIndex >= shape->polyhedronPoints.size()
        || shape->polyhedronPoints.size() <= 3)
        return;

    ShapeNode updated = *shape;
    updated.polyhedronPoints.remove(pointIndex);

    auto *command = new UpdateShapeCommand(&m_scene, *shape, updated,
                                           [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

void SceneController::handlePolygon2DPointAdjusted(int nodeId, int pointIndex, int coord, qreal delta)
{
    const ShapeNode *shape = polygon2DShapeForNode(&m_scene, nodeId);
    if (!shape || pointIndex < 0 || pointIndex >= shape->polyhedronPoints.size()
        || coord < 0 || coord > 1 || qFuzzyIsNull(delta))
        return;

    ShapeNode updated = *shape;
    QVector3D &point = updated.polyhedronPoints[pointIndex];
    if (coord == 0)
        point.setX(point.x() + static_cast<float>(delta));
    else
        point.setY(point.y() + static_cast<float>(delta));

    auto *command = new UpdateShapeCommand(&m_scene, *shape, updated,
                                           [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

void SceneController::handlePolygon2DPointExpressionEdited(int nodeId, int pointIndex, int coord, const QString &expression)
{
    const ShapeNode *shape = polygon2DShapeForNode(&m_scene, nodeId);
    if (!shape || pointIndex < 0 || pointIndex >= shape->polyhedronPoints.size()
        || coord < 0 || coord > 1)
        return;

    QHash<QString, qreal> varValues;
    collectVariableValues(m_scene.treeRoot(), &varValues);
    qreal value = 0.0;
    if (!ExpressionSyntax::evaluate(expression.trimmed(), varValues, &value))
        return;

    ShapeNode updated = *shape;
    QVector3D &point = updated.polyhedronPoints[pointIndex];
    if (coord == 0)
        point.setX(static_cast<float>(value));
    else
        point.setY(static_cast<float>(value));

    auto *command = new UpdateShapeCommand(&m_scene, *shape, updated,
                                           [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

void SceneController::handleVariableExpressionEdited(int nodeId, const QString &expression)
{
    auto *command = new UpdateVariableExpressionCommand(&m_scene, nodeId, expression,
                                                        [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
    m_ctrlHighlight.active = false;
    emit ctrlHighlightChanged();
}

void SceneController::handleModuleCallArgumentAdjusted(int moduleCallId,
                                                        int parameterVariableId,
                                                        int start, int length,
                                                        qreal delta)
{
    if (moduleCallId <= 0 || parameterVariableId <= 0
        || start < 0 || length <= 0 || qFuzzyIsNull(delta))
        return;

    const SceneDocument::TreeNode *callNode      = m_scene.treeNodeById(moduleCallId);
    const SceneDocument::TreeNode *parameterNode = m_scene.treeNodeById(parameterVariableId);
    if (!callNode || callNode->type != SceneDocument::TreeNode::ModuleCall
        || !parameterNode || parameterNode->type != SceneDocument::TreeNode::Variable
        || !parameterNode->isParameter)
        return;

    const SceneDocument::TreeNode *moduleGroupNode = m_scene.treeNodeById(callNode->shapeId);
    const QHash<QString, QString> overrides = moduleGroupNode
        ? resolveModuleArguments(callNode->moduleCallArguments, *moduleGroupNode)
        : parseNamedArgumentExpressions(callNode->moduleCallArguments);
    QString expression = overrides.value(
        parameterNode->variableName,
        parameterNode->variableExpression.trimmed().isEmpty()
            ? QString::number(parameterNode->variableValue)
            : parameterNode->variableExpression.trimmed());

    if (start + length > expression.size()) return;

    const QString replacement = adjustedNumberReplacement(expression, start, length,
                                                          delta, 0.0);
    if (replacement.isEmpty()) return;

    expression.replace(start, length, replacement);

    setCtrlHighlight(moduleCallId, QString(), expression, start, int(replacement.size()));

    auto *command = new UpdateModuleCallArgumentCommand(&m_scene,
                                                        moduleCallId,
                                                        parameterNode->variableName,
                                                        expression,
                                                        [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
    emit ctrlHighlightChanged();
}

// ── Graphics-tree: for loop range adjusted ────────────────────────────────────

void SceneController::handleModuleCallArgumentExpressionEdited(int moduleCallId,
                                                               int parameterVariableId,
                                                               const QString &expression)
{
    const SceneDocument::TreeNode *parameterNode = m_scene.treeNodeById(parameterVariableId);
    if (!parameterNode || parameterNode->type != SceneDocument::TreeNode::Variable
        || !parameterNode->isParameter) {
        return;
    }

    auto *command = new UpdateModuleCallArgumentCommand(&m_scene,
                                                        moduleCallId,
                                                        parameterNode->variableName,
                                                        expression,
                                                        [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
    m_ctrlHighlight.active = false;
    emit ctrlHighlightChanged();
}

void SceneController::handleForLoopRangeAdjusted(int nodeId, int start, int length,
                                                   qreal delta)
{
    if (nodeId <= 0 || start < 0 || length <= 0 || qFuzzyIsNull(delta)) return;

    const SceneDocument::TreeNode *node = m_scene.treeNodeById(nodeId);
    if (!node || node->type != SceneDocument::TreeNode::Group
        || node->operation != SceneDocument::TreeNode::For)
        return;

    QString expression = SceneTreeGraphics::forLoopRangeExpression(*node);
    if (start + length > expression.size()) return;

    const QString replacement = adjustedNumberReplacement(expression, start, length,
                                                          delta, 0.0);
    if (replacement.isEmpty()) return;

    expression.replace(start, length, replacement);

    setCtrlHighlight(nodeId, QString(), expression, start, int(replacement.size()));

    auto *command = new UpdateForLoopCommand(&m_scene, nodeId,
                                             SceneTreeGraphics::forLoopVariableName(*node),
                                             expression,
                                             [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
    emit ctrlHighlightChanged();
}

// ── Graphics-tree: ctrl released ─────────────────────────────────────────────

void SceneController::handleConditionExpressionEdited(int nodeId, const QString &expression)
{
    auto *command = new UpdateConditionExpressionCommand(&m_scene, nodeId, expression,
                                                         [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
    m_ctrlHighlight.active = false;
    emit ctrlHighlightChanged();
}

void SceneController::handleRawCodeEdited(int nodeId, const QString &code)
{
    auto *command = new UpdateRawCodeCommand(&m_scene, nodeId, code,
                                             [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
    m_ctrlHighlight.active = false;
    emit ctrlHighlightChanged();
}

void SceneController::handleTextContentEdited(int shapeId, const QString &text)
{
    auto *command = new UpdateTextContentCommand(&m_scene, shapeId, text,
                                                 [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}

void SceneController::handleCtrlReleased()
{
    m_ctrlHighlight.active = false;
    emit ctrlHighlightChanged();
}

void SceneController::handlePolyhedronAddPoint(int groupNodeId)
{
    if (groupNodeId <= 0) return;
    ShapeNode shape = makeShapeForTool(QStringLiteral("point_3d"), m_scene.shapeCount() + 1);
    m_undoStack->push(new AddShapeCommand(&m_scene, shape, groupNodeId, -1,
                                          [this]() { emit sceneChanged(); }));
}

void SceneController::handlePolyhedronAddFace(int groupNodeId)
{
    if (groupNodeId <= 0) return;
    ShapeNode shape = makeShapeForTool(QStringLiteral("face_3d"), m_scene.shapeCount() + 1);
    m_undoStack->push(new AddShapeCommand(&m_scene, shape, groupNodeId, -1,
                                          [this]() { emit sceneChanged(); }));
}

void SceneController::handlePolyhedronAutoface(int groupNodeId)
{
    if (groupNodeId <= 0) return;
    const auto *group = m_scene.treeNodeById(groupNodeId);
    if (!group) return;

    // Collect Point3D children and their positions
    struct Pt { int nodeId; QVector3D pos; };
    QVector<Pt> pts;
    for (const auto &child : group->children) {
        if (child.type != SceneDocument::TreeNode::Primitive) continue;
        const ShapeNode *s = m_scene.shapeById(child.shapeId);
        if (!s || s->type != ShapeNode::Point3D) continue;
        pts.append({child.id, s->position});
    }
    if (pts.size() < 4) return;

    // Compute convex hull faces
    QVector<QVector3D> positions;
    positions.reserve(pts.size());
    for (const auto &p : pts)
        positions.append(p.pos);

    const auto variants = computePolyhedronFaceVariants(positions);
    if (variants.isEmpty()) return;

    QVector<QVector<int>> currentFaces;
    for (const auto &child : group->children) {
        if (child.type != SceneDocument::TreeNode::Primitive) continue;
        const ShapeNode *s = m_scene.shapeById(child.shapeId);
        if (s && s->type == ShapeNode::Face3D && !s->polyhedronFaces.isEmpty())
            currentFaces.append(s->polyhedronFaces.first());
    }

    int variantIndex = 0;
    for (int i = 0; i < variants.size(); ++i) {
        if (variants[i] == currentFaces) {
            variantIndex = (i + 1) % variants.size();
            break;
        }
    }
    const auto faces = variants[variantIndex];
    if (faces.isEmpty()) return;

    // Collect existing Face3D children to replace, before modifying the scene
    struct FaceToRemove { int shapeId; int childIndex; };
    QVector<FaceToRemove> toRemove;
    for (int i = 0; i < group->children.size(); ++i) {
        const auto &child = group->children[i];
        if (child.type != SceneDocument::TreeNode::Primitive) continue;
        const ShapeNode *s = m_scene.shapeById(child.shapeId);
        if (s && s->type == ShapeNode::Face3D)
            toRemove.append({child.shapeId, i});
    }

    m_undoStack->beginMacro(QObject::tr("Autoface"));

    // Remove existing faces in reverse order so indices stay valid
    for (int i = toRemove.size() - 1; i >= 0; --i) {
        m_undoStack->push(new RemovePolyhedronElementCommand(&m_scene,
                                                             toRemove[i].shapeId,
                                                             groupNodeId,
                                                             -1,
                                                             [this]() { emit sceneChanged(); }));
    }

    // Add one Face3D per convex hull face
    for (const auto &face : faces) {
        ShapeNode shape;
        shape.type = ShapeNode::Face3D;
        shape.polyhedronFaces.append(face);
        shape.name = QStringLiteral("F%1").arg(m_scene.shapeCount() + 1);
        m_undoStack->push(new AddShapeCommand(&m_scene, shape, groupNodeId, -1,
                                               [this]() { emit sceneChanged(); }));
    }
    m_undoStack->endMacro();
}

void SceneController::handlePolyhedronApplyTemplate(int groupNodeId, int templateType)
{
    if (groupNodeId <= 0) return;

    struct TemplateData {
        QVector<QVector3D> points;
        QVector<QVector<int>> faces;
    };

    static const TemplateData templates[] = {
        // 0: Pyramid — square base 10×10, height 8
        {
            {{0,0,0},{10,0,0},{10,10,0},{0,10,0},{5,5,8}},
            {{0,3,2,1},{0,1,4},{1,2,4},{2,3,4},{3,0,4}}
        },
        // 1: Prism — triangular base, height 8
        {
            {{0,0,0},{10,0,0},{5,9,0},{0,0,8},{10,0,8},{5,9,8}},
            {{0,2,1},{3,4,5},{0,1,4,3},{1,2,5,4},{2,0,3,5}}
        },
        // 2: Box — 10×10×8 cuboid
        {
            {{0,0,0},{10,0,0},{10,10,0},{0,10,0},{0,0,8},{10,0,8},{10,10,8},{0,10,8}},
            {{0,3,2,1},{4,5,6,7},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7}}
        },
        // 3: Tetrahedron — 4 equilateral-ish faces, edge ~10
        {
            {{0,0,0},{10,0,0},{5,9,0},{5,3,8}},
            {{0,2,1},{0,1,3},{1,2,3},{2,0,3}}
        },
        // 4: Octahedron — 6 vertices, 8 faces
        {
            {{5,5,10},{5,5,0},{10,5,5},{5,10,5},{0,5,5},{5,0,5}},
            {{0,2,3},{0,3,4},{0,4,5},{0,5,2},{1,3,2},{1,4,3},{1,5,4},{1,2,5}}
        },
        // 5: Frustum — 10×10 bottom, 6×6 top, height 8
        {
            {{0,0,0},{10,0,0},{10,10,0},{0,10,0},{2,2,8},{8,2,8},{8,8,8},{2,8,8}},
            {{0,3,2,1},{4,5,6,7},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7}}
        },
        // 6: Wedge — ramp, l=10, w=6, h1=2, h2=8
        {
            {{0,0,0},{10,0,0},{10,6,0},{0,6,0},{0,0,2},{10,0,8},{10,6,8},{0,6,2}},
            {{0,3,2,1},{4,5,6,7},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7}}
        },
        // 7: Hex prism — hexagonal base, height 8
        {
            {{10,5,0},{8,9,0},{2,9,0},{0,5,0},{2,1,0},{8,1,0},
             {10,5,8},{8,9,8},{2,9,8},{0,5,8},{2,1,8},{8,1,8}},
            {{0,5,4,3,2,1},{6,7,8,9,10,11},
             {0,1,7,6},{1,2,8,7},{2,3,9,8},{3,4,10,9},{4,5,11,10},{5,0,6,11}}
        },
        // 8: L-shape — L cross-section extrusion, 10×10 outer, bar 3, height 8
        {
            {{0,0,0},{10,0,0},{10,3,0},{3,3,0},{3,10,0},{0,10,0},
             {0,0,8},{10,0,8},{10,3,8},{3,3,8},{3,10,8},{0,10,8}},
            {{0,5,4,3,2,1},{6,7,8,9,10,11},
             {0,1,7,6},{1,2,8,7},{2,3,9,8},{3,4,10,9},{4,5,11,10},{5,0,6,11}}
        },
        // 9: C-shape — C channel extrusion, 10×10 outer, bar 3, height 8
        {
            {{0,0,0},{10,0,0},{10,3,0},{3,3,0},{3,7,0},{10,7,0},{10,10,0},{0,10,0},
             {0,0,8},{10,0,8},{10,3,8},{3,3,8},{3,7,8},{10,7,8},{10,10,8},{0,10,8}},
            {{0,7,6,5,4,3,2,1},{8,9,10,11,12,13,14,15},
             {0,1,9,8},{1,2,10,9},{2,3,11,10},{3,4,12,11},
             {4,5,13,12},{5,6,14,13},{6,7,15,14},{7,0,8,15}}
        },
        // 10: H-shape — H cross-section extrusion, 10×10 outer, flanges 3, web y=4-6, height 8
        {
            {{0,0,0},{3,0,0},{3,4,0},{7,4,0},{7,0,0},{10,0,0},
             {10,10,0},{7,10,0},{7,6,0},{3,6,0},{3,10,0},{0,10,0},
             {0,0,8},{3,0,8},{3,4,8},{7,4,8},{7,0,8},{10,0,8},
             {10,10,8},{7,10,8},{7,6,8},{3,6,8},{3,10,8},{0,10,8}},
            {{0,11,10,9,8,7,6,5,4,3,2,1},{12,13,14,15,16,17,18,19,20,21,22,23},
             {0,1,13,12},{1,2,14,13},{2,3,15,14},{3,4,16,15},{4,5,17,16},{5,6,18,17},
             {6,7,19,18},{7,8,20,19},{8,9,21,20},{9,10,22,21},{10,11,23,22},{11,0,12,23}}
        },
        // 11: O-shape — square tube, outer 10×10, inner 6×6 (wall 2), height 8
        {
            {{0,0,0},{10,0,0},{10,10,0},{0,10,0},
             {2,2,0},{8,2,0},{8,8,0},{2,8,0},
             {0,0,8},{10,0,8},{10,10,8},{0,10,8},
             {2,2,8},{8,2,8},{8,8,8},{2,8,8}},
            {{0,4,5,1},{1,5,6,2},{2,6,7,3},{3,7,4,0},
             {8,9,13,12},{9,10,14,13},{10,11,15,14},{11,8,12,15},
             {0,1,9,8},{1,2,10,9},{2,3,11,10},{3,0,8,11},
             {5,4,12,13},{6,5,13,14},{7,6,14,15},{4,7,15,12}}
        }
    };

    if (templateType < 0 || templateType >= 12) return;
    const TemplateData &tmpl = templates[templateType];

    m_undoStack->beginMacro(QObject::tr("Apply template"));

    for (const QVector3D &pos : tmpl.points) {
        ShapeNode shape;
        shape.type = ShapeNode::Point3D;
        shape.position = pos;
        shape.name = QStringLiteral("Point %1").arg(m_scene.shapeCount() + 1);
        m_undoStack->push(new AddShapeCommand(&m_scene, shape, groupNodeId, -1,
                                              [this]() { emit sceneChanged(); }));
    }

    for (const QVector<int> &face : tmpl.faces) {
        ShapeNode shape;
        shape.type = ShapeNode::Face3D;
        shape.polyhedronFaces.append(face);
        shape.name = QStringLiteral("Face %1").arg(m_scene.shapeCount() + 1);
        m_undoStack->push(new AddShapeCommand(&m_scene, shape, groupNodeId, -1,
                                              [this]() { emit sceneChanged(); }));
    }

    m_undoStack->endMacro();
}

void SceneController::handlePolyhedronClearAll(int groupNodeId)
{
    if (groupNodeId <= 0) return;
    const auto *group = m_scene.treeNodeById(groupNodeId);
    if (!group) return;

    struct ToRemove { int shapeId; int childIndex; };
    QVector<ToRemove> toRemove;
    for (int i = 0; i < group->children.size(); ++i) {
        const auto &child = group->children[i];
        if (child.type != SceneDocument::TreeNode::Primitive) continue;
        const ShapeNode *s = m_scene.shapeById(child.shapeId);
        if (s && (s->type == ShapeNode::Point3D || s->type == ShapeNode::Face3D))
            toRemove.append({child.shapeId, i});
    }

    if (toRemove.isEmpty()) return;

    m_undoStack->beginMacro(QObject::tr("Clear polyhedron"));
    for (int i = toRemove.size() - 1; i >= 0; --i) {
        m_undoStack->push(new RemovePolyhedronElementCommand(&m_scene,
                                                             toRemove[i].shapeId,
                                                             groupNodeId,
                                                             -1,
                                                             [this]() { emit sceneChanged(); }));
    }
    m_undoStack->endMacro();
}

void SceneController::handlePolyhedronFaceParticipationAdjusted(int faceNodeId, int pointNodeId, int newPosition)
{
    if (faceNodeId <= 0 || pointNodeId <= 0) return;

    const SceneDocument::TreeNode *faceNode = m_scene.treeNodeById(faceNodeId);
    if (!faceNode || faceNode->type != SceneDocument::TreeNode::Primitive) return;
    const ShapeNode *shape = m_scene.shapeById(faceNode->shapeId);
    if (!shape || shape->type != ShapeNode::Face3D) return;

    // Find the polyhedron parent group
    int parentId = 0;
    int childIndex = 0;
    if (!SceneDocument::findChildParent(m_scene.treeRoot(), faceNodeId, &parentId, &childIndex))
        return;
    const SceneDocument::TreeNode *parent = m_scene.treeNodeById(parentId);
    if (!parent || parent->operation != SceneDocument::TreeNode::Polyhedron) return;

    // Find the point's index (position among Point3D children)
    int pointIndex = -1;
    for (const auto &child : parent->children) {
        if (child.type != SceneDocument::TreeNode::Primitive) continue;
        const ShapeNode *cs = m_scene.shapeById(child.shapeId);
        if (!cs || cs->type != ShapeNode::Point3D) continue;
        ++pointIndex;
        if (child.id == pointNodeId) break;
    }
    if (pointIndex < 0) return;

    ShapeNode updated = *shape;
    if (updated.polyhedronFaces.isEmpty())
        updated.polyhedronFaces.append(QVector<int>());
    auto &indices = updated.polyhedronFaces.first();

    // Current position of this point in the face
    const int oldPos = indices.indexOf(pointIndex);
    if (oldPos == newPosition)
        return; // no change

    // Remove from old position if present
    if (oldPos >= 0)
        indices.remove(oldPos);

    // Insert at new position (if >= 0)
    if (newPosition >= 0) {
        indices.insert(qBound(0, newPosition, indices.size()), pointIndex);
    }

    auto *command = new UpdateShapeCommand(&m_scene, *shape, updated,
                                           [this]() { emit sceneChanged(); });
    if (!command->isValid()) { delete command; return; }
    m_undoStack->push(command);
}
