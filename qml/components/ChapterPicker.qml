import QtQuick
import ShelfRemote

// Modal chapter list for the current book, opened from Now Playing. Opens on the
// chapter playing now; Up/Down move, Enter jumps there, Esc/Back or a click
// outside dismisses. Owned by the shell (like LibraryPicker) so the window-level
// Back shortcut can close it instead of popping the screen underneath.
FocusScope {
    id: root
    anchors.fill: parent
    visible: opened
    z: 900

    property bool opened: false
    // Emitted when the picker closes, whether a chapter was chosen or not.
    signal closed()

    function fmtTime(s) {
        s = Math.max(0, Math.floor(s));
        var h = Math.floor(s / 3600);
        var m = Math.floor((s % 3600) / 60);
        var sec = s % 60;
        function p(n) { return (n < 10 ? "0" : "") + n; }
        return h > 0 ? (h + ":" + p(m) + ":" + p(sec)) : (m + ":" + p(sec));
    }

    function open() {
        if (Playback.chapters.length === 0)
            return;
        opened = true;
        chapterList.currentIndex = Math.max(0, Playback.chapterIndex);
        chapterList.positionViewAtIndex(chapterList.currentIndex, ListView.Center);
        chapterList.forceActiveFocus();
    }
    function close() {
        opened = false;
        root.closed();
    }
    function choose(index) {
        Playback.seekGlobal(Playback.chapters[index].start);
        close();
    }
    function cancel() { close(); }

    // Nothing left to pick from once the session ends underneath the picker.
    Connections {
        target: Playback
        function onActiveChanged() { if (!Playback.active && root.opened) root.close(); }
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
        width: Math.min(720, parent.width - Theme.spacingLarge * 2)
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
                width: parent.width
                text: "Chapters"
                color: Theme.textPrimary
                font.pixelSize: Theme.fontHeader
                font.bold: true
            }

            ListView {
                id: chapterList
                width: parent.width
                // Up to eight rows, and never taller than the window allows.
                height: Math.min(contentHeight, 64 * 8,
                                 root.height - Theme.spacingLarge * 6 - Theme.fontHeader)
                clip: true
                focus: true
                keyNavigationEnabled: true
                highlightMoveDuration: 0
                spacing: Theme.spacingSmall
                boundsBehavior: Flickable.StopAtBounds
                model: root.opened ? Playback.chapters : []

                Keys.onEscapePressed: root.cancel()
                Keys.onBackPressed: root.cancel()

                delegate: Item {
                    id: row
                    required property var modelData
                    required property int index
                    width: chapterList.width
                    height: 56
                    focus: ListView.isCurrentItem
                    readonly property bool playingNow: index === Playback.chapterIndex

                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radius
                        color: row.activeFocus ? Theme.accent
                              : row.playingNow ? Theme.surfaceAlt : "transparent"
                        border.width: row.activeFocus ? Theme.focusBorder : 0
                        border.color: Theme.focusRing

                        Text {
                            id: startLabel
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spacing
                            text: root.fmtTime(row.modelData.start)
                            font.pixelSize: Theme.fontSmall
                            color: row.activeFocus ? "#e8f1ff" : Theme.textMuted
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacing
                            anchors.right: startLabel.left
                            anchors.rightMargin: Theme.spacing
                            elide: Text.ElideRight
                            text: (row.playingNow ? "▶  " : "")
                                  + (row.modelData.title || ("Chapter " + (row.index + 1)))
                            font.pixelSize: Theme.fontBody
                            color: row.activeFocus ? "#ffffff" : Theme.textPrimary
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: root.choose(row.index)
                    }
                    Keys.onReturnPressed: root.choose(row.index)
                    Keys.onEnterPressed: root.choose(row.index)
                }
            }
        }
    }
}
