import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HExplorer.Core

FocusScope {
    id: root
    required property PaneController pane
    objectName: "file-pane-" + pane.id
    property bool active: false
    readonly property var workspace: appController.workspace
    readonly property color accent: paletteSource.palette.highlight
    property bool draggingFiles: false
    property bool restoringTab: false
    function restoreTabView() {
        restoringTab = true
        address.text = pane.path
        filter.text = pane.query
        fileView.currentIndex = Math.min(pane.currentRow, fileView.count - 1)
        if (!pane.loading)
            fileView.contentY = Math.min(pane.scrollPosition, Math.max(0, fileView.contentHeight - fileView.height))
        restoringTab = false
    }
    clip: true
    onActiveChanged: if (active && address && filter && fileView && !address.activeFocus && !filter.activeFocus) fileView.forceActiveFocus()
    Component.onCompleted: if (active) fileView.forceActiveFocus()
    Control { id: paletteSource; visible: false }
    Rectangle {
        anchors.fill: parent
        color: paletteSource.palette.base
        radius: 8
        border.color: root.active ? root.accent : paletteSource.palette.mid
        border.width: root.active ? 2 : 1
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4
        RowLayout {
            spacing: 2
            ToolButton {
                visible: root.width > 240
                text: "\uE72B"; font.family: "Segoe Fluent Icons"; enabled: root.pane.canBack
                Accessible.name: qsTr("Back"); ToolTip.visible: hovered; ToolTip.text: qsTr("Back (Alt+Left)")
                onClicked: { root.workspace.activate(root.pane); root.pane.back() }
            }
            ToolButton {
                visible: root.width > 360
                text: "\uE72A"; font.family: "Segoe Fluent Icons"; enabled: root.pane.canForward
                Accessible.name: qsTr("Forward"); onClicked: { root.workspace.activate(root.pane); root.pane.forward() }
            }
            ToolButton {
                visible: root.width > 240
                text: "\uE74A"; font.family: "Segoe Fluent Icons"
                Accessible.name: qsTr("Up"); ToolTip.visible: hovered; ToolTip.text: qsTr("Parent folder (Alt+Up)")
                onClicked: { root.workspace.activate(root.pane); root.pane.up() }
            }
            TextField {
                id: address
                Layout.fillWidth: true
                Layout.minimumWidth: 40
                text: root.pane.path
                selectByMouse: true
                Accessible.name: qsTr("Folder address")
                onActiveFocusChanged: if (activeFocus) root.workspace.activate(root.pane)
                onAccepted: { root.pane.navigateText(text); fileView.forceActiveFocus() }
                ToolTip.visible: hovered; ToolTip.text: root.pane.path
            }
            ToolButton {
                visible: root.width > 320
                text: "\uE72C"; font.family: "Segoe Fluent Icons"
                Accessible.name: qsTr("Refresh"); onClicked: root.pane.refresh()
            }
            ToolButton {
                text: "\uE712"; font.family: "Segoe Fluent Icons"; Accessible.name: qsTr("Pane options")
                onClicked: { root.workspace.activate(root.pane); paneMenu.popup() }
                Menu {
                    id: paneMenu
                    MenuItem { text: qsTr("Grid view"); checkable: true; checked: root.pane.grid; onTriggered: root.pane.grid = checked }
                    MenuItem { text: qsTr("Show hidden files"); checkable: true; checked: root.pane.showHidden; onTriggered: root.pane.showHidden = checked }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Sort by name"); checkable: true; checked: root.pane.sortKey === 0; onTriggered: root.pane.sortKey = 0 }
                    MenuItem { text: qsTr("Sort by size"); checkable: true; checked: root.pane.sortKey === 1; onTriggered: root.pane.sortKey = 1 }
                    MenuItem { text: qsTr("Sort by modified date"); checkable: true; checked: root.pane.sortKey === 2; onTriggered: root.pane.sortKey = 2 }
                    MenuItem { text: qsTr("Descending"); checkable: true; checked: root.pane.descending; onTriggered: root.pane.descending = checked }
                    MenuSeparator {}
                    MenuItem { text: qsTr("New tab"); onTriggered: root.pane.newTab() }
                    MenuItem { text: qsTr("Close tab"); enabled: root.pane.tabs.length > 1; onTriggered: root.pane.closeTab(root.pane.activeTab) }
                    MenuSeparator {}
                    MenuItem { text: qsTr("Split vertically"); onTriggered: root.workspace.split(root.pane, true) }
                    MenuItem { text: qsTr("Split horizontally"); onTriggered: root.workspace.split(root.pane, false) }
                    MenuItem { text: qsTr("Close pane"); enabled: root.workspace.panes.length > 1; onTriggered: root.workspace.closePane(root.pane) }
                }
            }
        }
        TextField {
            id: filter
            Layout.fillWidth: true
            placeholderText: root.pane.tagView ? qsTr("Filter tagged items") : qsTr("Filter this folder")
            text: root.pane.query
            Accessible.name: placeholderText
            onTextEdited: root.pane.query = text
            onActiveFocusChanged: if (activeFocus) root.workspace.activate(root.pane)
        }
        Label {
            visible: root.pane.tagView
            text: qsTr("%1 — across all tagged folders").arg(root.pane.title)
            Layout.fillWidth: true
            elide: Text.ElideRight
            font.bold: true
        }
        Label {
            visible: root.pane.error.length > 0 && root.pane.files.count > 0
            text: root.pane.error
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
        }
        RowLayout {
            visible: !root.pane.grid
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 14
            Label { text: qsTr("Name"); font.pixelSize: 12; opacity: 0.65; Layout.fillWidth: true }
            Label { text: qsTr("Size"); font.pixelSize: 12; opacity: 0.65; Layout.preferredWidth: 70; visible: root.width > 350 }
            Label { text: qsTr("Modified"); font.pixelSize: 12; opacity: 0.65; Layout.preferredWidth: 140; visible: root.width > 550 }
        }
        Item {
            id: viewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            GridView {
                id: fileView
                objectName: "pane-file-view-" + root.pane.id
                anchors.fill: parent
                model: root.pane.files
                cellWidth: root.pane.grid ? Math.max(96, width / Math.max(1, Math.floor(width / 112))) : width
                cellHeight: root.pane.grid ? 112 : root.pane.tagView ? 56 : 38
                boundsBehavior: Flickable.StopAtBounds
                keyNavigationEnabled: false
                reuseItems: true
                focus: root.active
                ScrollBar.vertical: ScrollBar {}
                onMovementEnded: if (!root.restoringTab && !root.pane.loading) root.pane.scrollPosition = contentY
                onCurrentIndexChanged: if (!root.restoringTab && !root.pane.loading) root.pane.currentRow = currentIndex
                Accessible.name: qsTr("Files in %1").arg(root.pane.path)
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Down || event.key === Qt.Key_Up || event.key === Qt.Key_Left || event.key === Qt.Key_Right || event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                        const columns = Math.max(1, Math.round(width / cellWidth))
                        const step = event.key === Qt.Key_Down ? columns : event.key === Qt.Key_Up ? -columns : event.key === Qt.Key_Right ? 1 : -1
                        currentIndex = event.key === Qt.Key_Home ? 0 : event.key === Qt.Key_End ? count - 1 : Math.max(0, Math.min(count - 1, currentIndex + step))
                        if (!(event.modifiers & Qt.ControlModifier))
                            root.pane.files.select(currentIndex, event.modifiers)
                        positionViewAtIndex(currentIndex, GridView.Contain)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        root.pane.activateEntry(currentIndex)
                        event.accepted = true
                    } else if (event.key === Qt.Key_Space) {
                        root.pane.files.select(currentIndex, Qt.ControlModifier)
                        event.accepted = true
                    }
                }
                delegate: Item {
                    id: entry
                    required property int index
                    required property string fileName
                    required property url fileUrl
                    required property string iconUrl
                    required property string sizeText
                    required property string modifiedText
                    required property bool isDirectory
                    required property bool selected
                    required property var tagColors
                    required property string filePath
                    objectName: "file-item-" + entry.fileName
                    width: fileView.cellWidth
                    height: fileView.cellHeight
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 2
                        radius: 5
                        color: entry.selected ? paletteSource.palette.highlight : itemMouse.containsMouse ? paletteSource.palette.alternateBase : "transparent"
                        opacity: entry.selected ? 0.22 : 1
                    }
                    Rectangle {
                        visible: fileView.activeFocus && fileView.currentIndex === entry.index
                        anchors.fill: parent
                        anchors.margins: 2
                        color: "transparent"
                        radius: 5
                        border.color: root.accent
                    }
                    Image {
                        id: fileIcon
                        objectName: "file-icon"
                        source: entry.iconUrl
                        asynchronous: true
                        sourceSize: Qt.size(root.pane.grid ? 48 : 24, root.pane.grid ? 48 : 24)
                        width: root.pane.grid ? 48 : 24
                        height: width
                        x: root.pane.grid ? (parent.width - width) / 2 : 10
                        y: root.pane.grid ? 10 : (parent.height - height) / 2
                        fillMode: Image.PreserveAspectFit
                    }
                    Label {
                        x: root.pane.grid ? 4 : 44
                        y: root.pane.grid ? 64 : root.pane.tagView ? 5 : (parent.height - height) / 2
                        width: root.pane.grid ? parent.width - 8 : parent.width - 112 - (root.width > 350 ? 80 : 0) - (root.width > 550 ? 150 : 0)
                        text: entry.fileName
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        maximumLineCount: root.pane.grid ? 2 : 1
                        wrapMode: root.pane.grid ? Text.Wrap : Text.NoWrap
                        horizontalAlignment: root.pane.grid ? Text.AlignHCenter : Text.AlignLeft
                    }
                    Label {
                        visible: root.pane.tagView && !root.pane.grid
                        x: 44; y: 30; width: parent.width - 54
                        text: entry.filePath
                        textFormat: Text.PlainText
                        elide: Text.ElideMiddle
                        font.pixelSize: 10
                        opacity: 0.6
                    }
                    Row {
                        x: root.pane.grid ? (parent.width - width) / 2 : parent.width - 66 - (root.width > 350 ? 80 : 0) - (root.width > 550 ? 150 : 0)
                        y: root.pane.grid ? 56 : (parent.height - height) / 2
                        spacing: 2
                        Repeater {
                            model: entry.tagColors
                            delegate: Rectangle {
                                required property var modelData
                                width: 7; height: 7; radius: 3.5; color: modelData
                            }
                        }
                    }
                    Label {
                        visible: !root.pane.grid && root.width > 350
                        x: parent.width - 84 - (root.width > 550 ? 150 : 0)
                        width: 76
                        anchors.verticalCenter: parent.verticalCenter
                        text: entry.sizeText; font.pixelSize: 12; opacity: 0.7
                    }
                    Label {
                        visible: !root.pane.grid && root.width > 550
                        x: parent.width - 154; width: 148
                        anchors.verticalCenter: parent.verticalCenter
                        text: entry.modifiedText; elide: Text.ElideRight; font.pixelSize: 12; opacity: 0.7
                    }
                    MouseArea {
                        id: itemMouse
                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        hoverEnabled: true
                        preventStealing: true
                        property point startPoint
                        onPressed: function(mouse) {
                            root.workspace.activate(root.pane)
                            fileView.forceActiveFocus()
                            fileView.currentIndex = entry.index
                            startPoint = Qt.point(mouse.x, mouse.y)
                            root.draggingFiles = false
                            if (!entry.selected || (mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier)))
                                root.pane.files.select(entry.index, mouse.button === Qt.RightButton ? 0 : mouse.modifiers)
                        }
                        onPositionChanged: function(mouse) {
                            if (pressed && (pressedButtons & Qt.LeftButton) && !root.draggingFiles
                                    && Math.abs(mouse.x - startPoint.x) + Math.abs(mouse.y - startPoint.y) > Qt.styleHints.startDragDistance) {
                                root.draggingFiles = true
                                appController.native.startDrag(root.pane.files.selectedUrls)
                            }
                        }
                        onClicked: function(mouse) {
                            if (root.draggingFiles) return
                            if (mouse.button === Qt.RightButton) {
                                const point = mapToItem(root, mouse.x, mouse.y)
                                fileMenu.popup(root, point.x, point.y)
                            } else if (!(mouse.modifiers & (Qt.ControlModifier | Qt.ShiftModifier))) {
                                root.pane.files.select(entry.index)
                            }
                        }
                        onDoubleClicked: function(mouse) { if (mouse.button === Qt.LeftButton) root.pane.activateEntry(entry.index) }
                        ToolTip.visible: containsMouse && !pressed
                        ToolTip.delay: 800
                        ToolTip.text: root.pane.tagView ? entry.filePath : entry.fileName
                    }
                    DropArea {
                        id: folderDrop
                        anchors.fill: parent
                        enabled: entry.isDirectory
                        onEntered: function(drag) {
                            drag.accepted = drag.hasUrls
                            drag.action = appController.dropAction(drag.urls, entry.fileUrl, appController.dragModifiers(), drag.supportedActions)
                        }
                        onPositionChanged: function(drag) { drag.action = appController.dropAction(drag.urls, entry.fileUrl, appController.dragModifiers(), drag.supportedActions) }
                        onDropped: function(drop) {
                            const action = appController.dropAction(drop.urls, entry.fileUrl, appController.dragModifiers(), drop.supportedActions)
                            if (action !== Qt.IgnoreAction && appController.drop(drop.urls, entry.fileUrl, action))
                                drop.accept(appController.dropAcknowledgement(action, drop.supportedActions))
                        }
                        Rectangle { anchors.fill: parent; visible: folderDrop.containsDrag; color: "transparent"; border.color: root.accent; border.width: 2; radius: 5 }
                    }
                    Accessible.role: Accessible.ListItem
                    Accessible.name: entry.fileName
                    Accessible.selected: entry.selected
                    Accessible.onPressAction: { root.workspace.activate(root.pane); root.pane.files.select(entry.index) }
                }
            }
            MouseArea {
                id: blankMouse
                anchors.fill: parent
                z: 2
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                property point startPoint
                property point endPoint
                property real startContentY: 0
                property bool selecting: false
                onPressed: function(mouse) {
                    if (mouse.x > width - 18 || fileView.indexAt(mouse.x, mouse.y + fileView.contentY) >= 0) {
                        mouse.accepted = false
                        return
                    }
                    root.workspace.activate(root.pane)
                    fileView.forceActiveFocus()
                    startPoint = Qt.point(mouse.x, mouse.y)
                    endPoint = startPoint
                    startContentY = fileView.contentY
                    selecting = mouse.button === Qt.LeftButton
                    if (selecting) root.pane.files.beginBoxSelection((mouse.modifiers & Qt.ControlModifier) !== 0)
                    else root.pane.files.clearSelection()
                }
                onPositionChanged: function(mouse) {
                    if (!pressed || !selecting) return
                    endPoint = Qt.point(mouse.x, mouse.y)
                    root.pane.files.selectBox(Qt.rect(startPoint.x, startPoint.y + startContentY,
                        mouse.x - startPoint.x, mouse.y + fileView.contentY - startPoint.y - startContentY),
                        fileView.cellWidth, fileView.cellHeight, Math.max(1, Math.round(fileView.width / fileView.cellWidth)))
                }
                onReleased: selecting = false
                onCanceled: selecting = false
                onWheel: function(wheel) { wheel.accepted = false }
                onClicked: function(mouse) {
                    if (mouse.button === Qt.RightButton) {
                        if (!root.pane.tagView) {
                            const point = mapToGlobal(mouse.x, mouse.y)
                            appController.contextMenu(point.x, point.y)
                        }
                    }
                }
                Rectangle {
                    z: 3
                    visible: blankMouse.selecting
                    x: Math.min(blankMouse.startPoint.x, blankMouse.endPoint.x)
                    y: Math.min(blankMouse.startPoint.y, blankMouse.endPoint.y)
                    width: Math.abs(blankMouse.endPoint.x - blankMouse.startPoint.x)
                    height: Math.abs(blankMouse.endPoint.y - blankMouse.startPoint.y)
                    color: root.accent
                    opacity: 0.2
                    border.color: root.accent
                    border.width: 2
                }
            }
            Label {
                anchors.centerIn: parent
                width: parent.width - 32
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: root.pane.error ? root.pane.error : root.pane.loading ? qsTr("Loading folder...") : root.pane.query ? qsTr("No matching items") : qsTr("This folder is empty")
                visible: root.pane.files.count === 0
                opacity: 0.7
            }
            DropArea {
                id: paneDrop
                anchors.fill: parent
                z: -2
                enabled: !root.pane.tagView
                property int action: Qt.IgnoreAction
                onEntered: function(drag) {
                    drag.accepted = drag.hasUrls
                    action = appController.dropAction(drag.urls, root.pane.url, appController.dragModifiers(), drag.supportedActions)
                    drag.action = action
                }
                onPositionChanged: function(drag) {
                    action = appController.dropAction(drag.urls, root.pane.url, appController.dragModifiers(), drag.supportedActions)
                    drag.action = action
                }
                onDropped: function(drop) {
                    const chosen = appController.dropAction(drop.urls, root.pane.url, appController.dragModifiers(), drop.supportedActions)
                    if (chosen !== Qt.IgnoreAction && appController.drop(drop.urls, root.pane.url, chosen))
                        drop.accept(appController.dropAcknowledgement(chosen, drop.supportedActions))
                }
            }
            Rectangle {
                anchors.fill: parent
                visible: paneDrop.containsDrag
                color: "transparent"; border.width: 2; border.color: root.accent; radius: 5
                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 8
                    padding: 8
                    text: paneDrop.action === Qt.MoveAction ? qsTr("Move into this folder") : qsTr("Copy into this folder")
                    background: Rectangle { color: paletteSource.palette.window; radius: 5 }
                }
            }
        }
        ProgressBar { Layout.fillWidth: true; Layout.preferredHeight: 2; visible: root.pane.loading; indeterminate: true }
        PaneTabs {
            pane: root.pane
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            onActivated: {
                root.workspace.activate(root.pane)
                fileView.forceActiveFocus()
            }
        }
    }
    Menu {
        id: fileMenu
        MenuItem { text: qsTr("Open"); onTriggered: root.pane.activateEntry(fileView.currentIndex) }
        MenuItem {
            text: qsTr("Open folder in new tab")
            enabled: root.pane.files.selectionCount === 1
                     && root.pane.files.entry(fileView.currentIndex).isDirectory === true
                     && root.pane.files.entry(fileView.currentIndex).selected === true
            onTriggered: root.pane.newTab(root.pane.files.selectedUrls[0])
        }
        TagMenu {}
        MenuItem { text: qsTr("Rename"); onTriggered: appController.requestRename() }
        MenuItem {
            text: qsTr("Show in Explorer")
            enabled: root.pane.files.selectionCount === 1
            onTriggered: appController.native.reveal(root.pane.files.selectedUrls[0])
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("Windows context menu")
            onTriggered: {
                const point = root.mapToGlobal(fileMenu.x, fileMenu.y)
                appController.contextMenu(point.x, point.y)
            }
        }
    }
    Connections {
        target: root.pane
        function onActiveTabAboutToChange() {
            if (!root.pane.loading) root.pane.scrollPosition = fileView.contentY
            root.restoringTab = true
        }
        function onActiveTabChanged() {
            fileMenu.close()
            Qt.callLater(root.restoreTabView)
        }
        function onLocationChanged() {
            address.text = root.pane.path
            if (!root.restoringTab) fileView.currentIndex = root.pane.currentRow
        }
        function onLoadingChanged() {
            if (!root.pane.loading) Qt.callLater(root.restoreTabView)
        }
    }
    Shortcut { sequence: "Ctrl+L"; enabled: root.active; onActivated: { address.forceActiveFocus(); address.selectAll() } }
    Shortcut { sequence: "Ctrl+F"; enabled: root.active; onActivated: { filter.forceActiveFocus(); filter.selectAll() } }
    Shortcut { sequence: "Ctrl+T"; enabled: root.active; onActivated: root.pane.newTab() }
    Shortcut { sequence: "Ctrl+W"; enabled: root.active; onActivated: root.pane.closeTab(root.pane.activeTab) }
    Shortcut { sequence: "Ctrl+PgDown"; enabled: root.active; onActivated: root.pane.nextTab() }
    Shortcut { sequence: "Ctrl+PgUp"; enabled: root.active; onActivated: root.pane.nextTab(-1) }
    Shortcut { sequence: "Alt+Left"; enabled: root.active; onActivated: root.pane.back() }
    Shortcut { sequence: "Alt+Right"; enabled: root.active; onActivated: root.pane.forward() }
    Shortcut { sequence: "Alt+Up"; enabled: root.active; onActivated: root.pane.up() }
    Shortcut { sequence: "F5"; enabled: root.active; onActivated: root.pane.refresh() }
    Shortcut { sequence: "Ctrl+A"; enabled: fileView.activeFocus; onActivated: root.pane.files.selectAll() }
    Shortcut { sequence: "Ctrl+C"; enabled: fileView.activeFocus; onActivated: appController.copy(false) }
    Shortcut { sequence: "Ctrl+X"; enabled: fileView.activeFocus; onActivated: appController.copy(true) }
    Shortcut { sequence: "Ctrl+V"; enabled: fileView.activeFocus; onActivated: appController.paste() }
    Shortcut { sequence: "F2"; enabled: fileView.activeFocus; onActivated: appController.requestRename() }
    Shortcut { sequence: "Delete"; enabled: fileView.activeFocus; onActivated: appController.requestDelete() }
    Shortcut { sequence: "Shift+Delete"; enabled: fileView.activeFocus; onActivated: appController.requestDelete(true) }
    Shortcut { sequence: "Ctrl+Shift+N"; enabled: root.active; onActivated: appController.requestFolder() }
    Shortcut { sequence: "Alt+Return"; enabled: fileView.activeFocus; onActivated: appController.properties() }
}
