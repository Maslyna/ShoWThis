#ifndef SHOWSURFACE_H
#define SHOWSURFACE_H

#include <QImage>
#include <QPainter>
#include <QRasterWindow>

class ShoWSurface : public QRasterWindow
{
    Q_OBJECT

public:
    explicit ShoWSurface(QWindow *parent = nullptr);
    ~ShoWSurface() override;
    
    void setImage(const QImage &image) {
        m_image = image;

        requestUpdate();
    }

protected:
    QRect screenRect() {
        return QRect(0, 0, width(), height());
    }

    void paintEvent(QPaintEvent *) override
    {

        QPainter p(this);

        p.drawImage(
            screenRect(),
            m_image
        );

        p.fillRect(screenRect(), QColor(0, 0, 0, 25));
    }
private:
    QImage m_image{};
};
#endif // SHOWSURFACE_H
