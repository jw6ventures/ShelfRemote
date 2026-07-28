import QtQuick
import ShelfRemote

// Item details: cover, metadata, listening progress, description, chapters, and
// Play/Resume. Play and Mark buttons are arrow-navigable (Left/Right).
FocusScope {
    id: root
    focus: true
    property string itemId
    property var item: ({})

    Component.onCompleted: {
        Backend.loadItem(itemId);
        refreshCover();
    }

    Connections {
        target: Backend
        function onItemLoaded(loaded) {
            if (loaded.id !== root.itemId) return;
            root.item = loaded;
            // Land focus on the primary action once the item's type is known
            // (the Play button for a book, the episode list for a podcast).
            if (!root._focused) {
                root._focused = true;
                if (root.isPodcast) episodeList.forceActiveFocus();
                else playBtn.forceActiveFocus();
            }
        }
    }

    readonly property var media: item.media ? item.media : ({})
    readonly property var meta: media.metadata ? media.metadata : ({})
    readonly property var progress: item.userMediaProgress ? item.userMediaProgress : ({})
    readonly property real totalSecs: media.duration ? media.duration : 0
    readonly property real doneSecs: progress.currentTime ? progress.currentTime : 0
    readonly property real pct: progress.progress ? progress.progress : 0
    readonly property bool hasProgress: pct > 0 || root.doneSecs > 0

    readonly property bool isPodcast: (item.mediaType || "") === "podcast"
    // Podcast episodes, newest first. Empty for books.
    readonly property var episodes: {
        var eps = (media.episodes && media.episodes.length) ? media.episodes.slice() : [];
        eps.sort(function(a, b) { return (b.publishedAt || 0) - (a.publishedAt || 0); });
        return eps;
    }
    // Guards the one-time focus hand-off in onItemLoaded.
    property bool _focused: false

    function clock(s) {
        if (isNaN(s) || s < 0) s = 0;
        var h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = Math.floor(s % 60);
        function p(n){ return (n < 10 ? "0" : "") + n; }
        return (h > 0 ? h + ":" : "") + p(m) + ":" + p(sec);
    }
    function human(s) {
        if (isNaN(s) || s < 0) s = 0;
        var h = Math.floor(s / 3600), m = Math.round((s % 3600) / 60);
        return (h > 0 ? h + "h " : "") + m + "m";
    }
    function startedDate(ms) {
        if (!ms || ms <= 0) return "";
        return new Date(ms).toLocaleDateString(Qt.locale(), Locale.LongFormat);
    }

    property string coverUrl: ""
    function refreshCover() { coverUrl = Covers.localUrl(itemId, 400, 640); }
    onItemIdChanged: refreshCover()
    Connections {
        target: Covers
        function onCoverReady(id, url) { if (id === root.itemId) root.coverUrl = url; }
    }

    Row {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        spacing: Theme.spacingLarge

        Rectangle {
            width: 320; height: 480; radius: Theme.radius; color: Theme.surfaceAlt; clip: true
            Image {
                anchors.fill: parent; source: root.coverUrl
                fillMode: Image.PreserveAspectCrop; visible: root.coverUrl !== ""
            }
        }

        Flickable {
            visible: !root.isPodcast
            width: parent.width - 320 - Theme.spacingLarge
            height: parent.height
            contentHeight: col.height
            clip: true

            Column {
                id: col
                width: parent.width
                spacing: Theme.spacing

                Text {
                    text: root.meta.title || root.item.title || ""
                    color: Theme.textPrimary; font.pixelSize: Theme.fontTitle; font.bold: true
                    wrapMode: Text.WordWrap; width: parent.width
                }
                Text {
                    text: (root.meta.authorName || "") +
                          (root.meta.narratorName ? "  ·  Narrated by " + root.meta.narratorName : "")
                    color: Theme.textMuted; font.pixelSize: Theme.fontBody; visible: text.length > 0
                }

                // --- Progress panel ---
                Rectangle {
                    visible: root.hasProgress || root.progress.isFinished
                    width: parent.width
                    height: progCol.height + Theme.spacing * 2
                    radius: Theme.radius
                    color: Theme.surface

                    Column {
                        id: progCol
                        x: Theme.spacing; y: Theme.spacing
                        width: parent.width - Theme.spacing * 2
                        spacing: Theme.spacingSmall

                        // progress bar
                        Rectangle {
                            width: parent.width; height: 10; radius: 5; color: Theme.surfaceAlt
                            Rectangle {
                                height: parent.height; radius: 5
                                width: parent.width * Math.min(1, root.progress.isFinished ? 1 : root.pct)
                                color: Theme.progress
                            }
                        }
                        Row {
                            width: parent.width
                            Text {
                                text: root.progress.isFinished
                                      ? "Finished"
                                      : Math.round(root.pct * 100) + "%  ·  "
                                        + root.clock(root.doneSecs) + " of " + root.clock(root.totalSecs)
                                color: Theme.textPrimary; font.pixelSize: Theme.fontSmall
                            }
                            Item { width: parent.width - 520; height: 1 }
                            Text {
                                visible: !root.progress.isFinished
                                text: root.human(root.totalSecs - root.doneSecs) + " remaining"
                                color: Theme.textMuted; font.pixelSize: Theme.fontSmall
                            }
                        }
                        Text {
                            visible: root.startedDate(root.progress.startedAt) !== ""
                            text: "Started " + root.startedDate(root.progress.startedAt)
                            color: Theme.textMuted; font.pixelSize: Theme.fontSmall
                        }
                    }
                }

                Row {
                    spacing: Theme.spacing
                    FocusButton {
                        id: playBtn
                        focus: true
                        text: (root.pct > 0 && root.pct < 1 && !root.progress.isFinished)
                              ? "▶ Resume" : "▶ Play"
                        KeyNavigation.right: markBtn
                        onClicked: Playback.playItem(root.itemId)
                    }
                    FocusButton {
                        id: markBtn
                        text: root.progress.isFinished ? "Mark unfinished" : "Mark finished"
                        KeyNavigation.left: playBtn
                        onClicked: Backend.markFinished(root.itemId, !root.progress.isFinished)
                    }
                }

                Text {
                    text: root.meta.description || ""
                    color: Theme.textPrimary; font.pixelSize: Theme.fontBody
                    wrapMode: Text.WordWrap; width: parent.width; visible: text.length > 0
                }

                Text {
                    text: "Chapters"; color: Theme.textPrimary
                    font.pixelSize: Theme.fontHeader; font.bold: true
                    visible: chapterRepeater.count > 0; topPadding: Theme.spacing
                }
                Repeater {
                    id: chapterRepeater
                    model: root.media.chapters ? root.media.chapters : []
                    delegate: Text {
                        required property var modelData
                        text: (modelData.title || "Chapter")
                        color: Theme.textMuted; font.pixelSize: Theme.fontSmall
                    }
                }
            }
        }

        // Podcast view: a scrollable, focus-navigable episode list. Enter on a
        // row plays that episode (which auto-opens Now Playing via Playback).
        ListView {
            id: episodeList
            visible: root.isPodcast
            width: parent.width - 320 - Theme.spacingLarge
            height: parent.height
            clip: true
            keyNavigationEnabled: true
            spacing: Theme.spacingSmall
            boundsBehavior: Flickable.StopAtBounds
            cacheBuffer: 2000
            model: root.episodes

            header: Column {
                width: episodeList.width
                spacing: Theme.spacing
                bottomPadding: Theme.spacing

                Text {
                    text: root.meta.title || root.item.title || ""
                    color: Theme.textPrimary; font.pixelSize: Theme.fontTitle; font.bold: true
                    wrapMode: Text.WordWrap; width: parent.width
                }
                Text {
                    text: root.meta.author || root.meta.authorName || ""
                    color: Theme.textMuted; font.pixelSize: Theme.fontBody
                    wrapMode: Text.WordWrap; width: parent.width; visible: text.length > 0
                }
                Text {
                    text: root.meta.description || ""
                    color: Theme.textPrimary; font.pixelSize: Theme.fontBody
                    wrapMode: Text.WordWrap; width: parent.width; visible: text.length > 0
                    maximumLineCount: 4; elide: Text.ElideRight
                }
                Text {
                    text: root.episodes.length + (root.episodes.length === 1 ? " episode" : " episodes")
                    color: Theme.textPrimary; font.pixelSize: Theme.fontHeader; font.bold: true
                    topPadding: Theme.spacing
                    visible: root.episodes.length > 0
                }
            }

            delegate: Item {
                id: epDelegate
                required property var modelData
                required property int index
                width: episodeList.width
                height: 88
                focus: ListView.isCurrentItem

                readonly property real epDuration: modelData.duration
                    ? modelData.duration
                    : (modelData.audioFile && modelData.audioFile.duration ? modelData.audioFile.duration : 0)

                Rectangle {
                    anchors.fill: parent
                    anchors.rightMargin: Theme.spacingSmall
                    radius: Theme.radius
                    color: epDelegate.activeFocus ? Theme.accent : Theme.surface
                    border.width: epDelegate.activeFocus ? Theme.focusBorder : 0
                    border.color: Theme.focusRing

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.leftMargin: Theme.spacing
                        anchors.rightMargin: Theme.spacing
                        spacing: Theme.spacingSmall

                        Text {
                            width: parent.width
                            text: epDelegate.modelData.title || "Episode"
                            color: epDelegate.activeFocus ? "#ffffff" : Theme.textPrimary
                            font.pixelSize: Theme.fontBody
                            elide: Text.ElideRight
                        }
                        Text {
                            width: parent.width
                            text: {
                                var d = root.startedDate(epDelegate.modelData.publishedAt);
                                var len = epDelegate.epDuration > 0 ? root.human(epDelegate.epDuration) : "";
                                return [d, len].filter(function(s){ return s.length > 0; }).join("  ·  ");
                            }
                            color: epDelegate.activeFocus ? "#e8f1ff" : Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                            visible: text.length > 0
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        episodeList.currentIndex = epDelegate.index;
                        Playback.playEpisode(root.itemId, epDelegate.modelData.id);
                    }
                }
                Keys.onReturnPressed: Playback.playEpisode(root.itemId, epDelegate.modelData.id)
                Keys.onEnterPressed: Playback.playEpisode(root.itemId, epDelegate.modelData.id)
            }

            // Empty state.
            Text {
                anchors.centerIn: parent
                visible: root.isPodcast && episodeList.count === 0
                text: "No episodes"
                color: Theme.textMuted
                font.pixelSize: Theme.fontBody
            }
        }
    }
}
