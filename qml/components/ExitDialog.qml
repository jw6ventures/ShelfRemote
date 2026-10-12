import QtQuick
import ShelfRemote

// Modal "exit the app?" prompt, opened by Back from the sidebar while Home is
// showing. Left/Right move between the buttons, Enter picks one, Esc/Back or a
// click outside dismisses. Owned by the shell (like LibraryPicker) so the
// window-level Back shortcut can close it.
FocusScope {
    id: root
    anchors.fill: parent
    visible: opened
    z: 950

    property bool opened: false
    // Emitted when the prompt closes without exiting.
    signal dismissed()

    function open() {
        opened = true;
        // Start on Cancel: one stray Enter should not close the app.
        cancelBtn.forceActiveFocus();
    }
    function cancel() {
        opened = false;
        root.dismissed();
    }

    Keys.onEscapePressed: root.cancel()
    Keys.onBackPressed: root.cancel()

    // Scrim. Clicking outside the panel dismisses.
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.6
        MouseArea { anchors.fill: parent; onClicked: root.cancel() }
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: Math.max(480, buttons.width + Theme.spacingLarge * 2)
        height: panelCol.height + Theme.spacingLarge * 2
        radius: Theme.radius
        color: Theme.surface
        border.width: 1
        border.color: Theme.surfaceAlt

        // Swallow clicks so they don't fall through to the dismiss scrim.
        MouseArea { anchors.fill: parent }

        Column {
            id: panelCol
            x: Theme.spacingLarge
            y: Theme.spacingLarge
            width: parent.width - Theme.spacingLarge * 2
            spacing: Theme.spacingLarge

            Text {
                text: "Exit ShelfRemote?"
                color: Theme.textPrimary
                font.pixelSize: Theme.fontHeader
                font.bold: true
            }

            Row {
                id: buttons
                anchors.right: parent.right
                spacing: Theme.spacing

                FocusButton {
                    id: cancelBtn
                    text: "Cancel"
                    KeyNavigation.right: exitBtn
                    onClicked: root.cancel()
                }
                FocusButton {
                    id: exitBtn
                    text: "Exit"
                    accentColor: Theme.danger
                    KeyNavigation.left: cancelBtn
                    // aboutToQuit flushes and closes any playback session.
                    onClicked: Qt.quit()
                }
            }
        }
    }
}
