# Core library holds all non-QML C++ so it can be linked by both the app and the
# test targets.
add_library(shelfremote_core STATIC
    app/Application.cpp
    app/UriHandler.cpp
    app/AppConfig.cpp
    app/AppSettings.cpp
    app/Backend.cpp
    app/DebugLog.cpp
    net/ApiClient.cpp
    net/Endpoints.cpp
    auth/AuthManager.cpp
    auth/Pkce.cpp
    auth/TokenStore.cpp
    storage/Database.cpp
    storage/SecureStore.cpp
    storage/CoverCache.cpp
    server/ServerManager.cpp
    model/ShelfModel.cpp
    model/PlaylistModel.cpp
    model/LibraryItemsModel.cpp
    model/ProgressStore.cpp
    model/BookmarkStore.cpp
    player/MpvController.cpp
    player/PlaybackSession.cpp
    player/ProgressSyncer.cpp
    player/MprisAdapter.cpp
)

target_compile_definitions(shelfremote_core PUBLIC
    SHELFREMOTE_APP_ID="${SHELFREMOTE_APP_ID}"
    SHELFREMOTE_URI_SCHEME="${SHELFREMOTE_URI_SCHEME}"
    SHELFREMOTE_CLIENT_ID="${SHELFREMOTE_CLIENT_ID}"
    SHELFREMOTE_VERSION="${PROJECT_VERSION}"
)

target_include_directories(shelfremote_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(shelfremote_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Network Qt6::DBus Qt6::Sql
    PkgConfig::MPV
    OpenSSL::Crypto
)

# --- Application executable + QML module ------------------------------------
qt_add_executable(shelfremote main.cpp)

# The QML files live under ../qml/ with subfolders on disk. Flatten them into the
# module root via a per-file resource alias (all type names are unique), so
# loadFromModule("ShelfRemote","Main") resolves to qrc:/qt/qml/ShelfRemote/Main.qml.
set(SHELFREMOTE_QML_FILES
    ../qml/Main.qml
    ../qml/theme/Theme.qml
    ../qml/screens/ServerSetup.qml
    ../qml/screens/Home.qml
    ../qml/screens/Library.qml
    ../qml/screens/Playlists.qml
    ../qml/screens/Search.qml
    ../qml/screens/ItemDetails.qml
    ../qml/screens/NowPlaying.qml
    ../qml/screens/Settings.qml
    ../qml/components/FocusCard.qml
    ../qml/components/MediaGrid.qml
    ../qml/components/ShelfRow.qml
    ../qml/components/OnScreenKeyboard.qml
    ../qml/components/TransportBar.qml
    ../qml/components/NavSidebar.qml
    ../qml/components/FocusButton.qml
    ../qml/components/LibraryPicker.qml
)
foreach(qml_file ${SHELFREMOTE_QML_FILES})
    get_filename_component(qml_base ${qml_file} NAME)
    set_source_files_properties(${qml_file} PROPERTIES QT_RESOURCE_ALIAS ${qml_base})
endforeach()

# Theme.qml is a QML singleton (pragma Singleton).
set_source_files_properties(../qml/theme/Theme.qml PROPERTIES
    QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(shelfremote
    URI ShelfRemote
    VERSION 1.0
    QML_FILES ${SHELFREMOTE_QML_FILES}
)

target_link_libraries(shelfremote PRIVATE
    shelfremote_core
    Qt6::Quick Qt6::QuickControls2
)

set_target_properties(shelfremote PROPERTIES OUTPUT_NAME shelfremote)

install(TARGETS shelfremote
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
