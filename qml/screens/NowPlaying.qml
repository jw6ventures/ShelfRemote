import QtQuick
import ShelfRemote

// Now Playing: large cover, title/chapter, transport, speed + sleep timer.
FocusScope {
    id: root
    focus: true

    property string coverUrl: ""
    function refreshCover() { coverUrl = Covers.localUrl(Playback.itemId, 400, 640); }

    // Bookmarks for the current item (jump / delete list below the transport).
    property var marks: []
    function refreshBookmarks() { marks = Bookmarks.forItem(Playback.itemId); }
    function fmtTime(s) {
        s = Math.max(0, Math.floor(s));
        var h = Math.floor(s / 3600);
        var m = Math.floor((s % 3600) / 60);
        var sec = s % 60;
        function p(n) { return (n < 10 ? "0" : "") + n; }
        return h > 0 ? (h + ":" + p(m) + ":" + p(sec)) : (m + ":" + p(sec));
    }

    Component.onCompleted: {
        refreshCover();
        refreshBookmarks();
        transport.playButton.forceActiveFocus();
    }
    Connections {
        target: Playback
        function onMetadataChanged() { root.refreshCover(); root.refreshBookmarks(); }
    }
    Connections {
        target: Bookmarks
        function onChanged() { root.refreshBookmarks(); }
    }
    Connections {
        target: Covers
        function onCoverReady(id, url) { if (id === Playback.itemId) root.coverUrl = url; }
    }
    // When the session ends (stop, end of book, logout), don't strand the user on a
    // dead Now Playing screen — pop back, but only if we're still the top screen.
    Connections {
        target: Playback
        function onActiveChanged() {
            if (!Playback.active && root.StackView.view
                    && root.StackView.view.currentItem === root)
                root.StackView.view.pop();
        }
    }

    Column {
        anchors.centerIn: parent
        width: Math.min(900, parent.width - Theme.spacingLarge * 2)
        spacing: Theme.spacingLarge

        Rectangle {
            width: 260; height: 390
            radius: Theme.radius
            color: Theme.surfaceAlt
            clip: true
            anchors.horizontalCenter: parent.horizontalCenter
            Image {
                anchors.fill: parent
                source: root.coverUrl
                fillMode: Image.PreserveAspectCrop
                visible: root.coverUrl !== ""
            }
        }

        Text {
            text: Playback.title
            color: Theme.textPrimary
            font.pixelSize: Theme.fontTitle
            font.bold: true
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }
        Text {
            text: Playback.chapterTitle || Playback.author
            color: Theme.textMuted
            font.pixelSize: Theme.fontBody
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }

        TransportBar {
            id: transport
            width: parent.width
            navDown: speedBtn
        }

        // Speed + sleep timer row
        Row {
            spacing: Theme.spacingLarge
            anchors.horizontalCenter: parent.horizontalCenter

            FocusButton {
                id: speedBtn
                text: "Speed: " + Playback.speed.toFixed(2) + "×"
                KeyNavigation.right: sleepBtn
                KeyNavigation.up: transport.playButton
                KeyNavigation.down: bmRep.count > 0 ? bmRep.itemAt(0) : null
                onClicked: {
                    var next = Playback.speed + 0.25;
                    if (next > 3.0) next = 0.75;
                    Playback.setSpeed(next);
                }
            }
            FocusButton {
                id: sleepBtn
                // The countdown itself lives in Playback (survives leaving this
                // screen); this button only reflects and cycles it.
                text: Playback.sleepMinutes > 0 ? ("Sleep: " + Playback.sleepMinutes + "m")
                                                : "Sleep timer"
                KeyNavigation.left: speedBtn
                KeyNavigation.right: bookmarkBtn
                KeyNavigation.up: transport.playButton
                KeyNavigation.down: bmRep.count > 0 ? bmRep.itemAt(0) : null
                onClicked: Playback.cycleSleepTimer()
            }
            FocusButton {
                id: bookmarkBtn
                text: "＋ Bookmark"
                KeyNavigation.left: sleepBtn
                KeyNavigation.right: stopBtn
                KeyNavigation.up: transport.playButton
                KeyNavigation.down: bmRep.count > 0 ? bmRep.itemAt(0) : null
                onClicked: {
                    var title = (Playback.chapterTitle && Playback.chapterTitle.length)
                        ? Playback.chapterTitle : root.fmtTime(Playback.position);
                    Bookmarks.add(Playback.itemId, Playback.position, title);
                }
            }
            FocusButton {
                id: stopBtn
                text: "Stop"
                accentColor: Theme.danger
                KeyNavigation.left: bookmarkBtn
                KeyNavigation.up: transport.playButton
                KeyNavigation.down: bmRep.count > 0 ? bmRep.itemAt(0) : null
                onClicked: Playback.stopAndClose()
            }
        }

        // Bookmarks: Enter jumps, Menu/Delete removes. Only shown when the current
        // item has any.
        Column {
            id: bookmarksCol
            visible: root.marks.length > 0
            width: parent.width
            spacing: Theme.spacing

            Text {
                text: "Bookmarks"
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
            }
            Repeater {
                id: bmRep
                model: root.marks
                FocusButton {
                    required property int index
                    required property var modelData
                    width: bookmarksCol.width
                    text: root.fmtTime(modelData.time)
                          + (modelData.title ? "   —   " + modelData.title : "")
                    KeyNavigation.up: index > 0 ? bmRep.itemAt(index - 1) : bookmarkBtn
                    KeyNavigation.down: index < root.marks.length - 1
                                        ? bmRep.itemAt(index + 1) : null
                    onClicked: Playback.seekGlobal(modelData.time)
                    Keys.onMenuPressed: Bookmarks.remove(Playback.itemId, modelData.time)
                    Keys.onDeletePressed: Bookmarks.remove(Playback.itemId, modelData.time)
                }
            }
        }
    }
}
