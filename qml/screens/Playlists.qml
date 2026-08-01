import QtQuick
import ShelfRemote

// Read-only playlist browser. Audiobookshelf returns expanded, ordered playlist
// items, so selecting an item can start the same book/episode playback flow used
// elsewhere in the app without another metadata request.
FocusScope {
    id: root
    focus: true

    signal playRequested(var entry)
    signal requestSidebar()

    readonly property var selectedEntries:
        playlistList.currentItem ? playlistList.currentItem.playlistEntries : []
    readonly property string selectedName:
        playlistList.currentItem ? playlistList.currentItem.playlistName : ""

    function enterItems() {
        if (itemList.count > 0) {
            if (itemList.currentIndex < 0)
                itemList.currentIndex = 0;
            itemList.forceActiveFocus();
        }
    }

    Component.onCompleted: {
        Backend.refreshPlaylists();
        playlistList.forceActiveFocus();
    }
    onActiveFocusChanged: if (activeFocus &&
                              !playlistList.activeFocus &&
                              !itemList.activeFocus) {
        playlistList.forceActiveFocus();
    }

    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacingLarge
        spacing: Theme.spacing

        Text {
            text: "Playlists"
            color: Theme.textPrimary
            font.pixelSize: Theme.fontTitle
            font.bold: true
        }

        Row {
            width: parent.width
            height: parent.height - Theme.fontTitle - Theme.spacing
            spacing: Theme.spacing

            Rectangle {
                width: Math.min(390, parent.width * 0.36)
                height: parent.height
                radius: Theme.radius
                color: Theme.surface

                ListView {
                    id: playlistList
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSmall
                    model: Backend.playlists
                    focus: true
                    clip: true
                    spacing: Theme.spacingSmall
                    keyNavigationEnabled: true
                    boundsBehavior: Flickable.StopAtBounds

                    Keys.onLeftPressed: function(event) {
                        root.requestSidebar();
                        event.accepted = true;
                    }
                    Keys.onRightPressed: function(event) {
                        root.enterItems();
                        event.accepted = true;
                    }
                    Keys.onReturnPressed: root.enterItems()
                    Keys.onEnterPressed: root.enterItems()

                    delegate: Item {
                        id: playlistDelegate
                        required property string name
                        required property string description
                        required property int itemCount
                        required property var entries
                        required property int index

                        readonly property string playlistName: name
                        readonly property var playlistEntries: entries

                        width: playlistList.width
                        height: description === "" ? 78 : 102
                        focus: ListView.isCurrentItem

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radius
                            color: playlistDelegate.activeFocus
                                   ? Theme.accent : Theme.surfaceAlt
                            border.width: playlistDelegate.activeFocus
                                          ? Theme.focusBorder : 0
                            border.color: Theme.focusRing

                            Column {
                                anchors.fill: parent
                                anchors.margins: Theme.spacing
                                spacing: 3

                                Text {
                                    width: parent.width
                                    text: playlistDelegate.name
                                    color: playlistDelegate.activeFocus
                                           ? "#ffffff" : Theme.textPrimary
                                    font.pixelSize: Theme.fontBody
                                    font.bold: true
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    text: playlistDelegate.itemCount === 1
                                          ? "1 item"
                                          : playlistDelegate.itemCount + " items"
                                    color: playlistDelegate.activeFocus
                                           ? "#e8f1ff" : Theme.textMuted
                                    font.pixelSize: Theme.fontSmall
                                }
                                Text {
                                    width: parent.width
                                    visible: text !== ""
                                    text: playlistDelegate.description
                                    color: playlistDelegate.activeFocus
                                           ? "#e8f1ff" : Theme.textMuted
                                    font.pixelSize: Theme.fontSmall
                                    elide: Text.ElideRight
                                }
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                playlistList.currentIndex = playlistDelegate.index;
                                playlistList.forceActiveFocus();
                            }
                            onDoubleClicked: root.enterItems()
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - Theme.spacingLarge
                    visible: Backend.playlists.count === 0
                    text: "No playlists in this library."
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontBody
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                }
            }

            Rectangle {
                width: parent.width - x
                height: parent.height
                radius: Theme.radius
                color: Theme.surface

                Column {
                    anchors.fill: parent
                    anchors.margins: Theme.spacing
                    spacing: Theme.spacingSmall

                    Text {
                        width: parent.width
                        text: root.selectedName
                        color: Theme.textPrimary
                        font.pixelSize: Theme.fontHeader
                        font.bold: true
                        elide: Text.ElideRight
                    }

                    ListView {
                        id: itemList
                        width: parent.width
                        height: parent.height - Theme.fontHeader - Theme.spacingSmall
                        model: root.selectedEntries
                        clip: true
                        spacing: Theme.spacingSmall
                        keyNavigationEnabled: true
                        boundsBehavior: Flickable.StopAtBounds
                        onModelChanged: currentIndex = count > 0 ? 0 : -1

                        Keys.onLeftPressed: function(event) {
                            playlistList.forceActiveFocus();
                            event.accepted = true;
                        }
                        Keys.onReturnPressed: if (currentIndex >= 0)
                            root.playRequested(root.selectedEntries[currentIndex])
                        Keys.onEnterPressed: if (currentIndex >= 0)
                            root.playRequested(root.selectedEntries[currentIndex])

                        delegate: Item {
                            id: itemDelegate
                            required property var modelData
                            required property int index

                            property string coverUrl: ""
                            function refreshCover() {
                                coverUrl = Covers.localUrl(
                                    modelData.coverId, 120, 120);
                            }

                            width: itemList.width
                            height: 104
                            focus: ListView.isCurrentItem
                            Component.onCompleted: refreshCover()
                            Connections {
                                target: Covers
                                function onCoverReady(readyId, fileUrl) {
                                    if (readyId === itemDelegate.modelData.coverId)
                                        itemDelegate.coverUrl = fileUrl;
                                }
                            }

                            Rectangle {
                                anchors.fill: parent
                                radius: Theme.radius
                                color: itemDelegate.activeFocus
                                       ? Theme.accent : Theme.surfaceAlt
                                border.width: itemDelegate.activeFocus
                                              ? Theme.focusBorder : 0
                                border.color: Theme.focusRing

                                Row {
                                    anchors.fill: parent
                                    anchors.margins: Theme.spacingSmall
                                    spacing: Theme.spacing

                                    Rectangle {
                                        width: 64
                                        height: 88
                                        radius: Theme.radius
                                        color: Theme.background
                                        clip: true

                                        Image {
                                            anchors.fill: parent
                                            source: itemDelegate.coverUrl
                                            fillMode: Image.PreserveAspectCrop
                                            asynchronous: true
                                        }
                                    }

                                    Column {
                                        anchors.verticalCenter: parent.verticalCenter
                                        width: parent.width - 64 - Theme.spacing
                                        spacing: 5

                                        Text {
                                            width: parent.width
                                            text: (itemDelegate.index + 1) + ". " +
                                                  itemDelegate.modelData.title
                                            color: itemDelegate.activeFocus
                                                   ? "#ffffff" : Theme.textPrimary
                                            font.pixelSize: Theme.fontBody
                                            font.bold: true
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            width: parent.width
                                            visible: text !== ""
                                            text: itemDelegate.modelData.author
                                            color: itemDelegate.activeFocus
                                                   ? "#e8f1ff" : Theme.textMuted
                                            font.pixelSize: Theme.fontSmall
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            text: itemDelegate.modelData.kind === "episode"
                                                  ? "Podcast episode" : "Audiobook"
                                            color: itemDelegate.activeFocus
                                                   ? "#e8f1ff" : Theme.textMuted
                                            font.pixelSize: Theme.fontSmall
                                        }
                                    }
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: {
                                    itemList.currentIndex = itemDelegate.index;
                                    itemList.forceActiveFocus();
                                    root.playRequested(itemDelegate.modelData);
                                }
                            }
                            Keys.onReturnPressed:
                                root.playRequested(itemDelegate.modelData)
                            Keys.onEnterPressed:
                                root.playRequested(itemDelegate.modelData)
                        }
                    }

                }

                Text {
                    anchors.centerIn: parent
                    width: parent.width - Theme.spacingLarge
                    visible: Backend.playlists.count > 0 &&
                             itemList.count === 0
                    text: "This playlist is empty."
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontBody
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
