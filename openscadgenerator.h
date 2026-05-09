#ifndef OPENSCADGENERATOR_H
#define OPENSCADGENERATOR_H

#include "scenedocument.h"

#include <QString>

class OpenScadGenerator
{
public:
    static QString generate(const SceneDocument &scene);

private:
    static QString shapeToOpenScad(const ShapeNode &shape);
};

#endif
