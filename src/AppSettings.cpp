#include "AppSettings.h"

#include <QDebug>

namespace showthis {

AppSettings::AppSettings(const QString &file_name)
    : m_settings(file_name, QSettings::IniFormat) {}

QString AppSettings::ocr_language(const QStringList &available_languages) const {
    const auto saved = m_settings.value(QStringLiteral("ocr/language")).toString().split('+');
    QStringList selected;
    for (const auto &language : available_languages) {
        if (saved.contains(language))
            selected.append(language);
    }
    // Keep any surviving languages when a previously selected pack is removed.
    return (selected.isEmpty() ? available_languages : selected).join('+');
}

void AppSettings::set_ocr_language(const QString &language) {
    m_settings.setValue(QStringLiteral("ocr/language"), language);
    // Save immediately, even if the app is subsequently terminated from the tray.
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError)
        qWarning() << "Could not save OCR language settings:" << m_settings.fileName();
}

}  // namespace showthis
