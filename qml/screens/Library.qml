import QtQuick
import ShelfRemote

// Library: full grid browse with a small sort control.
FocusScope {
    id: root
    focus: true
    signal itemActivated(string itemId)
    signal requestSidebar()

    property var sortOptions: [
        { label: "Title",   key: "media.metadata.title" },
        { label: "Author",  key: "media.metadata.authorName" },
        { label: "Added",   key: "addedAt" },
        { label: "Recent",  key: "media.metadata.publishedYear" }
    ]
    property int sortIndex: 0
    property bool sortDesc: false

    Component.onCompleted: {
        // Don't clobber an in-flight load (e.g. a series/author filter kicked off
        // just before this screen was pushed) with the default browse.
        if (Backend.libraryItems.count === 0 && !Backend.libraryItems.loading)
            Backend.browse(sortOptions[sortIndex].key, sortDesc, "");
        grid.forceActiveFocus();
    }

    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        spacing: Theme.spacing

        Row {
            id: header
            spacing: Theme.spacing
            Text {
                text: "Library"
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.bold: true
                anchors.verticalCenter: parent.verticalCenter
            }
            Item { width: Theme.spacingLarge; height: 1 }
            // Both controls are wired into the arrow-key chain: reachable only by
            // Tab or mouse, they did not exist for anyone driving this from a
            // remote. Up from the grid's first row comes back here.
            FocusButton {
                id: sortBtn
                text: "Sort: " + root.sortOptions[root.sortIndex].label
                KeyNavigation.right: orderBtn
                KeyNavigation.down: grid
                Keys.onLeftPressed: function(event) {
                    root.requestSidebar();
                    event.accepted = true;
                }
                onClicked: {
                    root.sortIndex = (root.sortIndex + 1) % root.sortOptions.length;
                    Backend.browse(root.sortOptions[root.sortIndex].key, root.sortDesc, "");
                }
            }
            FocusButton {
                id: orderBtn
                text: root.sortDesc ? "▼ Desc" : "▲ Asc"
                KeyNavigation.left: sortBtn
                KeyNavigation.down: grid
                onClicked: {
                    root.sortDesc = !root.sortDesc;
                    Backend.browse(root.sortOptions[root.sortIndex].key, root.sortDesc, "");
                }
            }
        }

        MediaGrid {
            id: grid
            width: parent.width
            // Measured off the header, not off its font size: the sort buttons
            // make that row far taller than fontTitle, so the grid was sized
            // against a number that had nothing to do with the space left.
            height: parent.height - header.height - Theme.spacing
            itemsModel: Backend.libraryItems
            onItemActivated: function(id) { root.itemActivated(id); }
            onAtLeftEdge: root.requestSidebar()
            // First row (or an empty grid): Up leaves for the sort controls.
            // Anywhere else it stays unaccepted so the grid moves the cursor.
            Keys.onUpPressed: function(event) {
                event.accepted = grid.currentIndex < grid.cellsPerRow();
                if (event.accepted)
                    sortBtn.forceActiveFocus();
            }
        }
    }
}
