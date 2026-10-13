cmake_minimum_required(VERSION 3.22)
project(newteller VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick Svg)

# wlr-layer-shell support (panel, desktop, menu). Package names differ per distro,
# so we try both the Qt6 and the generic CMake package names.
find_package(LayerShellQt QUIET)
if(NOT TARGET LayerShellQt::Interface)
    find_package(LayerShellQt6 QUIET)
endif()
if(NOT TARGET LayerShellQt::Interface)
    message(FATAL_ERROR "LayerShellQt (Qt6) not found. Install liblayershellqtinterface-dev or layer-shell-qt-devel.")
endif()

qt_add_resources(newteller_resources resources.qrc)

qt_add_executable(newteller
    ${newteller_resources}
    src/main.cpp
    src/settings.cpp
    src/theme.cpp
    src/appmodel.cpp
    src/shellcontroller.cpp
    src/plugins.cpp
)

target_compile_definitions(newteller PRIVATE NEWTELLER_VERSION="${PROJECT_VERSION}")
target_link_libraries(newteller PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Svg
    LayerShellQt::Interface
)

include(GNUInstallDirs)
install(TARGETS newteller RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY plugins/ DESTINATION ${CMAKE_INSTALL_DATADIR}/newteller/plugins)
install(FILES data/org.newteller.Shell.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/newteller-autostart.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/newteller)
