#ifndef SHOWSURFACE_H
#define SHOWSURFACE_H

#include <QImage>
#include <QQuickView>
#include <QQuickItem>
#include <QPointer>
#include <QStringList>
#include <QVariantList>

#include "OcrEngine.h"

class QFileDialog;

namespace showthis {

class ScreenshotProvider;
class LearningService;

class ShoWSurface : public QQuickView {
    Q_OBJECT
    Q_PROPERTY(QVariantList words READ words NOTIFY wordsChanged)
    Q_PROPERTY(QSize imageSize READ image_size NOTIFY imageChanged)
    Q_PROPERTY(QUrl imageSource READ image_source NOTIFY imageChanged)
    Q_PROPERTY(QStringList languages READ languages NOTIFY languagesChanged)
    Q_PROPERTY(QString selectedLanguage READ language WRITE set_language NOTIFY selectedLanguageChanged)
    Q_PROPERTY(bool scanning READ scanning WRITE set_scanning NOTIFY scanningChanged)
    Q_PROPERTY(QString error READ error WRITE set_error NOTIFY errorChanged)
    Q_PROPERTY(bool export_dialog_open READ export_dialog_open NOTIFY export_dialog_changed)
    Q_PROPERTY(QQuickItem *notebook_dialog READ notebook_dialog CONSTANT)
    Q_PROPERTY(QQuickItem *translator_dialog READ translator_dialog CONSTANT)
    Q_PROPERTY(bool has_selected_card READ has_selected_card NOTIFY selected_card_changed)
    Q_PROPERTY(QString selected_card_word READ selected_card_word NOTIFY selected_card_changed)
    Q_PROPERTY(QString selected_card_id READ selected_card_id NOTIFY selected_card_changed)
    Q_PROPERTY(QString translator_source_language READ translator_source_language WRITE set_translator_source_language NOTIFY translator_languages_changed)
    Q_PROPERTY(QString translator_target_language READ translator_target_language WRITE set_translator_target_language NOTIFY translator_languages_changed)

public:
    // An injected service remains owned by the caller and must outlive the view.
    explicit ShoWSurface(QWindow *parent = nullptr, LearningService *learning = nullptr);
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
    bool export_dialog_open() const { return !m_export_dialog.isNull(); }
    Q_INVOKABLE void export_vocabulary(const QStringList &card_ids = {});
    void raise_export_dialog();
    void suppress_export_restore() { m_restore_after_export = false; }

    QQuickItem *notebook_dialog() const { return m_notebook_dialog; }
    QQuickItem *translator_dialog() const { return m_translator_dialog; }
    bool has_selected_card() const { return m_notebook_dialog && !m_selected_card_word.trimmed().isEmpty(); }
    QString selected_card_word() const { return m_selected_card_word; }
    QString selected_card_id() const { return m_selected_card_id; }
    QString translator_source_language() const { return m_translator_source_language; }
    QString translator_target_language() const { return m_translator_target_language; }
    void set_translator_source_language(const QString &language);
    void set_translator_target_language(const QString &language);
    Q_INVOKABLE void open_notebook_word(const QString &word, const QString &context);
    Q_INVOKABLE void open_notebook();
    Q_INVOKABLE void close_notebook();
    Q_INVOKABLE void open_translator(const QString &text = {}, const QString &source_language = {},
                                     const QString &field = QStringLiteral("notes"));
    Q_INVOKABLE void update_selected_card(const QString &id, const QString &word);
    Q_INVOKABLE bool add_to_selected_card(const QString &field, const QString &text,
                                         const QString &target_language = {});
    Q_INVOKABLE void begin_context_selection();
    Q_INVOKABLE void complete_context_selection(const QString &text);
    Q_INVOKABLE void cancel_context_selection();
    Q_INVOKABLE void copy_text(const QString &text);
signals:
    void wordsChanged();
    void imageChanged();
    void languagesChanged();
    void selectedLanguageChanged();
    void scanningChanged();
    void errorChanged();
    void hide_requested();
    void export_dialog_changed();
    void selected_card_changed();
    void translator_languages_changed();

private:
    ScreenshotProvider *m_provider;
    LearningService *m_learning = nullptr;
    QPointer<QFileDialog> m_export_dialog;
    bool m_restore_after_export = false;
    QPointer<QQuickItem> m_notebook_dialog;
    QPointer<QQuickItem> m_translator_dialog;
    QString m_selected_card_word;
    QString m_selected_card_id;
    QString m_translator_source_language;
    QString m_translator_target_language;
    bool m_context_selection_active = false;
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
