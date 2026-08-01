import QtQuick
import QtQuick.Controls.Basic
import ShelfRemote

// Home: personalized shelves stacked vertically. The outer ListView handles
// Up/Down between shelves; each ShelfRow handles Left/Right within a shelf.
FocusScope {
    id: root
    focus: true
    signal itemActivated(var entry)
    signal requestSidebar()
    // The horizontal cursor is shared by all shelf rows so vertical movement
    // remains spatially predictable.
    property int preferredColumn: 0

    // True when Home is the visible screen in the StackView.
    readonly property bool isActiveScreen: StackView.status === StackView.Active

    Component.onCompleted: { Backend.refreshHome(); shelvesList.forceActiveFocus(); }
    // Re-assert focus whenever this screen becomes active again.
    onActiveFocusChanged: if (activeFocus) shelvesList.forceActiveFocus()

    ListView {
        id: shelvesList
        anchors.fill: parent
        anchors.topMargin: Theme.spacingLarge
        focus: true
        keyNavigationEnabled: true
        clip: true
        spacing: Theme.spacingLarge
        boundsBehavior: Flickable.StopAtBounds
        cacheBuffer: 2000
        model: Backend.homeShelves

        // Shelves arrive asynchronously after the screen is built. Once they
        // populate, put the cursor on the very first item if Home is the visible
        // screen (covers the cold-launch case where focus had nowhere to land).
        onCountChanged: if (count > 0 && root.isActiveScreen) {
            currentIndex = 0;
            root.preferredColumn = 0;
            shelvesList.forceActiveFocus();
        }

        header: Text {
            text: "Home"
            color: Theme.textPrimary
            font.pixelSize: Theme.fontTitle
            font.bold: true
            leftPadding: Theme.spacingLarge
            bottomPadding: Theme.spacing
        }

        delegate: ShelfRow {
            width: shelvesList.width
            label: model.label
            items: model.items
            preferredIndex: root.preferredColumn
            focus: ListView.isCurrentItem
            onIndexSelected: function(index) { root.preferredColumn = index; }
            onItemActivated: function(entry) { root.itemActivated(entry); }
            onAtLeftEdge: root.requestSidebar()
        }

        // Empty state.
        Text {
            anchors.centerIn: parent
            visible: shelvesList.count === 0
            text: "Nothing here yet."
            color: Theme.textMuted
            font.pixelSize: Theme.fontBody
        }
    }
}
