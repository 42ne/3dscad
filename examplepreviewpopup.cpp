#include "examplepreviewpopup.h"

#include <QLabel>
#include <QVBoxLayout>
#include <QScreen>
#include <QApplication>

static constexpr int ThumbW = 280;
static constexpr int ThumbH = 210;

ExamplePreviewPopup::ExamplePreviewPopup(QWidget *parent)
    : QFrame(parent,
             Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus)
{
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setAttribute(Qt::WA_TranslucentBackground, false);

    setFrameShape(QFrame::Box);
    setLineWidth(1);
    setStyleSheet("ExamplePreviewPopup { background: #1e2024; border: 1px solid #4a4f58; }");

    m_imageLabel = new QLabel(this);
    m_imageLabel->setFixedSize(ThumbW, ThumbH);
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setStyleSheet("color: #6a7080; font-size: 12px;");

    m_nameLabel = new QLabel(this);
    m_nameLabel->setAlignment(Qt::AlignCenter);
    m_nameLabel->setStyleSheet("color: #c0c8d8; font-size: 11px; padding: 4px 8px;");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_imageLabel);
    layout->addWidget(m_nameLabel);
}

void ExamplePreviewPopup::showAt(const QPoint &globalPos)
{
    // Position the popup to the right and slightly below the cursor/menu.
    const QSize sz = sizeHint();
    QPoint pos = globalPos + QPoint(12, -sz.height() / 2);

    // Keep inside the primary screen.
    if (const QScreen *screen = QApplication::primaryScreen()) {
        const QRect available = screen->availableGeometry();
        if (pos.x() + sz.width() > available.right())
            pos.setX(globalPos.x() - sz.width() - 12);
        pos.setY(qBound(available.top(), pos.y(), available.bottom() - sz.height()));
    }

    move(pos);
    show();
}

void ExamplePreviewPopup::setLoading(const QString &exampleName)
{
    m_imageLabel->clear();
    m_imageLabel->setText("Rendering…");
    m_nameLabel->setText(exampleName);
    adjustSize();
}

void ExamplePreviewPopup::setImage(const QImage &image, const QString &exampleName)
{
    if (image.isNull()) {
        m_imageLabel->setText("(no preview)");
    } else {
        m_imageLabel->setPixmap(
            QPixmap::fromImage(image).scaled(ThumbW, ThumbH,
                                             Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation));
    }
    m_nameLabel->setText(exampleName);
    adjustSize();
}

void ExamplePreviewPopup::hidePopup()
{
    hide();
    m_imageLabel->clear();
    m_nameLabel->clear();
}
