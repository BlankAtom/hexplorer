import QtQuick
import QtQuick.Controls
import QtQml.Models

Menu {
    id: root
    title: qsTr("Color tags")
    Instantiator {
        model: appController.selectionTags
        delegate: MenuItem {
            required property var modelData
            objectName: "tag-action-" + modelData.id
            text: (modelData.mixed ? qsTr("%1 (some selected items)").arg(modelData.name) : modelData.name)
            checkable: true
            checked: modelData.checked
            leftPadding: 24
            enabled: appController.workspace.activePane && appController.workspace.activePane.files.selectionCount > 0
            icon.source: ""
            onTriggered: {
                const tag = modelData.id
                const apply = checked
                root.dismiss()
                appController.tagSelection(tag, apply)
            }
            indicator: Label {
                x: 6
                anchors.verticalCenter: parent.verticalCenter
                text: modelData.checked ? "\u2713" : modelData.mixed ? "\u2014" : ""
            }
            contentItem: Row {
                spacing: 8
                Rectangle {
                    width: 10; height: 10; radius: 5
                    color: modelData.color
                    anchors.verticalCenter: parent.verticalCenter
                }
                Label { text: parent.parent.text; textFormat: Text.PlainText }
            }
        }
        onObjectAdded: function(index, object) { root.insertItem(index, object) }
        onObjectRemoved: function(index, object) { root.removeItem(object) }
    }
    MenuSeparator {}
    MenuItem {
        text: qsTr("Remove all color tags")
        onTriggered: {
            root.dismiss()
            appController.clearSelectionTags()
        }
    }
}
