#pragma once

#include <QVariantMap>
#include <QStringList>
#include <optional>

namespace showthis {

enum class NumberStatus { Unknown, Available, Unavailable, NotApplicable };
enum class ArticleOrigin { None, Dictionary, Profile, Manual };
struct NumberForms {
    QStringList alternatives;
    NumberStatus status = NumberStatus::Unknown;
};

struct CardForms {
    QString base;
    QString base_article;
    QString profile;
    bool number_relevant = false;
    NumberForms singular;
    NumberForms plural;
    ArticleOrigin article_origin = ArticleOrigin::None;
    ArticleOrigin base_article_origin = ArticleOrigin::None;

    static std::optional<CardForms> parse(const QVariantMap &fields, QString &error);
    QVariantMap fields(const QString &article) const;
};

}  // namespace showthis
