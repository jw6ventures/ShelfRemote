import QtQuick
import ShelfRemote

// Modal overlay for switching the active Audiobookshelf library. Fills the shell,
// dims it, and shows a centred panel listing Backend.libraries. Up/Down move,
// Enter selects (Backend.selectLibrary), Esc/Back or a click outside dismisses.
FocusScope {
    id: root
    anchors.fill: parent
    visible: opened
    z: 900

    property bool opened: false
    // Emitted after a library is chosen (Backend.selectLibrary already called).
    signal selected(string libraryId)
    // Emitted when the picker is dismissed without a change.
    signal dismissed()

    function indexOfCurrent() {
        var libs = Backend.libraries;
        for (var i = 0; i < libs.length; ++i)
            if (libs[i].id === Backend.currentLibraryId) return i;
        return 0;
    }
    function glyph(mediaType) {
        return mediaType === "podcast" ? "🎙  " : mediaType === "book" ? "📚  " : "";
    }

    function open() {
        opened = true;
        libList.currentIndex = indexOfCurrent();
        libList.forceActiveFocus();
    }
    function close() { opened = false; }

    function choose(libraryId) {
        Backend.selectLibrary(libraryId);
        close();
        root.selected(libraryId);
    }
    function cancel() {
        close();
        root.dismissed();
    }

    // Scrim. Clicking outside the panel dismisses.
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.6
        MouseArea { anchors.fill: parent; onClicked: root.cancel() }
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: 480
        height: panelCol.height + Theme.spacingLarge * 2
        radius: Theme.radius
        color: Theme.surface
        border.width: 1
        border.color: Theme.surfaceAlt

        // Swallow clicks so they don't fall through to the dismiss scrim.
        MouseArea { anchors.fill: parent }

        Column {
            id: panelCol
            x: Theme.spacingLarge
            y: Theme.spacingLarge
            width: parent.width - Theme.spacingLarge * 2
            spacing: Theme.spacing

            Text {
                text: "Switch Library"
                color: Theme.textPrimary
                font.pixelSize: Theme.fontHeader
                font.bold: true
            }

            ListView {
                id: libList
                width: parent.width
                height: Math.min(contentHeight, 64 * 6)
                clip: true
                focus: true
                keyNavigationEnabled: true
                spacing: Theme.spacingSmall
                boundsBehavior: Flickable.StopAtBounds
                model: Backend.libraries

                Keys.onEscapePressed: root.cancel()
                Keys.onBackPressed: root.cancel()

                delegate: Item {
                    required property var modelData
                    required property int index
                    width: libList.width
                    height: 64
                    focus: ListView.isCurrentItem

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radius
                        color: parent.activeFocus ? Theme.accent
                              : modelData.id === Backend.currentLibraryId ? Theme.surfaceAlt
                              : "transparent"
                        border.width: parent.activeFocus ? Theme.focusBorder : 0
                        border.color: Theme.focusRing

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacing
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spacing
                            elide: Text.ElideRight
                            text: root.glyph(modelData.mediaType) + modelData.name
                            font.pixelSize: Theme.fontBody
                            color: parent.parent.activeFocus ? "#ffffff" : Theme.textPrimary
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: { libList.currentIndex = index; root.choose(modelData.id); }
                    }
                    Keys.onReturnPressed: root.choose(modelData.id)
                    Keys.onEnterPressed: root.choose(modelData.id)
                }
            }
        }
    }
}
