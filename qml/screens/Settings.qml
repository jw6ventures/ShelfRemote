import QtQuick
import QtQuick.Dialogs
import ShelfRemote

// Settings: skip intervals, default rate, audio output, cover cache, debug log,
// server management, logout.
FocusScope {
    id: root
    focus: true
    signal requestSidebar()
    Component.onCompleted: firstBtn.forceActiveFocus()
    // Every other top-level screen hands Left to the rail; here it went nowhere and
    // only Esc got back out. Nothing on this screen uses Left itself.
    Keys.onLeftPressed: root.requestSidebar()

    // ScrollArea rather than a bare Flickable: the rows run past the bottom of a
    // short screen, and moving focus down a plain Flickable does not scroll it —
    // "Sign out" took focus while staying off screen.
    ScrollArea {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        contentHeight: col.height

        Column {
            id: col
            width: parent.width
            spacing: Theme.spacingLarge

            Text {
                text: "Settings"
                color: Theme.textPrimary
                font.pixelSize: Theme.fontTitle
                font.bold: true
            }

            // Skip interval
            Row {
                spacing: Theme.spacing
                Text {
                    text: "Skip interval"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontBody
                    anchors.verticalCenter: parent.verticalCenter
                    width: 260
                }
                FocusButton {
                    id: firstBtn
                    property var options: [10, 15, 30, 60]
                    text: AppSettings.skipSeconds + " seconds"
                    KeyNavigation.down: rateBtn
                    onClicked: {
                        var i = options.indexOf(AppSettings.skipSeconds);
                        AppSettings.skipSeconds = options[(i + 1) % options.length];
                    }
                }
            }

            // Default playback rate
            Row {
                spacing: Theme.spacing
                Text {
                    text: "Default playback rate"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontBody
                    anchors.verticalCenter: parent.verticalCenter
                    width: 260
                }
                FocusButton {
                    id: rateBtn
                    text: AppSettings.defaultRate.toFixed(2) + "×"
                    KeyNavigation.up: firstBtn
                    KeyNavigation.down: audioBtn
                    onClicked: {
                        var next = AppSettings.nextRate(AppSettings.defaultRate);
                        AppSettings.defaultRate = next;   // persisted; applied to new sessions
                        Playback.setSpeed(next);          // and to the current one right now
                    }
                }
            }

            // Audio output
            Row {
                spacing: Theme.spacing
                Text {
                    text: "Audio output"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontBody
                    anchors.verticalCenter: parent.verticalCenter
                    width: 260
                }
                FocusButton {
                    id: audioBtn
                    property var devices: Player.audioDevices()
                    function descFor(name) {
                        for (var i = 0; i < devices.length; ++i)
                            if (devices[i].name === name) return devices[i].description;
                        return name;   // device unplugged since it was chosen
                    }
                    text: descFor(AppSettings.audioDevice)
                    KeyNavigation.up: rateBtn
                    KeyNavigation.down: cacheBtn
                    onClicked: {
                        devices = Player.audioDevices();  // re-probe: sinks can appear later
                        if (devices.length === 0) return;
                        var idx = 0;
                        for (var i = 0; i < devices.length; ++i)
                            if (devices[i].name === AppSettings.audioDevice) { idx = i; break; }
                        var next = devices[(idx + 1) % devices.length];
                        AppSettings.audioDevice = next.name;  // persisted
                        Player.setAudioDevice(next.name);     // applied now
                    }
                }
            }

            // Cover cache
            Row {
                spacing: Theme.spacing
                Text {
                    text: "Cover cache"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontBody
                    anchors.verticalCenter: parent.verticalCenter
                    width: 260
                }
                FocusButton {
                    id: cacheBtn
                    // Held in a property so the label refreshes after a clear (a bare
                    // Covers.cacheSizeBytes() call would not re-evaluate on its own).
                    property real mb: Covers.cacheSizeBytes() / 1048576
                    text: "Clear (" + mb.toFixed(1) + " MB)"
                    KeyNavigation.up: audioBtn
                    KeyNavigation.down: logBtn
                    onClicked: {
                        Covers.clearCache();
                        mb = Covers.cacheSizeBytes() / 1048576;
                    }
                }
            }

            // Debug log
            Row {
                spacing: Theme.spacing
                Text {
                    text: "Debug log"
                    color: Theme.textPrimary
                    font.pixelSize: Theme.fontBody
                    anchors.verticalCenter: parent.verticalCenter
                    width: 260
                }
                FocusButton {
                    id: logBtn
                    text: "Save debug log…"
                    KeyNavigation.up: cacheBtn
                    KeyNavigation.down: storageBtn
                    onClicked: logDialog.open()
                }
            }

            // Where saved sign-ins are kept. The keyring can stay locked on a box
            // that logs in automatically, which signs the user out at every boot;
            // keeping the key on the device trades some protection for not
            // depending on it, so the choice is the user's and needs a second press.
            Column {
                visible: Auth.canChooseSignInStorage
                spacing: Theme.spacingSmall
                width: parent.width

                Row {
                    spacing: Theme.spacing
                    Text {
                        text: "Saved sign-in"
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontBody
                        anchors.verticalCenter: parent.verticalCenter
                        width: 260
                    }
                    FocusButton {
                        id: storageBtn
                        // Armed by the first press; the second one switches.
                        property bool armed: false
                        property string result: ""
                        text: armed ? (Auth.signInOnDevice ? "Press again to use the keyring"
                                                           : "Press again to keep on this device")
                                    : (Auth.signInOnDevice ? "This device" : "System keyring")
                        KeyNavigation.up: logBtn
                        KeyNavigation.down: signoutBtn
                        onActiveFocusChanged: if (!activeFocus) armed = false
                        onClicked: {
                            if (!armed) {
                                armed = true;
                                result = "";
                                disarm.restart();
                                return;
                            }
                            armed = false;
                            result = Auth.setSignInOnDevice(!Auth.signInOnDevice);
                        }
                        Timer { id: disarm; interval: 8000; onTriggered: storageBtn.armed = false }
                    }
                }
                Text {
                    x: 260 + Theme.spacing
                    width: parent.width - x
                    wrapMode: Text.WordWrap
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                    text: storageBtn.result !== "" ? storageBtn.result
                        : storageBtn.armed
                          ? (Auth.signInOnDevice
                             ? "Moves the key into the system keyring. Saved servers other than this one may need you to sign in again."
                             : "Moves the key out of the keyring into a file in ShelfRemote's data folder. Sign-ins no longer depend on the keyring being unlocked, but anyone who can use this computer account could read them.")
                        : (Auth.signInOnDevice
                           ? "Encrypted with a key kept in ShelfRemote's own data folder. Works without the keyring; anyone who can use this computer account could read it."
                           : "Encrypted with a key held by the system keyring. If this computer logs in automatically, the keyring may stay locked and ask you to sign in again.")
                }
            }

            // Account
            FocusButton {
                id: signoutBtn
                text: "Sign out"
                accentColor: Theme.danger
                KeyNavigation.up: storageBtn
                onClicked: Auth.logout()
            }

            Text {
                text: "ShelfRemote " + appVersion
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
                topPadding: Theme.spacingLarge
            }
        }
    }

    // Under Flatpak this routes through the xdg file-chooser portal, which grants
    // write access to the chosen file — no extra sandbox permission needed.
    FileDialog {
        id: logDialog
        title: "Save debug log"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Log files (*.log)", "All files (*)"]
        defaultSuffix: "log"
        selectedFile: "shelfremote.log"
        onAccepted: DebugLog.saveTo(selectedFile)
    }
}
