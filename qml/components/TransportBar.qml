import QtQuick
import ShelfRemote

// Large transport controls bound to Playback. The five buttons are arrow-
// navigable (Left/Right). `navDown` lets the host wire Down to the next row, and
// `playButton` is exposed so the host can wire that row's Up back here.
Column {
    id: transport
    spacing: Theme.spacing
    // Live-bound to the persisted user preference (Settings screen writes it).
    property int skipSeconds: AppSettings.skipSeconds
    property Item navDown: null
    property alias playButton: playBtn

    function fmt(s) {
        if (isNaN(s) || s < 0) s = 0;
        var h = Math.floor(s / 3600), m = Math.floor((s % 3600) / 60), sec = Math.floor(s % 60);
        function p(n){ return (n < 10 ? "0" : "") + n; }
        return (h > 0 ? h + ":" : "") + p(m) + ":" + p(sec);
    }

    Rectangle {
        width: transport.width; height: 10; radius: 5; color: Theme.surfaceAlt
        Rectangle {
            height: parent.height; radius: 5
            width: Playback.duration > 0 ? parent.width * (Playback.position / Playback.duration) : 0
            color: Theme.progress
        }
    }

    Row {
        width: transport.width
        Text { text: transport.fmt(Playback.position); color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
        Item { width: transport.width - 240; height: 1 }
        Text { text: "-" + transport.fmt(Playback.duration - Playback.position)
               color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
    }

    Row {
        spacing: Theme.spacingLarge
        anchors.horizontalCenter: parent.horizontalCenter

        FocusButton {
            id: prevChapBtn
            text: "⏮ Chapter"
            KeyNavigation.right: back30; KeyNavigation.down: transport.navDown
            onClicked: Playback.previousChapter()
        }
        FocusButton {
            id: back30
            text: "« " + transport.skipSeconds + "s"
            KeyNavigation.left: prevChapBtn; KeyNavigation.right: playBtn; KeyNavigation.down: transport.navDown
            onClicked: Playback.skip(-transport.skipSeconds)
        }
        FocusButton {
            id: playBtn
            focus: true
            text: Playback.playing ? "⏸ Pause" : "▶ Play"
            KeyNavigation.left: back30; KeyNavigation.right: fwd30; KeyNavigation.down: transport.navDown
            onClicked: Playback.togglePlayPause()
        }
        FocusButton {
            id: fwd30
            text: transport.skipSeconds + "s »"
            KeyNavigation.left: playBtn; KeyNavigation.right: nextChapBtn; KeyNavigation.down: transport.navDown
            onClicked: Playback.skip(transport.skipSeconds)
        }
        FocusButton {
            id: nextChapBtn
            text: "Chapter ⏭"
            KeyNavigation.left: fwd30; KeyNavigation.down: transport.navDown
            onClicked: Playback.nextChapter()
        }
    }
}
