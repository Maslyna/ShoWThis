#include "TranslationProvider.h"
#include "DeepLTranslator.h"
#include "LibreTranslateTranslator.h"

namespace showthis {

TranslationProviders default_translation_providers() {
    TranslationProviders providers;
    providers.push_back(std::make_unique<DeepLTranslator>());
    providers.push_back(std::make_unique<LibreTranslateTranslator>());
    return providers;
}

}  // namespace showthis
