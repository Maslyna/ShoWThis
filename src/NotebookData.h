#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantList>

namespace showthis {

QString normalized_language_code(QString code);
// Dictionaries index word languages, not translator regional/script variants.
QString dictionary_language_code(const QString &code);
QString word_with_article(const QString &word, const QString &article);
QVariantList dictionary_senses(const QJsonObject &data, const QString &language);
QString number_availability_text(const QVariantMap &forms);

}  // namespace showthis
