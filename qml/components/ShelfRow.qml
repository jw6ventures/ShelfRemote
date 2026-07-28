import QtQuick
import ShelfRemote

// One labelled horizontal row of cards for the Home screen. FocusScope whose
// inner horizontal ListView takes focus when this row is current. Left/Right move
// within the row (Left at the first card jumps to the sidebar); Up/Down bubble to
// the outer vertical ListView to change rows; Enter activates.
FocusScope {
    id: row
    property string label
    property var items: []
    // Carries the whole entity map (kind/itemId/episodeId/…) so the shell can
    // route by type: books/podcasts open details, episodes play, series/authors
    // filter the library.
    signal itemActivated(var entry)
    signal atLeftEdge()

    width: parent ? parent.width : 0
    height: labelText.height + Theme.spacingSmall + list.height

    Text {
        id: labelText
        text: row.label
        color: Theme.textPrimary
        font.pixelSize: Theme.fontHeader
        font.bold: true
        leftPadding: Theme.spacingLarge
    }

    ListView {
        id: list
        anchors.top: labelText.bottom
        anchors.topMargin: Theme.spacingSmall
        width: row.width
        height: Theme.cardCellH
        orientation: ListView.Horizontal
        spacing: 0
        leftMargin: Theme.spacing
        rightMargin: Theme.spacing
        clip: true
        model: row.items
        focus: true
        keyNavigationEnabled: false   // we drive Left/Right ourselves
        boundsBehavior: Flickable.StopAtBounds
        highlightMoveDuration: Theme.animFast
        preferredHighlightBegin: Theme.spacingLarge
        preferredHighlightEnd: width - Theme.spacingLarge
        highlightRangeMode: ListView.ApplyRange

        delegate: FocusCard {
            required property var modelData
            required property int index
            itemId: modelData.itemId
            title: modelData.title
            author: modelData.author
            coverId: modelData.coverId
            coverKind: modelData.coverKind
            focus: ListView.isCurrentItem
            onActivated: row.itemActivated(modelData)
        }

        Keys.onLeftPressed: function(e) {
            if (currentIndex <= 0) row.atLeftEdge();
            else currentIndex = currentIndex - 1;
            e.accepted = true;
        }
        Keys.onRightPressed: function(e) {
            if (currentIndex < count - 1) currentIndex = currentIndex + 1;
            e.accepted = true;
        }
        Keys.onReturnPressed: if (currentIndex >= 0) row.itemActivated(items[currentIndex])
        Keys.onEnterPressed: if (currentIndex >= 0) row.itemActivated(items[currentIndex])
    }
}
