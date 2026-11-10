import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    readonly property var updater: appController.updates
    signal installRequested()
    spacing: 8
    Label { text: qsTr("HExplorer %1").arg(root.updater.currentVersion); font.bold: true; font.pixelSize: 18 }
    Label { text: qsTr("Windows x64 · interactive setup updates"); opacity: 0.7 }
    CheckBox {
        text: qsTr("Check for updates when HExplorer starts")
        checked: root.updater.autoCheck
        onClicked: root.updater.autoCheck = checked
    }
    Label {
        Layout.fillWidth: true
        text: qsTr("Checks contact GitHub Releases. No local paths, file contents or account credentials are sent. Downloads and installation always require your action.")
        wrapMode: Text.Wrap
        opacity: 0.7
    }
    Label {
        objectName: "updateStatus"
        Layout.fillWidth: true
        text: root.updater.message
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
        Accessible.name: text
    }
    ProgressBar {
        Layout.fillWidth: true
        visible: root.updater.busy
        value: root.updater.progress
        indeterminate: root.updater.status === "checking"
    }
    RowLayout {
        Button {
            objectName: "checkForUpdates"
            text: qsTr("Check for updates")
            enabled: !root.updater.busy && root.updater.status !== "installing"
            onClicked: root.updater.check()
        }
        Button {
            text: qsTr("Cancel")
            visible: root.updater.busy
            onClicked: root.updater.cancel()
        }
        Button {
            text: qsTr("Download %1").arg(root.updater.latestVersion)
            visible: root.updater.status === "available"
            onClicked: root.updater.download()
        }
        Button {
            objectName: "installUpdate"
            text: qsTr("Install and close")
            visible: root.updater.status === "ready"
            enabled: !appController.busy
            onClicked: root.installRequested()
        }
    }
    Label {
        visible: root.updater.status === "ready" && appController.busy
        text: qsTr("Wait for file operations to finish before installing.")
        wrapMode: Text.Wrap
        Layout.fillWidth: true
    }
    Button {
        text: qsTr("View release on GitHub")
        visible: root.updater.releaseUrl.toString().length > 0
        onClicked: root.updater.openRelease()
    }
    ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: root.updater.releaseNotes.length > 0
        TextArea {
            text: root.updater.releaseNotes
            textFormat: TextEdit.PlainText
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            Accessible.name: qsTr("Release notes")
        }
    }
    Item { Layout.fillHeight: true; visible: root.updater.releaseNotes.length === 0 }
    Label {
        Layout.fillWidth: true
        text: qsTr("Updates use an interactive Windows installer, not an EXE overwrite. Portable copies are not replaced: setup installs a separate managed copy. Releases are currently unsigned; SHA-256 verifies download integrity, not publisher identity. Never bypass Windows security warnings.")
        wrapMode: Text.Wrap
        font.pixelSize: 12
        opacity: 0.7
    }
}
