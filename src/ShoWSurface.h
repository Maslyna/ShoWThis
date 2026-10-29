#ifndef SHOWSURFACE_H
#define SHOWSURFACE_H

#include <QImage>
#include <QQuickView>
#include <QStringList>
#include <QVariantList>

#include "OcrEngine.h"

namespace showthis {

class ScreenshotProvider;

class ShoWSurface : public QQuickView {
    Q_OBJECT
    Q_PROPERTY(QVariantList words READ words NOTIFY wordsChanged)
    Q_PROPERTY(QSize imageSize READ image_size NOTIFY imageChanged)
    Q_PROPERTY(QUrl imageSource READ image_source NOTIFY imageChanged)
    Q_PROPERTY(QStringList languages READ languages NOTIFY languagesChanged)
    Q_PROPERTY(QString selectedLanguage READ language WRITE set_language NOTIFY selectedLanguageChanged)
    Q_PROPERTY(bool scanning READ scanning WRITE set_scanning NOTIFY scanningChanged)
    Q_PROPERTY(QString error READ error WRITE set_error NOTIFY errorChanged)

public:
    explicit ShoWSurface(QWindow *parent = nullptr);
    ~ShoWSurface() override;

    void set_image(const QImage &image, const QList<ocr::WordBox> &words = {});
    void set_words(const QList<ocr::WordBox> &words);
    void set_languages(const QStringList &languages);
    void set_language(const QString &language);
    void set_scanning(bool scanning);
    void set_error(const QString &error);

    QVariantList words() const { return m_words; }
    QSize image_size() const { return m_image_size; }
    QUrl image_source() const;
    QStringList languages() const { return m_languages; }
    QString language() const { return m_language; }
    bool scanning() const { return m_scanning; }
    QString error() const { return m_error; }

signals:
    void wordsChanged();
    void imageChanged();
    void languagesChanged();
    void selectedLanguageChanged();
    void scanningChanged();
    void errorChanged();
    void closeRequested();

private:
    ScreenshotProvider *m_provider;
    QVariantList m_words;
    QSize m_image_size;
    quint64 m_image_revision = 0;
    QStringList m_languages;
    QString m_language;
    bool m_scanning = false;
    QString m_error;
};

}  // namespace showthis
#endif  // SHOWSURFACE_H
