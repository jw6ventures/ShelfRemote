cmake_minimum_required(VERSION 3.21)

project(ShelfRemote
    VERSION 0.1.1
    DESCRIPTION "A remote-friendly Linux client for Audiobookshelf"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Reverse-DNS application identity. Kept in one place; consumed by the C++ code,
# the MPRIS name, and the packaging files.
set(SHELFREMOTE_APP_ID "com.jw6ventures.ShelfRemote")
set(SHELFREMOTE_URI_SCHEME "jw6-shelfremote")
set(SHELFREMOTE_CLIENT_ID "ShelfRemote")

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2 Network DBus Sql)

qt_standard_project_setup(REQUIRES 6.5)

# --- libmpv -----------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)

# --- OpenSSL (PKCE / token encryption helpers) ------------------------------
find_package(OpenSSL REQUIRED)

add_subdirectory(src)

# --- Tests ------------------------------------------------------------------
option(SHELFREMOTE_BUILD_TESTS "Build unit tests" ON)
if(SHELFREMOTE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# --- Install packaging metadata --------------------------------------------
install(FILES packaging/${SHELFREMOTE_APP_ID}.desktop
    DESTINATION ${CMAKE_INSTALL_PREFIX}/share/applications)
install(FILES packaging/${SHELFREMOTE_APP_ID}.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_PREFIX}/share/metainfo)
install(FILES packaging/icons/${SHELFREMOTE_APP_ID}.svg
    DESTINATION ${CMAKE_INSTALL_PREFIX}/share/icons/hicolor/scalable/apps)
