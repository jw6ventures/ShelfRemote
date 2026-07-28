import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
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
            KeyNavigation.down: connectBtn
            onAccepted: connectBtn.clicked()
        }

        FocusButton {
            id: connectBtn
            text: Auth.isBusy ? "Connecting…" : "Connect"
            enabled: !Auth.isBusy
            Layout.fillWidth: true
            KeyNavigation.down: localFields.visible ? usernameField : oidcBtn
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
                KeyNavigation.down: passwordField
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
                KeyNavigation.down: loginBtn
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
                model: Servers
                delegate: FocusButton {
                    required property string serverId
                    required property string name
                    required property string baseUrl
                    text: name
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
