import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: root
    width: 480
    height: 480
    padding: 16
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    ColumnLayout {
        anchors.fill: parent
        RowLayout {
            Label { text: qsTr("Background tasks"); font.pixelSize: 18; font.bold: true; Layout.fillWidth: true }
            ToolButton { text: qsTr("Clear finished"); onClicked: appController.tasks.clearFinished() }
        }
        Label { text: qsTr("Tasks continue when this panel is closed."); opacity: 0.6; font.pixelSize: 12 }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            clip: true
            model: appController.tasks.model
            ScrollBar.vertical: ScrollBar {}
            delegate: Frame {
                required property string taskId
                required property string title
                required property string status
                required property real progress
                required property string currentFile
                required property string error
                required property bool cancellable
                required property real processedBytes
                required property real totalBytes
                required property real bytesPerSecond
                width: list.width
                ColumnLayout {
                    width: parent.width
                    RowLayout {
                        Label { text: title; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                        ToolButton { text: qsTr("Cancel"); visible: cancellable; onClicked: appController.tasks.cancel(taskId) }
                    }
                    Label { text: status; font.pixelSize: 12 }
                    ProgressBar { Layout.fillWidth: true; value: progress; indeterminate: progress < 0 && cancellable }
                    Label {
                        visible: progress >= 0
                        text: appController.transferSummary(processedBytes, totalBytes, bytesPerSecond)
                        font.pixelSize: 12
                        opacity: 0.7
                    }
                    Label { text: currentFile; Layout.fillWidth: true; elide: Text.ElideMiddle; font.pixelSize: 12; textFormat: Text.PlainText }
                    Label { text: error; visible: error.length > 0; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText; font.pixelSize: 12 }
                }
            }
            Label { anchors.centerIn: parent; visible: list.count === 0; text: qsTr("No background tasks"); opacity: 0.6 }
        }
    }
}
