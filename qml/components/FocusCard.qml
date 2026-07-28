import QtQuick
import ShelfRemote

// A cover-art card. The outer item is a fixed CELL that reserves `focusPad`
// around the visible content, so the focus zoom grows *within* the cell and
// never clips the poster or pushes the author label off screen. Progress and the
// completed state come from the shared Progress store, keyed by item id.
Item {
    id: card

    property string itemId
    property string title
    property string author

    // Cover source may differ from the navigation id: a series card shows a
    // book's cover, an author card shows the author portrait (coverKind "author").
    property string coverId: itemId
    property string coverKind: "item"

    signal activated()

    width: Theme.cardCellW
    height: Theme.cardCellH
    z: activeFocus ? 1 : 0

    Keys.onReturnPressed: card.activated()
    Keys.onEnterPressed: card.activated()

    // --- progress from the store ---
    property real progress: 0.0
    property bool finished: false
    function refreshProgress() {
        progress = Progress.fraction(itemId);
        finished = Progress.isFinished(itemId);
    }
    onItemIdChanged: refreshProgress()
    Component.onCompleted: { refreshProgress(); refreshCover(); }
    Connections {
        target: Progress
        function onChanged() { card.refreshProgress(); }
    }

    // --- cover from the cache ---
    property string coverUrl: ""
    function refreshCover() {
        coverUrl = coverKind === "author" ? Covers.authorImage(coverId, 400, 640)
                                          : Covers.localUrl(coverId, 400, 640);
    }
    onCoverIdChanged: refreshCover()
    Connections {
        target: Covers
        function onCoverReady(readyId, fileUrl) {
            if (readyId === card.coverId) card.coverUrl = fileUrl;
        }
    }

    // Content that zooms on focus, inset by focusPad so growth stays in the cell.
    Item {
        id: content
        x: Theme.focusPad
        y: Theme.focusPad
        width: Theme.cardWidth
        height: Theme.cardHeight + Theme.cardLabelH
        transformOrigin: Item.Center
        scale: card.activeFocus
               ? Math.min(1 + 2 * Theme.focusPad / width, 1 + 2 * Theme.focusPad / height)
               : 1.0
        Behavior on scale { NumberAnimation { duration: Theme.animFast } }

        Rectangle {
            id: art
            width: Theme.cardWidth
            height: Theme.cardHeight
            radius: Theme.radius
            color: Theme.surfaceAlt
            clip: true
            border.width: card.activeFocus ? Theme.focusBorder : 0
            border.color: Theme.focusRing

            Image {
                anchors.fill: parent
                source: card.coverUrl
                fillMode: Image.PreserveAspectCrop
                visible: card.coverUrl !== ""
                asynchronous: true
            }
            Text {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.spacing
                visible: card.coverUrl === ""
                text: card.title
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
            }

            // Completed badge.
            Rectangle {
                visible: card.finished
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: Theme.spacingSmall
                width: 40; height: 40; radius: 20
                color: Theme.progress
                Text { anchors.centerIn: parent; text: "✓"; color: "#0e1013"
                       font.pixelSize: 26; font.bold: true }
            }
            // In-progress bar (hidden when finished).
            Rectangle {
                visible: !card.finished && card.progress > 0
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: 8
                color: "#88000000"
                Rectangle {
                    height: parent.height
                    width: parent.width * Math.min(1, card.progress)
                    color: Theme.progress
                }
            }
        }

        Column {
            anchors.top: art.bottom
            anchors.topMargin: Theme.spacingSmall
            width: Theme.cardWidth
            spacing: 2
            Text {
                text: card.title; color: Theme.textPrimary
                font.pixelSize: Theme.fontSmall; elide: Text.ElideRight; width: parent.width
            }
            Text {
                text: card.author; color: Theme.textMuted
                font.pixelSize: Theme.fontSmall - 2; elide: Text.ElideRight; width: parent.width
                visible: card.author !== ""
            }
        }
    }

    MouseArea {
        anchors.fill: content
        hoverEnabled: true
        onEntered: card.forceActiveFocus()
        onClicked: { card.forceActiveFocus(); card.activated(); }
    }
}
