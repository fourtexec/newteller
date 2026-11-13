cmake_minimum_required(VERSION 3.22)
project(newteller VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

include(GNUInstallDirs)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Qml Quick Svg DBus Network)

# The compositor is what turns NewTeller from a panel that runs inside somebody
# else's desktop into a real Wayland session. It only needs C libraries, not Qt.
find_package(PkgConfig REQUIRED)
pkg_search_module(WLROOTS REQUIRED IMPORTED_TARGET wlroots-0.20 wlroots-0.19 wlroots-0.18)
pkg_check_modules(LIBINPUT REQUIRED IMPORTED_TARGET libinput)
# Title bars and window thumbnails are drawn with cairo and pango.
pkg_check_modules(CAIRO REQUIRED IMPORTED_TARGET cairo pangocairo)
# The effects renderer builds clip regions with pixman directly.
pkg_check_modules(PIXMAN REQUIRED IMPORTED_TARGET pixman-1)
find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner REQUIRED)

# wlr-layer-shell support (panel, desktop, menu). Package names differ per distro,
# so we try both the Qt6 and the generic CMake package names.
find_package(LayerShellQt QUIET)
if(NOT TARGET LayerShellQt::Interface)
    find_package(LayerShellQt6 QUIET)
endif()
if(NOT TARGET LayerShellQt::Interface)
    message(FATAL_ERROR "LayerShellQt (Qt6) not found. Install liblayershellqtinterface-dev or layer-shell-qt-devel.")
endif()

# wayland-client: checks whether the compositor supports wlr-layer-shell before
# Qt starts (otherwise NewTeller runs through XWayland).
# xcb: X11 window types and reserved screen space for the panel.
# Searched for directly, so pkg-config is not needed.
find_path(WAYLAND_CLIENT_INCLUDE_DIR wayland-client.h)
find_library(WAYLAND_CLIENT_LIBRARY NAMES wayland-client)
if(NOT WAYLAND_CLIENT_INCLUDE_DIR OR NOT WAYLAND_CLIENT_LIBRARY)
    message(FATAL_ERROR "wayland-client development files not found. Install libwayland-dev "
                        "(sudo bash scripts/install-build-deps.sh) or wayland-devel.")
endif()
find_path(XCB_INCLUDE_DIR xcb/xcb.h)
find_library(XCB_LIBRARY NAMES xcb)
if(NOT XCB_INCLUDE_DIR OR NOT XCB_LIBRARY)
    message(FATAL_ERROR "xcb development files not found. Install libxcb1-dev "
                        "(sudo bash scripts/install-build-deps.sh) or libxcb-devel.")
endif()

# ---------------------------------------------------------------------------
# The Rust core (search index, metrics, watcher, config). Built with cargo and
# linked as a static library. AUTO enables it when cargo is available;
# -DNEWTELLER_WITH_RUST=OFF builds the pure C++/Qt fallback.
option(NEWTELLER_WITH_RUST "Build the Rust core (ON/OFF/AUTO)" AUTO)
find_program(CARGO_EXECUTABLE cargo)
if(NEWTELLER_WITH_RUST STREQUAL "OFF")
    set(NEWTELLER_RUST_ENABLED FALSE)
elseif(CARGO_EXECUTABLE)
    set(NEWTELLER_RUST_ENABLED TRUE)
elseif(NEWTELLER_WITH_RUST STREQUAL "ON")
    message(FATAL_ERROR "NEWTELLER_WITH_RUST=ON but cargo was not found")
else()
    message(STATUS "cargo not found: building without the Rust core")
    set(NEWTELLER_RUST_ENABLED FALSE)
endif()

if(NEWTELLER_RUST_ENABLED)
    set(NEWTELLER_RUST_LIB ${CMAKE_BINARY_DIR}/rust-target/release/libnewteller_core.a)
    file(GLOB_RECURSE NEWTELLER_RUST_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/rust/*)
    add_custom_command(OUTPUT ${NEWTELLER_RUST_LIB}
        COMMAND ${CMAKE_COMMAND} -E env CARGO_TARGET_DIR=${CMAKE_BINARY_DIR}/rust-target
                ${CARGO_EXECUTABLE} build --release
                --manifest-path ${CMAKE_SOURCE_DIR}/rust/Cargo.toml
                -p newteller-core -p newteller-ctl
        DEPENDS ${NEWTELLER_RUST_SOURCES}
        COMMENT "Building the Rust core (newteller-core, newteller-ctl)")
    add_custom_target(newteller_rust DEPENDS ${NEWTELLER_RUST_LIB})
    add_library(newteller_core STATIC IMPORTED GLOBAL)
    set_target_properties(newteller_core PROPERTIES IMPORTED_LOCATION ${NEWTELLER_RUST_LIB})
    add_dependencies(newteller_core newteller_rust)
endif()

qt_add_resources(newteller_resources resources.qrc)
qt_add_executable(newteller
    ${newteller_resources}
    src/main.cpp
    src/settings.cpp
    src/theme.cpp
    src/appmodel.cpp
    src/shellcontroller.cpp
    src/fileportal.cpp
    src/plugins.cpp
    src/rustbridge.cpp
    src/systemmonitor.cpp
    src/windowlist.cpp
    src/shellwindow.cpp
    src/windowsystem.cpp
    src/notifications.cpp
    src/tray.cpp
    src/quicksettings.cpp
    src/sessionservices.cpp
    src/clipboardhistory.cpp
    src/screenrecorder.cpp
    src/updater.cpp
    src/audiodevices.cpp
    src/bluetooth.cpp
    src/polkitagent.cpp
    src/screensaver.cpp
    src/translator.cpp
    src/bluetoothagent.cpp
    src/drivemonitor.cpp
)

target_compile_definitions(newteller PRIVATE NEWTELLER_VERSION="${PROJECT_VERSION}")
target_include_directories(newteller PRIVATE ${WAYLAND_CLIENT_INCLUDE_DIR} ${XCB_INCLUDE_DIR})
target_link_libraries(newteller PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Svg
    Qt6::DBus
    Qt6::Network
    LayerShellQt::Interface
    ${WAYLAND_CLIENT_LIBRARY}
    ${XCB_LIBRARY}
)

# The built-in administrator prompt (polkit authentication agent). Without
# libpolkit-agent-1 the session uses an external agent instead.
pkg_check_modules(POLKIT_AGENT QUIET IMPORTED_TARGET polkit-agent-1)
if(POLKIT_AGENT_FOUND)
    message(STATUS "Administrator prompt: built in (polkit-agent-1 ${POLKIT_AGENT_VERSION})")
    target_compile_definitions(newteller PRIVATE NEWTELLER_HAVE_POLKIT=1)
    target_link_libraries(newteller PRIVATE PkgConfig::POLKIT_AGENT)
else()
    message(STATUS "Administrator prompt: external agent (polkit-agent-1 not found)")
endif()

if(NEWTELLER_RUST_ENABLED)
    target_include_directories(newteller PRIVATE ${CMAKE_SOURCE_DIR}/rust/crates/newteller-core/include)
    target_compile_definitions(newteller PRIVATE NEWTELLER_HAVE_RUST=1)
    target_link_libraries(newteller PRIVATE newteller_core pthread dl m)
    install(PROGRAMS ${CMAKE_BINARY_DIR}/rust-target/release/newteller-ctl DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

# ---------------------------------------------------------------------------
# newteller-compositor: the small wlroots compositor that owns the DRM/KMS (or
# nested/headless) outputs and runs the shell as its first client.
# ---------------------------------------------------------------------------
set(NEWTELLER_LAYER_SHELL_HEADER ${CMAKE_BINARY_DIR}/wlr-layer-shell-unstable-v1-protocol.h)
set(NEWTELLER_XDG_SHELL_HEADER ${CMAKE_BINARY_DIR}/xdg-shell-protocol.h)
# wlr_pointer_constraints_v1.h includes this generated header.
set(NEWTELLER_POINTER_CONSTRAINTS_HEADER ${CMAKE_BINARY_DIR}/pointer-constraints-unstable-v1-protocol.h)
# Likewise wlr_cursor_shape_v1.h.
set(NEWTELLER_CURSOR_SHAPE_HEADER ${CMAKE_BINARY_DIR}/cursor-shape-v1-protocol.h)
# Likewise wlr_output_power_management_v1.h.
set(NEWTELLER_OUTPUT_POWER_HEADER ${CMAKE_BINARY_DIR}/wlr-output-power-management-unstable-v1-protocol.h)
# Likewise wlr_tablet_v2.h (wlroots 0.18 and 0.19).
set(NEWTELLER_TABLET_HEADER ${CMAKE_BINARY_DIR}/tablet-v2-protocol.h)
# Likewise wlr_ext_image_copy_capture_v1.h (wlroots 0.19).
set(NEWTELLER_IMAGE_COPY_HEADER ${CMAKE_BINARY_DIR}/ext-image-copy-capture-v1-protocol.h)
add_custom_command(
    OUTPUT ${NEWTELLER_IMAGE_COPY_HEADER}
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} server-header
            ${CMAKE_SOURCE_DIR}/protocols/ext-image-copy-capture-v1.xml
            ${NEWTELLER_IMAGE_COPY_HEADER}
    DEPENDS ${CMAKE_SOURCE_DIR}/protocols/ext-image-copy-capture-v1.xml
    COMMENT "Generating ext-image-copy-capture protocol header")
add_custom_command(
    OUTPUT ${NEWTELLER_TABLET_HEADER}
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} server-header
            ${CMAKE_SOURCE_DIR}/protocols/tablet-v2.xml
            ${NEWTELLER_TABLET_HEADER}
    DEPENDS ${CMAKE_SOURCE_DIR}/protocols/tablet-v2.xml
    COMMENT "Generating tablet-v2 protocol header")
add_custom_command(
    OUTPUT ${NEWTELLER_LAYER_SHELL_HEADER}
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} server-header
            ${CMAKE_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml
            ${NEWTELLER_LAYER_SHELL_HEADER}
    DEPENDS ${CMAKE_SOURCE_DIR}/protocols/wlr-layer-shell-unstable-v1.xml
    COMMENT "Generating wlr-layer-shell protocol header")
add_custom_command(
    OUTPUT ${NEWTELLER_XDG_SHELL_HEADER}
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} server-header
            ${CMAKE_SOURCE_DIR}/protocols/xdg-shell.xml
            ${NEWTELLER_XDG_SHELL_HEADER}
    DEPENDS ${CMAKE_SOURCE_DIR}/protocols/xdg-shell.xml
    COMMENT "Generating xdg-shell protocol header")
add_custom_command(
    OUTPUT ${NEWTELLER_POINTER_CONSTRAINTS_HEADER}
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} server-header
            ${CMAKE_SOURCE_DIR}/protocols/pointer-constraints-unstable-v1.xml
            ${NEWTELLER_POINTER_CONSTRAINTS_HEADER}
    DEPENDS ${CMAKE_SOURCE_DIR}/protocols/pointer-constraints-unstable-v1.xml
    COMMENT "Generating pointer-constraints protocol header")
add_custom_command(
    OUTPUT ${NEWTELLER_CURSOR_SHAPE_HEADER}
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} server-header
            ${CMAKE_SOURCE_DIR}/protocols/cursor-shape-v1.xml
            ${NEWTELLER_CURSOR_SHAPE_HEADER}
    DEPENDS ${CMAKE_SOURCE_DIR}/protocols/cursor-shape-v1.xml
    COMMENT "Generating cursor-shape protocol header")
add_custom_command(
    OUTPUT ${NEWTELLER_OUTPUT_POWER_HEADER}
    COMMAND ${WAYLAND_SCANNER_EXECUTABLE} server-header
            ${CMAKE_SOURCE_DIR}/protocols/wlr-output-power-management-unstable-v1.xml
            ${NEWTELLER_OUTPUT_POWER_HEADER}
    DEPENDS ${CMAKE_SOURCE_DIR}/protocols/wlr-output-power-management-unstable-v1.xml
    COMMENT "Generating wlr-output-power-management protocol header")

add_executable(newteller-compositor
    src/session/main.c
    src/session/config.c
    ${NEWTELLER_LAYER_SHELL_HEADER}
    ${NEWTELLER_XDG_SHELL_HEADER}
    ${NEWTELLER_POINTER_CONSTRAINTS_HEADER}
    ${NEWTELLER_CURSOR_SHAPE_HEADER}
    ${NEWTELLER_OUTPUT_POWER_HEADER}
    ${NEWTELLER_TABLET_HEADER}
    ${NEWTELLER_IMAGE_COPY_HEADER}
)
target_compile_definitions(newteller-compositor PRIVATE
    WLR_USE_UNSTABLE
    NEWTELLER_VERSION="${PROJECT_VERSION}")
set_target_properties(newteller-compositor PROPERTIES
    C_STANDARD 11
    C_STANDARD_REQUIRED ON
    LINKER_LANGUAGE C
    AUTOMOC OFF)
target_include_directories(newteller-compositor PRIVATE
    ${CMAKE_BINARY_DIR}
    ${CMAKE_SOURCE_DIR}/src/session
    ${WLROOTS_INCLUDE_DIRS}
)
target_link_directories(newteller-compositor PRIVATE ${WLROOTS_LIBRARY_DIRS})
target_link_libraries(newteller-compositor PRIVATE
    PkgConfig::WLROOTS
    PkgConfig::LIBINPUT
    PkgConfig::CAIRO
    PkgConfig::PIXMAN
    m
    wayland-server
    xkbcommon
)

install(TARGETS newteller newteller-compositor RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS scripts/newteller-session scripts/newteller-screenshot scripts/newteller-lock
                 scripts/newteller-run-as-root scripts/newteller-terminal
                 scripts/newteller-update-install DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY plugins/ DESTINATION ${CMAKE_INSTALL_DATADIR}/newteller/plugins)
install(FILES data/org.newteller.Shell.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/newteller-session.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/wayland-sessions RENAME newteller.desktop)
install(FILES data/newteller-autostart.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/newteller)
install(FILES data/newteller-portals.conf DESTINATION ${CMAKE_INSTALL_DATADIR}/xdg-desktop-portal)
# The text of the administrator prompt for newteller-update-install. polkit
# reads actions from /usr/share only, wherever the rest is installed.
install(FILES data/org.newteller.update.policy DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
install(FILES data/wallpapers/default.jpg DESTINATION ${CMAKE_INSTALL_DATADIR}/backgrounds/newteller)
