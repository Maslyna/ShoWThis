#pragma once

#include <QJsonObject>
#include <QVariantMap>

namespace showthis {

QJsonObject default_grammar_profiles();
bool missing_dictionary_form(const QString &word);
void apply_grammar_profile(QVariantMap &sense, const QString &word_language,
                          const QJsonObject &profiles = default_grammar_profiles());

}  // namespace showthis
