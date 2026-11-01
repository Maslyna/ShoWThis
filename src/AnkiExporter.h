#pragma once

#include <QStringList>
#include <QUrl>
#include <QVariantList>

namespace showthis {

struct AnkiExportResult {
    bool success;
    QString message;
};

class AnkiExporter {
public:
    static QString format_card(const QVariantMap &card);
    static AnkiExportResult write(const QUrl &destination, const QString &store_path,
                                  const QVariantList &cards, const QStringList &card_ids = {});
};

}  // namespace showthis
