#include "themeeditordialog.h"
#include "scenetreepalette.h"
#include "scenetreenoderenderer.h"
#include "shapenode.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLinearGradient>
#include <QPushButton>
#include <QResizeEvent>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

namespace {

QString roleLabel(const QString &role)
{
    const QHash<QString, QString> labels = {
        {QStringLiteral("window"), QStringLiteral("Window background")},
        {QStringLiteral("surface"), QStringLiteral("Input / editor surface")},
        {QStringLiteral("panel"), QStringLiteral("Panel and dock")},
        {QStringLiteral("panelAlt"), QStringLiteral("Hovered / toolbar")},
        {QStringLiteral("titleBase"), QStringLiteral("Title bar")},
        {QStringLiteral("titlePulse"), QStringLiteral("Title pulse")},
        {QStringLiteral("text"), QStringLiteral("Primary text")},
        {QStringLiteral("mutedText"), QStringLiteral("Secondary text")},
        {QStringLiteral("border"), QStringLiteral("Borders / separators")},
        {QStringLiteral("accent"), QStringLiteral("Selection accent")},
        {QStringLiteral("accentHover"), QStringLiteral("Accent hover")},
        {QStringLiteral("danger"), QStringLiteral("Danger / close")},
        {QStringLiteral("canvas"), QStringLiteral("Tree canvas")},
        {QStringLiteral("minorGrid"), QStringLiteral("Tree minor grid")},
        {QStringLiteral("majorGrid"), QStringLiteral("Tree major grid")},
        {QStringLiteral("card"), QStringLiteral("Tree card body")},
        {QStringLiteral("header"), QStringLiteral("Tree card header")},
        {QStringLiteral("input"), QStringLiteral("Tree input field")},
        {QStringLiteral("glassTop"), QStringLiteral("Glass top tint")},
        {QStringLiteral("glassBottom"), QStringLiteral("Glass bottom tint")},
        {QStringLiteral("glassBorder"), QStringLiteral("Glass border")},
        {QStringLiteral("numBorder"), QStringLiteral("Number pill border")},
        {QStringLiteral("numFill"), QStringLiteral("Number pill fill")},
        {QStringLiteral("numBorderActive"), QStringLiteral("Pill active border")},
        {QStringLiteral("numFillActive"), QStringLiteral("Pill active fill")},
        {QStringLiteral("numText"), QStringLiteral("Number text")},
        {QStringLiteral("numLabelText"), QStringLiteral("Number label text")},
        {QStringLiteral("background"), QStringLiteral("Viewport background")},
        {QStringLiteral("grid"), QStringLiteral("Viewport grid")},
        {QStringLiteral("solid"), QStringLiteral("Viewport object")},
        {QStringLiteral("computedSolid"), QStringLiteral("Computed object")},
    };
    return labels.value(role, role);
}

// ── Region definitions ────────────────────────────────────────────────────────

RegionMap buildRegionMap()
{
    RegionMap map;

    map[ThemeEditorDialog::WindowTarget] = {
        { QStringLiteral("titleBase"),    QStringLiteral("Title bar"),
          QRectF(0.00, 0.00, 1.00, 0.05) },
        { QStringLiteral("text"),         QStringLiteral("Title text"),
          QRectF(0.02, 0.00, 0.50, 0.05) },
        { QStringLiteral("danger"),       QStringLiteral("Close button"),
          QRectF(0.94, 0.00, 0.05, 0.05) },
        { QStringLiteral("surface"),      QStringLiteral("Menu bar"),
          QRectF(0.00, 0.05, 1.00, 0.03) },
        { QStringLiteral("panel"),        QStringLiteral("Left dock"),
          QRectF(0.00, 0.08, 0.22, 0.92) },
        { QStringLiteral("panel"),        QStringLiteral("Right dock"),
          QRectF(0.78, 0.08, 0.22, 0.92) },
        { QStringLiteral("window"),       QStringLiteral("Viewport area"),
          QRectF(0.22, 0.08, 0.56, 0.92) },
        { QStringLiteral("panelAlt"),     QStringLiteral("Toolbar bg"),
          QRectF(0.24, 0.12, 0.20, 0.04) },
        { QStringLiteral("accent"),       QStringLiteral("Accent swatch"),
          QRectF(0.40, 0.12, 0.08, 0.04) },
        { QStringLiteral("border"),       QStringLiteral("Dock borders"),
          QRectF(0.22, 0.08, 0.00, 0.92) },
        { QStringLiteral("panelAlt"),     QStringLiteral("Apply button"),
          QRectF(0.80, 0.85, 0.15, 0.06) },
        { QStringLiteral("danger"),       QStringLiteral("Danger accent"),
          QRectF(0.90, 0.85, 0.05, 0.06) },
        { QStringLiteral("mutedText"),    QStringLiteral("Secondary labels"),
          QRectF(0.02, 0.50, 0.18, 0.04) },
    };

    map[ThemeEditorDialog::TreeTarget] = {
        { QStringLiteral("canvas"),       QStringLiteral("Tree canvas"),
          QRectF(0.00, 0.08, 0.22, 0.92) },
        { QStringLiteral("minorGrid"),    QStringLiteral("Minor grid"),
          QRectF(0.01, 0.10, 0.20, 0.15) },
        { QStringLiteral("majorGrid"),    QStringLiteral("Major grid"),
          QRectF(0.01, 0.26, 0.20, 0.03) },
        { QStringLiteral("card"),         QStringLiteral("Card body"),
          QRectF(0.02, 0.30, 0.18, 0.25) },
        { QStringLiteral("header"),       QStringLiteral("Card header"),
          QRectF(0.02, 0.30, 0.18, 0.05) },
        { QStringLiteral("text"),         QStringLiteral("Node label"),
          QRectF(0.04, 0.31, 0.14, 0.04) },
        { QStringLiteral("input"),        QStringLiteral("Numeric input"),
          QRectF(0.05, 0.42, 0.12, 0.05) },
        { QStringLiteral("accent"),       QStringLiteral("Selection tint"),
          QRectF(0.05, 0.48, 0.12, 0.04) },
        { QStringLiteral("glassTop"),     QStringLiteral("Glass overlay"),
          QRectF(0.02, 0.80, 0.18, 0.08) },
        { QStringLiteral("mutedText"),    QStringLiteral("Secondary text"),
          QRectF(0.04, 0.55, 0.14, 0.03) },
        { QStringLiteral("numFill"),      QStringLiteral("Pill background"),
          QRectF(0.01, 0.60, 0.06, 0.04) },
        { QStringLiteral("numBorder"),    QStringLiteral("Pill border"),
          QRectF(0.01, 0.60, 0.06, 0.04) },
        { QStringLiteral("numText"),      QStringLiteral("Pill number text"),
          QRectF(0.02, 0.61, 0.04, 0.02) },
        { QStringLiteral("numLabelText"), QStringLiteral("Axis / operator text"),
          QRectF(0.08, 0.61, 0.10, 0.02) },
        { QStringLiteral("numFillActive"),QStringLiteral("Active pill fill"),
          QRectF(0.01, 0.66, 0.06, 0.04) },
        { QStringLiteral("numBorderActive"),QStringLiteral("Active pill border"),
          QRectF(0.01, 0.66, 0.06, 0.04) },
    };

    map[ThemeEditorDialog::ViewportTarget] = {
        { QStringLiteral("background"),   QStringLiteral("Viewport bg"),
          QRectF(0.22, 0.08, 0.56, 0.92) },
        { QStringLiteral("grid"),         QStringLiteral("Grid lines"),
          QRectF(0.22, 0.08, 0.56, 0.92) },
        { QStringLiteral("solid"),        QStringLiteral("Solid object"),
          QRectF(0.38, 0.25, 0.24, 0.25) },
        { QStringLiteral("computedSolid"),QStringLiteral("Computed overlay"),
          QRectF(0.42, 0.30, 0.16, 0.15) },
        { QStringLiteral("text"),         QStringLiteral("Viewport text"),
          QRectF(0.25, 0.12, 0.20, 0.04) },
        { QStringLiteral("mutedText"),    QStringLiteral("Muted labels"),
          QRectF(0.25, 0.17, 0.15, 0.03) },
        { QStringLiteral("accent"),       QStringLiteral("Viewport accent"),
          QRectF(0.44, 0.55, 0.12, 0.04) },
        { QStringLiteral("glassTop"),     QStringLiteral("Glass tooltip"),
          QRectF(0.66, 0.65, 0.18, 0.08) },
    };

    return map;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════════
//  InvisibleZoneItem — clickable zone with NO visible paint, only hover hint
// ═══════════════════════════════════════════════════════════════════════════════

class InvisibleZoneItem : public QGraphicsItem
{
public:
    InvisibleZoneItem(const QString &id, const QString &label, const QRectF &rect,
                      ThemeEditorDialog *dialog)
        : m_id(id), m_label(label), m_rect(rect), m_dialog(dialog)
    {
        setAcceptHoverEvents(true);
        setCursor(Qt::PointingHandCursor);
    }

    QRectF boundingRect() const override { return m_rect; }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        if (!m_hovered)
            return;
        // Subtle highlight on hover
        painter->setPen(QPen(QColor(255, 255, 255, 80), 1));
        painter->drawRect(m_rect);
    }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *) override
    {
        m_hovered = true;
        update();
        if (m_dialog) m_dialog->updateHint(m_id);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent *) override
    {
        m_hovered = false;
        update();
        if (m_dialog) m_dialog->updateHint(QString());
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_dialog)
            m_dialog->chooseColorForRole(m_id);
    }

private:
    QString m_id;
    QString m_label;
    QRectF m_rect;
    ThemeEditorDialog *m_dialog = nullptr;
    bool m_hovered = false;
};

// ═══════════════════════════════════════════════════════════════════════════════
//  RegionOverlayItem — visible coloured overlay for screenshot targets
// ═══════════════════════════════════════════════════════════════════════════════

class RegionOverlayItem : public QGraphicsItem
{
public:
    RegionOverlayItem(const QString &id, const QString &label, const QRectF &rect,
                      ThemeEditorDialog::Target target,
                      ThemeEditorDialog *dialog)
        : m_id(id), m_label(label), m_rect(rect)
        , m_target(target), m_dialog(dialog)
    {
        setAcceptHoverEvents(true);
        setCursor(Qt::PointingHandCursor);
    }

    QRectF boundingRect() const override { return m_rect; }

    void paint(QPainter *painter, const QStyleOptionGraphicsItem *, QWidget *) override
    {
        const QColor roleColor = resolveColor();
        const bool hovered = m_hovered;

        QColor fill = roleColor;
        fill.setAlpha(hovered ? 170 : 110);
        painter->fillRect(m_rect, fill);

        painter->setPen(QPen(hovered ? Qt::white : roleColor.lighter(160), hovered ? 2 : 1));
        painter->drawRect(m_rect);

        QRectF swatch(m_rect.right() - 14, m_rect.top() + 2, 10, 10);
        painter->setPen(Qt::NoPen);
        painter->fillRect(swatch, roleColor);
        painter->setPen(QPen(roleColor.lightness() > 128 ? Qt::black : Qt::white, 1));
        painter->drawRect(swatch);

        QFont f = painter->font();
        f.setPointSize(hovered ? 10 : 8);
        f.setBold(hovered);
        painter->setFont(f);
        painter->setPen(hovered ? Qt::white : QColor(240, 240, 255));
        painter->drawText(m_rect.adjusted(4, 2, -18, -2),
                          Qt::AlignLeft | Qt::AlignTop, m_label);
    }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *) override
    {
        m_hovered = true;
        update();
        if (m_dialog) m_dialog->updateHint(m_id);
    }

    void hoverLeaveEvent(QGraphicsSceneHoverEvent *) override
    {
        m_hovered = false;
        update();
        if (m_dialog) m_dialog->updateHint(QString());
    }

    void mousePressEvent(QGraphicsSceneMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_dialog)
            m_dialog->chooseColorForRole(m_id);
    }

private:
    QColor resolveColor() const
    {
        if (m_target == ThemeEditorDialog::WindowTarget) {
            if (m_id == QLatin1String("window"))       return m_dialog->windowTheme().window;
            if (m_id == QLatin1String("surface"))      return m_dialog->windowTheme().surface;
            if (m_id == QLatin1String("panel"))        return m_dialog->windowTheme().panel;
            if (m_id == QLatin1String("panelAlt"))     return m_dialog->windowTheme().panelAlt;
            if (m_id == QLatin1String("titleBase"))    return m_dialog->windowTheme().titleBase;
            if (m_id == QLatin1String("titlePulse"))   return m_dialog->windowTheme().titlePulse;
            if (m_id == QLatin1String("text"))         return m_dialog->windowTheme().text;
            if (m_id == QLatin1String("mutedText"))    return m_dialog->windowTheme().mutedText;
            if (m_id == QLatin1String("border"))       return m_dialog->windowTheme().border;
            if (m_id == QLatin1String("accent"))       return m_dialog->windowTheme().accent;
            if (m_id == QLatin1String("accentHover"))  return m_dialog->windowTheme().accentHover;
            if (m_id == QLatin1String("danger"))       return m_dialog->windowTheme().danger;
        }
        if (m_target == ThemeEditorDialog::TreeTarget) {
            if (m_id == QLatin1String("canvas"))       return m_dialog->treeTheme().canvas;
            if (m_id == QLatin1String("minorGrid"))    return m_dialog->treeTheme().minorGrid;
            if (m_id == QLatin1String("majorGrid"))    return m_dialog->treeTheme().majorGrid;
            if (m_id == QLatin1String("card"))         return m_dialog->treeTheme().card;
            if (m_id == QLatin1String("header"))       return m_dialog->treeTheme().header;
            if (m_id == QLatin1String("input"))        return m_dialog->treeTheme().input;
            if (m_id == QLatin1String("text"))         return m_dialog->treeTheme().text;
            if (m_id == QLatin1String("mutedText"))    return m_dialog->treeTheme().mutedText;
            if (m_id == QLatin1String("glassTop"))     return m_dialog->treeTheme().glassTop;
            if (m_id == QLatin1String("glassBottom"))  return m_dialog->treeTheme().glassBottom;
            if (m_id == QLatin1String("glassBorder"))  return m_dialog->treeTheme().glassBorder;
            if (m_id == QLatin1String("accent"))       return m_dialog->treeTheme().accent;
            if (m_id == QLatin1String("numBorder"))    return m_dialog->treeTheme().numBorder;
            if (m_id == QLatin1String("numFill"))      return m_dialog->treeTheme().numFill;
            if (m_id == QLatin1String("numBorderActive")) return m_dialog->treeTheme().numBorderActive;
            if (m_id == QLatin1String("numFillActive"))return m_dialog->treeTheme().numFillActive;
            if (m_id == QLatin1String("numText"))      return m_dialog->treeTheme().numText;
            if (m_id == QLatin1String("numLabelText")) return m_dialog->treeTheme().numLabelText;
        }
        if (m_target == ThemeEditorDialog::ViewportTarget) {
            if (m_id == QLatin1String("background"))   return m_dialog->viewportTheme().background;
            if (m_id == QLatin1String("grid"))         return m_dialog->viewportTheme().grid;
            if (m_id == QLatin1String("solid"))        return m_dialog->viewportTheme().solid;
            if (m_id == QLatin1String("computedSolid"))return m_dialog->viewportTheme().computedSolid;
            if (m_id == QLatin1String("text"))         return m_dialog->viewportTheme().text;
            if (m_id == QLatin1String("mutedText"))    return m_dialog->viewportTheme().mutedText;
            if (m_id == QLatin1String("glassTop"))     return m_dialog->viewportTheme().glassTop;
            if (m_id == QLatin1String("glassBottom"))  return m_dialog->viewportTheme().glassBottom;
            if (m_id == QLatin1String("glassBorder"))  return m_dialog->viewportTheme().glassBorder;
            if (m_id == QLatin1String("accent"))       return m_dialog->viewportTheme().accent;
        }
        return QColor(128, 128, 128);
    }

    QString m_id;
    QString m_label;
    QRectF m_rect;
    ThemeEditorDialog::Target m_target;
    ThemeEditorDialog *m_dialog = nullptr;
    bool m_hovered = false;
};

// ═══════════════════════════════════════════════════════════════════════════════
//  ThemePreviewView — zoomable QGraphicsView
// ═══════════════════════════════════════════════════════════════════════════════

class ThemePreviewView : public QGraphicsView
{
public:
    explicit ThemePreviewView(QWidget *parent = nullptr)
        : QGraphicsView(parent)
    {
        setScene(new QGraphicsScene(this));
        setRenderHint(QPainter::Antialiasing, true);
        setRenderHint(QPainter::SmoothPixmapTransform, true);
        setDragMode(QGraphicsView::ScrollHandDrag);
        setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
        setResizeAnchor(QGraphicsView::AnchorUnderMouse);
        setViewportUpdateMode(QGraphicsView::SmartViewportUpdate);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setFrameShape(QFrame::NoFrame);
        setMinimumSize(480, 320);
    }

    void zoomIn()
    {
        constexpr double f = 1.25;
        scale(f, f);
    }

    void zoomOut()
    {
        constexpr double f = 1.0 / 1.25;
        scale(f, f);
    }

    void fitContent()
    {
        const QRectF br = scene()->itemsBoundingRect();
        if (!br.isEmpty()) {
            resetTransform();
            fitInView(br, Qt::KeepAspectRatio);
        }
    }

    void resetZoom()
    {
        resetTransform();
        fitContent();
    }

protected:
    void wheelEvent(QWheelEvent *event) override
    {
        if (event->modifiers() & Qt::ControlModifier) {
            const double factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
            scale(factor, factor);
            event->accept();
        } else {
            QGraphicsView::wheelEvent(event);
        }
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QGraphicsView::resizeEvent(event);
        // auto-fit on first show
        if (!m_fitted) {
            m_fitted = true;
            fitContent();
        }
    }

private:
    bool m_fitted = false;
};

// ═══════════════════════════════════════════════════════════════════════════════
//  ThemeEditorDialog
// ═══════════════════════════════════════════════════════════════════════════════

ThemeEditorDialog::ThemeEditorDialog(const QPixmap &screenshot,
                                      const ThemeSpec &windowTheme,
                                      const TreeAppearanceTheme &treeTheme,
                                      const ViewportAppearanceTheme &viewportTheme,
                                      QWidget *parent)
    : QDialog(parent)
    , m_windowTheme(windowTheme)
    , m_treeTheme(treeTheme)
    , m_viewportTheme(viewportTheme)
    , m_screenshot(screenshot)
    , m_regionMap(buildRegionMap())
{
    setWindowTitle(QStringLiteral("Theme Editor"));
    resize(820, 620);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 8);
    layout->setSpacing(8);

    // ── Top form ───────────────────────────────────────────────────────────
    auto *form = new QFormLayout;
    m_targetCombo = new QComboBox(this);
    m_targetCombo->addItems({
        QStringLiteral("Window (title, panels, text)"),
        QStringLiteral("Scene Tree (cards, canvas, grid)"),
        QStringLiteral("Viewport (background, objects, grid)")
    });
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(QStringLiteral("Theme name to save as"));
    form->addRow(QStringLiteral("Editing:"), m_targetCombo);
    form->addRow(QStringLiteral("Save as:"), m_nameEdit);
    layout->addLayout(form);

    // ── Zoom toolbar ───────────────────────────────────────────────────────
    auto *zoomBar = new QHBoxLayout;
    zoomBar->setSpacing(4);
    auto *zoomOutBtn = new QToolButton(this);
    zoomOutBtn->setText(QStringLiteral("−"));
    zoomOutBtn->setToolTip(QStringLiteral("Zoom out"));
    auto *zoomInBtn = new QToolButton(this);
    zoomInBtn->setText(QStringLiteral("+"));
    zoomInBtn->setToolTip(QStringLiteral("Zoom in"));
    auto *fitBtn = new QToolButton(this);
    fitBtn->setText(QStringLiteral("Fit"));
    fitBtn->setToolTip(QStringLiteral("Fit to window"));
    zoomBar->addWidget(zoomOutBtn);
    zoomBar->addWidget(zoomInBtn);
    zoomBar->addWidget(fitBtn);
    zoomBar->addStretch();
    layout->addLayout(zoomBar);

    // ── Preview (QGraphicsView) ────────────────────────────────────────────
    m_preview = new ThemePreviewView(this);
    layout->addWidget(m_preview, 1);

    connect(zoomOutBtn, &QToolButton::clicked, this, [this]() { m_preview->zoomOut(); });
    connect(zoomInBtn,  &QToolButton::clicked, this, [this]() { m_preview->zoomIn(); });
    connect(fitBtn,     &QToolButton::clicked, this, [this]() { m_preview->fitContent(); });

    // ── Hint label ─────────────────────────────────────────────────────────
    m_hintLabel = new QLabel(QStringLiteral(
        "Hover a highlighted area on the preview, then click to pick a new colour."),
        this);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setStyleSheet(QStringLiteral(
        "QLabel { background: rgba(14, 21, 31, 205); color: #e8f2ff;"
        "border: 1px solid rgba(142, 178, 215, 120); border-radius: 8px; padding: 9px 12px; }"));
    layout->addWidget(m_hintLabel);

    // ── Buttons ────────────────────────────────────────────────────────────
    auto *buttons = new QDialogButtonBox(this);
    QPushButton *saveButton = buttons->addButton(
        QStringLiteral("Save && Apply"), QDialogButtonBox::AcceptRole);
    buttons->addButton(QDialogButtonBox::Cancel);

    connect(saveButton, &QPushButton::clicked, this, [this]() {
        if (m_nameEdit->text().trimmed().isEmpty()) {
            m_nameEdit->setFocus();
            return;
        }
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    // ── Save and override global custom theme for preview ─────────────────
    m_hadCustomTheme = SceneTreePalette::hasCustomTheme();
    m_savedCustomTheme = SceneTreePalette::customTheme();
    SceneTreePalette::setCustomTheme(m_treeTheme);

    // ── Connections ────────────────────────────────────────────────────────
    connect(m_targetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { buildScene(); });

    connect(this, &QDialog::finished, this, [this]() {
        if (m_hadCustomTheme)
            SceneTreePalette::setCustomTheme(m_savedCustomTheme);
        else
            SceneTreePalette::clearCustomTheme();
    });

    buildScene();
}

ThemeEditorDialog::Target ThemeEditorDialog::target() const
{
    return static_cast<Target>(m_targetCombo->currentIndex());
}

QString ThemeEditorDialog::themeName() const
{
    return m_nameEdit->text().trimmed();
}

void ThemeEditorDialog::buildScene()
{
    auto *scene = m_preview->scene();
    scene->clear();

    const Target t = target();

    if (t == TreeTarget) {
        buildTreePreview(scene);
        return;
    }

    // Window / Viewport — screenshot approach
    const QVector<RoleRegion> regions = m_regionMap.value(t);
    const qreal w = m_screenshot.width();
    const qreal h = m_screenshot.height();
    if (w <= 0 || h <= 0)
        return;

    auto *bg = scene->addPixmap(m_screenshot);
    bg->setPos(0, 0);
    auto *dim = scene->addRect(0, 0, w, h, Qt::NoPen, QColor(0, 0, 0, 70));
    dim->setZValue(1);

    for (const auto &r : regions) {
        const QRectF sceneRect(r.rect.x() * w, r.rect.y() * h,
                               r.rect.width() * w, r.rect.height() * h);
        auto *item = new RegionOverlayItem(r.id, r.label, sceneRect, t, this);
        item->setZValue(2);
        scene->addItem(item);
    }

    if (regions.isEmpty()) {
        auto *text = scene->addText(QStringLiteral("No themed regions for this target."));
        text->setPos(w * 0.2, h * 0.45);
        text->setDefaultTextColor(QColor(200, 210, 230, 180));
        text->setZValue(2);
    }

    scene->setSceneRect(0, 0, w, h);
    m_preview->fitContent();
    updateHint(QString());
}

void ThemeEditorDialog::buildTreePreview(QGraphicsScene *scene)
{
    // ── Override global custom theme so rendered items use our preview colours ──
    SceneTreePalette::setCustomTheme(m_treeTheme);

    constexpr qreal W = 400;
    constexpr qreal H = 340;
    scene->setSceneRect(0, 0, W, H);

    // ── Canvas background + grid ─────────────────────────────────────────────
    auto *bg = scene->addRect(0, 0, W, H, Qt::NoPen, m_treeTheme.canvas);
    bg->setZValue(-2);

    for (int x = 0; x <= W; x += 20)
        scene->addLine(x, 0, x, H, QPen(m_treeTheme.minorGrid, 1))->setZValue(-1);
    for (int y = 0; y <= H; y += 20)
        scene->addLine(0, y, W, y, QPen(m_treeTheme.minorGrid, 1))->setZValue(-1);
    for (int x = 0; x <= W; x += 100)
        scene->addLine(x, 0, x, H, QPen(m_treeTheme.majorGrid, 1.5))->setZValue(-1);
    for (int y = 0; y <= H; y += 100)
        scene->addLine(0, y, W, y, QPen(m_treeTheme.majorGrid, 1.5))->setZValue(-1);

    // Helper to add an invisible clickable zone
    auto zone = [&](const QString &id, const QString &label, const QRectF &r) {
        auto *z = new InvisibleZoneItem(id, label, r, this);
        z->setZValue(20);
        scene->addItem(z);
    };

    // ── 1. For-loop group (horizontal header) ────────────────────────────────
    {
        SceneDocument::TreeNode node;
        node.id       = 1;
        node.type     = SceneDocument::TreeNode::Group;
        node.operation = SceneDocument::TreeNode::For;
        node.loopVariable        = QStringLiteral("i");
        node.loopRangeExpression = QStringLiteral("[0 : 1 : 10]");

        constexpr qreal y0 = 8;
        const QRectF r(10, y0, 360, 28);
        SceneTreeNodeRenderer(scene, 0, nullptr).renderGroup(node, r, 0, 0);

        zone(QStringLiteral("header"), QStringLiteral("Card header"), r);
        // range pills at approximate positions [0, 1, 10]
        zone(QStringLiteral("numFill"),   QStringLiteral("Pill fill"),   QRectF(10 + 78, y0 + 1, 28, 22));
        zone(QStringLiteral("numBorder"), QStringLiteral("Pill border"), QRectF(10 + 78, y0 + 1, 28, 22));
        zone(QStringLiteral("numText"),   QStringLiteral("Pill text"),    QRectF(10 + 78, y0 + 1, 28, 22));
        zone(QStringLiteral("numLabelText"), QStringLiteral("separator"), QRectF(10 + 108, y0 + 2, 10, 18));
        zone(QStringLiteral("numFill"),   QStringLiteral("Pill fill"),   QRectF(10 + 116, y0 + 1, 22, 22));
        zone(QStringLiteral("numBorder"), QStringLiteral("Pill border"), QRectF(10 + 116, y0 + 1, 22, 22));
        zone(QStringLiteral("numText"),   QStringLiteral("Pill text"),    QRectF(10 + 116, y0 + 1, 22, 22));
    }

    // ── 2. Translate group (vertical header) ────────────────────────────────
    {
        SceneDocument::TreeNode node;
        node.id       = 2;
        node.type     = SceneDocument::TreeNode::Group;
        node.operation = SceneDocument::TreeNode::Translate;
        node.position = QVector3D(24, 10, 5);
        node.transformExpressions = QStringList()
            << QStringLiteral("24")
            << QStringLiteral("10")
            << QStringLiteral("5");

        constexpr qreal y0 = 42;
        const QRectF r(10, y0, 360, 80);
        SceneTreeNodeRenderer(scene, 0, nullptr).renderGroup(node, r, 0, 0);

        zone(QStringLiteral("header"), QStringLiteral("Header stripe"), QRectF(10, y0, 36, 80));
        zone(QStringLiteral("card"),   QStringLiteral("Card body"),    QRectF(10 + 36, y0, 360 - 36, 80));

        // X row (rowTop = y0 + 8, h=13)
        zone(QStringLiteral("numLabelText"), QStringLiteral("Axis X"), QRectF(10 + 40, y0 + 8, 20, 13));
        zone(QStringLiteral("numFill"),   QStringLiteral("Pill fill"),   QRectF(10 + 62, y0 + 8, 32, 13));
        zone(QStringLiteral("numBorder"), QStringLiteral("Pill border"), QRectF(10 + 62, y0 + 8, 32, 13));
        zone(QStringLiteral("numText"),   QStringLiteral("Pill text"),    QRectF(10 + 62, y0 + 8, 32, 13));

        // Y row (rowTop = y0 + 23)
        zone(QStringLiteral("numLabelText"), QStringLiteral("Axis Y"), QRectF(10 + 40, y0 + 23, 20, 13));
        zone(QStringLiteral("numFill"),   QStringLiteral("Pill fill"),   QRectF(10 + 62, y0 + 23, 32, 13));
        zone(QStringLiteral("numBorder"), QStringLiteral("Pill border"), QRectF(10 + 62, y0 + 23, 32, 13));
        zone(QStringLiteral("numText"),   QStringLiteral("Pill text"),    QRectF(10 + 62, y0 + 23, 32, 13));

        // Z row (rowTop = y0 + 38)
        zone(QStringLiteral("numLabelText"), QStringLiteral("Axis Z"), QRectF(10 + 40, y0 + 38, 20, 13));
        zone(QStringLiteral("numFill"),   QStringLiteral("Pill fill"),   QRectF(10 + 62, y0 + 38, 32, 13));
        zone(QStringLiteral("numBorder"), QStringLiteral("Pill border"), QRectF(10 + 62, y0 + 38, 32, 13));
        zone(QStringLiteral("numText"),   QStringLiteral("Pill text"),    QRectF(10 + 62, y0 + 38, 32, 13));
    }

    // ── 3. Sphere primitive ─────────────────────────────────────────────────
    {
        ShapeNode shape;
        shape.id   = 3;
        shape.type = ShapeNode::Sphere;
        shape.name = QStringLiteral("sphere");
        shape.radius               = 15.0f;
        shape.parameterExpressions = QStringList() << QStringLiteral("15");

        SceneDocument::TreeNode node;
        node.id       = 3;
        node.type     = SceneDocument::TreeNode::Primitive;
        node.shapeId  = 3;
        node.variableExpression = QStringLiteral("15");

        constexpr qreal y0 = 130;
        const QRectF r(20, y0, 200, 38);
        SceneTreeNodeRenderer(scene, 0, nullptr)
            .renderPrimitive(node, r, QStringLiteral("sphere"), &shape);

        zone(QStringLiteral("card"),   QStringLiteral("Card body"), QRectF(20, y0, 54, 38));
        zone(QStringLiteral("text"),   QStringLiteral("Label"),     QRectF(20 + 56, y0, 30, 38));
        zone(QStringLiteral("numFill"),   QStringLiteral("Pill fill"),   QRectF(20 + 90, y0 + 1, 30, 14));
        zone(QStringLiteral("numBorder"), QStringLiteral("Pill border"), QRectF(20 + 90, y0 + 1, 30, 14));
        zone(QStringLiteral("numText"),   QStringLiteral("Pill text"),    QRectF(20 + 90, y0 + 1, 30, 14));
    }

    // ── 4. Variable card: r = 24 ─────────────────────────────────────────────
    {
        SceneDocument::TreeNode node;
        node.id               = 4;
        node.type             = SceneDocument::TreeNode::Variable;
        node.variableName     = QStringLiteral("r");
        node.variableExpression = QStringLiteral("24");
        node.isParameter      = false;

        constexpr qreal y0 = 176;
        const QRectF r(20, y0, 360, 26);
        SceneTreeNodeRenderer(scene, 0, nullptr).renderVariable(node, r);

        zone(QStringLiteral("card"),       QStringLiteral("Card body"),    r);
        zone(QStringLiteral("text"),       QStringLiteral("Name"),         QRectF(20 + 38, y0, 20, 26));
        zone(QStringLiteral("numFill"),    QStringLiteral("Pill fill"),    QRectF(20 + 60, y0 + 1, 28, 16));
        zone(QStringLiteral("numBorder"),  QStringLiteral("Pill border"),  QRectF(20 + 60, y0 + 1, 28, 16));
        zone(QStringLiteral("numText"),    QStringLiteral("Pill text"),     QRectF(20 + 60, y0 + 1, 28, 16));
        zone(QStringLiteral("mutedText"),  QStringLiteral("Comment"),      QRectF(20 + 100, y0, 80, 26));
    }

    // ── 5. Variable card: $fn = 64 ───────────────────────────────────────────
    {
        SceneDocument::TreeNode node;
        node.id               = 5;
        node.type             = SceneDocument::TreeNode::Variable;
        node.variableName     = QStringLiteral("$fn");
        node.variableExpression = QStringLiteral("64");
        node.isParameter      = true;

        constexpr qreal y0 = 208;
        const QRectF r(20, y0, 360, 26);
        SceneTreeNodeRenderer(scene, 0, nullptr).renderVariable(node, r);

        zone(QStringLiteral("card"),       QStringLiteral("Card body"),    r);
        zone(QStringLiteral("text"),       QStringLiteral("Name"),         QRectF(20 + 38, y0, 30, 26));
        zone(QStringLiteral("numFill"),    QStringLiteral("Pill fill"),    QRectF(20 + 70, y0 + 1, 28, 16));
        zone(QStringLiteral("numBorder"),  QStringLiteral("Pill border"),  QRectF(20 + 70, y0 + 1, 28, 16));
        zone(QStringLiteral("numText"),    QStringLiteral("Pill text"),     QRectF(20 + 70, y0 + 1, 28, 16));
        zone(QStringLiteral("mutedText"),  QStringLiteral("Comment"),      QRectF(20 + 115, y0, 64, 26));
    }

    // ── 6. Glass overlay ────────────────────────────────────────────────────
    {
        constexpr qreal y0 = 248;
        constexpr qreal gh = 64;
        QLinearGradient grad(0, y0, 0, y0 + gh);
        grad.setColorAt(0, m_treeTheme.glassTop);
        grad.setColorAt(1, m_treeTheme.glassBottom);
        scene->addRect(10, y0, 360, gh,
                       QPen(m_treeTheme.glassBorder, 1.5), QBrush(grad))->setZValue(10);

        const QRectF gr(10, y0, 360, gh);
        zone(QStringLiteral("glassTop"),    QStringLiteral("Glass top"),    gr);
        zone(QStringLiteral("glassBottom"), QStringLiteral("Glass bottom"), gr);
        zone(QStringLiteral("glassBorder"), QStringLiteral("Glass border"), gr);
    }

    m_preview->fitContent();
    updateHint(QString());
}

void ThemeEditorDialog::updateHint(const QString &role)
{
    if (role.isEmpty()) {
        m_hintLabel->setText(QStringLiteral(
            "Hover a highlighted area on the preview, then click to pick a new colour."));
    } else {
        // resolve the current colour for this role
        QColor c(128, 128, 128);
        const Target t = target();
        if (t == WindowTarget) {
            if (role == QLatin1String("window"))       c = m_windowTheme.window;
            else if (role == QLatin1String("surface")) c = m_windowTheme.surface;
            else if (role == QLatin1String("panel"))   c = m_windowTheme.panel;
            else if (role == QLatin1String("panelAlt"))c = m_windowTheme.panelAlt;
            else if (role == QLatin1String("titleBase"))c = m_windowTheme.titleBase;
            else if (role == QLatin1String("titlePulse"))c = m_windowTheme.titlePulse;
            else if (role == QLatin1String("text"))    c = m_windowTheme.text;
            else if (role == QLatin1String("mutedText"))c = m_windowTheme.mutedText;
            else if (role == QLatin1String("border"))  c = m_windowTheme.border;
            else if (role == QLatin1String("accent"))  c = m_windowTheme.accent;
            else if (role == QLatin1String("accentHover"))c = m_windowTheme.accentHover;
            else if (role == QLatin1String("danger"))  c = m_windowTheme.danger;
        } else if (t == TreeTarget) {
            if (role == QLatin1String("canvas"))       c = m_treeTheme.canvas;
            else if (role == QLatin1String("minorGrid"))c = m_treeTheme.minorGrid;
            else if (role == QLatin1String("majorGrid"))c = m_treeTheme.majorGrid;
            else if (role == QLatin1String("card"))    c = m_treeTheme.card;
            else if (role == QLatin1String("header"))  c = m_treeTheme.header;
            else if (role == QLatin1String("input"))   c = m_treeTheme.input;
            else if (role == QLatin1String("text"))    c = m_treeTheme.text;
            else if (role == QLatin1String("mutedText"))c = m_treeTheme.mutedText;
            else if (role == QLatin1String("glassTop"))c = m_treeTheme.glassTop;
            else if (role == QLatin1String("glassBottom"))c = m_treeTheme.glassBottom;
            else if (role == QLatin1String("glassBorder"))c = m_treeTheme.glassBorder;
            else if (role == QLatin1String("accent"))  c = m_treeTheme.accent;
            else if (role == QLatin1String("numBorder"))c = m_treeTheme.numBorder;
            else if (role == QLatin1String("numFill")) c = m_treeTheme.numFill;
            else if (role == QLatin1String("numBorderActive"))c = m_treeTheme.numBorderActive;
            else if (role == QLatin1String("numFillActive"))c = m_treeTheme.numFillActive;
            else if (role == QLatin1String("numText")) c = m_treeTheme.numText;
            else if (role == QLatin1String("numLabelText"))c = m_treeTheme.numLabelText;
        } else {
            if (role == QLatin1String("background"))   c = m_viewportTheme.background;
            else if (role == QLatin1String("grid"))    c = m_viewportTheme.grid;
            else if (role == QLatin1String("solid"))   c = m_viewportTheme.solid;
            else if (role == QLatin1String("computedSolid"))c = m_viewportTheme.computedSolid;
            else if (role == QLatin1String("text"))    c = m_viewportTheme.text;
            else if (role == QLatin1String("mutedText"))c = m_viewportTheme.mutedText;
            else if (role == QLatin1String("glassTop"))c = m_viewportTheme.glassTop;
            else if (role == QLatin1String("glassBottom"))c = m_viewportTheme.glassBottom;
            else if (role == QLatin1String("glassBorder"))c = m_viewportTheme.glassBorder;
            else if (role == QLatin1String("accent"))  c = m_viewportTheme.accent;
        }
        m_hintLabel->setText(
            QStringLiteral("%1  •  %2  •  click to change")
                .arg(roleLabel(role), c.name(QColor::HexArgb)));
    }
}

void ThemeEditorDialog::chooseColorForRole(const QString &role)
{
    QColor *color = nullptr;
    const Target t = target();

    if (t == WindowTarget) {
        if (role == QLatin1String("window"))        color = &m_windowTheme.window;
        else if (role == QLatin1String("surface"))  color = &m_windowTheme.surface;
        else if (role == QLatin1String("panel"))    color = &m_windowTheme.panel;
        else if (role == QLatin1String("panelAlt")) color = &m_windowTheme.panelAlt;
        else if (role == QLatin1String("titleBase"))color = &m_windowTheme.titleBase;
        else if (role == QLatin1String("titlePulse"))color = &m_windowTheme.titlePulse;
        else if (role == QLatin1String("text"))     color = &m_windowTheme.text;
        else if (role == QLatin1String("mutedText"))color = &m_windowTheme.mutedText;
        else if (role == QLatin1String("border"))   color = &m_windowTheme.border;
        else if (role == QLatin1String("accent"))   color = &m_windowTheme.accent;
        else if (role == QLatin1String("accentHover"))color = &m_windowTheme.accentHover;
        else if (role == QLatin1String("danger"))   color = &m_windowTheme.danger;
    } else if (t == TreeTarget) {
        if (role == QLatin1String("canvas"))        color = &m_treeTheme.canvas;
        else if (role == QLatin1String("minorGrid"))color = &m_treeTheme.minorGrid;
        else if (role == QLatin1String("majorGrid"))color = &m_treeTheme.majorGrid;
        else if (role == QLatin1String("card"))     color = &m_treeTheme.card;
        else if (role == QLatin1String("header"))   color = &m_treeTheme.header;
        else if (role == QLatin1String("input"))    color = &m_treeTheme.input;
        else if (role == QLatin1String("text"))     color = &m_treeTheme.text;
        else if (role == QLatin1String("mutedText"))color = &m_treeTheme.mutedText;
        else if (role == QLatin1String("glassTop")) color = &m_treeTheme.glassTop;
        else if (role == QLatin1String("glassBottom"))color = &m_treeTheme.glassBottom;
        else if (role == QLatin1String("glassBorder"))color = &m_treeTheme.glassBorder;
        else if (role == QLatin1String("accent"))   color = &m_treeTheme.accent;
        else if (role == QLatin1String("numBorder"))color = &m_treeTheme.numBorder;
        else if (role == QLatin1String("numFill"))  color = &m_treeTheme.numFill;
        else if (role == QLatin1String("numBorderActive"))color = &m_treeTheme.numBorderActive;
        else if (role == QLatin1String("numFillActive"))color = &m_treeTheme.numFillActive;
        else if (role == QLatin1String("numText"))  color = &m_treeTheme.numText;
        else if (role == QLatin1String("numLabelText"))color = &m_treeTheme.numLabelText;
    } else {
        if (role == QLatin1String("background"))    color = &m_viewportTheme.background;
        else if (role == QLatin1String("grid"))     color = &m_viewportTheme.grid;
        else if (role == QLatin1String("solid"))    color = &m_viewportTheme.solid;
        else if (role == QLatin1String("computedSolid"))color = &m_viewportTheme.computedSolid;
        else if (role == QLatin1String("text"))     color = &m_viewportTheme.text;
        else if (role == QLatin1String("mutedText"))color = &m_viewportTheme.mutedText;
        else if (role == QLatin1String("glassTop")) color = &m_viewportTheme.glassTop;
        else if (role == QLatin1String("glassBottom"))color = &m_viewportTheme.glassBottom;
        else if (role == QLatin1String("glassBorder"))color = &m_viewportTheme.glassBorder;
        else if (role == QLatin1String("accent"))   color = &m_viewportTheme.accent;
    }

    if (!color)
        return;

    const QColor selected = QColorDialog::getColor(*color, this,
        QStringLiteral("Select %1").arg(roleLabel(role)),
        QColorDialog::ShowAlphaChannel);

    if (!selected.isValid())
        return;

    *color = selected;
    if (target() == TreeTarget) {
        // Push updated theme to the global palette so tree items repaint correctly
        SceneTreePalette::setCustomTheme(m_treeTheme);
        buildScene();
    } else {
        m_preview->scene()->update();
    }
    updateHint(role);
}
