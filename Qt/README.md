# HExplorer

A Windows 11-first, Qt 6 desktop file manager with Fluent WinUI 3 controls,
independent file panes, a folder tree, local previews, and background file tasks.
The UI is QML; filesystem policy and application state live in C++.

## Versions, releases and updates

The current application version is defined only in `VERSION.txt` (currently
`0.2.0`). CMake generates the C++ version header, Windows EXE version resource
and portable package version from it. Use a stable `major.minor.patch` value;
each component must fit a Windows version field. The `VERSION.txt` extension
is intentional: a file named `VERSION` shadows the C++ `<version>` header on
case-insensitive Windows filesystems.

Open **View > Settings > About and updates** for the installed version, release
notes and **Check for updates**. Startup checking is opt-in and off by default.
The application contacts the public GitHub Releases endpoint for
`BlankAtom/hexplorer`; no token, file contents or local paths are sent.
An unpublished/private repository, network failure or rate limit is reported
explicitly. Development/preview tags are not offered as stable updates.

The Windows x64 update method is **interactive per-user Inno Setup**, installed
by default in `%LOCALAPPDATA%\Programs\HExplorer`. It requires no elevation and
uses a stable AppId so later installers update the same installation. Settings,
workspace state and color tags remain in the existing user's QSettings.
Windows 10 1809 or newer is required; Windows 11 remains the primary target.

Updates are never installed silently by the application. After checking, the
user chooses Download; the C++ updater checks the HTTPS origin, exact asset
name, declared byte count and GitHub's release-asset SHA-256 digest. The file is
written atomically with bounded buffering and is rechecked before launch.
Downloads can be cancelled. Installation requires another explicit confirmation,
and file operations must be finished first. HExplorer then saves its workspace,
starts the setup wizard and closes. Setup refuses to modify the installation
while a new-version HExplorer instance holds its running mutex; it does not
force-close processes. Restart from the installer's final page or Start menu.

Portable copies are **not overwritten** by the updater: installing from a
portable copy creates/updates the managed per-user installation. Users wanting
to remain portable should download the release ZIP and extract it to a new
directory, then switch to that copy. Existing pre-updater versions cannot
self-update; install the first update-capable version manually once.

Releases are currently **unsigned**. A SHA-256 match checks download integrity
against GitHub metadata; it is not Authenticode publisher verification or
protection against a compromised publisher account. Do not bypass SmartScreen
or other Windows security warnings. Code signing and secure signing credentials
must be configured separately before claiming signed distributions.

### GitHub Actions

`.github\workflows\windows-release.yml` builds Windows x64 on pull requests,
manual dispatches, `v*` tag pushes, and calls from the release-PR workflow.
It runs CTest, deploys the runtime, creates
the portable ZIP and per-user setup, exercises installation/reinstallation and
uninstall, and uploads CI artifacts. A **tag push** or a **merged Release PR**
publishes a Release; ordinary CI/manual builds do not.
The tag must exactly match `v` plus `Qt\VERSION.txt` (for example `v0.2.0`).
A manual dispatch is a build-only dry run, not a publication request.

#### Automatic Release PRs

`.github\workflows\release-please.yml` runs on pushes to `main`, including
merged pull requests. Release Please v5 maintains a version PR based on
Conventional Commits:

| Commit / squash-merge title | Version change from 0.2.0 |
|---|---|
| `feat: add a feature` | `0.3.0` (minor; patch resets to zero) |
| `fix: repair a bug` | `0.2.1` (patch) |
| `feat!: change a contract` | `0.3.0` while below 1.0; major after 1.0 |
| `docs: ...`, `ci: ...`, `chore: ...` only | No standalone release PR |

Use the conventional prefix in the **squash-merge commit title**, not just the
individual commits if those will be squashed. Several feature PRs can accumulate
in one Release PR; merging every ordinary PR does not immediately publish a
version. The highest applicable change determines the pending version.

The Release PR updates `Qt\VERSION.txt`, `.release-please-manifest.json`, and
generates `Qt\CHANGELOG.md`. Do not manually edit a generated Release PR's body
or remove its `autorelease` labels: Release Please uses them to recognize a
merged release. The initial manifest is seeded at `0.2.0` with the existing
release implementation commit as the bootstrap boundary; older changes are not
re-released. The next feature after that boundary proposes `0.3.0`.

When the Release PR is merged, Release Please creates the exact `vX.Y.Z` tag
and an **empty draft** Release. Its outputs directly call the reusable Windows
workflow with the tagged commit SHA and draft ID. The build verifies the tag,
source SHA and `VERSION.txt` agree. After all assets and digests are validated,
the same draft is published, retaining its generated changelog notes.

No PAT is required: `GITHUB_TOKEN`-generated tags do not trigger another push
workflow, so publication uses `workflow_call` explicitly. Likewise, a generated
PR does not automatically trigger `pull_request` CI; the preparation job
explicitly dispatches the build-only Windows workflow on the PR's branch.
This keeps checks attached to the PR head instead of the `main` preparation
commit. Branch protections still apply; configure required checks to match the
Windows build rather than the conditional publication job.

Repository prerequisites: Actions must be enabled and the repository owner
must enable **Settings > Actions > General > Workflow permissions > Allow
GitHub Actions to create and approve pull requests**. If an organization policy
disallows this, its administrator must allow it or supply an approved GitHub
App/PAT design separately; this configuration does not bypass that restriction.
The automation does not approve or merge Release PRs for you.

Release assets are:

```text
HExplorer-0.2.0-win64.zip
HExplorer-0.2.0-windows-x64-setup.exe
HExplorer-0.2.0-windows-x64-setup.exe.sha256
SHA256SUMS.txt
```

The publication job has `contents: write`; normal build jobs are read-only.
The separate Release Please preparation job has contents/PR/issue writes for
branches, tags, release PRs and labels, plus `actions: write` for explicit PR CI.
The publication job uploads all assets to a draft, compares GitHub's installer
digest with the built file, and only then publishes as latest. Reusable publication accepts
only the specific empty draft ID supplied by Release Please. Manual tag pushes
create a new draft and refuse existing releases. If publishing fails, existing
assets are never overwritten: inspect the failed run and draft before retrying.
Re-running a failed publication run can retry an empty draft; if assets were
already uploaded, it intentionally stops for manual review instead of replacing
or deleting files. A failed draft stays unpublished, so the app updater cannot
offer a partial release. Do not expect a later unrelated push to retry an
already-tagged release automatically.
No repository/account settings, tags, PRs or public releases were changed by
adding these files.

Normal release flow: merge conventional feature/fix PRs, review the generated
Release PR, and merge it when ready. The manual fallback is to update VERSION.txt
and the manifest consistently, commit, and push the matching version tag when
authorized. Never put signing keys/tokens in source files; Actions uses its scoped
`GITHUB_TOKEN` for publication.

### Local installer packaging

After generating the portable deployment:

```powershell
.\scripts\install-inno.ps1
.\scripts\package-windows.ps1 -ExpectedTag v0.2.0
```

`install-inno.ps1` obtains a fixed Inno Setup 6.7.4 release and verifies its
Authenticode publisher before running its compiler installer. Review Inno
Setup's licensing requirements for your intended use. An existing compiler may
instead be supplied with `-Iscc`. `package-windows.ps1` refuses a deployed EXE
whose embedded version differs from VERSION.txt.

If an older portable EXE is open, do not overwrite or terminate it. Deploy to
a separate location and point the installer packager there:

```powershell
.\scripts\build.ps1 -Test -Package -DeployDirectory .\dist\HExplorer-0.2.0
.\scripts\package-windows.ps1 -SourceDirectory .\dist\HExplorer-0.2.0 -ExpectedTag v0.2.0
```

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
| `src/updates/UpdateController.*` | Release parsing, opt-in checks, bounded downloads and verified installer handoff |
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

The portable ZIP and installer are unsigned. Windows may display a reputation
warning. No file associations, shell extensions or administrator privileges are
installed automatically. The installer adds Start menu/uninstall entries and
an optional desktop shortcut, without deleting QSettings on uninstall.

## Third-party licensing

Qt is dynamically linked. An open-source Qt installation is subject to the
applicable Qt LGPL/GPL and third-party terms; commercial Qt licensing is a
separate option. The ZIP dependency has its own license. Review the license
notices and redistribution/source/relinking obligations before distributing a
build to other users. This repository does not grant a separate license to Qt
or its dependencies.
