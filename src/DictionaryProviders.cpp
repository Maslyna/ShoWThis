#include "DictionaryProvider.h"
#include "FreeDictionaryProvider.h"
#include "KaikkiDictionaryProvider.h"

#include <QLocale>

namespace showthis {

QVariantMap DictionaryProvider::metadata() const {
    const auto languages = definition_languages();
    const auto system = QLocale::languageToCode(QLocale::system().language());
    auto preferred = languages.isEmpty() ? QString{} : languages.first().toMap().value("code").toString();
    for (const auto &language : languages) {
        if (language.toMap().value("code").toString() == system)
            preferred = system;
    }
    return {{"id", id()}, {"name", name()}, {"description", description()},
        {"website", website().toString()}, {"languages", languages}, {"default_language", preferred},
        {"settings", QVariant::fromValue(static_cast<QObject *>(state()))}, {"settings_component", settings_component()}};
}

DictionaryProviders default_dictionary_providers(KaikkiOfflineStore *offline) {
    DictionaryProviders providers;
    providers.push_back(std::make_unique<KaikkiDictionaryProvider>(offline));
    providers.push_back(std::make_unique<FreeDictionaryProvider>());
    return providers;
}

}  // namespace showthis
