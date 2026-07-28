import QtQuick
import QtQuick.Controls.Basic
import ShelfRemote

// A large, remote-friendly button with an always-visible focus ring. No
// hover-only behaviour: focus is the primary affordance.
Button {
    id: control

    property color accentColor: Theme.accent

    font.pixelSize: Theme.fontBody
    padding: Theme.spacing
    leftPadding: Theme.spacingLarge
    rightPadding: Theme.spacingLarge

    background: Rectangle {
        radius: Theme.radius
        color: control.down ? Qt.darker(control.accentColor, 1.2)
              : control.activeFocus ? control.accentColor : Theme.surfaceAlt
        border.width: control.activeFocus ? Theme.focusBorder : 0
        border.color: Theme.focusRing
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.activeFocus || control.down ? "#ffffff" : Theme.textPrimary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    Keys.onReturnPressed: control.clicked()
    Keys.onEnterPressed: control.clicked()
}
