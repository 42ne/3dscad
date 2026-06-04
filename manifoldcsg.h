#ifndef MANIFOLDCSG_H
#define MANIFOLDCSG_H

#include "scenedocument.h"
#include "scenemesh.h"

#include <QString>
#include <QVector>

bool buildManifoldCsgMesh(const QVector<ShapeNode> &shapes, SceneMesh *mesh, QString *errorMessage);
bool buildManifoldCsgMesh(const SceneDocument &scene, SceneMesh *mesh, QString *errorMessage);
bool buildManifoldNodeMesh(const SceneDocument::TreeNode &node, const SceneDocument &scene, SceneMesh *mesh, QString *errorMessage);

#endif
