#include "ShoWSurface.h"
#include "LearningService.h"

#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QReadWriteLock>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

static void init_overlay_resources() {
    static const bool initialized = []() {
        Q_INIT_RESOURCE(overlay);
        return true;
    }();
    Q_UNUSED(initialized);
}

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

ShoWSurface::ShoWSurface(QWindow *parent, LearningService *learning)
    : QQuickView(parent), m_provider(new ScreenshotProvider) {
    init_overlay_resources();
    setResizeMode(QQuickView::SizeRootObjectToView);
    engine()->addImageProvider(QStringLiteral("screenshot"), m_provider);
    rootContext()->setContextProperty(QStringLiteral("overlay"), this);
    if (!learning) {
        learning = new LearningService(this);
        learning->setObjectName(QStringLiteral("learning_service"));
    }
    rootContext()->setContextProperty(QStringLiteral("learning"), learning);
    m_learning = learning;
    setSource(QUrl(QStringLiteral("qrc:/qml/Overlay.qml")));
}

ShoWSurface::~ShoWSurface() {
    if (m_export_dialog) {
        m_export_dialog->disconnect(this);
        delete m_export_dialog.data();
    }
}

void ShoWSurface::export_vocabulary(const QStringList &card_ids) {
    if (m_export_dialog) {
        raise_export_dialog();
        return;
    }
    QSettings settings;
    auto directory = settings.value("learning/export_directory",
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)).toString();
    if (!QFileInfo(directory).isDir())
        directory = QDir::homePath();
    // No transient parent: the layer-shell window must be hidden while the
    // platform's standard file dialog is visible, including on Wayland.
    auto *dialog = new QFileDialog(nullptr, tr("Export vocabulary for Anki"), directory,
        tr("Anki text files (*.txt *.tsv)"));
    dialog->setObjectName("anki_export_dialog");
    dialog->setAcceptMode(QFileDialog::AcceptSave);
    dialog->setFileMode(QFileDialog::AnyFile);
    dialog->setDefaultSuffix("txt");
    dialog->selectFile("showthis-anki.txt");
    dialog->setWindowModality(Qt::ApplicationModal);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_export_dialog = dialog;
    m_restore_after_export = isVisible();
    emit export_dialog_changed();
    hide(); // Release the overlay layer and its exclusive keyboard grab first.
    connect(dialog, &QFileDialog::finished, this, [this, dialog, card_ids](int result) {
        if (result == QDialog::Accepted && !dialog->selectedUrls().isEmpty()) {
            const auto destination = dialog->selectedUrls().first();
            if (m_learning->export_anki(destination, card_ids) && destination.isLocalFile())
                QSettings{}.setValue("learning/export_directory", QFileInfo(destination.toLocalFile()).absolutePath());
        }
        m_export_dialog.clear();
        emit export_dialog_changed();
        QTimer::singleShot(0, this, [this]() {
            if (m_restore_after_export && !m_export_dialog) {
                m_restore_after_export = false;
                show();
                requestActivate();
            }
        });
    });
    // Let the compositor process the hide before mapping the standard dialog.
    QTimer::singleShot(0, dialog, [dialog]() { dialog->show(); });
}

void ShoWSurface::raise_export_dialog() {
    if (m_export_dialog) {
        m_export_dialog->raise();
        m_export_dialog->activateWindow();
    }
}

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
