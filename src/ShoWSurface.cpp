#include "ShoWSurface.h"
#include "LearningService.h"
#include "NotebookData.h"
#include "ShoWThisConstants.h"

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
#include <QClipboard>
#include <QGuiApplication>
#include <QQuickItem>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>

static void init_overlay_resources() {
    static const bool initialized = []() {
        Q_INIT_RESOURCE(overlay);
        return true;
    }();
    Q_UNUSED(initialized);
}

namespace showthis {

namespace {
const QString settings_interface = QStringLiteral("org.freedesktop.portal.Settings");
const QString appearance_namespace = QStringLiteral("org.freedesktop.appearance");
const QString reduced_motion_key = QStringLiteral("reduced-motion");
}

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
    : QQuickView(parent),
      m_motion_bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("showthis-motion-settings"))),
      m_provider(new ScreenshotProvider) {
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
    QSettings settings;
    m_translator_source_language = settings.value("translator_window/source_language", learning->source_language()).toString();
    m_translator_target_language = settings.value("translator_window/target_language", learning->target_language()).toString();
    setSource(QUrl(QStringLiteral("qrc:/qml/Overlay.qml")));
    if (rootObject()) {
        m_notebook_dialog = rootObject()->findChild<QQuickItem *>(QStringLiteral("notebook_dialog"));
        m_translator_dialog = rootObject()->findChild<QQuickItem *>(QStringLiteral("translator_dialog"));
    }
    connect(this, &QWindow::visibleChanged, this, [this](bool visible) {
        if (!visible)
            cancel_context_selection();
    });
    // Portal calls freeze a peer's app identity; do not pre-empt shortcut registration.
    auto &bus = m_motion_bus;
    if (bus.connect(DESKTOP_PORTAL_SERVICE, DESKTOP_PORTAL_PATH, settings_interface, "SettingChanged",
            this, SLOT(on_motion_setting_changed(QString,QString,QDBusVariant)))) {
        auto *watcher = new QDBusServiceWatcher(DESKTOP_PORTAL_SERVICE, bus,
            QDBusServiceWatcher::WatchForOwnerChange, this);
        connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &owner) {
                apply_motion_preference({});
                if (!owner.isEmpty())
                    read_motion_preference();
            });
        bus.connect({}, "/org/freedesktop/DBus/Local", "org.freedesktop.DBus.Local", "Disconnected",
            this, SLOT(on_motion_bus_disconnected()));
        read_motion_preference();
    }
}

ShoWSurface::~ShoWSurface() {
    if (m_export_dialog) {
        m_export_dialog->disconnect(this);
        delete m_export_dialog.data();
    }
}

void ShoWSurface::read_motion_preference() {
    auto message = QDBusMessage::createMethodCall(DESKTOP_PORTAL_SERVICE, DESKTOP_PORTAL_PATH,
        settings_interface, "Read");
    message.setAutoStartService(false);
    message << appearance_namespace << reduced_motion_key;
    const auto generation = ++m_motion_generation;
    auto *watcher = new QDBusPendingCallWatcher(m_motion_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation]() {
        watcher->deleteLater();
        // A newer setting or portal owner supersedes this snapshot.
        if (generation != m_motion_generation)
            return;
        const QDBusPendingReply<QDBusVariant> reply = *watcher;
        apply_motion_preference(reply.isError() || !m_motion_bus.isConnected()
            ? QVariant{} : reply.value().variant());
    });
}

void ShoWSurface::apply_motion_preference(QVariant value) {
    ++m_motion_generation;
    while (value.metaType() == QMetaType::fromType<QDBusVariant>())
        value = value.value<QDBusVariant>().variant();
    // Unknown uint values mean no preference in the portal specification.
    const bool enabled = value.metaType() == QMetaType::fromType<uint>() && value.value<uint>() != 1;
    if (m_motion_enabled == enabled)
        return;
    m_motion_enabled = enabled;
    emit motion_preferences_changed();
}

void ShoWSurface::on_motion_setting_changed(const QString &name_space, const QString &key,
                                           const QDBusVariant &value) {
    if (name_space == appearance_namespace && key == reduced_motion_key)
        apply_motion_preference(value.variant());
}

void ShoWSurface::on_motion_bus_disconnected() {
    apply_motion_preference({});
}

void ShoWSurface::open_notebook_word(const QString &word, const QString &context) {
    if (m_export_dialog) {
        raise_export_dialog();
        return;
    }
    cancel_context_selection();
    if (QMetaObject::invokeMethod(rootObject(), "show_notebook_word",
                                 Q_ARG(QVariant, word), Q_ARG(QVariant, context))) {
        show();
        requestActivate();
    }
}

void ShoWSurface::open_notebook() {
    if (m_export_dialog) {
        raise_export_dialog();
        return;
    }
    cancel_context_selection();
    if (QMetaObject::invokeMethod(rootObject(), "show_notebook")) {
        show();
        requestActivate();
    }
}

void ShoWSurface::close_notebook() {
    QMetaObject::invokeMethod(rootObject(), "hide_notebook");
}

void ShoWSurface::open_translator(const QString &text, const QString &source_language, const QString &field) {
    if (m_export_dialog) {
        raise_export_dialog();
        return;
    }
    cancel_context_selection();
    if (QMetaObject::invokeMethod(rootObject(), "show_translator", Q_ARG(QVariant, text),
                                 Q_ARG(QVariant, source_language), Q_ARG(QVariant, field))) {
        show();
        requestActivate();
    }
}

void ShoWSurface::set_translator_source_language(const QString &language) {
    const auto normalized = normalized_language_code(language);
    if (normalized.isEmpty() || m_translator_source_language == normalized)
        return;
    m_learning->translator()->clear_session();
    m_translator_source_language = normalized;
    QSettings{}.setValue("translator_window/source_language", normalized);
    emit translator_languages_changed();
}

void ShoWSurface::set_translator_target_language(const QString &language) {
    const auto normalized = normalized_language_code(language);
    if (normalized.isEmpty() || m_translator_target_language == normalized)
        return;
    m_learning->translator()->clear_session();
    m_translator_target_language = normalized;
    QSettings{}.setValue("translator_window/target_language", normalized);
    emit translator_languages_changed();
}

void ShoWSurface::update_selected_card(const QString &id, const QString &word) {
    if (id == m_selected_card_id && word == m_selected_card_word)
        return;
    m_selected_card_id = id;
    m_selected_card_word = word;
    emit selected_card_changed();
}

bool ShoWSurface::add_to_selected_card(const QString &field, const QString &text, const QString &target_language) {
    if (!has_selected_card() || text.trimmed().isEmpty()
        || (field != "translation" && field != "context_translation" && field != "meaning"
            && field != "notes" && field != "context"))
        return false;
    QVariant added;
    const bool translated = field == "translation" || field == "context_translation";
    return QMetaObject::invokeMethod(m_notebook_dialog, "add_to_card",
        Q_RETURN_ARG(QVariant, added), Q_ARG(QVariant, field), Q_ARG(QVariant, text),
        Q_ARG(QVariant, translated ? target_language : QString{})) && added.toBool();
}

void ShoWSurface::begin_context_selection() {
    if (m_export_dialog || !m_notebook_dialog || scanning() || m_image_size.isEmpty()
        || m_words.isEmpty() || m_context_selection_active)
        return;
    m_context_selection_active = true;
    if (!QMetaObject::invokeMethod(rootObject(), "begin_context_selection"))
        m_context_selection_active = false;
    if (m_context_selection_active) {
        show();
        requestActivate();
    }
}

void ShoWSurface::complete_context_selection(const QString &text) {
    if (!m_context_selection_active)
        return;
    m_context_selection_active = false;
    QMetaObject::invokeMethod(rootObject(), "finish_context_selection",
                             Q_ARG(QVariant, text), Q_ARG(QVariant, true));
}

void ShoWSurface::cancel_context_selection() {
    if (!m_context_selection_active)
        return;
    m_context_selection_active = false;
    QMetaObject::invokeMethod(rootObject(), "finish_context_selection",
                             Q_ARG(QVariant, QString{}), Q_ARG(QVariant, false));
}

void ShoWSurface::copy_text(const QString &text) {
    QGuiApplication::clipboard()->setText(text, QClipboard::Clipboard);
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
    cancel_context_selection();
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
    cancel_context_selection();
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
    if (scanning)
        cancel_context_selection();
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
