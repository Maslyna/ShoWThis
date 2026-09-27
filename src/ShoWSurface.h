#ifndef SHOWSURFACE_H
#define SHOWSURFACE_H

#include <QImage>
#include <QPainter>
#include <QRasterWindow>

#include "OcrEngine.h"

namespace showthis {

class ShoWSurface : public QRasterWindow {
    Q_OBJECT

    using super = QRasterWindow;

public:
    explicit ShoWSurface(QWindow *parent = nullptr);
    ~ShoWSurface() override;

    void set_image(const QImage &image, const QList<ocr::WordBox> &words = {}) {
        m_image = image;
        m_words = words;

        super::requestUpdate();
    }

protected:
    QRect screen_rect() {
        return QRect(0, 0, width(), height());
    }

    void paintEvent(QPaintEvent *) override {
        QPainter p(this);

        p.drawImage(screen_rect(), m_image);

        p.fillRect(screen_rect(), QColor(0, 0, 0, 25));

        if (m_words.isEmpty())
            return;

        const qreal sx = qreal(width()) / m_image.width();
        const qreal sy = qreal(height()) / m_image.height();

        p.setPen(QPen(Qt::blue, 2));
        p.setBrush(Qt::NoBrush);

        for (const auto &word : m_words) {
            const QRectF rect{
                word.rect.x() * sx, word.rect.y() * sy, word.rect.width() * sx, word.rect.height() * sy};

            p.drawRect(rect);
        }
    }

private:
    QImage m_image{};
    QList<ocr::WordBox> m_words{};
};

}  // namespace showthis
#endif  // SHOWSURFACE_H
