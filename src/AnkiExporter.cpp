#include "AnkiExporter.h"
#include "NotebookData.h"

#include <QFileInfo>
#include <QObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace showthis {
namespace {

QString anki_html(QString text) {
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    return text.toHtmlEscaped().replace('\n', "<br>").replace('\t', "&#9;");
}

QString tsv_field(QString text) {
    return '"' + text.replace('"', "\"\"") + '"';
}

}  // namespace

QString AnkiExporter::format_card(const QVariantMap &card) {
    QString front = "<b>" + anki_html(word_with_article(card.value("word").toString(), card.value("article").toString()))
        + "</b> <small>(" + anki_html(card.value("language").toString().toUpper()) + ")</small>";
    const auto context = card.value("context").toString();
    if (!context.isEmpty())
        front += "<br><br>" + anki_html(context);
    QStringList back;
    for (const auto &field : {QStringLiteral("translation"), QStringLiteral("meaning"), QStringLiteral("notes")}) {
        const auto text = card.value(field).toString();
        if (!text.isEmpty())
            back.append(anki_html(text));
    }
    const auto sense = card.value("sense").toMap();
    const auto pronunciation = sense.value("pronunciation").toString();
    if (!pronunciation.isEmpty())
        back.append(anki_html(pronunciation));
    for (const auto &example : sense.value("examples").toList())
        back.append(anki_html(example.toString()));
    const auto source = card.value("source").toMap();
    if (!source.isEmpty()) {
        const auto license = source.value("license").toMap();
        back.append("<small>Wiktionary via " + anki_html(source.value("name", "FreeDictionaryAPI.com").toString()) + "<br>"
            + anki_html(source.value("url").toString()) + "<br>"
            + anki_html(license.value("name").toString()) + " "
            + anki_html(license.value("url").toString()) + "</small>");
    }
    QString language_tag = card.value("language").toString();
    language_tag.remove(QRegularExpression("[^a-zA-Z0-9_-]"));
    return tsv_field(front) + '\t' + tsv_field(back.join("<br><br>")) + '\t'
        + tsv_field("showthis lang::" + language_tag) + '\n';
}

AnkiExportResult AnkiExporter::write(const QUrl &destination, const QString &store_path,
                                    const QVariantList &cards, const QStringList &card_ids) {
    if (!destination.isLocalFile() || destination.toLocalFile().isEmpty())
        return {false, QObject::tr("Choose a local file for the Anki export.")};
    const auto path = QFileInfo(destination.toLocalFile()).absoluteFilePath();
    if (path == QFileInfo(store_path).absoluteFilePath() || (QFileInfo::exists(path)
        && QFileInfo(path).canonicalFilePath() == QFileInfo(store_path).canonicalFilePath()))
        return {false, QObject::tr("Choose another file: the export must not overwrite your notebook.")};
    const QSet<QString> requested(card_ids.begin(), card_ids.end());
    QSet<QString> found;
    QString output = "#separator:Tab\n#html:true\n#columns:Front\tBack\tTags\n#tags column:3\n";
    int count = 0;
    for (const auto &value : cards) {
        const auto card = value.toMap();
        const auto id = card.value("id").toString();
        if (!requested.isEmpty() && !requested.contains(id))
            continue;
        found.insert(id);
        output += format_card(card);
        ++count;
    }
    if (count == 0 || (!requested.isEmpty() && found != requested))
        return {false, QObject::tr("No words to export, or the selected words no longer exist.")};
    QSaveFile file(path);
    const auto bytes = output.toUtf8();
    if (!file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit())
        return {false, QObject::tr("Could not write Anki export: %1").arg(path)};
    return {true, QObject::tr("Exported %1 words. In Anki, import this file using the Basic note type.").arg(count)};
}

}  // namespace showthis
