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
    // Home owns this value and shares it across every shelf. That makes an
    // Up/Down move preserve the visible horizontal column instead of reviving a
    // stale, shelf-local cursor position.
    property int preferredIndex: 0
    // Carries the whole entity map (kind/itemId/episodeId/…) so the shell can
    // route by type: books/podcasts open details, episodes play, series/authors
    // filter the library.
    signal itemActivated(var entry)
    signal atLeftEdge()
    signal indexSelected(int index)

    function syncPreferredIndex() {
        if (list.count > 0)
            list.currentIndex = Math.min(Math.max(0, preferredIndex), list.count - 1);
        else
            list.currentIndex = -1;
    }

    onPreferredIndexChanged: syncPreferredIndex()
    onItemsChanged: Qt.callLater(syncPreferredIndex)
    Component.onCompleted: syncPreferredIndex()

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
        onCountChanged: row.syncPreferredIndex()

        delegate: FocusCard {
            required property var modelData
            required property int index
            itemId: modelData.itemId
            title: modelData.title
            author: modelData.author
            coverId: modelData.coverId
            coverKind: modelData.coverKind
            focus: ListView.isCurrentItem
            // Pointer focus must update the ListView cursor too. Otherwise the
            // card can look active while the next remote Left/Right starts from a
            // different logical index.
            onActiveFocusChanged: if (activeFocus && list.currentIndex !== index) {
                list.currentIndex = index;
                row.indexSelected(index);
            }
            onActivated: row.itemActivated(modelData)
        }

        Keys.onLeftPressed: function(e) {
            if (currentIndex <= 0) row.atLeftEdge();
            else {
                currentIndex = currentIndex - 1;
                row.indexSelected(currentIndex);
            }
            e.accepted = true;
        }
        Keys.onRightPressed: function(e) {
            if (currentIndex < count - 1) {
                currentIndex = currentIndex + 1;
                row.indexSelected(currentIndex);
            }
            e.accepted = true;
        }
        Keys.onReturnPressed: if (currentIndex >= 0) row.itemActivated(items[currentIndex])
        Keys.onEnterPressed: if (currentIndex >= 0) row.itemActivated(items[currentIndex])
    }
}
