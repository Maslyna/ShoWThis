cmake_minimum_required(VERSION 3.19)
project(ShoWThis LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets DBus)
find_package(LayerShellQt REQUIRED)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

qt_standard_project_setup()

qt_add_executable(ShoWThis
    WIN32 MACOSX_BUNDLE
    src/main.cpp
    src/ShoWSurface.h src/ShoWSurface.cpp
    src/AScreenCaptureBackend.h src/AScreenCaptureBackend.cpp
    src/KWinScreenCaptureBackend.h src/KWinScreenCaptureBackend.cpp
    src/GlobalShortcutPortal.h src/GlobalShortcutPortal.cpp
    src/ShoWThisConstants.h src/ShoWThisConstants.cpp
)

target_link_libraries(ShoWThis
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::DBus

        LayerShellQt::Interface
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
