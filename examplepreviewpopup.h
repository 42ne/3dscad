#ifndef EXAMPLEPREVIEWPOPUP_H
#define EXAMPLEPREVIEWPOPUP_H

#include <QFrame>
#include <QImage>

class QLabel;

// Frameless popup shown next to the example menu while hovering.
// Displays a thumbnail render of the example scene.
class ExamplePreviewPopup : public QFrame
{
    Q_OBJECT

public:
    explicit ExamplePreviewPopup(QWidget *parent = nullptr);

    // Show at the given global position (popup appears to the right of pos).
    void showAt(const QPoint &globalPos);

    // Show a "rendering..." placeholder.
    void setLoading(const QString &exampleName);

    // Replace the placeholder with the rendered thumbnail.
    void setImage(const QImage &image, const QString &exampleName);

    void hidePopup();

private:
    QLabel *m_imageLabel = nullptr;
    QLabel *m_nameLabel  = nullptr;
};

#endif // EXAMPLEPREVIEWPOPUP_H
