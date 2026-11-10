import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import HExplorer.Core

ApplicationWindow {
    id: window
    title: qsTr("HExplorer")
    width: Math.min(appController.windowGeometry().width, Screen.desktopAvailableWidth)
    height: Math.min(appController.windowGeometry().height, Screen.desktopAvailableHeight)
    minimumWidth: 960
    minimumHeight: 640
    visible: true
    color: palette.window
    property var workspace: appController.workspace
    property var activePane: workspace.activePane
    property bool allowExit: false
    property string notice: ""
    property string conflictId: ""
    property string conflictSource: ""
    property string conflictTarget: ""
    property bool permanentDeletion: false
    property int deletionCount: 0

    Component.onCompleted: {
        if (appController.windowGeometry().maximized)
            showMaximized()
    }
    onClosing: function(close) {
        appController.setWindowGeometry(width, height, visibility === Window.Maximized)
        appController.saveSession()
        if (appController.busy && !allowExit) {
            close.accepted = false
            exitDialog.open()
        }
    }

    header: ToolBar {
        objectName: "commandBar"
        implicitHeight: 48
        height: 48
        leftPadding: 8
        rightPadding: 8
        topPadding: 4
        bottomPadding: 4
        RowLayout {
            anchors.fill: parent
            spacing: 4
            Rectangle {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                radius: 8
                color: window.palette.highlight
                Label { anchors.centerIn: parent; text: "H"; color: window.palette.highlightedText; font.bold: true; font.pixelSize: 20 }
            }
            Label { text: "HExplorer"; font.bold: true; font.pixelSize: 17; Layout.leftMargin: 8; Layout.rightMargin: 16 }
            ToolButton { text: qsTr("New folder"); onClicked: appController.requestFolder(); Accessible.name: text }
            ToolSeparator {}
            ToolButton { text: qsTr("Cut"); enabled: window.activePane && window.activePane.files.selectionCount > 0; onClicked: appController.copy(true) }
            ToolButton { text: qsTr("Copy"); enabled: window.activePane && window.activePane.files.selectionCount > 0; onClicked: appController.copy(false) }
            ToolButton { text: qsTr("Paste"); onClicked: appController.paste() }
            ToolButton {
                text: qsTr("Tags")
                enabled: window.activePane && window.activePane.files.selectionCount > 0
                onClicked: toolbarTags.popup()
                TagMenu { id: toolbarTags; objectName: "toolbarTagMenu" }
            }
            ToolButton { visible: window.width >= 1200; text: qsTr("Rename"); enabled: window.activePane && window.activePane.files.selectionCount === 1; onClicked: appController.requestRename() }
            ToolButton { visible: window.width >= 1200; text: qsTr("Delete"); enabled: window.activePane && window.activePane.files.selectionCount > 0; onClicked: appController.requestDelete() }
            ToolButton { visible: window.width >= 1200; text: qsTr("Extract ZIP"); enabled: window.activePane && window.activePane.files.selectionCount > 0; onClicked: appController.extract() }
            ToolButton {
                visible: window.width < 1200
                text: qsTr("More")
                onClicked: actionMenu.popup()
                Menu {
                    id: actionMenu
                    MenuItem { text: qsTr("Rename"); enabled: window.activePane && window.activePane.files.selectionCount === 1; onTriggered: appController.requestRename() }
                    MenuItem { text: qsTr("Delete"); enabled: window.activePane && window.activePane.files.selectionCount > 0; onTriggered: appController.requestDelete() }
                    MenuItem { text: qsTr("Extract ZIP"); enabled: window.activePane && window.activePane.files.selectionCount > 0; onTriggered: appController.extract() }
                }
            }
            Item { Layout.fillWidth: true }
            ToolButton {
                text: qsTr("Layout")
                onClicked: layoutMenu.popup()
                Menu {
                    id: layoutMenu
                    MenuItem { text: qsTr("Single pane"); onTriggered: window.workspace.preset(1) }
                    MenuItem { text: qsTr("Two columns"); onTriggered: window.workspace.preset(2) }
                    MenuItem { text: qsTr("Three columns"); onTriggered: window.workspace.preset(3) }
                    MenuItem { text: qsTr("Four quadrants"); onTriggered: window.workspace.preset(4) }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Split active pane vertically"); onTriggered: window.workspace.split(window.activePane, true) }
                    MenuItem { text: qsTr("Split active pane horizontally"); onTriggered: window.workspace.split(window.activePane, false) }
                    MenuItem { text: qsTr("Close active pane"); enabled: window.workspace.panes.length > 1; onTriggered: window.workspace.closePane(window.activePane) }
                }
            }
            ToolButton {
                text: qsTr("View")
                onClicked: viewMenu.popup()
                Menu {
                    id: viewMenu
                    MenuItem { text: qsTr("Left sidebar"); checkable: true; checked: appController.leftVisible; onTriggered: appController.leftVisible = checked }
                    MenuItem { text: qsTr("Right sidebar"); checkable: true; checked: appController.rightVisible; onTriggered: appController.rightVisible = checked }
                    MenuItem { text: qsTr("Swap sidebars"); checkable: true; checked: appController.swapSides; onTriggered: appController.swapSides = checked }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Navigation settings"); onTriggered: navigationDialog.open() }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Follow system theme"); checkable: true; checked: appController.theme === 0; onTriggered: appController.theme = 0 }
                    MenuItem { text: qsTr("Light"); checkable: true; checked: appController.theme === 1; onTriggered: appController.theme = 1 }
                    MenuItem { text: qsTr("Dark"); checkable: true; checked: appController.theme === 2; onTriggered: appController.theme = 2 }
                }
            }
            ToolButton {
                text: appController.busy ? qsTr("Tasks (running)") : qsTr("Tasks")
                onClicked: taskCenter.open()
            }
        }
    }

    SplitView {
        id: shell
        anchors.fill: parent
        anchors.margins: 8
        orientation: Qt.Horizontal
        Sidebar {
            visible: appController.leftVisible
            previewMode: appController.swapSides
            onSettingsRequested: navigationDialog.open()
            SplitView.preferredWidth: appController.leftWidth
            SplitView.minimumWidth: 160
            SplitView.maximumWidth: 500
            onWidthChanged: if (shell.resizing) appController.leftWidth = width
        }
        Item {
            id: workspaceArea
            SplitView.fillWidth: true
            SplitView.minimumWidth: 400
            clip: true
            Repeater {
                model: window.workspace.panes
                delegate: FilePane {
                    required property var modelData
                    pane: modelData
                    x: pane.rect.x * workspaceArea.width + 3
                    y: pane.rect.y * workspaceArea.height + 3
                    width: pane.rect.width * workspaceArea.width - 6
                    height: pane.rect.height * workspaceArea.height - 6
                    active: window.activePane === pane
                }
            }
            Repeater {
                model: window.workspace.dividers
                delegate: Rectangle {
                    id: divider
                    required property var modelData
                    readonly property bool horizontal: modelData.horizontal
                    x: (modelData.bounds.x + (horizontal ? modelData.bounds.width * modelData.ratio : 0)) * workspaceArea.width - (horizontal ? 3 : 0)
                    y: (modelData.bounds.y + (horizontal ? 0 : modelData.bounds.height * modelData.ratio)) * workspaceArea.height - (horizontal ? 0 : 3)
                    width: horizontal ? 6 : modelData.bounds.width * workspaceArea.width
                    height: horizontal ? modelData.bounds.height * workspaceArea.height : 6
                    color: resizeMouse.containsMouse || resizeMouse.pressed ? window.palette.highlight : "transparent"
                    radius: 3
                    MouseArea {
                        id: resizeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: divider.horizontal ? Qt.SplitHCursor : Qt.SplitVCursor
                        onPositionChanged: function(mouse) {
                            if (pressed) {
                                const position = mapToItem(workspaceArea, mouse.x, mouse.y)
                                window.workspace.resizeSplit(divider.modelData, position.x / workspaceArea.width, position.y / workspaceArea.height)
                            }
                        }
                    }
                }
            }
        }
        Sidebar {
            visible: appController.rightVisible
            previewMode: !appController.swapSides
            onSettingsRequested: navigationDialog.open()
            SplitView.preferredWidth: appController.rightWidth
            SplitView.minimumWidth: 200
            SplitView.maximumWidth: 600
            onWidthChanged: if (shell.resizing) appController.rightWidth = width
        }
    }
    footer: ToolBar {
        height: 32
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            Label {
                text: window.activePane ? qsTr("%1 items  |  %2 selected").arg(window.activePane.files.count).arg(window.activePane.files.selectionCount) : ""
                font.pixelSize: 12
            }
            Item { Layout.fillWidth: true }
            Label { text: qsTr("%1 panes  |  Ctrl+Tab to switch").arg(window.workspace.panes.length); font.pixelSize: 12; opacity: 0.7 }
        }
    }
    Shortcut { sequence: "Ctrl+Tab"; onActivated: window.workspace.nextPane() }
    Shortcut { sequence: "Ctrl+1"; onActivated: window.workspace.preset(1) }
    Shortcut { sequence: "Ctrl+2"; onActivated: window.workspace.preset(2) }
    Shortcut { sequence: "Ctrl+3"; onActivated: window.workspace.preset(3) }
    Shortcut { sequence: "Ctrl+4"; onActivated: window.workspace.preset(4) }

    TaskCenter { id: taskCenter; objectName: "taskCenter"; parent: Overlay.overlay; x: Math.max(8, window.width - width - 16); y: 64 }
    NavigationDialog { id: navigationDialog; objectName: "navigationDialog"; anchors.centerIn: parent }
    Connections {
        target: appController
        function onNotification(message) { window.notice = message; noticeDialog.open() }
        function onDeletionRequested(count, permanent) {
            window.deletionCount = count
            window.permanentDeletion = permanent
            deleteDialog.open()
        }
        function onNameRequested(title, initialName) {
            nameDialog.title = title
            nameInput.text = initialName
            nameDialog.open()
            nameInput.forceActiveFocus()
            nameInput.selectAll()
        }
    }
    Connections {
        target: appController.tasks
        function onConflict(taskId, source, destination) {
            window.conflictId = taskId
            window.conflictSource = source
            window.conflictTarget = destination
            applyAll.checked = false
            conflictDialog.open()
        }
    }
    Dialog {
        id: noticeDialog
        title: qsTr("HExplorer")
        anchors.centerIn: parent
        width: Math.min(560, window.width - 64)
        modal: true
        standardButtons: Dialog.Ok
        Label { width: parent.width; text: window.notice; wrapMode: Text.Wrap; textFormat: Text.PlainText }
    }
    Dialog {
        id: nameDialog
        anchors.centerIn: parent
        width: 420
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: appController.acceptName(nameInput.text)
        TextField { id: nameInput; width: parent.width; Accessible.name: nameDialog.title; onAccepted: nameDialog.accept() }
    }
    Dialog {
        id: deleteDialog
        anchors.centerIn: parent
        width: 440
        title: window.permanentDeletion ? qsTr("Permanently delete?") : qsTr("Move to Recycle Bin?")
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: appController.confirmDelete()
        Label {
            width: parent.width
            wrapMode: Text.Wrap
            text: window.permanentDeletion
                ? qsTr("Permanently delete %1 selected item(s)? This cannot be undone.").arg(window.deletionCount)
                : qsTr("Move %1 selected item(s) to the Recycle Bin? Unsupported locations will report an error, not silently delete.").arg(window.deletionCount)
        }
    }
    Dialog {
        id: conflictDialog
        anchors.centerIn: parent
        width: 580
        title: qsTr("An item already exists")
        modal: true
        closePolicy: Popup.NoAutoClose
        ColumnLayout {
            width: parent.width
            Label { text: qsTr("Source: %1").arg(window.conflictSource); Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText }
            Label { text: qsTr("Destination: %1").arg(window.conflictTarget); Layout.fillWidth: true; wrapMode: Text.WrapAnywhere; textFormat: Text.PlainText }
            CheckBox { id: applyAll; text: qsTr("Apply to remaining conflicts in this task") }
            RowLayout {
                Button { text: qsTr("Replace"); onClicked: { appController.tasks.resolveConflict(window.conflictId, "overwrite", applyAll.checked); conflictDialog.close() } }
                Button { text: qsTr("Keep both"); onClicked: { appController.tasks.resolveConflict(window.conflictId, "rename", applyAll.checked); conflictDialog.close() } }
                Button { text: qsTr("Skip"); onClicked: { appController.tasks.resolveConflict(window.conflictId, "skip", applyAll.checked); conflictDialog.close() } }
                Button { text: qsTr("Cancel task"); onClicked: { appController.tasks.cancel(window.conflictId); conflictDialog.close() } }
            }
        }
    }
    Dialog {
        id: exitDialog
        anchors.centerIn: parent
        width: 480
        title: qsTr("Background tasks are still running")
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        onAccepted: { window.allowExit = true; window.close() }
        Label { width: parent.width; wrapMode: Text.Wrap; text: qsTr("Cancel unfinished tasks and exit? Already completed files will be kept. Choose No to let tasks continue.") }
    }
}
