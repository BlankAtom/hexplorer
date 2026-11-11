import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HExplorer.Core

Control {
    id: root
    required property PaneController pane
    signal activated()
    implicitHeight: 40
    readonly property real minimumTabWidth: 120
    readonly property real tabWidth: Math.max(minimumTabWidth, tabList.width / Math.max(1, pane.tabs.length))
    objectName: "pane-tabs-" + pane.id
    padding: 0

    background: Rectangle {
        color: root.palette.window
        radius: 4
        Rectangle { width: parent.width; height: 1; color: root.palette.mid; opacity: 0.4 }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 4
        ListView {
            id: tabList
            objectName: "pane-tab-list-" + root.pane.id
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: ListView.Horizontal
            model: root.pane.tabs
            currentIndex: root.pane.activeTab
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            flickableDirection: Flickable.HorizontalFlick
            reuseItems: true
            ScrollBar.horizontal: ScrollBar {
                height: 4
                policy: tabList.contentWidth > tabList.width ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            }
            function revealActive() {
                if (count > 0) positionViewAtIndex(root.pane.activeTab, ListView.Contain)
            }
            onCurrentIndexChanged: Qt.callLater(revealActive)
            onCountChanged: Qt.callLater(revealActive)
            onWidthChanged: Qt.callLater(revealActive)
            delegate: Item {
                required property var modelData
                required property int index
                width: root.tabWidth
                height: tabList.height
                Button {
                    id: tabButton
                    objectName: "pane-tab-" + parent.modelData.id
                    anchors.fill: parent
                    anchors.margins: 2
                    rightPadding: 28
                    leftPadding: 8
                    checkable: true
                    checked: parent.index === root.pane.activeTab
                    text: parent.modelData.title
                    Accessible.name: qsTr("Tab: %1").arg(text)
                    contentItem: Label {
                        text: tabButton.text
                        color: tabButton.palette.buttonText
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 4
                        color: tabButton.checked ? root.palette.base
                              : tabButton.hovered ? root.palette.alternateBase : "transparent"
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            height: 2
                            radius: 1
                            visible: tabButton.checked
                            color: root.palette.highlight
                        }
                    }
                    onClicked: {
                        root.activated()
                        root.pane.switchTab(parent.index)
                    }
                    ToolTip.visible: hovered
                    ToolTip.delay: 600
                    ToolTip.text: parent.modelData.url.toString()
                    ToolButton {
                        objectName: "close-pane-tab-" + tabButton.parent.modelData.id
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        width: 24
                        height: 28
                        text: "\u00D7"
                        enabled: root.pane.tabs.length > 1
                        Accessible.name: qsTr("Close tab: %1").arg(tabButton.text)
                        onClicked: {
                            root.activated()
                            root.pane.closeTab(tabButton.parent.index)
                        }
                    }
                }
            }
            MouseArea {
                parent: tabList
                anchors.fill: parent
                acceptedButtons: Qt.NoButton
                onWheel: function(event) {
                    const pixels = event.pixelDelta.x !== 0 ? event.pixelDelta.x : event.pixelDelta.y
                    const angle = event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y
                    const delta = pixels !== 0 ? pixels : angle / 120 * Math.min(root.tabWidth, 120)
                    tabList.contentX = Math.max(0, Math.min(Math.max(0, tabList.contentWidth - tabList.width),
                                                         tabList.contentX - delta))
                    event.accepted = true
                }
            }
        }
        ToolButton {
            objectName: "new-pane-tab-" + root.pane.id
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            text: "+"
            font.pixelSize: 20
            Accessible.name: qsTr("New tab")
            ToolTip.visible: hovered
            ToolTip.text: qsTr("New tab (Ctrl+T)")
            onClicked: {
                root.activated()
                root.pane.newTab()
            }
        }
    }
}
