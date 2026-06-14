#include "manifoldcsg.h"
#include "scenemesh.h"
#include "scenestringutils.h"

#ifdef HAVE_MANIFOLD_CSG
#include "expression.h"

#include <manifold/manifold.h>
#include <manifold/cross_section.h>

#include <QHash>
#include <QMutex>
#include <QStringList>
#include <QtMath>
#include <QFont>
#include <QFontMetricsF>
#include <QPainterPath>
#include <QPolygonF>
#include <algorithm>

// Manifold is not re-entrant: serialise all calls with this process-wide lock.
// Both the viewport background render and the group thumbnail background render
// acquire this before entering evaluateDocumentGeometry().
static QMutex s_manifoldMutex;

using manifold::Manifold;
using manifold::MeshGL;
using manifold::Polygons;
using manifold::SimplePolygon;
using manifold::vec2;
using manifold::vec3;

static QVector3D faceNormal(const QVector3D &a, const QVector3D &b, const QVector3D &c)
{
    QVector3D normal = -QVector3D::crossProduct(b - a, c - a);

    if (normal.lengthSquared() <= 0.0001f)
        return QVector3D(0.0f, 0.0f, 1.0f);

    return normal.normalized();
}

static MeshTriangle makeTriangle(const QVector3D &a, const QVector3D &b, const QVector3D &c)
{
    MeshTriangle triangle;
    triangle.a = a;
    triangle.b = b;
    triangle.c = c;
    triangle.normal = faceNormal(a, b, c);
    triangle.shade = 108;
    return triangle;
}

static Manifold manifoldFromSceneMesh(const SceneMesh &mesh)
{
    MeshGL meshGl;
    meshGl.numProp = 3;
    for (const MeshTriangle &triangle : mesh.triangles) {
        const QVector3D vertices[3] = {triangle.a, triangle.b, triangle.c};
        const uint32_t base = static_cast<uint32_t>(meshGl.vertProperties.size() / 3);
        for (const QVector3D &vertex : vertices) {
            meshGl.vertProperties.push_back(vertex.x());
            meshGl.vertProperties.push_back(vertex.y());
            meshGl.vertProperties.push_back(vertex.z());
        }
        meshGl.triVerts.push_back(base);
        meshGl.triVerts.push_back(base + 1);
        meshGl.triVerts.push_back(base + 2);
    }

    // buildShapeMesh() emits render-style triangles with duplicate position
    // vertices. Merge them back into topological vertices before asking
    // Manifold to validate the solid.
    meshGl.Merge();
    return Manifold(meshGl);
}

// Build manifold Polygons from a Polygon2D shape, respecting paths= holes.
// paths[0] = outer contour, paths[1..] = hole contours (counter-wound).
// When no paths are given, all points form a single outer contour.
static Polygons polygonPolygons(const ShapeNode &shape)
{
    Polygons out;
    if (shape.polyhedronPoints.size() < 3)
        return out;

    if (shape.polyhedronFaces.isEmpty()) {
        SimplePolygon sp;
        sp.reserve(shape.polyhedronPoints.size());
        for (const QVector3D &pt : shape.polyhedronPoints)
            sp.push_back({pt.x(), pt.y()});
        out.push_back(sp);
    } else {
        for (const QVector<int> &path : shape.polyhedronFaces) {
            if (path.size() < 3) continue;
            SimplePolygon sp;
            sp.reserve(path.size());
            for (int idx : path)
                if (idx >= 0 && idx < shape.polyhedronPoints.size())
                    sp.push_back({shape.polyhedronPoints[idx].x(),
                                  shape.polyhedronPoints[idx].y()});
            if (sp.size() >= 3)
                out.push_back(sp);
        }
    }
    return out;
}

// Converts a Text shape's glyph contours to manifold Polygons. Reads the
// contours cached on the GUI thread (ShapeNode::textContours) to avoid touching
// QFont/QPainterPath off the GUI thread; falls back to computing them if the
// cache is empty. Holes come out as separate sub-contours — feed the result to
// a CrossSection with the EvenOdd fill rule so they are subtracted.
static Polygons textPolygons(const ShapeNode &shape)
{
    Polygons out;
    QVector<QVector<QVector3D>> contours = shape.textContours;
    if (contours.isEmpty())
        contours = buildGlyphContours(shape);

    for (const QVector<QVector3D> &contour : contours) {
        if (contour.size() < 3)
            continue;
        SimplePolygon sp;
        sp.reserve(contour.size());
        for (const QVector3D &pt : contour)
            sp.push_back({pt.x(), pt.y()});
        out.push_back(sp);
    }
    return out;
}

static Manifold manifoldFromShape(const ShapeNode &shape, int fn = 0, double fa = 12.0, double fs = 2.0)
{
    Manifold result;

    if (shape.type == ShapeNode::Point3D
        || shape.type == ShapeNode::Face3D)
        return {}; // polyhedron data components are handled by their group

    const double r = (shape.type == ShapeNode::Sphere || shape.type == ShapeNode::Circle)
                     ? shape.radius
                     : qMax(shape.radius, shape.radius2);
    const int circularSegments = computeCircularSegments(fn, r, fa, fs);

    if (shape.type == ShapeNode::Cube) {
        result = Manifold::Cube(vec3(shape.size.x(), shape.size.y(), shape.size.z()), shape.center);
    } else if (shape.type == ShapeNode::Square) {
        // z is always 0..0.1 so linear_extrude can scale it to 0..height.
        // XY centering is handled separately to avoid coupling to z.
        result = Manifold::Cube(vec3(shape.size.x(), shape.size.y(), 0.1f), false);
        if (shape.center)
            result = result.Translate(vec3(-shape.size.x() * 0.5f, -shape.size.y() * 0.5f, 0.0f));
    } else if (shape.type == ShapeNode::Polyhedron
               || shape.type == ShapeNode::ImportedMesh) {
        ShapeNode localShape = shape;
        localShape.position = QVector3D();
        localShape.rotation = QVector3D();
        result = manifoldFromSceneMesh(buildShapeMesh(localShape));
    } else if (shape.type == ShapeNode::Polygon2D) {
        {
            const Polygons polys = polygonPolygons(shape);
            if (!polys.empty()) {
                const manifold::CrossSection cs(polys, manifold::CrossSection::FillRule::EvenOdd);
                const Polygons filled = cs.ToPolygons();
                if (!filled.empty())
                    result = Manifold::Extrude(filled, 0.1);
            }
        }
    } else if (shape.type == ShapeNode::Sphere) {
        result = Manifold::Sphere(shape.radius, circularSegments);
    } else if (shape.type == ShapeNode::Cone) {
        result = Manifold::Cylinder(shape.height, shape.radius, shape.radius2, circularSegments, shape.center);
    } else if (shape.type == ShapeNode::Circle) {
        result = Manifold::Cylinder(0.1f, shape.radius, shape.radius, circularSegments, false);
    } else if (shape.type == ShapeNode::Text) {
        const Polygons polys = textPolygons(shape);
        if (polys.empty())
            return {};
        // EvenOdd so counter-wound inner contours become holes.
        const manifold::CrossSection cs(polys, manifold::CrossSection::FillRule::EvenOdd);
        const Polygons filled = cs.ToPolygons();
        if (filled.empty())
            return {};
        result = Manifold::Extrude(filled, 0.1); // 2D-as-thin-solid convention
    } else {
        result = Manifold::Cylinder(shape.height, shape.radius, shape.radius, circularSegments, shape.center);
    }

    return result.Rotate(shape.rotation.x(), shape.rotation.y(), shape.rotation.z())
        .Translate(vec3(shape.position.x(), shape.position.y(), shape.position.z()));
}

// Recursively collect 2D polygons from rotate_extrude children.
// Applies XY translation offsets from Translate group wrappers.
// This is needed because rotate_extrude profiles are often written as:
//   translate([r, 0, 0]) { circle(d=tube); }
static void collectRotateExtrudePolygons(
    const SceneDocument::TreeNode &node,
    const SceneDocument &scene,
    const QHash<QString, qreal> &variables,
    int segs,
    Polygons &out)
{
    if (node.type == SceneDocument::TreeNode::Primitive) {
        const ShapeNode *shape = scene.shapeById(node.shapeId);
        if (!shape) return;
        const ShapeNode ev = shapeWithEvaluatedParameters(*shape, variables);
        SimplePolygon poly;
        if (ev.type == ShapeNode::Circle) {
            for (int i = 0; i < segs; ++i) {
                const double a = 2.0 * M_PI * i / segs;
                poly.push_back({ev.radius * std::cos(a),
                                ev.radius * std::sin(a)});
            }
        } else if (ev.type == ShapeNode::Square) {
            const float hx = ev.size.x() * 0.5f;
            const float hy = ev.size.y() * 0.5f;
            const float cx = ev.center ? 0.0f : hx;
            const float cy = ev.center ? 0.0f : hy;
            poly = {{-hx + cx, -hy + cy},
                    { hx + cx, -hy + cy},
                    { hx + cx,  hy + cy},
                    {-hx + cx,  hy + cy}};
        } else if (ev.type == ShapeNode::Polygon2D) {
            if (ev.polyhedronFaces.isEmpty()) {
                for (const QVector3D &pt : ev.polyhedronPoints)
                    poly.push_back({pt.x(), pt.y()});
            } else {
                for (const QVector<int> &path : ev.polyhedronFaces) {
                    if (path.size() < 3) continue;
                    SimplePolygon sp;
                    for (int idx : path)
                        if (idx >= 0 && idx < ev.polyhedronPoints.size())
                            sp.push_back({ev.polyhedronPoints[idx].x(),
                                          ev.polyhedronPoints[idx].y()});
                    if (sp.size() >= 3)
                        out.push_back(sp);
                }
                return;
            }
        } else if (ev.type == ShapeNode::Text) {
            // Text yields multiple contours (incl. holes); append them all.
            const Polygons glyphs = textPolygons(ev);
            for (const SimplePolygon &g : glyphs)
                if (g.size() >= 3)
                    out.push_back(g);
            return;
        }
        if (static_cast<int>(poly.size()) >= 3)
            out.push_back(poly);
        return;
    }

    if (node.type != SceneDocument::TreeNode::Group)
        return;

    // Collect children first (innermost transform applies first).
    Polygons childPolys;
    for (const SceneDocument::TreeNode &child : node.children)
        collectRotateExtrudePolygons(child, scene, variables, segs, childPolys);

    // Apply this node's 2D transform to the collected child polygons.
    if (node.operation == SceneDocument::TreeNode::Translate) {
        const SceneDocument::TreeNode ev2 = nodeWithEvaluatedTransform(node, variables);
        const float tx = ev2.position.x();
        const float ty = ev2.position.y();
        if (qAbs(tx) > 0.001f || qAbs(ty) > 0.001f) {
            for (SimplePolygon &poly : childPolys)
                for (vec2 &pt : poly) {
                    pt.x += tx;
                    pt.y += ty;
                }
        }
    } else if (node.operation == SceneDocument::TreeNode::Scale) {
        const SceneDocument::TreeNode ev2 = nodeWithEvaluatedTransform(node, variables);
        const float sx = ev2.scale.x();
        const float sy = ev2.scale.y();
        if (qAbs(sx - 1.0f) > 0.001f || qAbs(sy - 1.0f) > 0.001f) {
            for (SimplePolygon &poly : childPolys)
                for (vec2 &pt : poly) {
                    pt.x *= sx;
                    pt.y *= sy;
                }
        }
    } else if (node.operation == SceneDocument::TreeNode::Rotate) {
        const SceneDocument::TreeNode ev2 = nodeWithEvaluatedTransform(node, variables);
        const float angle = ev2.rotation.z();
        if (qAbs(angle) > 0.001f) {
            const float rad = angle * M_PI / 180.0f;
            const float c = std::cos(rad);
            const float s = std::sin(rad);
            for (SimplePolygon &poly : childPolys)
                for (vec2 &pt : poly) {
                    const float nx = pt.x * c - pt.y * s;
                    const float ny = pt.x * s + pt.y * c;
                    pt.x = nx;
                    pt.y = ny;
                }
        }
    }

    out.insert(out.end(), childPolys.begin(), childPolys.end());
}

static bool isUnionLikeOperation(SceneDocument::TreeNode::Operation operation)
{
    return operation == SceneDocument::TreeNode::Union
           || operation == SceneDocument::TreeNode::Module
           || operation == SceneDocument::TreeNode::Scene
           || operation == SceneDocument::TreeNode::Translate
           || operation == SceneDocument::TreeNode::Rotate
           || operation == SceneDocument::TreeNode::Scale
           || operation == SceneDocument::TreeNode::Mirror
           || operation == SceneDocument::TreeNode::Color
           || operation == SceneDocument::TreeNode::LinearExtrude
           || operation == SceneDocument::TreeNode::RotateExtrude
           || operation == SceneDocument::TreeNode::Resize;
}

static Manifold applyNodeTransform(const Manifold &source, const SceneDocument::TreeNode &node)
{
    if (node.operation == SceneDocument::TreeNode::Translate)
        return source.Translate(vec3(node.position.x(), node.position.y(), node.position.z()));
    if (node.operation == SceneDocument::TreeNode::Rotate)
        return source.Rotate(node.rotation.x(), node.rotation.y(), node.rotation.z());
    if (node.operation == SceneDocument::TreeNode::Scale)
        return source.Scale(vec3(node.scale.x(), node.scale.y(), node.scale.z()));
    if (node.operation == SceneDocument::TreeNode::Resize)
        return source.Scale(vec3(node.scale.x(), node.scale.y(), node.scale.z()));
    if (node.operation == SceneDocument::TreeNode::LinearExtrude) {
        // Handled directly in evaluateNode via Manifold::Extrude — no post-transform needed.
        return source;
    }
    if (node.operation == SceneDocument::TreeNode::Mirror) {
        const QVector3D &n = node.position;
        if (qFuzzyIsNull(n.x()) && qFuzzyIsNull(n.y()) && qFuzzyIsNull(n.z()))
            return source; // zero normal = no-op, matches OpenSCAD behaviour
        return source.Mirror(vec3(n.x(), n.y(), n.z()));
    }

    return source;
}

static Manifold evaluateNode(const SceneDocument::TreeNode &node,
                             const SceneDocument &scene,
                             QHash<QString, qreal> variables,
                             const QHash<QString, QString> &moduleArgumentOverrides = {})
{
    if (node.type == SceneDocument::TreeNode::Primitive) {
        const ShapeNode *shape = scene.shapeById(node.shapeId);
        if (!shape)
            return {};

        const int fn = static_cast<int>(variables.value(QStringLiteral("$fn"), 0.0));
        const double fa = variables.value(QStringLiteral("$fa"), 12.0);
        const double fs = variables.value(QStringLiteral("$fs"), 2.0);
        return manifoldFromShape(shapeWithEvaluatedParameters(*shape, variables), fn, fa, fs);
    }

    if (node.type == SceneDocument::TreeNode::Variable)
        return {};

    if (node.type == SceneDocument::TreeNode::ModuleCall) {
        const SceneDocument::TreeNode *module = scene.treeNodeById(node.shapeId);
        if (!module || module->type != SceneDocument::TreeNode::Group
            || module->operation != SceneDocument::TreeNode::Module)
            return {};

        return evaluateNode(*module, scene, variables, resolveModuleArguments(node.moduleCallArguments, *module));
    }

    const QHash<QString, qreal> localVariables = variablesWithScopedVariables(node, variables, moduleArgumentOverrides);

    if (node.operation == SceneDocument::TreeNode::Conditional) {
        const QString cond = node.conditionExpression.trimmed();
        qreal condVal = 0.0;
        const bool condTrue = cond.isEmpty()
            || (ExpressionSyntax::evaluate(cond, localVariables, &condVal, nullptr) && condVal != 0.0);
        const SceneDocument::TreeNode *branch =
            condTrue ? (node.children.size() > 0 ? &node.children[0] : nullptr)
                     : (node.children.size() > 1 ? &node.children[1] : nullptr);
        if (!branch)
            return {};
        const bool branchContainer = branch->type == SceneDocument::TreeNode::Group
                                     && branch->operation == SceneDocument::TreeNode::Union;
        if (!branchContainer)
            return evaluateNode(*branch, scene, localVariables);
        if (branch->children.isEmpty())
            return {};
        Manifold result;
        bool hasResult = false;
        for (const SceneDocument::TreeNode &child : branch->children) {
            if (child.type == SceneDocument::TreeNode::Variable) continue;
            const Manifold childResult = evaluateNode(child, scene, localVariables);
            if (!hasResult) { result = childResult; hasResult = true; }
            else result += childResult;
        }
        return result;
    }

    if (node.operation == SceneDocument::TreeNode::For) {
        QVector<qreal> values;
        const QString variableName = node.loopVariable.trimmed().isEmpty()
                                         ? QStringLiteral("i")
                                         : node.loopVariable.trimmed();
        const QString rangeExpression = node.loopRangeExpression.trimmed().isEmpty()
                                            ? QStringLiteral("[0 : 1 : 3]")
                                            : node.loopRangeExpression.trimmed();
        if (!evaluateRangeExpression(rangeExpression, variables, &values) || values.isEmpty())
            return {};

        bool hasResult = false;
        Manifold result;
        for (qreal value : values) {
            QHash<QString, qreal> iterationVariables = variables;
            iterationVariables[variableName] = value;
            for (const SceneDocument::TreeNode &child : node.children) {
                if (child.type == SceneDocument::TreeNode::Variable)
                    continue;

                const Manifold childResult = evaluateNode(child, scene, iterationVariables);
                if (!hasResult) {
                    result = childResult;
                    hasResult = true;
                } else {
                    result += childResult;
                }
            }
        }

        return result;
    }

    const SceneDocument::TreeNode evaluatedNode = nodeWithEvaluatedTransform(node, localVariables);

    QVector<const SceneDocument::TreeNode *> geometryChildren;
    for (const SceneDocument::TreeNode &child : evaluatedNode.children) {
        if (child.type != SceneDocument::TreeNode::Variable)
            geometryChildren.append(&child);
    }

    if (geometryChildren.isEmpty())
        return {};

    if (evaluatedNode.operation == SceneDocument::TreeNode::Polyhedron) {
        // Collect points and faces from Point3D and Face3D children
        QVector<QVector3D> pts;
        QVector<QVector<int>> faces;
        for (const SceneDocument::TreeNode *child : geometryChildren) {
            if (child->type != SceneDocument::TreeNode::Primitive)
                continue;
            const ShapeNode *shape = scene.shapeById(child->shapeId);
            if (!shape)
                continue;
            if (shape->type == ShapeNode::Point3D)
                pts.append(shape->position);
            else if (shape->type == ShapeNode::Face3D && !shape->polyhedronFaces.isEmpty())
                faces.append(shape->polyhedronFaces.first());
        }
        if (pts.isEmpty() || faces.isEmpty())
            return {};
        const int pointCount = pts.size();
        QVector<QVector<int>> validFaces;
        validFaces.reserve(faces.size());
        for (const QVector<int> &face : faces) {
            if (face.size() < 3)
                continue;
            // Clamp indices to valid range
            auto safe = [pointCount](int idx) -> int {
                return qBound(0, idx, pointCount - 1);
            };
            QVector<int> clampedFace = face;
            for (int &idx : clampedFace)
                idx = safe(idx);
            validFaces.append(clampedFace);
        }

        ShapeNode polyShape;
        polyShape.type = ShapeNode::Polyhedron;
        polyShape.polyhedronPoints = pts;
        polyShape.polyhedronFaces = validFaces;
        return applyNodeTransform(manifoldFromSceneMesh(buildShapeMesh(polyShape)), evaluatedNode);
    }

    if (evaluatedNode.operation == SceneDocument::TreeNode::Hull) {
        std::vector<Manifold> parts;
        for (const SceneDocument::TreeNode *child : geometryChildren) {
            const Manifold part = evaluateNode(*child, scene, localVariables);
            if (!part.IsEmpty())
                parts.push_back(part);
        }
        if (parts.empty())
            return {};
        return applyNodeTransform(Manifold::Hull(parts), evaluatedNode);
    }

    if (evaluatedNode.operation == SceneDocument::TreeNode::LinearExtrude) {
        const int fn     = static_cast<int>(localVariables.value(QStringLiteral("$fn"), 0.0));
        const double fa  = localVariables.value(QStringLiteral("$fa"), 12.0);
        const double fs  = localVariables.value(QStringLiteral("$fs"), 2.0);
        const float height = qMax(0.01f, evaluatedNode.scale.x());
        const float twist  = evaluatedNode.linearExtrudeTwist;
        const int slices   = evaluatedNode.linearExtrudeSlices > 0
                                 ? evaluatedNode.linearExtrudeSlices
                                 : (qAbs(twist) > 0.001f ? qMax(2, fn > 0 ? fn : 20) : 0);
        const float es = evaluatedNode.linearExtrudeScaleVal > 0.01f
                             ? evaluatedNode.linearExtrudeScaleVal : 1.0f;
        const int profileSegs = computeCircularSegments(fn, 10.0, fa, fs);

        Polygons polygons;
        for (const SceneDocument::TreeNode *child : geometryChildren)
            collectRotateExtrudePolygons(*child, scene, localVariables, profileSegs, polygons);
        if (polygons.empty())
            return {};

        // Normalise winding/holes via CrossSection (EvenOdd, like OpenSCAD's 2D
        // fill) so glyph outlines and overlapping profiles extrude cleanly
        // instead of failing Manifold's strict triangulator.
        const Polygons profile = manifold::CrossSection(
            polygons, manifold::CrossSection::FillRule::EvenOdd).ToPolygons();
        if (profile.empty())
            return {};

        Manifold extruded = Manifold::Extrude(profile, static_cast<double>(height),
                                              slices,
                                              static_cast<double>(twist),
                                              vec2(es, es));
        if (evaluatedNode.linearExtrudeCenter)
            extruded = extruded.Translate(vec3(0.0, 0.0, static_cast<double>(-height * 0.5f)));
        return extruded;
    }

    if (evaluatedNode.operation == SceneDocument::TreeNode::RotateExtrude) {
        const int fn = static_cast<int>(localVariables.value(QStringLiteral("$fn"), 0.0));
        const double fa = localVariables.value(QStringLiteral("$fa"), 12.0);
        const double fs = localVariables.value(QStringLiteral("$fs"), 2.0);
        const float angle = qBound(0.1f, evaluatedNode.scale.x(), 360.0f);
        const int segs = computeCircularSegments(fn, 10.0, fa, fs);
        Polygons polygons;
        for (const SceneDocument::TreeNode *child : geometryChildren)
            collectRotateExtrudePolygons(*child, scene, localVariables, segs, polygons);
        if (polygons.empty())
            return {};
        return applyNodeTransform(Manifold::Revolve(polygons, fn > 0 ? fn : 0, static_cast<double>(angle)), evaluatedNode);
    }

    if (evaluatedNode.operation == SceneDocument::TreeNode::Offset) {
        using manifold::CrossSection;
        const int fn = static_cast<int>(localVariables.value(QStringLiteral("$fn"), 0.0));
        const double fa = localVariables.value(QStringLiteral("$fa"), 12.0);
        const double fs = localVariables.value(QStringLiteral("$fs"), 2.0);
        const int segs = computeCircularSegments(fn, 10.0, fa, fs);
        Polygons polygons;
        for (const SceneDocument::TreeNode *child : geometryChildren)
            collectRotateExtrudePolygons(*child, scene, localVariables, segs, polygons);
        if (polygons.empty())
            return {};

        // Union the child profiles, then grow/shrink the outline.
        CrossSection cs(polygons, CrossSection::FillRule::NonZero);
        CrossSection::JoinType jt = CrossSection::JoinType::Round; // r= (rounded)
        if (evaluatedNode.offsetUseDelta)
            jt = evaluatedNode.offsetChamfer ? CrossSection::JoinType::Square
                                             : CrossSection::JoinType::Miter;
        const CrossSection grown = cs.Offset(static_cast<double>(evaluatedNode.offsetAmount),
                                             jt, 2.0, fn > 0 ? fn : 0);
        const Polygons result = grown.ToPolygons();
        if (result.empty())
            return {};
        // Keep the 2D-as-thin-solid convention (z 0..0.1) used elsewhere.
        return applyNodeTransform(Manifold::Extrude(result, 0.1), evaluatedNode);
    }

    if (evaluatedNode.operation == SceneDocument::TreeNode::Projection) {
        // Evaluate the first (only) geometry child as a 3D Manifold, then
        // collapse to 2D: cut=true → slice at z=0, cut=false → full XY projection.
        if (geometryChildren.isEmpty())
            return {};
        const Manifold childMesh = evaluateNode(*geometryChildren.first(), scene, localVariables);
        if (childMesh.IsEmpty())
            return {};
        const Polygons projected = evaluatedNode.projectionCut
                                       ? childMesh.Slice(0.0)
                                       : childMesh.Project();
        if (projected.empty())
            return {};
        // 2D-as-thin-solid convention (z 0..0.1).
        return applyNodeTransform(Manifold::Extrude(projected, 0.1), evaluatedNode);
    }

    if (evaluatedNode.operation == SceneDocument::TreeNode::Minkowski) {
        if (geometryChildren.size() < 2)
            return geometryChildren.isEmpty() ? Manifold{} : evaluateNode(*geometryChildren.first(), scene, localVariables);
        Manifold result = evaluateNode(*geometryChildren.first(), scene, localVariables);
        for (int i = 1; i < geometryChildren.size(); ++i)
            result = result.MinkowskiSum(evaluateNode(*geometryChildren[i], scene, localVariables));
        return applyNodeTransform(result, evaluatedNode);
    }

    Manifold result = evaluateNode(*geometryChildren.first(), scene, localVariables);

    for (int i = 1; i < geometryChildren.size(); ++i) {
        const Manifold child = evaluateNode(*geometryChildren[i], scene, localVariables);

        if (isUnionLikeOperation(evaluatedNode.operation))
            result += child;
        else if (evaluatedNode.operation == SceneDocument::TreeNode::Difference)
            result -= child;
        else if (evaluatedNode.operation == SceneDocument::TreeNode::Intersection)
            result ^= child;
    }

    return applyNodeTransform(result, evaluatedNode);
}

static Manifold evaluateDocumentGeometry(const SceneDocument &scene)
{
    const QHash<QString, qreal> variables = topLevelVariables(scene.treeRoot());
    bool hasResult = false;
    Manifold result;

    for (const SceneDocument::TreeNode &child : scene.treeRoot().children) {
        if (isTopLevelModuleDeclaration(child))
            continue;

        const Manifold childResult = evaluateNode(child, scene, variables);
        if (!hasResult) {
            result = childResult;
            hasResult = true;
        } else {
            result += childResult;
        }
    }

    return result;
}

static SceneMesh sceneMeshFromManifold(const Manifold &manifold)
{
    SceneMesh mesh;
    const MeshGL meshGl = manifold.GetMeshGL();

    mesh.triangles.reserve(static_cast<int>(meshGl.triVerts.size() / 3));
    mesh.shadowPoints.reserve(static_cast<int>(meshGl.vertProperties.size() / meshGl.numProp));

    for (size_t i = 0; i < meshGl.vertProperties.size(); i += meshGl.numProp) {
        mesh.shadowPoints.append(QVector3D(meshGl.vertProperties[i],
                                           meshGl.vertProperties[i + 1],
                                           meshGl.vertProperties[i + 2]));
    }

    auto vertex = [&](uint32_t index) {
        const size_t offset = static_cast<size_t>(index) * meshGl.numProp;
        return QVector3D(meshGl.vertProperties[offset],
                         meshGl.vertProperties[offset + 1],
                         meshGl.vertProperties[offset + 2]);
    };

    for (size_t i = 0; i + 2 < meshGl.triVerts.size(); i += 3) {
        mesh.triangles.append(makeTriangle(vertex(meshGl.triVerts[i]),
                                           vertex(meshGl.triVerts[i + 1]),
                                           vertex(meshGl.triVerts[i + 2])));
    }

    return mesh;
}
#endif

bool buildManifoldCsgMesh(const QVector<ShapeNode> &shapes, SceneMesh *mesh, QString *errorMessage)
{
    SceneDocument scene;
    scene.replaceShapes(shapes);
    return buildManifoldCsgMesh(scene, mesh, errorMessage);
}

bool buildManifoldCsgMesh(const SceneDocument &scene, SceneMesh *mesh, QString *errorMessage)
{
#ifdef HAVE_MANIFOLD_CSG
    if (!mesh)
        return false;

    QMutexLocker locker(&s_manifoldMutex); // serialise concurrent calls from any thread

    const Manifold result = evaluateDocumentGeometry(scene);

    if (result.Status() != Manifold::Error::NoError) {
        if (errorMessage)
            *errorMessage = "Manifold returned an invalid CSG result.";
        return false;
    }

    if (result.IsEmpty()) {
        if (errorMessage)
            *errorMessage = "Manifold returned an empty CSG result.";
        return false;
    }

    *mesh = sceneMeshFromManifold(result);
    return !mesh->triangles.isEmpty();
#else
    Q_UNUSED(scene);
    Q_UNUSED(mesh);
    if (errorMessage)
        *errorMessage = "Manifold CSG is not built.";
    return false;
#endif
}

// Finds the path from root to the node with targetId. Returns true if found.
static bool findNodePath(const SceneDocument::TreeNode &current,
                          int targetId,
                          QVector<const SceneDocument::TreeNode *> &path)
{
    path.push_back(&current);
    if (current.id == targetId)
        return true;
    for (const SceneDocument::TreeNode &child : current.children) {
        if (findNodePath(child, targetId, path))
            return true;
    }
    path.pop_back();
    return false;
}

// Evaluates the node with targetId in world-space by applying all parent transforms.
static Manifold evaluateNodeById(int targetId,
                                  const SceneDocument &scene,
                                  const QHash<QString, qreal> &rootVariables)
{
    QVector<const SceneDocument::TreeNode *> path;
    if (!findNodePath(scene.treeRoot(), targetId, path) || path.isEmpty())
        return {};

    // Accumulate variable context from root down to (but not including) the target
    QHash<QString, qreal> vars = rootVariables;
    for (int i = 0; i < static_cast<int>(path.size()) - 1; ++i) {
        for (const SceneDocument::TreeNode &c : path[i]->children) {
            if (c.type == SceneDocument::TreeNode::Variable)
                vars[c.variableName] = c.variableValue;
        }
    }

    // Evaluate the target node itself in local space
    Manifold result = evaluateNode(*path.last(), scene, vars);
    if (result.IsEmpty() || result.Status() != Manifold::Error::NoError)
        return {};

    // Apply parent transform groups (Translate/Rotate/Scale/Mirror/Resize)
    // Process from direct parent up toward root
    for (int i = static_cast<int>(path.size()) - 2; i >= 0; --i) {
        const SceneDocument::TreeNode *ancestor = path[i];
        if (ancestor->type != SceneDocument::TreeNode::Group)
            continue;
        const auto op = ancestor->operation;
        if (op != SceneDocument::TreeNode::Translate
            && op != SceneDocument::TreeNode::Rotate
            && op != SceneDocument::TreeNode::Scale
            && op != SceneDocument::TreeNode::Mirror
            && op != SceneDocument::TreeNode::Resize)
            continue;
        // Recompute variable context up to this ancestor's level
        QHash<QString, qreal> ancestorVars = rootVariables;
        for (int j = 0; j < i; ++j) {
            for (const SceneDocument::TreeNode &c : path[j]->children) {
                if (c.type == SceneDocument::TreeNode::Variable)
                    ancestorVars[c.variableName] = c.variableValue;
            }
        }
        const SceneDocument::TreeNode evAncestor = nodeWithEvaluatedTransform(*ancestor, ancestorVars);
        result = applyNodeTransform(result, evAncestor);
    }

    return result;
}

bool buildManifoldNodeMesh(const SceneDocument::TreeNode &node,
                           const SceneDocument &scene,
                           SceneMesh *mesh,
                           QString *errorMessage)
{
#ifdef HAVE_MANIFOLD_CSG
    if (!mesh)
        return false;

    QMutexLocker locker(&s_manifoldMutex);

    const QHash<QString, qreal> variables = topLevelVariables(scene.treeRoot());
    const Manifold result = evaluateNodeById(node.id, scene, variables);

    if (result.Status() != Manifold::Error::NoError || result.IsEmpty())
        return false;

    *mesh = sceneMeshFromManifold(result);
    return !mesh->triangles.isEmpty();
#else
    Q_UNUSED(node); Q_UNUSED(scene); Q_UNUSED(mesh);
    if (errorMessage) *errorMessage = "Manifold CSG is not built.";
    return false;
#endif
}
