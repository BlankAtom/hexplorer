---
name: hexplorer-fluent-ui
description: "Implement or refine HExplorer's Fluent Qt Quick UI, QML file panes, dynamic layouts, sidebars, previews, task popup, themes, keyboard focus and accessibility. Use for visual and desktop interaction changes without moving business logic into QML."
---

# HExplorer Fluent UI

For repository commits, also follow
`..\hexplorer-git-sync\SKILL.md`: after each successful primary-branch commit,
synchronize that branch with `origin` and verify the remote tip.

Paths are workspace-root relative; the application source root is `Qt`.

## Read the affected surface

| File | Surface |
|---|---|
| `Qt\qml\Main.qml` | ApplicationWindow, command bar, sidebars, pane placement, dialogs |
| `Qt\qml\FilePane.qml` | Address/filter controls, GridView delegates, selection, drops, shortcuts |
| `Qt\qml\Sidebar.qml` | Shared tree/preview component selected by `previewMode` |
| `Qt\qml\TaskCenter.qml` | Non-modal task popup bound to the C++ task model |
| `Qt\qml\NavigationDialog.qml` | Settings tabs, Quick Access visibility and named custom folders |
| `Qt\qml\UpdateSettings.qml` | Version display, opt-in checks and download/install controls |
| `Qt\qml\TagMenu.qml` | Multi-selection color-tag menu with checked/mixed states |
| `Qt\src\main.cpp` | FluentWinUI3 style, image provider and GUI smoke-test scenario |
| `Qt\src\core\Workspace.*` | Layout geometry and split ratios |
| `Qt\src\platform\PreviewController.*` | Safe preview state and asynchronous results |

## Design rules

- Qt Quick Controls uses `FluentWinUI3`, selected before loading QML. It is a Qt
  style, not Microsoft WinUI controls and not a web UI.
- Use palette roles (`base`, `window`, `highlight`, `mid`, text roles) rather
  than hard-coded theme colors. Preserve system/light/dark behavior.
- Keep the decorated system window; do not replace it with a frameless QML
  title bar without implementing equivalent snap, resize and system-menu
  behavior. Mica is not currently implemented.
- Preserve compact-pane behavior. The command bar collapses some actions into
  More on narrow windows; pane controls and metadata columns also adapt.
- `qsTr` marks localizable strings but does not supply translations. Do not
  claim Chinese localization is complete merely because strings use `qsTr`.
- Register added QML component files in `Qt\CMakeLists.txt`.

## Models, focus and interaction

Use model roles through typed `required property` declarations in delegates.
The file view uses GridView in both row and grid modes with item reuse. Do not
store durable file state in delegates, since delegates can be recycled.
Repeaters are appropriate for the small number of panes, not large directories.

Keep per-pane FocusScope behavior. A click activates the owning pane; file-view
shortcuts must not steal copy/paste or selection from address fields, filters
or text previews. Preserve Ctrl-click, Shift-click, keyboard navigation and
the distinction between current/focused item and selected items.

Use anchors within a component and Layout properties for its placement in a
layout. Do not combine competing anchors and layout-managed sizing on the same
item. Central pane/divider geometry comes from C++, not a duplicate QML tree.

Render filenames and file content with PlainText. Keep preview reads in C++;
images use bounded requested dimensions and asynchronous loading. Shell icons
and thumbnails come from `image://shell/`, not synchronous QML filesystem work.
Closing TaskCenter must not stop tasks.

For drag/drop changes, also read `hexplorer-file-operations`. Qt 6.8 QML
DragEvent has no `modifiers` property; the existing C++ event filter supplies
`appController.dragModifiers()`. Incoming URL lists use `QList<QUrl>`.

## Validation

Quick Access collapsed/visible state and entries belong to NavigationSettings,
not transient QML state. Tag dots use the model's `tagColors` role. Tag views
can span directories, so preserve origin-path display and reject folder-only
operations on their virtual address; folder-row drop targets still work.

Run CTest `qml_smoke` after rebuilding `HExplorer`. It drives real file-view
selection and both blank-area and folder-row drops, previews and layout changes.
It is not a substitute for all interactive Explorer/OLE or mixed-DPI scenarios.

The executable supports:

```powershell
.\Qt\dist\HExplorer\HExplorer.exe --smoke-test --smoke-log '<absolute-log-path>' --smoke-screenshot '<absolute-png-path>'
```

Use fresh build output or a freshly deployed executable, not a stale release.
Inspect the rendered image for clipping, contrast, focus visibility and layout
at relevant sizes. Use a Qt window capture rather than a browser preview:
this application is a native Qt Quick desktop process.
