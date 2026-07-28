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
            spacing: Theme.spacing
            Text {
                text: "Library"
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.bold: true
                anchors.verticalCenter: parent.verticalCenter
            }
            Item { width: Theme.spacingLarge; height: 1 }
            FocusButton {
                text: "Sort: " + root.sortOptions[root.sortIndex].label
                onClicked: {
                    root.sortIndex = (root.sortIndex + 1) % root.sortOptions.length;
                    Backend.browse(root.sortOptions[root.sortIndex].key, root.sortDesc, "");
                }
            }
            FocusButton {
                text: root.sortDesc ? "▼ Desc" : "▲ Asc"
                onClicked: {
                    root.sortDesc = !root.sortDesc;
                    Backend.browse(root.sortOptions[root.sortIndex].key, root.sortDesc, "");
                }
            }
        }

        MediaGrid {
            id: grid
            width: parent.width
            height: parent.height - Theme.fontTitle - Theme.spacing * 3
            itemsModel: Backend.libraryItems
            onItemActivated: function(id) { root.itemActivated(id); }
            onAtLeftEdge: root.requestSidebar()
        }
    }
}
