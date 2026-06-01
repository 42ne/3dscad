#include "themeeditordialog.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGraphicsScene>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
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
                                      const ViewportAppearanceTheme &viewportTheme,
                                      QWidget *parent)
    : QDialog(parent)
    , m_windowTheme(windowTheme)
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

    // ── Connections ────────────────────────────────────────────────────────
    connect(m_targetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { buildScene(); });

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
    m_preview->scene()->update();
    updateHint(role);
}
