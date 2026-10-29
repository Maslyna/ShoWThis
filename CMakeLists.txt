cmake_minimum_required(VERSION 3.19)
project(ShoWThis LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- External libs ---
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets DBus Gui Quick Qml QuickControls2 QuickDialogs2 Concurrent Network)
find_package(LayerShellQt REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(TESSERACT REQUIRED IMPORTED_TARGET tesseract)
pkg_check_modules(LEPTONICA REQUIRED IMPORTED_TARGET lept)


qt_standard_project_setup()
qt_add_resources(SHOWTHIS_QML_RESOURCES qml/overlay.qrc)

qt_add_executable(ShoWThis
    WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/ShoWSurface.h src/ShoWSurface.cpp
    src/AScreenCaptureBackend.h src/AScreenCaptureBackend.cpp
    src/KWinScreenCaptureBackend.h src/KWinScreenCaptureBackend.cpp
    src/GlobalShortcutPortal.h src/GlobalShortcutPortal.cpp
    src/ShoWThisConstants.h
    src/OcrEngine.h src/OcrEngine.cpp
    src/AppSettings.h src/AppSettings.cpp
    src/LearningService.h src/LearningService.cpp
    ${SHOWTHIS_QML_RESOURCES}
)

target_link_libraries(ShoWThis
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::DBus
        Qt6::OpenGL
        Qt6::Gui
        Qt6::Quick
        Qt6::Qml
        Qt6::QuickControls2
        Qt6::QuickDialogs2
        Qt6::Concurrent
        Qt6::Network

        LayerShellQt::Interface

        PkgConfig::TESSERACT
        PkgConfig::LEPTONICA
)

install(TARGETS ShoWThis
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
qt_generate_deploy_app_script(
    TARGET ShoWThis
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    qt_add_executable(OverlayTest
        tests/OverlayTest.cpp
        src/ShoWSurface.h src/ShoWSurface.cpp
        src/AppSettings.h src/AppSettings.cpp
        src/LearningService.h src/LearningService.cpp
        ${SHOWTHIS_QML_RESOURCES}
    )
    target_include_directories(OverlayTest PRIVATE src)
    target_link_libraries(OverlayTest PRIVATE Qt6::Quick Qt6::QuickControls2 Qt6::QuickDialogs2 Qt6::Test Qt6::Network)
    add_test(NAME OverlayTest COMMAND OverlayTest)
    set_tests_properties(OverlayTest PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software"
    )
    qt_add_executable(LearningServiceTest
        tests/LearningServiceTest.h tests/LearningServiceTest.cpp
        src/LearningService.h src/LearningService.cpp
    )
    target_include_directories(LearningServiceTest PRIVATE src)
    target_link_libraries(LearningServiceTest PRIVATE Qt6::Network Qt6::Test)
    add_test(NAME LearningServiceTest COMMAND LearningServiceTest)
endif()
