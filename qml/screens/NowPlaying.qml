import QtQuick
import ShelfRemote

// Now Playing: large cover, title/chapter, transport, speed + sleep timer.
FocusScope {
    id: root
    focus: true
    // Lets the shell recognise this screen on top of the stack.
    objectName: "nowPlaying"
    // Asks the shell to open the chapter picker (it owns the overlay so that Back
    // closes the picker rather than this screen).
    signal requestChapters()

    property string coverUrl: ""
    function refreshCover() { coverUrl = Covers.localUrl(Playback.itemId, 400, 640); }

    // Bookmarks for the current item (jump / delete list below the transport).
    property var marks: []
    function refreshBookmarks() { marks = Bookmarks.forItem(Playback.itemId); }
    // Removing a bookmark rebuilds the list, destroying the focused row with it;
    // focus then fell to the screen itself, where no arrow key did anything. Land
    // on the row that took its place (or the one above, or the Bookmark button).
    function removeBookmark(index, time) {
        Bookmarks.remove(Playback.itemId, time);
        Qt.callLater(function() {
            if (bmRep.count > 0)
                bmRep.itemAt(Math.min(index, bmRep.count - 1)).forceActiveFocus();
            else
                bookmarkBtn.forceActiveFocus();
        });
    }
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

    // The cover, transport, buttons and bookmark list add up to more than a 720p
    // screen can show, and a bookmark list has no fixed length at all: centred in a
    // plain Item, the bottom rows simply fell off the screen while still taking
    // focus. Centred while it fits, scrolled (following the focus) when it does not.
    ScrollArea {
        id: page
        anchors.fill: parent
        contentHeight: column.height + Theme.spacingLarge * 2

        Column {
            id: column
            width: Math.min(900, page.width - Theme.spacingLarge * 2)
            x: (page.width - width) / 2
            y: Math.max(Theme.spacingLarge, (page.height - height) / 2)
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
                text: Playback.author
                visible: text.length > 0
                color: Theme.textMuted
                font.pixelSize: Theme.fontBody
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }

            // Where in the book this is, and the way into the chapter list. It used
            // to replace the author line, and offered no way to pick a chapter
            // other than stepping through them one at a time.
            FocusButton {
                id: chapterBtn
                visible: Playback.chapters.length > 0
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(implicitWidth, parent.width)
                text: {
                    var n = Playback.chapters.length;
                    var i = Playback.chapterIndex;
                    if (i < 0 || i >= n)
                        return "Chapters (" + n + ")";
                    var left = (Playback.chapters[i].end - Playback.position)
                               / Math.max(0.01, Playback.speed);
                    return "Ch " + (i + 1) + " of " + n
                           + (Playback.chapterTitle ? "  ·  " + Playback.chapterTitle : "")
                           + "  ·  " + root.fmtTime(left) + " left";
                }
                KeyNavigation.down: transport.playButton
                onClicked: root.requestChapters()
            }

            TransportBar {
                id: transport
                width: parent.width
                navDown: speedBtn
                navUp: chapterBtn.visible ? chapterBtn : null
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
                    onClicked: Playback.setSpeed(AppSettings.nextRate(Playback.speed))
                }
                FocusButton {
                    id: sleepBtn
                    // The countdown itself lives in Playback (survives leaving this
                    // screen); this button only reflects and cycles it.
                    text: Playback.sleepAtChapterEnd ? "Sleep: end of chapter"
                          : Playback.sleepMinutes > 0 ? ("Sleep: " + root.fmtTime(Playback.sleepRemaining))
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
                    // Many remotes have Menu but no Delete key; say which works.
                    text: "Bookmarks   ·   Menu or Delete removes the selected one"
                    width: parent.width
                    elide: Text.ElideRight
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
                        Keys.onMenuPressed: root.removeBookmark(index, modelData.time)
                        Keys.onDeletePressed: root.removeBookmark(index, modelData.time)
                    }
                }
            }
        }
    }
}
