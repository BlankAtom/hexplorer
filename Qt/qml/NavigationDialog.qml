import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    title: qsTr("Navigation settings")
    width: 560
    height: 540
    modal: true
    standardButtons: Dialog.Close
    ColumnLayout {
        anchors.fill: parent
        CheckBox {
            text: qsTr("Show Quick Access")
            checked: appController.navigation.visible
            onClicked: appController.navigation.visible = checked
        }
        Label { text: qsTr("Quick Access entries"); font.bold: true }
        ListView {
            id: entries
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: appController.navigation.places
            ScrollBar.vertical: ScrollBar {}
            delegate: RowLayout {
                required property var modelData
                required property int index
                width: entries.width
                ColumnLayout {
                    Layout.fillWidth: true
                    Label { text: modelData.name; textFormat: Text.PlainText }
                    Label { text: modelData.url; textFormat: Text.PlainText; elide: Text.ElideMiddle; Layout.fillWidth: true; opacity: 0.6 }
                }
                ToolButton {
                    text: qsTr("Remove")
                    onClicked: appController.navigation.remove(index)
                    Accessible.name: qsTr("Remove %1 from Quick Access").arg(modelData.name)
                }
            }
        }
        TextField { id: entryName; Layout.fillWidth: true; placeholderText: qsTr("Display name"); Accessible.name: placeholderText }
        TextField { id: entryPath; Layout.fillWidth: true; placeholderText: qsTr("Absolute folder path, e.g. D:\\Projects"); Accessible.name: placeholderText }
        RowLayout {
            Button {
                text: qsTr("Add folder")
                onClicked: appController.navigation.add(entryName.text, entryPath.text)
            }
            Button {
                text: qsTr("Use current folder")
                enabled: appController.workspace.activePane && !appController.workspace.activePane.tagView
                onClicked: {
                    entryName.text = appController.workspace.activePane.title
                    entryPath.text = appController.workspace.activePane.path
                }
            }
            Button { text: qsTr("Restore defaults"); onClicked: appController.navigation.reset() }
        }
        Label { text: qsTr("Removing an entry does not delete the folder."); opacity: 0.65 }
    }
}
