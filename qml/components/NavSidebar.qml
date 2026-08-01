import QtQuick
import ShelfRemote

// Left navigation rail. A FocusScope so forceActiveFocus() lands on the list.
// Up/Down move between entries, Enter activates, Right jumps into the content.
// When the server exposes more than one library, a switcher row sits above the
// section list; Up from the top section reaches it, Down/Enter operate it.
FocusScope {
    id: bar
    property alias currentName: bar._current
    property string _current: "home"
    signal navigate(string name)
    signal enterContent()
    signal openLibraryPicker()

    width: 220

    // Callers use this (not forceActiveFocus) to return focus to the rail, so it
    // always lands on the section list even after the library row was visited.
    function focusSidebar() { list.forceActiveFocus(); }

    readonly property var entries: [
        { name: "home",     label: "Home" },
        { name: "library",  label: "Library" },
        { name: "playlists", label: "Playlists" },
        { name: "search",   label: "Search" },
        { name: "settings", label: "Settings" }
    ]

    // Only worth showing a switcher when there is more than one library.
    readonly property bool hasMultipleLibraries: Backend.libraries.length > 1
    // Reads Backend.libraries + currentLibraryId, so it re-evaluates on either change.
    readonly property string currentLibraryLabel: {
        var libs = Backend.libraries;
        for (var i = 0; i < libs.length; ++i) {
            if (libs[i].id === Backend.currentLibraryId) {
                var mt = libs[i].mediaType;
                var g = mt === "podcast" ? "🎙  " : mt === "book" ? "📚  " : "";
                return g + libs[i].name;
            }
        }
        return "";
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.surface
    }

    // Library switcher row.
    Item {
        id: libRow
        visible: bar.hasMultipleLibraries
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: Theme.spacingLarge
        anchors.leftMargin: Theme.spacingSmall
        anchors.rightMargin: Theme.spacingSmall
        height: visible ? 84 : 0

        Text {
            id: libCaption
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing
            text: "LIBRARY"
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }

        Rectangle {
            anchors.top: libCaption.bottom
            anchors.topMargin: Theme.spacingSmall
            anchors.left: parent.left
            anchors.right: parent.right
            height: 52
            radius: Theme.radius
            color: libRow.activeFocus ? Theme.accent : Theme.surfaceAlt
            border.width: libRow.activeFocus ? Theme.focusBorder : 0
            border.color: Theme.focusRing

            Text {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacing
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacing
                elide: Text.ElideRight
                text: bar.currentLibraryLabel
                font.pixelSize: Theme.fontBody
                color: libRow.activeFocus ? "#ffffff" : Theme.textPrimary
            }
        }

        MouseArea { anchors.fill: parent; onClicked: bar.openLibraryPicker() }
        Keys.onReturnPressed: bar.openLibraryPicker()
        Keys.onEnterPressed: bar.openLibraryPicker()
        Keys.onDownPressed: list.forceActiveFocus()
        Keys.onRightPressed: bar.enterContent()
    }

    ListView {
        id: list
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.top: libRow.visible ? libRow.bottom : parent.top
        anchors.topMargin: libRow.visible ? Theme.spacing : Theme.spacingLarge * 2
        model: bar.entries
        focus: true
        keyNavigationEnabled: true
        spacing: Theme.spacingSmall

        Keys.onRightPressed: bar.enterContent()
        // At the top of the section list, Up reaches the library switcher row.
        Keys.onUpPressed: function(event) {
            if (list.currentIndex <= 0 && libRow.visible) {
                libRow.forceActiveFocus();
                event.accepted = true;
            } else {
                event.accepted = false;
            }
        }

        delegate: Item {
            required property var modelData
            required property int index
            width: list.width
            height: 64
            focus: ListView.isCurrentItem

            Rectangle {
                anchors.fill: parent
                anchors.margins: Theme.spacingSmall
                radius: Theme.radius
                color: parent.activeFocus ? Theme.accent
                      : bar._current === modelData.name ? Theme.surfaceAlt : "transparent"
                border.width: parent.activeFocus ? Theme.focusBorder : 0
                border.color: Theme.focusRing

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacing
                    text: modelData.label
                    font.pixelSize: Theme.fontBody
                    color: parent.parent.activeFocus ? "#ffffff" : Theme.textPrimary
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: { list.currentIndex = index; bar.navigate(modelData.name); }
            }
            Keys.onReturnPressed: bar.navigate(modelData.name)
            Keys.onEnterPressed: bar.navigate(modelData.name)
        }
    }
}
