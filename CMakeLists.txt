cmake_minimum_required(VERSION 3.19)
project(WildPalms VERSION 0.1.1 LANGUAGES CXX)

# Build shared libs (required for KDE plugin .so loading via kcoreaddons_add_plugin)
set(BUILD_SHARED_LIBS ON)

# Required by kcoreaddons_add_plugin for MODULE library output location
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# C++ Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt6 Configuration
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Qt6
find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets Network Concurrent Sql)
qt_standard_project_setup()

# KDE Extra CMake Modules — provides KDE_INSTALL_PLUGINDIR etc.
find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} ${CMAKE_MODULE_PATH})
include(KDEInstallDirs6)

# RPATH for installed binaries/libraries
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")

# Find KDE Frameworks
find_package(KF6CalendarCore REQUIRED)
find_package(KF6CoreAddons REQUIRED)
find_package(KF6I18n REQUIRED)
find_package(KF6XmlGui REQUIRED)
find_package(KF6WidgetsAddons REQUIRED)
find_package(KF6Config REQUIRED)
find_package(KF6ConfigWidgets REQUIRED)
find_package(KF6Wallet REQUIRED)
find_package(KF6Crash REQUIRED)
find_package(KF6Notifications REQUIRED)
find_package(KF6StatusNotifierItem REQUIRED)

# System libraries
find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV REQUIRED libudev)

# libkalburator — shared calendar-sync substrate. Fetched via FetchContent
# from the published Codeberg repo, pinned to the canon-convergence campaign
# tip on main (note domain + shape-graph canon for all five PIM domains).
# Pass -DWILDPALMS_LIBKALBURATOR_SOURCE_DIR=/path/to/libkalburator to
# override with a local checkout (development convenience).
#
# See docs/LIBKALBURATOR.md and docs/plans/2026-04-20-libkalburator-integration.md
# for historical context.
set(KALBURATOR_HAVE_ORG_IO OFF CACHE BOOL "" FORCE)
# Accounts-first wizard (2026-06-09): Akonadi is a first-class local target.
option(KALBURATOR_HAVE_AKONADI "Build with KPim6 Akonadi support" ON)

set(WILDPALMS_LIBKALBURATOR_SOURCE_DIR "" CACHE PATH
    "Optional path to a local libkalburator checkout (override FetchContent)")
# Tag pin v1.07 (2026-09-13): KND-001 — deletes the DAV kind-demux, so a
# DAV account always registers one unfiltered :cal backend per iCalendar
# collection. Also carries the WP-009 CollectionRuntime facade cutover
# from v1.06 (see docs/CONSUMING.md in libkalburator and the WP-side
# response doc docs/2026-09-03-libkalburator-consuming-contract-
# response-wildpalms.md). All libkalburator public headers live under
# the namespaced kalburator/<domain>/<header>.h include convention
# introduced at v1.06; see docs/LIBKALBURATOR.md.
set(WILDPALMS_LIBKALBURATOR_GIT_TAG "v1.07" CACHE STRING
    "libkalburator tag to fetch when WILDPALMS_LIBKALBURATOR_SOURCE_DIR is unset")

if(WILDPALMS_LIBKALBURATOR_SOURCE_DIR)
    message(STATUS "libkalburator: using local source at ${WILDPALMS_LIBKALBURATOR_SOURCE_DIR}")
    add_subdirectory(${WILDPALMS_LIBKALBURATOR_SOURCE_DIR} libkalburator EXCLUDE_FROM_ALL)
else()
    message(STATUS "libkalburator: fetching ${WILDPALMS_LIBKALBURATOR_GIT_TAG} from GitHub")
    include(FetchContent)
    FetchContent_Declare(libkalburator
        GIT_REPOSITORY https://github.com/clintonthegeek/libkalburator.git
        GIT_TAG        ${WILDPALMS_LIBKALBURATOR_GIT_TAG}
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(libkalburator)
endif()

# Canonical path to libkalburator's checked-out sources, whichever way it
# was consumed. Tests that compile lib test fixtures directly (e.g.
# fakecaldavserver.cpp) must use this instead of assuming a flat sibling
# clone at ../libkalburator — that broke non-flat checkouts.
if(NOT WILDPALMS_LIBKALBURATOR_SOURCE_DIR)
    set(WILDPALMS_LIBKALBURATOR_DIR "${libkalburator_SOURCE_DIR}")
else()
    set(WILDPALMS_LIBKALBURATOR_DIR "${WILDPALMS_LIBKALBURATOR_SOURCE_DIR}")
endif()

# Graffodil — graph rendering library (Sync Patchbay). Same consumption
# pattern as libkalburator: sibling-dir override for development, pinned
# tag via FetchContent otherwise. See docs/superpowers/specs/
# 2026-06-11-sync-patchbay-design.md §9.
set(WILDPALMS_GRAFFODIL_SOURCE_DIR "" CACHE PATH
    "Optional path to a local Graffodil checkout (override FetchContent)")
set(WILDPALMS_GRAFFODIL_GIT_TAG "v0.2.0" CACHE STRING
    "Graffodil tag to fetch when WILDPALMS_GRAFFODIL_SOURCE_DIR is unset")
set(GRAFFODIL_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(GRAFFODIL_BUILD_TESTS OFF CACHE BOOL "" FORCE)

if(WILDPALMS_GRAFFODIL_SOURCE_DIR)
    message(STATUS "Graffodil: using local source at ${WILDPALMS_GRAFFODIL_SOURCE_DIR}")
    add_subdirectory(${WILDPALMS_GRAFFODIL_SOURCE_DIR} graffodil EXCLUDE_FROM_ALL)
else()
    message(STATUS "Graffodil: fetching ${WILDPALMS_GRAFFODIL_GIT_TAG} from GitHub")
    include(FetchContent)
    FetchContent_Declare(graffodil
        GIT_REPOSITORY https://github.com/clintonthegeek/Graffodil.git
        GIT_TAG        ${WILDPALMS_GRAFFODIL_GIT_TAG}
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(graffodil)
endif()

# Build pilot-link first
add_subdirectory(lib)

# Main source library
add_subdirectory(src)

# Testing support
option(BUILD_TESTS "Build the test suite" ON)

if(BUILD_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

# Main executable
qt_add_executable(wildpalms
    WIN32 MACOSX_BUNDLE
    src/main.cpp
)

target_link_libraries(wildpalms
    PRIVATE
        Qt::Core
        Qt::Widgets
        KF6::CalendarCore
        KF6::CoreAddons
        KF6::I18n
        KF6::XmlGui
        KF6::WidgetsAddons
        KF6::ConfigCore
        KF6::ConfigWidgets
        KF6::Crash
        KF6::Notifications
        KF6::StatusNotifierItem
        WildPalmsCore
        WildPalmsRuntime  # Phase J Task 7: palmruntime.cpp uses CalendarCollection_WP
)

# Install target
install(TARGETS wildpalms
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

# Install desktop file and icon for AppImage/Linux desktop integration
install(FILES data/ca.vibekoder.wildpalms.desktop
    DESTINATION share/applications
)
install(FILES data/ca.vibekoder.wildpalms.svg
    DESTINATION share/icons/hicolor/scalable/apps
)
install(FILES data/wildpalmsui.rc
    DESTINATION ${KDE_INSTALL_KXMLGUIDIR}/wildpalms
)

# === SDK: CMake config-file package ===

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/WildPalmsConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion
)

configure_package_config_file(
    cmake/WildPalmsConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/WildPalmsConfig.cmake
    INSTALL_DESTINATION lib/cmake/WildPalms
)

install(EXPORT WildPalmsTargets
    FILE WildPalmsTargets.cmake
    NAMESPACE WildPalms::
    DESTINATION lib/cmake/WildPalms
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/WildPalmsConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/WildPalmsConfigVersion.cmake
    DESTINATION lib/cmake/WildPalms
)
