import QtQuick
import ShelfRemote

// Search: on-screen keyboard on the left, results grid on the right.
FocusScope {
    id: root
    focus: true
    signal itemActivated(string itemId)
    signal requestSidebar()

    Component.onCompleted: {
        // The results model outlives this screen: a fresh, empty keyboard would
        // otherwise sit beside the hits for whatever was searched last time.
        Backend.clearSearch();
        keyboard.forceActiveFocus();
    }

    // Drives the status line: a search in flight, or the last query that came
    // back (so an empty grid can say "no results" rather than look broken).
    property bool searching: false
    property string answeredQuery: ""
    function runSearch(text) {
        if (text.trim().length >= 2) {
            searching = true;
            Backend.search(text);
        } else {
            searching = false;
            answeredQuery = "";
            Backend.clearSearch(); // emptied/too short: don't leave stale results
        }
    }
    Connections {
        target: Backend
        function onSearchFinished(query, ok) {
            root.searching = false;
            root.answeredQuery = ok ? query.trim() : "";
        }
    }

    // Search-as-you-type: debounce keystrokes so a close-enough partial query
    // returns results without needing to press GO. GO still triggers immediately.
    Timer {
        id: searchDebounce
        interval: 350
        onTriggered: root.runSearch(keyboard.text)
    }

    Row {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        spacing: Theme.spacingLarge

        OnScreenKeyboard {
            id: keyboard
            width: implicitWidth
            onTextChanged: searchDebounce.restart()
            onAccepted: function(text) { root.runSearch(text); results.forceActiveFocus(); }
            onMoveRight: results.forceActiveFocus()
            onMoveLeft: root.requestSidebar()
        }

        Column {
            width: parent.width - keyboard.width - Theme.spacingLarge
            height: parent.height
            spacing: Theme.spacing

            Text {
                id: resultsLabel
                text: "Results"
                color: Theme.textPrimary
                font.pixelSize: Theme.fontHeader
                font.bold: true
            }
            Text {
                id: statusLine
                visible: text.length > 0
                width: parent.width
                elide: Text.ElideRight
                color: Theme.textMuted
                font.pixelSize: Theme.fontBody
                text: root.searching && Backend.searchResults.count === 0 ? "Searching…"
                      : !root.searching && root.answeredQuery !== ""
                        && Backend.searchResults.count === 0
                        ? "No results for “" + root.answeredQuery + "”"
                      : ""
            }
            MediaGrid {
                id: results
                width: parent.width
                // A 26px font is taller than 26px once line spacing is counted,
                // so subtracting the font size left the last row clipped.
                height: parent.height - resultsLabel.height - Theme.spacing
                        - (statusLine.visible ? statusLine.height + Theme.spacing : 0)
                itemsModel: Backend.searchResults
                onItemActivated: function(id) { root.itemActivated(id); }
                onAtLeftEdge: keyboard.forceActiveFocus()
            }
        }
    }
}
