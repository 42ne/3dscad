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

void ExamplePreviewPopup::showAt(int menuRight, int cursorY)
{
    const QSize sz = sizeHint();
    const int gap = 4;
    QPoint pos(menuRight + gap, cursorY - sz.height() / 2);

    // Use the screen that contains the menu cursor, not always the primary screen.
    const QScreen *screen = QApplication::screenAt(QPoint(menuRight, cursorY));
    if (!screen)
        screen = QApplication::primaryScreen();
    if (screen) {
        const QRect available = screen->availableGeometry();
        // If the popup doesn't fit to the right of the menu, flip it to the left.
        if (pos.x() + sz.width() > available.right())
            pos.setX(menuRight - gap - sz.width());
        // Clamp to screen bounds.
        pos.setX(qBound(available.left(), pos.x(), available.right() - sz.width()));
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
