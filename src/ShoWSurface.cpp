#include "ShoWSurface.h"
#include "LearningService.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QReadWriteLock>

namespace showthis {

class ScreenshotProvider : public QQuickImageProvider {
public:
    ScreenshotProvider() : QQuickImageProvider(QQuickImageProvider::Image) {}

    QImage requestImage(const QString &, QSize *size, const QSize &) override {
        QReadLocker lock(&m_lock);
        if (size)
            *size = m_image.size();
        return m_image;
    }

    void set_image(const QImage &image) {
        QWriteLocker lock(&m_lock);
        m_image = image;
    }

private:
    QReadWriteLock m_lock;
    QImage m_image;
};

ShoWSurface::ShoWSurface(QWindow *parent)
    : QQuickView(parent), m_provider(new ScreenshotProvider) {
    setResizeMode(QQuickView::SizeRootObjectToView);
    engine()->addImageProvider(QStringLiteral("screenshot"), m_provider);
    rootContext()->setContextProperty(QStringLiteral("overlay"), this);
    auto *learning = new LearningService(this);
    learning->setObjectName(QStringLiteral("learning_service"));
    rootContext()->setContextProperty(QStringLiteral("learning"), learning);
    setSource(QUrl(QStringLiteral("qrc:/qml/Overlay.qml")));
}

ShoWSurface::~ShoWSurface() = default;

void ShoWSurface::set_image(const QImage &image, const QList<ocr::WordBox> &words) {
    m_provider->set_image(image);
    m_image_size = image.size();
    ++m_image_revision;
    set_words(words);
    emit imageChanged();
}

QUrl ShoWSurface::image_source() const {
    return m_image_size.isEmpty() ? QUrl{} : QUrl(QStringLiteral("image://screenshot/%1").arg(m_image_revision));
}

void ShoWSurface::set_words(const QList<ocr::WordBox> &words) {
    m_words.clear();
    m_words.reserve(words.size());
    for (const auto &word : words) {
        m_words.append(QVariantMap{{QStringLiteral("text"), word.text},
                                   {QStringLiteral("x"), word.rect.x()},
                                   {QStringLiteral("y"), word.rect.y()},
                                   {QStringLiteral("width"), word.rect.width()},
                                   {QStringLiteral("height"), word.rect.height()},
                                   {QStringLiteral("context"), word.context}});
    }
    emit wordsChanged();
}

void ShoWSurface::set_languages(const QStringList &languages) {
    if (m_languages == languages)
        return;
    m_languages = languages;
    emit languagesChanged();
    const auto previous = m_language.split('+');
    QStringList selected;
    for (const auto &language : m_languages) {
        if (previous.contains(language))
            selected.append(language);
    }
    if (m_languages.isEmpty()) {
        if (!m_language.isEmpty()) {
            m_language.clear();
            emit selectedLanguageChanged();
        }
    } else {
        set_language((selected.isEmpty() ? m_languages : selected).join('+'));
    }
}

void ShoWSurface::set_language(const QString &language) {
    const auto requested = language.split('+');
    for (const auto &code : requested) {
        if (!m_languages.contains(code))
            return;
    }
    QStringList selected;
    for (const auto &code : m_languages) {
        if (requested.contains(code))
            selected.append(code);
    }
    const auto normalized = selected.join('+');
    if (normalized.isEmpty() || m_language == normalized)
        return;
    m_language = normalized;
    emit selectedLanguageChanged();
}

void ShoWSurface::set_scanning(bool scanning) {
    if (m_scanning == scanning)
        return;
    m_scanning = scanning;
    emit scanningChanged();
}

void ShoWSurface::set_error(const QString &error) {
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

}  // namespace showthis
