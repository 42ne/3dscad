#include "scenemesh.h"

#include <algorithm>
#include <QVector2D>
#include <QtMath>
#include <QFont>
#include <QFontMetricsF>
#include <QPainterPath>
#include <QPolygonF>
#include <QFile>
#include <QTextStream>
#include <QDataStream>

static QVector3D rotatePoint(const QVector3D &point, const QVector3D &degrees)
{
    const float rx = qDegreesToRadians(degrees.x());
    const float ry = qDegreesToRadians(degrees.y());
    const float rz = qDegreesToRadians(degrees.z());

    QVector3D p = point;

    p = QVector3D(
        p.x(),
        p.y() * qCos(rx) - p.z() * qSin(rx),
        p.y() * qSin(rx) + p.z() * qCos(rx));

    p = QVector3D(
        p.x() * qCos(ry) + p.z() * qSin(ry),
        p.y(),
        -p.x() * qSin(ry) + p.z() * qCos(ry));

    p = QVector3D(
        p.x() * qCos(rz) - p.y() * qSin(rz),
        p.x() * qSin(rz) + p.y() * qCos(rz),
        p.z());

    return p;
}

static QVector3D faceNormal(const QVector3D &a, const QVector3D &b, const QVector3D &c)
{
    QVector3D normal = -QVector3D::crossProduct(b - a, c - a);

    if (normal.lengthSquared() <= 0.0001f)
        return QVector3D(0.0f, 0.0f, 1.0f);

    return normal.normalized();
}

static MeshTriangle makeTriangle(const QVector3D &a, const QVector3D &b, const QVector3D &c, int shade = 100)
{
    MeshTriangle triangle;
    triangle.a = a;
    triangle.b = b;
    triangle.c = c;
    triangle.normal = faceNormal(a, b, c);
    triangle.shade = shade;

    return triangle;
}

static QVector3D polygonNormal(const QVector<QVector3D> &vertices)
{
    QVector3D normal;
    for (int i = 0; i < vertices.size(); ++i) {
        const QVector3D &a = vertices[i];
        const QVector3D &b = vertices[(i + 1) % vertices.size()];
        normal.setX(normal.x() + (a.y() - b.y()) * (a.z() + b.z()));
        normal.setY(normal.y() + (a.z() - b.z()) * (a.x() + b.x()));
        normal.setZ(normal.z() + (a.x() - b.x()) * (a.y() + b.y()));
    }
    return normal;
}

static void appendPolygon(QVector<MeshTriangle> *triangles, const QVector<QVector3D> &vertices, int shade = 100)
{
    const int n = vertices.size();
    if (n < 3) return;
    if (n == 3) {
        triangles->append(makeTriangle(vertices[0], vertices[1], vertices[2], shade));
        return;
    }

    // Project to the polygon's own 2D plane so ear-clip works for non-convex faces.
    const QVector3D nrm = polygonNormal(vertices).normalized();
    if (nrm.isNull()) return;
    const QVector3D ref = qAbs(nrm.x()) < 0.9f ? QVector3D(1,0,0) : QVector3D(0,1,0);
    const QVector3D bu  = QVector3D::crossProduct(ref, nrm).normalized();
    const QVector3D bv  = QVector3D::crossProduct(nrm, bu).normalized();

    QVector<QVector2D> p2(n);
    for (int i = 0; i < n; ++i)
        p2[i] = {QVector3D::dotProduct(vertices[i], bu),
                 QVector3D::dotProduct(vertices[i], bv)};

    auto cross2 = [&](int a, int b, int c) -> float {
        return (p2[b].x()-p2[a].x())*(p2[c].y()-p2[a].y())
             - (p2[b].y()-p2[a].y())*(p2[c].x()-p2[a].x());
    };

    QVector<int> ring(n);
    for (int i = 0; i < n; ++i) ring[i] = i;

    float area = 0.0f;
    for (int i = 0; i < n; ++i)
        area += p2[ring[i]].x() * p2[ring[(i+1)%n]].y()
              - p2[ring[(i+1)%n]].x() * p2[ring[i]].y();
    const float winding = area >= 0.0f ? 1.0f : -1.0f;
    constexpr float eps = 1.0e-5f;

    while (ring.size() > 3) {
        const int rn = ring.size();
        bool clipped = false;
        for (int i = 0; i < rn; ++i) {
            const int a = ring[(i-1+rn)%rn], b = ring[i], c = ring[(i+1)%rn];
            if (winding * cross2(a, b, c) <= eps) continue;
            bool blocked = false;
            for (int j = 0; j < rn && !blocked; ++j) {
                if (j==(i-1+rn)%rn || j==i || j==(i+1)%rn) continue;
                const int w = ring[j];
                if (winding * cross2(a,b,w) >= -eps
                    && winding * cross2(b,c,w) >= -eps
                    && winding * cross2(c,a,w) >= -eps)
                    blocked = true;
            }
            if (blocked) continue;
            triangles->append(makeTriangle(vertices[a], vertices[b], vertices[c], shade));
            ring.remove(i);
            clipped = true;
            break;
        }
        if (!clipped) break; // degenerate polygon
    }
    if (ring.size() == 3)
        triangles->append(makeTriangle(vertices[ring[0]], vertices[ring[1]], vertices[ring[2]], shade));
}

static SceneMesh buildCubeMesh(const ShapeNode &shape)
{
    const QVector3D half = shape.size * 0.5f;
    const QVector3D shift = shape.center ? QVector3D(0, 0, 0) : half;
    SceneMesh mesh = buildBoxMesh(-half + shift, half + shift);

    for (MeshTriangle &triangle : mesh.triangles) {
        triangle.a = rotatePoint(triangle.a, shape.rotation) + shape.position;
        triangle.b = rotatePoint(triangle.b, shape.rotation) + shape.position;
        triangle.c = rotatePoint(triangle.c, shape.rotation) + shape.position;
        triangle.normal = faceNormal(triangle.a, triangle.b, triangle.c);
    }

    for (QVector3D &point : mesh.shadowPoints)
        point = rotatePoint(point, shape.rotation) + shape.position;

    return mesh;
}

SceneMesh buildBoxMesh(const QVector3D &minimum, const QVector3D &maximum)
{
    SceneMesh mesh;
    QVector<QVector3D> vertices = {
        {minimum.x(), minimum.y(), minimum.z()}, {maximum.x(), minimum.y(), minimum.z()},
        {maximum.x(), maximum.y(), minimum.z()}, {minimum.x(), maximum.y(), minimum.z()},
        {minimum.x(), minimum.y(), maximum.z()}, {maximum.x(), minimum.y(), maximum.z()},
        {maximum.x(), maximum.y(), maximum.z()}, {minimum.x(), maximum.y(), maximum.z()}
    };

    mesh.shadowPoints = vertices;

    const QVector<QVector<int>> faceIndices = {
        {0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1},
        {1, 5, 6, 2}, {2, 6, 7, 3}, {3, 7, 4, 0}
    };

    const QVector<int> shades = {82, 116, 92, 105, 122, 96};
    for (int i = 0; i < faceIndices.size(); ++i) {
        QVector<QVector3D> faceVertices;
        for (int index : faceIndices[i])
            faceVertices.append(vertices[index]);

        appendPolygon(&mesh.triangles, faceVertices, shades[i]);
    }

    return mesh;
}

static SceneMesh buildSphereMesh(const ShapeNode &shape, int fn = 0, double fa = 12.0, double fs = 2.0)
{
    SceneMesh mesh;
    const int sectors = computeCircularSegments(fn, shape.radius, fa, fs);
    const int stacks  = sectors / 2;

    for (int i = 0; i < sectors; ++i) {
        const float angle = 2.0f * M_PI * i / sectors;
        mesh.shadowPoints.append(shape.position + QVector3D(shape.radius * qCos(angle),
                                                            shape.radius * qSin(angle),
                                                            shape.radius));
    }

    auto spherePoint = [&](int stack, int sector) {
        const float theta = M_PI * stack / stacks;
        const float phi = 2.0f * M_PI * sector / sectors;
        const float ringRadius = shape.radius * qSin(theta);

        return shape.position + QVector3D(
                   ringRadius * qCos(phi),
                   ringRadius * qSin(phi),
                   shape.radius * qCos(theta));
    };

    for (int stack = 0; stack < stacks; ++stack) {
        for (int sector = 0; sector < sectors; ++sector) {
            const int nextSector = (sector + 1) % sectors;
            const QVector3D topLeft = spherePoint(stack, sector);
            const QVector3D bottomLeft = spherePoint(stack + 1, sector);
            const QVector3D bottomRight = spherePoint(stack + 1, nextSector);
            const QVector3D topRight = spherePoint(stack, nextSector);

            if (stack == 0)
                appendPolygon(&mesh.triangles, {topLeft, bottomLeft, bottomRight});
            else if (stack == stacks - 1)
                appendPolygon(&mesh.triangles, {topLeft, bottomLeft, topRight});
            else
                appendPolygon(&mesh.triangles, {topLeft, bottomLeft, bottomRight, topRight});
        }
    }

    return mesh;
}

static SceneMesh buildCylinderMesh(const ShapeNode &shape, int fn = 0, double fa = 12.0, double fs = 2.0)
{
    SceneMesh mesh;
    QVector<QVector3D> top;
    QVector<QVector3D> bottom;
    const int sectors = computeCircularSegments(fn, shape.radius, fa, fs);

    const float zBottom = shape.center ? -shape.height * 0.5f : 0.0f;
    const float zTop    = shape.center ?  shape.height * 0.5f : shape.height;

    for (int i = 0; i < sectors; ++i) {
        const float angle = 2.0f * M_PI * i / sectors;
        const QVector3D ringPoint(shape.radius * qCos(angle), shape.radius * qSin(angle), 0);
        top.append(rotatePoint(ringPoint + QVector3D(0, 0, zTop),    shape.rotation) + shape.position);
        bottom.append(rotatePoint(ringPoint + QVector3D(0, 0, zBottom), shape.rotation) + shape.position);
    }

    mesh.shadowPoints = bottom + top;

    for (int i = 0; i < top.size(); ++i) {
        const int next = (i + 1) % top.size();
        appendPolygon(&mesh.triangles, {bottom[i], bottom[next], top[next], top[i]});
    }

    appendPolygon(&mesh.triangles, bottom, 82);
    appendPolygon(&mesh.triangles, top, 120);

    return mesh;
}

static SceneMesh buildConeMesh(const ShapeNode &shape, int fn = 0, double fa = 12.0, double fs = 2.0)
{
    // Frustum/cone: bottom radius = shape.radius (r1), top radius = shape.radius2 (r2)
    SceneMesh mesh;
    QVector<QVector3D> top;
    QVector<QVector3D> bottom;
    const int sectors = computeCircularSegments(fn, qMax(shape.radius, shape.radius2), fa, fs);

    const float zBottom = shape.center ? -shape.height * 0.5f : 0.0f;
    const float zTop    = shape.center ?  shape.height * 0.5f : shape.height;

    for (int i = 0; i < sectors; ++i) {
        const float angle = 2.0f * M_PI * i / sectors;
        const float ca = qCos(angle), sa = qSin(angle);
        const QVector3D topPt(shape.radius2 * ca, shape.radius2 * sa, 0);
        const QVector3D botPt(shape.radius  * ca, shape.radius  * sa, 0);
        top.append(rotatePoint(topPt + QVector3D(0, 0, zTop),    shape.rotation) + shape.position);
        bottom.append(rotatePoint(botPt + QVector3D(0, 0, zBottom), shape.rotation) + shape.position);
    }

    mesh.shadowPoints = bottom;
    if (shape.radius2 > 0.0f)
        mesh.shadowPoints += top;
    else {
        // apex is a single point
        const QVector3D apex = rotatePoint(QVector3D(0, 0, shape.height * 0.5f), shape.rotation) + shape.position;
        mesh.shadowPoints.append(apex);
    }

    for (int i = 0; i < sectors; ++i) {
        const int next = (i + 1) % sectors;
        if (shape.radius2 > 0.0f)
            appendPolygon(&mesh.triangles, {bottom[i], bottom[next], top[next], top[i]});
        else
            appendPolygon(&mesh.triangles, {bottom[i], bottom[next], top[i]});
    }

    appendPolygon(&mesh.triangles, bottom, 82);
    if (shape.radius2 > 0.0f)
        appendPolygon(&mesh.triangles, top, 120);

    return mesh;
}

static SceneMesh buildCircleMesh(const ShapeNode &shape, int fn = 0, double fa = 12.0, double fs = 2.0)
{
    SceneMesh mesh;
    QVector<QVector3D> top;
    QVector<QVector3D> bottom;
    const int sectors = computeCircularSegments(fn, shape.radius, fa, fs);
    constexpr float thickness = 0.1f;

    for (int i = 0; i < sectors; ++i) {
        const float angle = 2.0f * M_PI * i / sectors;
        const QVector3D ringPoint(shape.radius * qCos(angle), shape.radius * qSin(angle), 0.0f);
        top.append(rotatePoint(ringPoint + QVector3D(0, 0, thickness * 0.5f), shape.rotation) + shape.position);
        bottom.append(rotatePoint(ringPoint - QVector3D(0, 0, thickness * 0.5f), shape.rotation) + shape.position);
    }

    mesh.shadowPoints = bottom + top;
    for (int i = 0; i < sectors; ++i) {
        const int next = (i + 1) % sectors;
        appendPolygon(&mesh.triangles, {bottom[i], bottom[next], top[next], top[i]}, 96);
    }
    appendPolygon(&mesh.triangles, bottom, 82);
    appendPolygon(&mesh.triangles, top, 120);
    return mesh;
}

static SceneMesh buildFlatPolygonMesh(const QVector<QVector3D> &localPoints,
                                      const ShapeNode &shape)
{
    SceneMesh mesh;
    if (localPoints.size() < 3)
        return mesh;

    QVector<QVector3D> top;
    QVector<QVector3D> bottom;
    top.reserve(localPoints.size());
    bottom.reserve(localPoints.size());
    constexpr float thickness = 0.1f;

    for (const QVector3D &point : localPoints) {
        const QVector3D xy(point.x(), point.y(), 0.0f);
        top.append(rotatePoint(xy + QVector3D(0, 0, thickness * 0.5f), shape.rotation) + shape.position);
        bottom.append(rotatePoint(xy - QVector3D(0, 0, thickness * 0.5f), shape.rotation) + shape.position);
    }

    appendPolygon(&mesh.triangles, top, 108);
    QVector<QVector3D> reversedBottom = bottom;
    std::reverse(reversedBottom.begin(), reversedBottom.end());
    appendPolygon(&mesh.triangles, reversedBottom, 82);

    const int n = top.size();
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        mesh.triangles.append(makeTriangle(bottom[i], bottom[j], top[j], 92));
        mesh.triangles.append(makeTriangle(bottom[i], top[j], top[i], 96));
    }

    mesh.shadowPoints = bottom + top;
    return mesh;
}

static SceneMesh buildSquareMesh(const ShapeNode &shape)
{
    const float hx = shape.size.x() * 0.5f;
    const float hy = shape.size.y() * 0.5f;
    const float ox = shape.center ? 0.0f : hx;
    const float oy = shape.center ? 0.0f : hy;
    return buildFlatPolygonMesh({
        QVector3D(-hx + ox, -hy + oy, 0.0f),
        QVector3D( hx + ox, -hy + oy, 0.0f),
        QVector3D( hx + ox,  hy + oy, 0.0f),
        QVector3D(-hx + ox,  hy + oy, 0.0f)
    }, shape);
}

QVector<QVector<QVector3D>> buildGlyphContours(const ShapeNode &shape)
{
    QVector<QVector<QVector3D>> contours;
    if (shape.textValue.isEmpty())
        return contours;

    QFont font;
    if (!shape.fontName.isEmpty()) {
        QString family = shape.fontName;
        const int colon = family.indexOf(QLatin1Char(':'));
        if (colon >= 0)
            family = family.left(colon);
        if (!family.trimmed().isEmpty())
            font.setFamily(family.trimmed());
    }
    constexpr qreal renderPx = 128.0;
    font.setPixelSize(static_cast<int>(renderPx));

    const QFontMetricsF fm(font);
    const qreal spacing = shape.textSpacing > 0.0f ? static_cast<qreal>(shape.textSpacing) : 1.0;

    // spacing scales the per-character advance (OpenSCAD semantics). At spacing=1
    // lay out the whole string at once to keep kerning; otherwise advance the pen
    // glyph-by-glyph.
    QPainterPath path;
    qreal advance = 0.0;
    if (qAbs(spacing - 1.0) < 1e-4) {
        path.addText(0.0, 0.0, font, shape.textValue);
        advance = fm.horizontalAdvance(shape.textValue);
    } else {
        qreal penX = 0.0;
        for (const QChar &ch : shape.textValue) {
            path.addText(penX, 0.0, font, QString(ch));
            penX += fm.horizontalAdvance(ch) * spacing;
        }
        advance = penX;
    }
    if (path.isEmpty())
        return contours;

    const qreal ascent  = fm.ascent();
    const qreal descent = fm.descent();

    qreal hx = 0.0;
    if (shape.textHalign == QStringLiteral("center")) hx = -advance * 0.5;
    else if (shape.textHalign == QStringLiteral("right")) hx = -advance;

    qreal vy = 0.0;
    if (shape.textValign == QStringLiteral("bottom")) vy = -descent;
    else if (shape.textValign == QStringLiteral("top")) vy = ascent;
    else if (shape.textValign == QStringLiteral("center")) vy = (ascent - descent) * 0.5;

    const qreal scale = (shape.textSize > 0.01f ? shape.textSize : 10.0f) / renderPx;

    const QList<QPolygonF> subPaths = path.toSubpathPolygons();
    for (const QPolygonF &qp : subPaths) {
        QVector<QVector3D> pts;
        pts.reserve(qp.size());
        for (const QPointF &p : qp)
            pts.append(QVector3D(static_cast<float>((p.x() + hx) * scale),
                                 static_cast<float>(-(p.y() + vy) * scale), 0.0f)); // flip Y to model space
        if (pts.size() >= 3)
            contours.append(pts);
    }
    return contours;
}

// Preview/thumbnail mesh for Text. Each glyph contour is fan-triangulated as a
// thin slab; counter-wound holes are not subtracted here (the Manifold render
// path handles holes correctly via CrossSection) — acceptable for previews.
static SceneMesh buildTextMesh(const ShapeNode &shape)
{
    SceneMesh mesh;
    // Prefer the contours cached on the GUI thread; compute as a fallback.
    QVector<QVector<QVector3D>> contours = shape.textContours;
    if (contours.isEmpty())
        contours = buildGlyphContours(shape);

    for (const QVector<QVector3D> &contour : contours) {
        if (contour.size() < 3)
            continue;
        const SceneMesh sub = buildFlatPolygonMesh(contour, shape);
        mesh.triangles += sub.triangles;
        mesh.shadowPoints += sub.shadowPoints;
    }
    return mesh;
}

static SceneMesh buildPolyhedronMesh(const ShapeNode &shape)
{
    SceneMesh mesh;
    if (shape.polyhedronPoints.isEmpty() || shape.polyhedronFaces.isEmpty())
        return mesh;

    for (const QVector<int> &face : shape.polyhedronFaces) {
        if (face.size() < 3)
            continue;
        QVector<QVector3D> faceVertices;
        bool valid = true;
        for (int idx : face) {
            if (idx < 0 || idx >= shape.polyhedronPoints.size()) { valid = false; break; }
            faceVertices.append(shape.polyhedronPoints[idx]);
        }
        if (valid)
            appendPolygon(&mesh.triangles, faceVertices);
    }

    mesh.shadowPoints = shape.polyhedronPoints;
    return mesh;
}

SceneMesh buildShapeMesh(const ShapeNode &shape, int fn, double fa, double fs)
{
    if (shape.type == ShapeNode::Polyhedron)
        return buildPolyhedronMesh(shape);

    if (shape.type == ShapeNode::Circle)
        return buildCircleMesh(shape, fn, fa, fs);

    if (shape.type == ShapeNode::Square)
        return buildSquareMesh(shape);

    if (shape.type == ShapeNode::Polygon2D) {
        // If paths are specified, use paths[0] (outer contour) to order points.
        if (!shape.polyhedronFaces.isEmpty()) {
            const QVector<int> &outerPath = shape.polyhedronFaces.first();
            QVector<QVector3D> ordered;
            ordered.reserve(outerPath.size());
            for (int idx : outerPath)
                if (idx >= 0 && idx < shape.polyhedronPoints.size())
                    ordered.append(shape.polyhedronPoints[idx]);
            if (ordered.size() >= 3)
                return buildFlatPolygonMesh(ordered, shape);
        }
        return buildFlatPolygonMesh(shape.polyhedronPoints, shape);
    }

    if (shape.type == ShapeNode::Text)
        return buildTextMesh(shape);

    if (shape.type == ShapeNode::Sphere)
        return buildSphereMesh(shape, fn, fa, fs);

    if (shape.type == ShapeNode::Cylinder)
        return buildCylinderMesh(shape, fn, fa, fs);

    if (shape.type == ShapeNode::Cone)
        return buildConeMesh(shape, fn, fa, fs);

    if (shape.type == ShapeNode::Point3D) {
        // Render as a small sphere (dot) at the point's position
        ShapeNode dot;
        dot.type = ShapeNode::Sphere;
        dot.radius = 0.5f;
        dot.position = shape.position;
        return buildSphereMesh(dot, fn, fa, fs);
    }

    if (shape.type == ShapeNode::Face3D)
        return {};

    if (shape.type == ShapeNode::ImportedMesh) {
        return buildPolyhedronMesh(shape);
    }

    return buildCubeMesh(shape);
}

bool loadStlFile(const QString &filePath,
                 QVector<QVector3D> *outPoints,
                 QVector<QVector<int>> *outFaces,
                 QString *errorMessage)
{
    outPoints->clear();
    outFaces->clear();

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (errorMessage) *errorMessage = QStringLiteral("Cannot open file: %1").arg(file.errorString());
        return false;
    }

    QByteArray data = file.readAll();
    file.close();

    if (data.size() < 84) {
        if (errorMessage) *errorMessage = QStringLiteral("File too small to be a valid STL.");
        return false;
    }

    // Check for ASCII STL: starts with "solid" keyword
    const QString header = QString::fromLatin1(data.left(80).trimmed());
    if (header.startsWith(QStringLiteral("solid"), Qt::CaseInsensitive)
        && data.indexOf("facet") > 0) {
        // ── ASCII STL ──
        QTextStream in(data);
        QString line;
        QVector<QVector3D> verts;
        int triCount = 0;
        bool inFacet = false;
        while (in.readLineInto(&line)) {
            line = line.trimmed();
            if (line.isEmpty()) continue;

            if (line.startsWith(QStringLiteral("facet"), Qt::CaseInsensitive)) {
                inFacet = true;
                verts.clear();
            } else if (line.startsWith(QStringLiteral("endfacet"), Qt::CaseInsensitive)) {
                inFacet = false;
                if (verts.size() >= 3) {
                    QVector<int> face(3);
                    for (int i = 0; i < 3; ++i) {
                        const int idx = outPoints->size();
                        outPoints->append(verts[i]);
                        face[i] = idx;
                    }
                    outFaces->append(face);
                    ++triCount;
                }
                verts.clear();
            } else if (line.startsWith(QStringLiteral("vertex"), Qt::CaseInsensitive) && inFacet) {
                const QStringList parts = line.split(QStringLiteral(" "), Qt::SkipEmptyParts);
                if (parts.size() >= 4) {
                    bool ok1 = false, ok2 = false, ok3 = false;
                    const float x = parts[1].toFloat(&ok1);
                    const float y = parts[2].toFloat(&ok2);
                    const float z = parts[3].toFloat(&ok3);
                    if (ok1 && ok2 && ok3)
                        verts.append(QVector3D(x, y, z));
                }
            }
        }
        return triCount > 0;
    }

    // ── Binary STL ──
    // Header: 80 bytes (ignored), 4-byte uint32 triangle count.
    const quint32 triCount32 = *reinterpret_cast<const quint32*>(data.constData() + 80);
    const int expectedSize = 84 + static_cast<int>(triCount32) * 50;
    if (data.size() < expectedSize) {
        if (errorMessage) *errorMessage = QStringLiteral("Binary STL truncated: expected %1 bytes, got %2.")
                                               .arg(expectedSize).arg(data.size());
        return false;
    }

    const int count = static_cast<int>(triCount32);
    outPoints->reserve(count * 3);
    outFaces->reserve(count);

    const uchar *ptr = reinterpret_cast<const uchar*>(data.constData()) + 84;
    for (int i = 0; i < count; ++i) {
        // Skip normal (12 bytes), read 3 vertices (36 bytes), skip attribute (2 bytes)
        ptr += 12;
        QVector<int> face(3);
        for (int v = 0; v < 3; ++v) {
            float x = 0, y = 0, z = 0;
            memcpy(&x, ptr, sizeof(float)); ptr += 4;
            memcpy(&y, ptr, sizeof(float)); ptr += 4;
            memcpy(&z, ptr, sizeof(float)); ptr += 4;
            const int idx = outPoints->size();
            outPoints->append(QVector3D(x, y, z));
            face[v] = idx;
        }
        outFaces->append(face);
        ptr += 2; // attribute byte count
    }

    return count > 0;
}
