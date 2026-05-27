#ifndef THEMEEDITORDIALOG_H
#define THEMEEDITORDIALOG_H

#include "appearancethemes.h"

#include <QDialog>
#include <QPixmap>
#include <QVector>
#include <QRectF>
#include <QHash>

class QComboBox;
class QGraphicsScene;
class QLabel;
class QLineEdit;
class ThemePreviewView;

struct RoleRegion
{
    QString id;
    QString label;
    QRectF rect; // normalized 0..1
};

using RegionMap = QHash<int, QVector<RoleRegion>>;

class ThemeEditorDialog : public QDialog
{
    Q_OBJECT

public:
    enum Target {
        WindowTarget,
        TreeTarget,
        ViewportTarget
    };

    explicit ThemeEditorDialog(const QPixmap &screenshot,
                                const ThemeSpec &windowTheme,
                                const TreeAppearanceTheme &treeTheme,
                                const ViewportAppearanceTheme &viewportTheme,
                                QWidget *parent = nullptr);

    Target target() const;
    QString themeName() const;
    ThemeSpec windowTheme() const { return m_windowTheme; }
    TreeAppearanceTheme treeTheme() const { return m_treeTheme; }
    ViewportAppearanceTheme viewportTheme() const { return m_viewportTheme; }
    void chooseColorForRole(const QString &role);
    void updateHint(const QString &role);

private:
    void buildScene();
    void buildTreePreview(QGraphicsScene *scene);

    ThemeSpec m_windowTheme;
    TreeAppearanceTheme m_treeTheme;
    ViewportAppearanceTheme m_viewportTheme;
    QPixmap m_screenshot;
    RegionMap m_regionMap;
    TreeAppearanceTheme m_savedCustomTheme;
    bool m_hadCustomTheme = false;
    QComboBox *m_targetCombo = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QLabel *m_hintLabel = nullptr;
    ThemePreviewView *m_preview = nullptr;
};

#endif // THEMEEDITORDIALOG_H
