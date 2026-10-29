#pragma once

#include <QSettings>
#include <QStringList>

namespace showthis {

class AppSettings {
public:
    AppSettings() = default;
    explicit AppSettings(const QString &file_name);

    QString ocr_language(const QStringList &available_languages) const;
    void set_ocr_language(const QString &language);

private:
    QSettings m_settings;
};

}  // namespace showthis
