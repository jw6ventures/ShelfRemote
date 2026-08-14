import QtQuick
import ShelfRemote

// A focus-navigable grid of cover cards. Emits itemActivated with the item id and
// atLeftEdge when Left is pressed in the first column (to reach the sidebar).
// Triggers lazy pagination when focus nears the end.
GridView {
    id: grid

    property var itemsModel
    signal itemActivated(string itemId)
    signal atLeftEdge()

    model: itemsModel
    cellWidth: Theme.cardCellW
    cellHeight: Theme.cardCellH
    clip: true
    focus: true
    keyNavigationEnabled: true
    cacheBuffer: cellHeight * 4

    highlightMoveDuration: Theme.animFast
    boundsBehavior: Flickable.StopAtBounds

    function cellsPerRow() { return Math.max(1, Math.floor(width / cellWidth)); }

    Keys.onLeftPressed: function(e) {
        if (currentIndex % cellsPerRow() === 0) { grid.atLeftEdge(); e.accepted = true; }
        else { moveCurrentIndexLeft(); e.accepted = true; }
    }

    delegate: FocusCard {
        itemId: model.itemId
        title: model.title
        author: model.author
        focus: GridView.isCurrentItem
        onFocusRequested: {
            grid.currentIndex = index;
            grid.forceActiveFocus();
        }
        onActivated: grid.itemActivated(model.itemId)
    }

    onCurrentIndexChanged: {
        if (itemsModel && itemsModel.hasMore && currentIndex >= count - cellsPerRow() * 2)
            itemsModel.loadMore();
    }
}
