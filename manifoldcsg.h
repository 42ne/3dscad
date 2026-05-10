#ifndef MANIFOLDCSG_H
#define MANIFOLDCSG_H

#include "scenemesh.h"

#include <QString>
#include <QVector>

bool buildManifoldCsgMesh(const QVector<ShapeNode> &shapes, SceneMesh *mesh, QString *errorMessage);

#endif
