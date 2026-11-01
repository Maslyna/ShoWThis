#include "TranslationProvider.h"
#include "DeepLTranslator.h"

namespace showthis {

TranslationProviders default_translation_providers() {
    TranslationProviders providers;
    providers.push_back(std::make_unique<DeepLTranslator>());
    return providers;
}

}  // namespace showthis
