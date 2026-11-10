# HExplorer

A Windows 11-first, Qt 6 desktop file manager with Fluent WinUI 3 controls,
independent file panes, a folder tree, local previews, and background file tasks.
The UI is QML; filesystem policy and application state live in C++.

## Build

Requirements:

- Qt 6.8 or newer, including Qt Quick, Quick Controls, Concurrent, Widgets and Test.
- Visual Studio 2022 or newer with the Desktop development with C++ workload.
- CMake 3.24 or newer and Ninja.
- Internet access on the first full configure to obtain the pinned ZIP dependency.

The application lives in the workspace's `Qt` directory. From PowerShell in
this directory:

```powershell
.\scripts\build.ps1 -QtRoot 'C:\Qt\6.8.3\msvc2022_64' -Test -Package
```

The default Qt SDK location is `..\.tools\Qt\6.8.3\msvc2022_64`, shared at the
workspace root rather than inside the application sources.
The script initializes the MSVC environment, builds the application, optionally
runs the tests, and creates a deployable directory and ZIP under `dist`.

From the workspace root, the equivalent command is
`.\Qt\scripts\build.ps1 -Test -Package`. Build output is under `Qt\build` and
release output is under `Qt\dist`. CMake build trees contain absolute paths;
configure a new build tree after relocating the source directory instead of
reusing an old cache.

Launch `dist\HExplorer\HExplorer.exe`. An optional command-line folder opens in
the active pane:

```powershell
.\dist\HExplorer\HExplorer.exe 'D:\Projects'
```

The source build executable needs the matching Qt DLLs on `PATH`; the deployed
copy includes the runtime dependencies. Do not copy just the source build EXE.

## Use

Choose **Layout** for one pane, two columns, three columns, or four quadrants.
The active pane can also be split in either direction. Drag the boundaries to
resize them; a workspace supports up to 16 panes. Panes keep independent folder
histories, filters, sorting, view modes, and selections.

**View** controls the sidebars, swaps their content, and selects the system,
light, or dark theme. The folder tree loads on demand. The preview sidebar
supports local images, bounded text, and metadata; **Pin** keeps a preview while
browsing. Files are never uploaded to a preview service.

### Quick Access and color tags

The command bar is 48 device-independent pixels high. Quick Access headings
are left-aligned; subtle, edge-faded separators distinguish Quick Access,
color tags and the folder tree in both light and dark themes.

Click the Quick Access heading to collapse or expand its entries. The last
state is saved immediately and restored on the next launch. Open the sidebar's
gear button or **View > Navigation settings** to hide/show the entire section,
remove individual entries, add a named local/network folder, or restore the
default entries. Hiding the section does not erase its custom entries; removing
an entry does not delete the folder.

Select one or more files/folders and use **Tags** in the command bar or
**Color tags** in the file context menu. Each item can have multiple colors.
Checked colors apply to every selected item; a "some selected items" label
indicates mixed tagging. Choose a color to apply/remove it, or remove all tags.
Colored dots appear in both list and grid views.

Click a sidebar color to show all known items with that tag across directories
in the active pane. The address uses `hexplorer-tag:/red` (or another color);
Back/Forward works normally. The name filter and sorting also work in this
view. List mode shows each item's original path to distinguish matching names.
Double-click a tagged folder to browse it, or use **Show in Explorer** to reveal
one selected item. Paste, new-folder creation and extraction require a real
destination folder; dropping onto a folder result is still supported.

Tags are local HExplorer metadata stored with its QSettings, not file contents,
NTFS alternate streams or macOS Finder metadata. Successful in-app rename/move
operations migrate tags, including directory descendants, keep-both names and
the completed portions of partially successful/cancelled moves. Moving into an
existing directory preserves its own folder tags and combines source folder
tags; replacing a file follows the moved source's tags. Copying does not
duplicate source tags.

Operations performed outside HExplorer, including rename/move commands inside
the optional Windows context menu, are not tracked. Missing/offline tagged
items are omitted with an explicit notice and retain their metadata; reconnect
and refresh to show them again. Since these records are path-based, externally
replacing a file at the same path can inherit its old tag. The tag view is not a
recursive disk search or a live system-wide index; use Refresh after external
changes. No file is uploaded or modified just to assign a color.

Drag files between panes or between HExplorer and Windows Explorer. Folder rows
are drop targets; blank space targets the current pane's directory. Same-volume
drops prefer moving, cross-volume drops prefer copying. Hold Ctrl to copy or
Shift to move. Shortcut creation and non-filesystem virtual drag payloads are
not supported.

Copy, move, delete and ZIP extraction are represented in **Tasks**. Closing the
task panel does not stop them. Conflicts can be replaced, skipped or given a new
name, optionally for the remainder of that task. Cancellation keeps completed
work rather than pretending to roll it back. Closing the application while it
is busy asks whether to cancel unfinished work.

Tasks display byte totals and logical processing speed. Same-volume moves to
absent destinations use native rename, including whole directories, rather
than copying their contents. Copy fallbacks preserve file modification times
and those of newly created directories.

| Shortcut | Action |
|---|---|
| Ctrl+1 / 2 / 3 / 4 | Layout preset |
| Ctrl+Tab | Next file pane |
| Ctrl+L | Edit the current folder address |
| Ctrl+F | Filter names in the current folder, not a recursive search |
| Alt+Left / Right / Up | Back / forward / parent |
| F5 | Refresh |
| Ctrl+A | Select all files in the focused file view |
| Ctrl+C / X / V | Copy / cut / paste |
| F2 | Rename one selected item |
| Ctrl+Shift+N | New folder |
| Delete | Recycle selected items, with confirmation |
| Shift+Delete | Permanently delete, with confirmation |
| Alt+Enter | Windows properties |

File-view shortcuts do not replace normal editing shortcuts in address,
filter, or text-preview controls. Ctrl-click, Shift-click, arrow keys, and
blank-space rectangle selection are supported.

## Architecture

| Area | Responsibility |
|---|---|
| `src/core/DirectoryModel.*` | Cancellable, batched background directory enumeration; sorted and filtered file model; selection |
| `src/core/PaneController.*` | Per-pane navigation, history and view state |
| `src/core/Workspace.*` | Binary split tree, normalized geometry and layout persistence |
| `src/core/AppController.*` | Application commands, active-pane coordination, settings and UI requests |
| `src/core/NavigationSettings.*` | Persistent Quick Access visibility, expanded state and custom entries |
| `src/core/TagStore.*` | Local multi-color metadata and rename/move path migration |
| `src/operations/` | Background task queue, copy/move policies, conflict handling, cancellation and ZIP extraction |
| `src/platform/` | Windows Shell operations, native drag and clipboard, icons, preview |
| `qml/` | Fluent controls, presentation, keyboard and pointer interaction |
| `tests/` | QtTest coverage and application smoke test |

Directory workers do not mutate Qt models directly. Results are delivered to the
model thread and tagged with a generation so stale scans cannot replace a newly
opened directory. File tasks are independent from the pane that started them.

Windows uses a normal decorated top-level window instead of a simulated QML
title bar, retaining native resize, snap, taskbar and system-menu behavior.
Settings use `QSettings`, including the split tree, pane folders and sidebar
sizes. Running tasks are not persisted as resumable jobs.

## Safety and current scope

This version targets ordinary filesystem files and directories. It is not a
replacement for every Windows Shell namespace extension.

- No elevation service, virtual folders, cloud-placeholder hydration UI, or
  universal undo.
- No task pause/resume or recovery across application restarts.
- ZIP support does not imply support for every encrypted ZIP or other archive
  format. ZIP names must be UTF-8; extraction is limited to 100,000 entries and
  10 GiB of total uncompressed data. Unsupported inputs report an error.
- File operations reject symbolic links, junctions and other reparse points.
  Copy fallbacks reject NTFS alternate streams instead of silently dropping
  them. They do not promise ACL, extended-attribute or sparse-file preservation.
  Native same-volume rename preserves the item's native metadata.
- File-operation cancellation is cooperative. An operating-system call blocked
  on an unavailable network share cannot always be interrupted immediately.
- Preview is intentionally limited; there is no embedded browser, Office
  renderer, or execution of arbitrary preview-handler plugins.
- Windows integration is isolated for future platform adapters; macOS and Linux
  do not have the same native integration guarantees.

Review the operation errors before retrying a partially completed task. Always
keep separate backups of important files.

## Tests and packaging

`ctest --test-dir build --output-on-failure` runs the registered tests when Qt is
on `PATH`. `HExplorer --smoke-test` uses temporary fixture files and isolated
settings, exercises layouts and previews, submits a copy, opens the task UI and
exits with a failure code on QML warnings or incomplete copying. It also sends
mouse-selection and file-drop events through the real QML view, covering both
blank-space and folder-row drop targets.

For model-only development, configure `-DHEXPLORER_CORE_ONLY=ON`. This avoids the
native application and ZIP integration targets. Set it back to `OFF` for the
full application.

The portable ZIP is unsigned. Windows may display the usual reputation warning
for an unsigned executable. No file associations, shell extensions or
administrator privileges are installed automatically.

## Third-party licensing

Qt is dynamically linked. An open-source Qt installation is subject to the
applicable Qt LGPL/GPL and third-party terms; commercial Qt licensing is a
separate option. The ZIP dependency has its own license. Review the license
notices and redistribution/source/relinking obligations before distributing a
build to other users. This repository does not grant a separate license to Qt
or its dependencies.
