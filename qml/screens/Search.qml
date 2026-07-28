import QtQuick
import ShelfRemote

// Search: on-screen keyboard on the left, results grid on the right.
FocusScope {
    id: root
    focus: true
    signal itemActivated(string itemId)
    signal requestSidebar()

    Component.onCompleted: keyboard.forceActiveFocus()

    // Search-as-you-type: debounce keystrokes so a close-enough partial query
    // returns results without needing to press GO. GO still triggers immediately.
    Timer {
        id: searchDebounce
        interval: 350
        onTriggered: {
            if (keyboard.text.trim().length >= 2)
                Backend.search(keyboard.text);
            else
                Backend.clearSearch(); // emptied/too short: don't leave stale results
        }
    }

    Row {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        spacing: Theme.spacingLarge

        OnScreenKeyboard {
            id: keyboard
            width: implicitWidth
            onTextChanged: searchDebounce.restart()
            onAccepted: function(text) { Backend.search(text); results.forceActiveFocus(); }
            onMoveRight: results.forceActiveFocus()
            onMoveLeft: root.requestSidebar()
        }

        Column {
            width: parent.width - keyboard.width - Theme.spacingLarge
            height: parent.height
            spacing: Theme.spacing

            Text {
                text: "Results"
                color: Theme.textPrimary
                font.pixelSize: Theme.fontHeader
                font.bold: true
            }
            MediaGrid {
                id: results
                width: parent.width
                height: parent.height - Theme.fontHeader - Theme.spacing
                itemsModel: Backend.searchResults
                onItemActivated: function(id) { root.itemActivated(id); }
                onAtLeftEdge: keyboard.forceActiveFocus()
            }
        }
    }
}
