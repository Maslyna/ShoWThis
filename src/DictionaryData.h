#pragma once

#include <QJsonObject>
#include <QStringList>

namespace showthis {

QJsonObject free_dictionary_document(const QJsonObject &data, const QString &word, const QString &language);
QJsonObject kaikki_dictionary_document(const QByteArray &jsonl, const QString &word,
    const QString &language, const QString &edition, QString &error);
QStringList dictionary_form_targets(const QJsonObject &document, const QString &language);
bool has_dictionary_meanings(const QJsonObject &document, const QString &language);

}  // namespace showthis
