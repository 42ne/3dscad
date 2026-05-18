#ifndef OPENSCADPARSER_H
#define OPENSCADPARSER_H

#include "scenedocument.h"
#include "shapenode.h"

#include <QString>
#include <QVector>

class OpenScadParser
{
public:
    static bool parse(const QString &code, QVector<ShapeNode> *shapes, QString *errorMessage = nullptr);
    static bool parseScene(const QString &code, SceneDocument::Snapshot *snapshot, QString *errorMessage = nullptr);

private:
    static bool parseVector3(const QString &text, QVector3D *vector);
};

#endif
