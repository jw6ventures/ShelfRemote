import QtQuick
import ShelfRemote

// A remote-navigable on-screen keyboard. A FocusScope with a single focusable
// child that handles all keys centrally (arrows move the highlighted key, Enter
// types). Right at the right edge emits moveRight() so the host can hand focus to
// the results grid; the grid hands focus back on its left edge.
FocusScope {
    id: kb
    property string text: ""
    signal accepted(string text)
    signal moveRight()
    signal moveLeft()

    readonly property int cols: 10
    property int cur: 0

    readonly property var keys: {
        var flat = [];
        var rows = ["ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ0123", "456789"];
        for (var r = 0; r < rows.length; ++r)
            for (var c = 0; c < rows[r].length; ++c) flat.push(rows[r][c]);
        flat.push("␣"); flat.push("⌫"); flat.push("CLR"); flat.push("GO");
        return flat;
    }

    readonly property int keySize: 62
    readonly property int gap: Theme.spacingSmall
    implicitWidth: cols * keySize + (cols - 1) * gap
    implicitHeight: 60 + Theme.spacing + Math.ceil(keys.length / cols) * (keySize + gap)

    function press(k) {
        if (k === "␣") text += " ";
        else if (k === "⌫") text = text.slice(0, -1);
        else if (k === "CLR") text = "";
        else if (k === "GO") accepted(text);
        else text += k;
    }

    Item {
        id: keypad
        anchors.fill: parent
        focus: true

        Keys.onLeftPressed: function(e) {
            if (kb.cur % kb.cols === 0) kb.moveLeft();
            else kb.cur = kb.cur - 1;
            e.accepted = true;
        }
        Keys.onUpPressed: kb.cur = Math.max(0, kb.cur - kb.cols)
        Keys.onDownPressed: kb.cur = Math.min(kb.keys.length - 1, kb.cur + kb.cols)
        Keys.onRightPressed: function(e) {
            if (kb.cur % kb.cols === kb.cols - 1 || kb.cur === kb.keys.length - 1)
                kb.moveRight();
            else
                kb.cur = Math.min(kb.keys.length - 1, kb.cur + 1);
            e.accepted = true;
        }
        Keys.onReturnPressed: kb.press(kb.keys[kb.cur])
        Keys.onEnterPressed: kb.press(kb.keys[kb.cur])

        Column {
            anchors.fill: parent
            spacing: Theme.spacing

            Rectangle {
                width: parent.width; height: 56; radius: Theme.radius; color: Theme.surfaceAlt
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.left: parent.left; anchors.leftMargin: Theme.spacing
                    text: kb.text.length ? kb.text : "Type to search…"
                    color: kb.text.length ? Theme.textPrimary : Theme.textMuted
                    font.pixelSize: Theme.fontBody
                }
            }

            Grid {
                columns: kb.cols
                spacing: kb.gap
                Repeater {
                    model: kb.keys
                    delegate: Rectangle {
                        required property string modelData
                        required property int index
                        width: kb.keySize
                        height: kb.keySize
                        radius: Theme.radius
                        // Only show the highlight while the keyboard has focus, so
                        // the last key doesn't stay lit after focus moves away.
                        property bool highlighted: index === kb.cur && kb.activeFocus
                        color: highlighted ? Theme.accent : Theme.surface
                        border.width: highlighted ? Theme.focusBorder : 0
                        border.color: Theme.focusRing
                        Text {
                            anchors.centerIn: parent
                            text: modelData
                            color: parent.highlighted ? "#ffffff" : Theme.textPrimary
                            font.pixelSize: (modelData.length > 1) ? Theme.fontSmall : Theme.fontBody
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: { kb.cur = index; keypad.forceActiveFocus(); kb.press(modelData); }
                        }
                    }
                }
            }
        }
    }
}
