cmake_minimum_required(VERSION 3.19)
project(ShoWThis LANGUAGES CXX)
include(GNUInstallDirs)
option(SHOWTHIS_ENABLE_SANITIZERS "Enable address/undefined-behaviour sanitizers" OFF)
if(SHOWTHIS_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
    else()
        message(FATAL_ERROR "SHOWTHIS_ENABLE_SANITIZERS requires GCC or Clang")
    endif()
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets DBus Gui OpenGL Quick Qml QuickControls2 QuickDialogs2 Concurrent Network Sql)
find_package(LayerShellQt REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(TESSERACT REQUIRED IMPORTED_TARGET tesseract)
pkg_check_modules(LEPTONICA REQUIRED IMPORTED_TARGET lept)

qt_standard_project_setup()

# Share the production implementation with tests rather than maintaining
# independent copies of each executable's source and dependency lists.
qt_add_resources(SHOWTHIS_GRAMMAR_RESOURCES data/grammar.qrc)
qt_add_library(showthis_core STATIC
    src/AppSettings.h src/AppSettings.cpp
    src/LearningService.h src/LearningService.cpp
    src/LearningStore.h src/LearningStore.cpp
    src/LookupCacheStore.h src/LookupCacheStore.cpp
    src/NotebookData.h src/NotebookData.cpp
    src/CardForms.h src/CardForms.cpp
    src/NotebookListModel.h src/NotebookListModel.cpp
    src/GrammarProfile.h src/GrammarProfile.cpp
    ${SHOWTHIS_GRAMMAR_RESOURCES}
    src/DictionaryData.h src/DictionaryData.cpp
    src/DictionaryLookup.h src/DictionaryLookup.cpp
    src/DictionaryProvider.h src/DictionaryProviders.cpp
    src/FreeDictionaryProvider.h src/FreeDictionaryProvider.cpp
    src/KaikkiDictionaryProvider.h src/KaikkiDictionaryProvider.cpp
    src/KaikkiOfflineStore.h src/KaikkiOfflineStore.cpp
    src/AnkiExporter.h src/AnkiExporter.cpp
    src/TranslationProvider.h src/TranslationProviders.cpp
    src/TranslationService.h src/TranslationService.cpp
    src/DeepLTranslator.h src/DeepLTranslator.cpp
)
target_include_directories(showthis_core PUBLIC src)
target_link_libraries(showthis_core PUBLIC Qt6::Core Qt6::Network Qt6::Concurrent Qt6::Sql ZLIB::ZLIB)

qt_add_resources(SHOWTHIS_QML_RESOURCES qml/overlay.qrc)
qt_add_library(showthis_ui STATIC
    src/ShoWSurface.h src/ShoWSurface.cpp
    src/OverlayController.h src/OverlayController.cpp
    src/AScreenCaptureBackend.h src/AScreenCaptureBackend.cpp
    ${SHOWTHIS_QML_RESOURCES}
)
target_link_libraries(showthis_ui PUBLIC showthis_core Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2 Qt6::Concurrent Qt6::Widgets)

qt_add_library(showthis_ocr STATIC src/OcrEngine.h src/OcrEngine.cpp)
target_include_directories(showthis_ocr PUBLIC src)
target_link_libraries(showthis_ocr PUBLIC Qt6::Gui PkgConfig::TESSERACT PkgConfig::LEPTONICA)

qt_add_library(showthis_desktop STATIC
    src/KWinScreenCaptureBackend.h src/KWinScreenCaptureBackend.cpp
    src/GlobalShortcutPortal.h src/GlobalShortcutPortal.cpp)
target_link_libraries(showthis_desktop PUBLIC showthis_ui Qt6::DBus)

qt_add_executable(ShoWThis
    WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/ShoWThisConstants.h
)
target_link_libraries(ShoWThis PRIVATE
    showthis_ui showthis_ocr showthis_desktop Qt6::Widgets Qt6::DBus Qt6::OpenGL
    LayerShellQt::Interface
)

install(TARGETS ShoWThis
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
qt_generate_deploy_app_script(
    TARGET ShoWThis
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})
configure_file(data/dev.maslyna.ShoWThis.desktop.in dev.maslyna.ShoWThis.desktop @ONLY)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/dev.maslyna.ShoWThis.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(OcrEngineTest tests/OcrEngineTest.cpp)
    target_link_libraries(OcrEngineTest PRIVATE showthis_ocr Qt6::Test)
    add_test(NAME OcrEngineTest COMMAND OcrEngineTest)
    qt_add_executable(NotebookArchitectureTest tests/NotebookArchitectureTest.cpp)
    target_link_libraries(NotebookArchitectureTest PRIVATE showthis_core Qt6::Test)
    add_test(NAME NotebookArchitectureTest COMMAND NotebookArchitectureTest)
    find_program(DBUS_RUN_SESSION dbus-run-session)
    if(DBUS_RUN_SESSION)
        qt_add_executable(DesktopIntegrationTest tests/DesktopIntegrationTest.cpp)
        target_link_libraries(DesktopIntegrationTest PRIVATE showthis_desktop Qt6::Test)
        add_test(NAME DesktopIntegrationTest COMMAND ${DBUS_RUN_SESSION} -- $<TARGET_FILE:DesktopIntegrationTest>)
    endif()

    qt_add_executable(OverlayTest tests/OverlayTest.cpp)
    target_link_libraries(OverlayTest PRIVATE showthis_ui Qt6::Test)
    add_test(NAME OverlayTest COMMAND OverlayTest)
    set_tests_properties(OverlayTest PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    )

    qt_add_executable(LearningServiceTest tests/LearningServiceTest.h tests/LearningServiceTest.cpp)
    target_link_libraries(LearningServiceTest PRIVATE showthis_core Qt6::Test)
    add_test(NAME LearningServiceTest COMMAND LearningServiceTest)

    qt_add_executable(NotebookDataTest tests/NotebookDataTest.cpp)
    target_link_libraries(NotebookDataTest PRIVATE showthis_core Qt6::Test)
    add_test(NAME NotebookDataTest COMMAND NotebookDataTest)

    qt_add_executable(LearningStoreTest tests/LearningStoreTest.cpp)
    target_link_libraries(LearningStoreTest PRIVATE showthis_core Qt6::Test)
    add_test(NAME LearningStoreTest COMMAND LearningStoreTest)

    qt_add_executable(TranslationServiceTest tests/TranslationServiceTest.cpp)
    target_link_libraries(TranslationServiceTest PRIVATE showthis_core Qt6::Test)
    add_test(NAME TranslationServiceTest COMMAND TranslationServiceTest)

    qt_add_executable(DictionaryLookupTest tests/DictionaryLookupTest.cpp)
    target_link_libraries(DictionaryLookupTest PRIVATE showthis_core Qt6::Test)
    add_test(NAME DictionaryLookupTest COMMAND DictionaryLookupTest)

    qt_add_executable(KaikkiOfflineStoreTest tests/KaikkiOfflineStoreTest.cpp)
    target_link_libraries(KaikkiOfflineStoreTest PRIVATE showthis_core Qt6::Test)
    add_test(NAME KaikkiOfflineStoreTest COMMAND KaikkiOfflineStoreTest)
    get_property(SHOWTHIS_TEST_NAMES DIRECTORY PROPERTY TESTS)
    set_tests_properties(${SHOWTHIS_TEST_NAMES} PROPERTIES TIMEOUT 60)
endif()
