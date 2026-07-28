import QtQuick
import QtQuick.Dialogs
import ShelfRemote

// Settings: skip intervals, default rate, audio output, cover cache, debug log,
// server management, logout.
FocusScope {
    id: root
    focus: true
    Component.onCompleted: firstBtn.forceActiveFocus()

    Flickable {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        contentHeight: col.height
        clip: true

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
                        var next = AppSettings.defaultRate >= 2.0 ? 0.75 : AppSettings.defaultRate + 0.25;
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
                    KeyNavigation.down: signoutBtn
                    onClicked: logDialog.open()
                }
            }

            // Account
            FocusButton {
                id: signoutBtn
                text: "Sign out"
                accentColor: Theme.danger
                KeyNavigation.up: logBtn
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
