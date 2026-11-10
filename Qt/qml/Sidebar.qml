import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root
    property bool previewMode: false
    signal settingsRequested()
    color: colors.palette.base
    radius: 8
    clip: true
    Control { id: colors; visible: false }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        RowLayout {
            Label { text: root.previewMode ? qsTr("Preview") : qsTr("Explorer"); font.bold: true; font.pixelSize: 16; Layout.fillWidth: true }
            ToolButton {
                visible: !root.previewMode
                text: "\uE713"
                font.family: "Segoe Fluent Icons"
                Accessible.name: qsTr("Navigation settings")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Navigation settings")
                onClicked: root.settingsRequested()
            }
            ToolButton {
                visible: root.previewMode
                text: qsTr("Pin")
                checkable: true
                checked: appController.preview.pinned
                onClicked: appController.preview.pinned = checked
                Accessible.name: qsTr("Pin current preview")
            }
        }
        ColumnLayout {
            visible: !root.previewMode
            Layout.fillWidth: true
            Layout.fillHeight: true
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(navigationItems.implicitHeight, root.height * 0.65)
                clip: true
                contentWidth: availableWidth
                ColumnLayout {
                    id: navigationItems
                    width: parent.width
                    spacing: 2
                    ToolButton {
                        objectName: "quickAccessToggle"
                        visible: appController.navigation.visible
                        Layout.fillWidth: true
                        text: (appController.navigation.expanded ? "\u2304  " : "\u203A  ") + qsTr("QUICK ACCESS")
                        Accessible.name: qsTr("Quick Access")
                        Accessible.description: appController.navigation.expanded ? qsTr("Expanded") : qsTr("Collapsed")
                        onClicked: appController.navigation.expanded = !appController.navigation.expanded
                    }
                    Repeater {
                        model: appController.navigation.visible && appController.navigation.expanded ? appController.navigation.places : []
                        delegate: ItemDelegate {
                            required property var modelData
                            Layout.fillWidth: true
                            text: modelData.name
                            onClicked: appController.workspace.activePane.navigate(modelData.url)
                            height: 32
                        }
                    }
                    Label { text: qsTr("COLOR TAGS"); font.pixelSize: 10; font.letterSpacing: 1; opacity: 0.6; Layout.topMargin: 12 }
                    Repeater {
                        model: appController.tags.palette
                        delegate: ItemDelegate {
                            required property var modelData
                            objectName: "sidebar-tag-" + modelData.id
                            Layout.fillWidth: true
                            height: 32
                            highlighted: appController.workspace.activePane && appController.workspace.activePane.tagId === modelData.id
                            contentItem: Row {
                                spacing: 10
                                Rectangle {
                                    width: 10; height: 10; radius: 5; color: modelData.color
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Label { text: modelData.name; textFormat: Text.PlainText }
                            }
                            Accessible.name: qsTr("%1 tagged items").arg(modelData.name)
                            onClicked: appController.showTag(modelData.id)
                        }
                    }
                }
            }
            Label { text: qsTr("FOLDERS"); font.pixelSize: 10; font.letterSpacing: 1; opacity: 0.6; Layout.topMargin: 8 }
            TreeView {
                id: treeView
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: appController.tree
                clip: true
                columnWidthProvider: function(column) { return column === 0 ? width : 0 }
                ScrollBar.vertical: ScrollBar {}
                delegate: TreeViewDelegate {
                    id: treeDelegate
                    implicitHeight: 32
                    onClicked: appController.navigateTree(treeView.index(row, column))
                }
                Accessible.name: qsTr("Folder tree")
            }
        }
        ColumnLayout {
            visible: root.previewMode
            Layout.fillWidth: true
            Layout.fillHeight: true
            Label {
                Layout.fillWidth: true
                text: appController.preview.title
                font.bold: true
                wrapMode: Text.WrapAnywhere
                textFormat: Text.PlainText
            }
            Image {
                visible: appController.preview.kind === "image"
                Layout.fillWidth: true
                Layout.preferredHeight: 220
                source: visible ? appController.preview.url : ""
                sourceSize: Qt.size(640, 640)
                asynchronous: true
                fillMode: Image.PreserveAspectFit
            }
            Label {
                Layout.fillWidth: true
                text: appController.preview.details
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                opacity: 0.7
                font.pixelSize: 12
            }
            ScrollView {
                visible: appController.preview.kind === "text"
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    text: appController.preview.text
                    textFormat: TextEdit.PlainText
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    font.family: "Cascadia Mono"
                    font.pixelSize: 12
                    Accessible.name: qsTr("Text preview")
                }
            }
            Label {
                visible: appController.preview.kind === "none" || appController.preview.kind === "error"
                Layout.fillWidth: true
                text: appController.preview.kind === "error" ? appController.preview.error : qsTr("Select one file to preview it.\n\nImages and text stay local. Other files show their details.")
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                opacity: 0.6
            }
            Item { Layout.fillHeight: true; visible: appController.preview.kind !== "text" }
            Button {
                visible: appController.preview.kind !== "none"
                text: qsTr("Open in default app")
                Layout.fillWidth: true
                onClicked: appController.native.open(appController.preview.url)
            }
        }
    }
}
