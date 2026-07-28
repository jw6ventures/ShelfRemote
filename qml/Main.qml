import QtQuick
import QtQuick.Controls.Basic
import ShelfRemote

ApplicationWindow {
    id: window
    visible: true
    width: 1280
    height: 720
    visibility: Window.Maximized
    title: "ShelfRemote"
    color: Theme.background

    // --- Global remote-control key contract -------------------------------
    // These fire regardless of focused element (unless the child accepts first).
    Shortcut { sequence: "Media Play"; onActivated: Playback.togglePlayPause() }
    Shortcut { sequence: "Media Pause"; onActivated: Playback.pause() }
    Shortcut { sequence: "Toggle Media Play/Pause"; onActivated: Playback.togglePlayPause() }
    Shortcut { sequence: "Media Stop"; onActivated: Playback.stopAndClose() }
    Shortcut { sequence: "Media Next"; onActivated: Playback.nextChapter() }
    Shortcut { sequence: "Media Previous"; onActivated: Playback.previousChapter() }
    Shortcut { sequence: StandardKey.MoveToStartOfLine; onActivated: sidebar.navigate("home") }

    // Back navigation (Esc / Browser Back).
    Shortcut {
        sequences: [StandardKey.Cancel, StandardKey.Back]
        onActivated: {
            if (libraryPicker.opened) { libraryPicker.cancel(); return; }
            if (stack.depth > 1) stack.pop();
            else if (contentRoot.visible) sidebar.focusSidebar();
        }
    }

    // --- Auth gate --------------------------------------------------------
    Loader {
        id: gate
        anchors.fill: parent
        active: !Auth.isAuthenticated
        sourceComponent: serverSetupComp
        onActiveChanged: if (active) forceActiveFocus()
    }
    Component { id: serverSetupComp; ServerSetup {} }

    // --- Authenticated shell ---------------------------------------------
    Item {
        id: contentRoot
        anchors.fill: parent
        visible: Auth.isAuthenticated

        Row {
            anchors.fill: parent
            NavSidebar {
                id: sidebar
                height: parent.height
                currentName: "home"
                onEnterContent: if (stack.currentItem) stack.currentItem.forceActiveFocus()
                onOpenLibraryPicker: libraryPicker.open()
                onNavigate: function(name) {
                    sidebar.currentName = name;
                    stack.clear();
                    switch (name) {
                    case "home":     stack.push(homeComp); break;
                    case "library":  stack.push(libraryComp); break;
                    case "search":   stack.push(searchComp); break;
                    case "settings": stack.push(settingsComp); break;
                    }
                    if (stack.currentItem) stack.currentItem.forceActiveFocus();
                }
            }

            StackView {
                id: stack
                width: parent.width - sidebar.width
                height: parent.height
                initialItem: homeComp
            }
        }

        function openItem(itemId) {
            stack.push(detailsComp, { itemId: itemId });
            stack.forceActiveFocus();
        }
        function openNowPlaying() {
            stack.push(nowPlayingComp);
            stack.forceActiveFocus();
        }
        // Route a Home shelf card by its entity kind. Only books/podcasts have an
        // item detail page; episodes play directly; series/authors filter the grid.
        function activate(entry) {
            switch (entry.kind) {
            case "episode": Playback.playEpisode(entry.itemId, entry.episodeId); break;
            case "series":  Backend.browseSeries(entry.itemId); break;
            case "author":  Backend.browseAuthor(entry.itemId); break;
            default:        openItem(entry.itemId);
            }
        }

        Connections {
            target: Playback
            function onActiveChanged() { if (Playback.active) contentRoot.openNowPlaying(); }
        }
        Connections {
            target: Backend
            function onNavigateToLibrary() { sidebar.navigate("library"); }
        }

        // When login completes, move focus into the content (Home) rather than
        // leaving it on the now-hidden login screen.
        Connections {
            target: Auth
            function onIsAuthenticatedChanged() {
                if (Auth.isAuthenticated)
                    Qt.callLater(function() { if (stack.currentItem) stack.currentItem.forceActiveFocus(); });
            }
        }
    }

    Component { id: homeComp;       Home {
        onItemActivated: function(entry) { contentRoot.activate(entry); }
        onRequestSidebar: sidebar.focusSidebar()
    } }
    Component { id: libraryComp;    Library {
        onItemActivated: function(id) { contentRoot.openItem(id); }
        onRequestSidebar: sidebar.focusSidebar()
    } }
    Component { id: searchComp;     Search {
        onItemActivated: function(id) { contentRoot.openItem(id); }
        onRequestSidebar: sidebar.focusSidebar()
    } }
    Component { id: settingsComp;   Settings {} }
    Component { id: detailsComp;    ItemDetails {} }
    Component { id: nowPlayingComp; NowPlaying {} }

    // --- Library switcher overlay -----------------------------------------
    // Modal picker opened from the sidebar's library row. Selecting a library
    // lands on the refreshed Home; dismissing returns focus to the rail.
    LibraryPicker {
        id: libraryPicker
        onSelected: sidebar.navigate("home")
        onDismissed: sidebar.focusSidebar()
    }

    // --- Transient error toast --------------------------------------------
    // Surfaces playback/stream failures and backend request errors that would
    // otherwise fail silently.
    Rectangle {
        id: toast
        z: 1000
        visible: opacity > 0
        opacity: 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingLarge
        width: Math.min(toastText.implicitWidth + Theme.spacingLarge * 2, parent.width - Theme.spacingLarge * 2)
        height: toastText.implicitHeight + Theme.spacing * 2
        radius: Theme.radius
        color: Theme.danger
        Behavior on opacity { NumberAnimation { duration: 200 } }

        Text {
            id: toastText
            anchors.centerIn: parent
            width: parent.width - Theme.spacingLarge * 2
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            color: Theme.textPrimary
            font.pixelSize: Theme.fontBody
        }

        Timer { id: toastTimer; interval: 5000; onTriggered: toast.opacity = 0 }
        function show(message) {
            if (!message) return;
            toastText.text = message;
            opacity = 1;
            toastTimer.restart();
        }
    }
    Connections {
        target: Playback
        function onPlaybackError(message) { toast.show(message); }
    }
    Connections {
        target: Backend
        function onErrorOccurred(message) { toast.show(message); }
    }
}
