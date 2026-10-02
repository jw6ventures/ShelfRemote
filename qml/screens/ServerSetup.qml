import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import ShelfRemote

// Server connection + login. Drives Auth.checkServer, then shows local and/or
// OIDC options based on the server's advertised auth methods.
FocusScope {
    id: root
    focus: true

    Component.onCompleted: {
        urlField.forceActiveFocus();
        Servers.reload();
    }

    // Up/Down walk every visible, enabled control top to bottom, the saved
    // servers included. Pairwise KeyNavigation only ever pointed down, so once off
    // the URL field a remote could not get back to it, and nothing led past "Sign
    // in" to the OIDC button, Cancel, or the saved servers.
    function focusChain() {
        var items = [urlField, connectBtn, usernameField, passwordField, loginBtn,
                     oidcBtn, cancelBtn];
        for (var i = 0; i < savedRepeater.count; ++i)
            items[items.length] = savedRepeater.itemAt(i);
        return items.filter(function(it) { return it && it.visible && it.enabled; });
    }
    function moveFocus(step) {
        var chain = focusChain();
        var i = chain.indexOf(root.Window.activeFocusItem);
        var next = i < 0 ? 0 : i + step;
        if (next < 0 || next >= chain.length)
            return false;
        chain[next].forceActiveFocus();
        return true;
    }
    Keys.onUpPressed: function(event) { event.accepted = root.moveFocus(-1); }
    Keys.onDownPressed: function(event) { event.accepted = root.moveFocus(1); }

    // Connect disables itself while busy, and Cancel / the login fields come and
    // go, so the focused control can vanish under the user. Land somewhere useful
    // instead of on nothing: Cancel while busy, else the first login option.
    function recoverFocus() {
        if (focusChain().indexOf(root.Window.activeFocusItem) >= 0)
            return;
        var preferred = Auth.isBusy ? [cancelBtn] : [usernameField, oidcBtn, connectBtn, urlField];
        for (var i = 0; i < preferred.length; ++i) {
            if (preferred[i].visible && preferred[i].enabled) {
                preferred[i].forceActiveFocus();
                return;
            }
        }
    }
    Connections {
        target: Auth
        function onStateChanged() { Qt.callLater(root.recoverFocus); }
        function onAuthMethodsChanged() { Qt.callLater(root.recoverFocus); }
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(720, parent.width - Theme.spacingLarge * 2)
        spacing: Theme.spacingLarge

        Text {
            text: "ShelfRemote"
            color: Theme.textPrimary
            font.pixelSize: Theme.fontHuge
            font.bold: true
            Layout.alignment: Qt.AlignHCenter
        }
        Text {
            text: "A remote-friendly client for Audiobookshelf"
            color: Theme.textMuted
            font.pixelSize: Theme.fontBody
            Layout.alignment: Qt.AlignHCenter
        }

        // Server URL entry
        TextField {
            id: urlField
            Layout.fillWidth: true
            placeholderText: "https://audiobookshelf.example.com"
            text: "https://"
            font.pixelSize: Theme.fontBody
            color: Theme.textPrimary
            background: Rectangle {
                radius: Theme.radius
                color: Theme.surfaceAlt
                border.width: urlField.activeFocus ? Theme.focusBorder : 0
                border.color: Theme.focusRing
            }
            onAccepted: connectBtn.clicked()
        }

        FocusButton {
            id: connectBtn
            text: Auth.isBusy ? "Connecting…" : "Connect"
            enabled: !Auth.isBusy
            Layout.fillWidth: true
            onClicked: Auth.checkServer(urlField.text)
        }

        // Local login (shown once the server advertises "local").
        ColumnLayout {
            id: localFields
            Layout.fillWidth: true
            spacing: Theme.spacing
            visible: Auth.needsLogin && Auth.supportsLocal

            TextField {
                id: usernameField
                Layout.fillWidth: true
                placeholderText: "Username"
                font.pixelSize: Theme.fontBody
                color: Theme.textPrimary
                background: Rectangle {
                    radius: Theme.radius; color: Theme.surfaceAlt
                    border.width: usernameField.activeFocus ? Theme.focusBorder : 0
                    border.color: Theme.focusRing
                }
            }
            TextField {
                id: passwordField
                Layout.fillWidth: true
                placeholderText: "Password"
                echoMode: TextInput.Password
                font.pixelSize: Theme.fontBody
                color: Theme.textPrimary
                background: Rectangle {
                    radius: Theme.radius; color: Theme.surfaceAlt
                    border.width: passwordField.activeFocus ? Theme.focusBorder : 0
                    border.color: Theme.focusRing
                }
                onAccepted: loginBtn.clicked()
            }
            FocusButton {
                id: loginBtn
                text: "Sign in"
                Layout.fillWidth: true
                onClicked: Auth.loginLocal(usernameField.text, passwordField.text)
            }
        }

        // OIDC option.
        FocusButton {
            id: oidcBtn
            visible: Auth.needsLogin && Auth.supportsOidc
            text: Auth.oidcButtonText
            Layout.fillWidth: true
            accentColor: Theme.progress
            onClicked: Auth.beginOidc()
        }

        // Escape hatch out of a stuck/abandoned attempt (e.g. the OIDC browser
        // handoff) instead of waiting for the timeout.
        FocusButton {
            id: cancelBtn
            visible: Auth.isBusy
            text: "Cancel"
            Layout.fillWidth: true
            onClicked: Auth.cancelAuth()
        }

        Text {
            // Show any error message (invalid login, OIDC timeout, unreachable
            // server), not just those that drop into the hard Error state.
            visible: Auth.lastError !== ""
            text: Auth.lastError
            color: Theme.danger
            font.pixelSize: Theme.fontSmall
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
        }
    }

    // Saved servers quick-connect list.
    Column {
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: Theme.spacingLarge
        spacing: Theme.spacingSmall
        visible: Servers.count > 0

        Text {
            text: "Saved servers"
            color: Theme.textMuted
            font.pixelSize: Theme.fontSmall
        }
        Row {
            spacing: Theme.spacing
            Repeater {
                id: savedRepeater
                model: Servers
                delegate: FocusButton {
                    required property int index
                    required property string serverId
                    required property string name
                    required property string baseUrl
                    text: name
                    KeyNavigation.left: index > 0 ? savedRepeater.itemAt(index - 1) : null
                    KeyNavigation.right: index < savedRepeater.count - 1
                                         ? savedRepeater.itemAt(index + 1) : null
                    // Try to restore this server's stored session first; only fall
                    // back to a fresh discovery/login if there are no valid tokens.
                    onClicked: {
                        urlField.text = baseUrl;
                        if (!Auth.restoreSession(baseUrl, serverId))
                            Auth.checkServer(baseUrl);
                    }
                }
            }
        }
    }
}
