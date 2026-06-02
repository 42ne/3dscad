#ifndef SCENETREEINLINEEDITOR_H
#define SCENETREEINLINEEDITOR_H

#include <QObject>
#include <QRectF>
#include <QString>

class SceneTreeGraphicsWidget;
class SceneTreeInlineTextInput;

struct ExpressionEditTarget {
    enum Kind {
        None,
        Transform,
        ShapeParameter,
        Variable,
        ModuleCallArgument,
        PolyhedronParticipation,
        Polygon2DPoint
    };
    Kind kind = None;
    QRectF hoverRect;
    QRectF editRect;
    QString label;
    QString expression;
    int nodeId = 0;
    int secondaryId = 0;
};

class SceneTreeInlineEditor : public QObject
{
    Q_OBJECT
    friend class SceneTreeGraphicsWidget;

public:
    explicit SceneTreeInlineEditor(SceneTreeGraphicsWidget *widget);

    void startInlineRename(int nodeId, bool isModule, const QRectF &sceneRect, const QString &currentName);
    void startInlineExpressionEdit(const ExpressionEditTarget &target);
    bool validateInlineExpression(const ExpressionEditTarget &target,
                                  const QString &expression, QString *errorMessage) const;
    void updateInlineInputGeometry();
    bool isEditing() const { return m_inlineInputActive; }

private:
    SceneTreeGraphicsWidget *m_widget;
    SceneTreeInlineTextInput *m_inlineInput = nullptr;
    bool m_inlineInputActive = false;
    QRectF m_inlineInputSceneRect;
};

#endif
