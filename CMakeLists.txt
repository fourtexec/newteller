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
    src/rustbridge.cpp
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

# Optional Rust core (rust/crates/newteller-core): the application catalogue,
# search index, metrics and desktop-file parsing. src/rustbridge.cpp degrades to
# stubs without NEWTELLER_HAVE_RUST and the shell then uses its C++/Qt fallback,
# so the build works without a Rust toolchain. -DNEWTELLER_WITH_RUST=ON builds
# the core with cargo and links it statically (it also builds newteller-ctl).
option(NEWTELLER_WITH_RUST "Build and link the Rust core from rust/ (requires cargo)" OFF)

if(NEWTELLER_WITH_RUST)
    find_program(NEWTELLER_CARGO_EXECUTABLE cargo)
    if(NOT NEWTELLER_CARGO_EXECUTABLE)
        message(FATAL_ERROR
            "NEWTELLER_WITH_RUST=ON but cargo was not found. Install a Rust "
            "toolchain (1.75 or newer, e.g. rustup or the rustc/cargo packages) "
            "or configure with -DNEWTELLER_WITH_RUST=OFF.")
    endif()
    find_package(Threads REQUIRED)

    set(newteller_cargo_dir "${CMAKE_CURRENT_SOURCE_DIR}/rust")
    set(newteller_cargo_target "${CMAKE_BINARY_DIR}/cargo-target")
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(newteller_cargo_profile dev)
        set(newteller_core_lib "${newteller_cargo_target}/debug/libnewteller_core.a")
    elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        set(newteller_cargo_profile release-debug) # defined in rust/Cargo.toml
        set(newteller_core_lib "${newteller_cargo_target}/release-debug/libnewteller_core.a")
    else()
        set(newteller_cargo_profile release)
        set(newteller_core_lib "${newteller_cargo_target}/release/libnewteller_core.a")
    endif()

    file(GLOB_RECURSE newteller_rust_sources CONFIGURE_DEPENDS
        "${newteller_cargo_dir}/crates/*.rs"
        "${newteller_cargo_dir}/crates/*.toml"
        "${newteller_cargo_dir}/crates/*.h")
    list(APPEND newteller_rust_sources "${newteller_cargo_dir}/Cargo.toml")

    add_custom_command(
        OUTPUT "${newteller_core_lib}"
        COMMAND "${NEWTELLER_CARGO_EXECUTABLE}" build
                --manifest-path "${newteller_cargo_dir}/Cargo.toml"
                --target-dir "${newteller_cargo_target}"
                --profile "${newteller_cargo_profile}"
        DEPENDS ${newteller_rust_sources}
        WORKING_DIRECTORY "${newteller_cargo_dir}"
        COMMENT "Building the Rust core (newteller-core staticlib, newteller-ctl)"
        VERBATIM)
    add_custom_target(newteller_rust_core DEPENDS "${newteller_core_lib}")
    add_dependencies(newteller newteller_rust_core)

    target_compile_definitions(newteller PRIVATE NEWTELLER_HAVE_RUST)
    target_include_directories(newteller PRIVATE
        "${newteller_cargo_dir}/crates/newteller-core/include")
    target_link_libraries(newteller PRIVATE
        "${newteller_core_lib}"
        Threads::Threads
        ${CMAKE_DL_LIBS}
        m)
else()
    message(STATUS "Rust core: disabled, the C++/Qt fallback back end is used "
        "(configure with -DNEWTELLER_WITH_RUST=ON to enable it)")
endif()

include(GNUInstallDirs)
install(TARGETS newteller RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(DIRECTORY plugins/ DESTINATION ${CMAKE_INSTALL_DATADIR}/newteller/plugins)
install(FILES data/org.newteller.Shell.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES data/newteller-autostart.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/newteller)
