#include "CardForms.h"
#include <QObject>
#include <QRegularExpression>

namespace showthis {
namespace {
const QStringList statuses{"unknown", "available", "unavailable", "not_applicable"};
const QStringList origins{"", "dictionary", "profile", "manual"};
bool short_line(const QString &text, int limit) {
    static const QRegularExpression multiline("[\\r\\n\\t]");
    return text.size() <= limit && !text.contains(multiline);
}
}

std::optional<CardForms> CardForms::parse(const QVariantMap &fields, QString &error) {
    CardForms result;
    for (const auto *key : {"base_form", "base_article", "grammar_profile", "singular_status", "plural_status",
             "article_origin", "base_article_origin"}) {
        const auto value = fields.value(key);
        if (value.isValid() && !value.isNull() && value.metaType() != QMetaType::fromType<QString>()) {
            error = QObject::tr("Invalid word-form field type.");
            return {};
        }
    }
    const auto relevant = fields.value("number_relevant");
    if (relevant.isValid() && !relevant.isNull() && relevant.metaType() != QMetaType::fromType<bool>()) {
        error = QObject::tr("Invalid number-form flag.");
        return {};
    }
    result.base = fields.value("base_form").toString().trimmed();
    result.base_article = fields.value("base_article").toString().trimmed();
    result.profile = fields.value("grammar_profile").toString();
    result.number_relevant = fields.value("number_relevant").toBool();
    if (!short_line(result.base, 200) || !short_line(result.base_article, 80)) {
        error = QObject::tr("Keep the base form and its prefix to one short line each.");
        return {};
    }
    const auto read_number = [&](const QString &name, NumberForms &number) {
        const auto alternatives = fields.value(name + "_forms");
        if (alternatives.isValid() && !alternatives.isNull()) {
            if (alternatives.metaType() != QMetaType::fromType<QStringList>()
                && alternatives.metaType() != QMetaType::fromType<QVariantList>()) {
                error = QObject::tr("Number forms must be a list of strings.");
                return false;
            }
            for (const auto &value : alternatives.toList()) {
                if (value.metaType() != QMetaType::fromType<QString>()) {
                    error = QObject::tr("Number forms must be a list of strings.");
                    return false;
                }
            }
        }
        auto status = fields.value(name + "_status").toString();
        if (status.isEmpty()) status = "unknown";
        const int index = statuses.indexOf(status);
        if (index < 0) { error = QObject::tr("Invalid number-form status."); return false; }
        number.status = NumberStatus(index);
        for (const auto &form : fields.value(name + "_forms").toStringList()) {
            const auto text = form.trimmed();
            if (!short_line(text, 200)) {
                error = QObject::tr("Keep each number form to one line, at most 200 characters.");
                return false;
            }
            if (!text.isEmpty() && !number.alternatives.contains(text)) number.alternatives.append(text);
        }
        if (number.alternatives.size() > 20) {
            error = QObject::tr("Keep at most 20 alternatives per number form.");
            return false;
        }
        if (!result.number_relevant) number.status = NumberStatus::NotApplicable;
        else if (!number.alternatives.isEmpty()) number.status = NumberStatus::Available;
        else if (number.status == NumberStatus::Available || number.status == NumberStatus::NotApplicable)
            number.status = NumberStatus::Unknown;
        return true;
    };
    if (!read_number("singular", result.singular) || !read_number("plural", result.plural)) return {};
    const int article = origins.indexOf(fields.value("article_origin").toString());
    const int base_article = origins.indexOf(fields.value("base_article_origin").toString());
    if (article < 0 || base_article < 0) { error = QObject::tr("Invalid article origin."); return {}; }
    result.article_origin = ArticleOrigin(article);
    result.base_article_origin = ArticleOrigin(base_article);
    error.clear();
    return result;
}

QVariantMap CardForms::fields(const QString &article) const {
    return {{"base_form", base}, {"base_article", base_article}, {"grammar_profile", profile},
        {"number_relevant", number_relevant}, {"singular_forms", singular.alternatives},
        {"plural_forms", plural.alternatives}, {"singular_status", statuses[int(singular.status)]},
        {"plural_status", statuses[int(plural.status)]},
        {"article_origin", article.isEmpty() ? QString{} : origins[int(article_origin)]},
        {"base_article_origin", base_article.isEmpty() ? QString{} : origins[int(base_article_origin)]}};
}

}  // namespace showthis
