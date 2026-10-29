---
name: hexplorer-architecture
description: "Implement or explain HExplorer's Qt 6 C++/QML architecture, directory models, navigation, selection, split-tree layouts and session state. Use when changing application behavior, wiring C++ APIs to QML, or deciding where logic belongs."
---

# HExplorer architecture

For repository commits, also follow
`..\hexplorer-git-sync\SKILL.md`: after each successful primary-branch commit,
synchronize that branch with `origin` and verify the remote tip.

## Locate the solution

The workspace root contains `Qt\CMakeLists.txt`. All application paths below
are relative to the workspace root, not to this skill's directory.
If the current directory is already `Qt`, omit that leading component.
Do not search `.tools`, `Qt\build`, or `Qt\dist` for application source.

Read the relevant headers and their implementations before changing APIs:

| File | Responsibility |
|---|---|
| `Qt\src\core\DirectoryModel.*` | Background enumeration and generation rejection |
| `Qt\src\core\DirectoryModel.*` (`DirectoryProxy`) | Model roles, filtering, sorting, selection and box selection |
| `Qt\src\core\PaneController.*` | Directory navigation, history, per-pane options and scroll position |
| `Qt\src\core\Workspace.*` | Binary split tree, normalized pane rectangles, active pane, persistence |
| `Qt\src\core\AppController.*` | Commands, settings, preview coordination and UI requests |
| `Qt\src\main.cpp` | Type registration, context object, image provider and QML entry point |

## Preserve the boundaries

- C++ owns filesystem policy, navigation/history, selected paths, layout state,
  task scheduling and persistence.
- QML owns presentation and local interaction state: hover, pointer coordinates,
  focus, popup visibility and view-coordinate calculations.
- Expose observable state with `Q_PROPERTY` and correct `NOTIFY` signals.
  Use `Q_INVOKABLE` for commands and signals for requests to show dialogs.
- `appController` is currently a context property. Controller types are
  registered as uncreatable; QML references objects owned by C++.
- Search existing controllers for a command before adding parallel state or a
  second implementation. Do not duplicate selected files in a JavaScript array.

## Implementation workflow

1. Trace the affected user action from QML to the C++ command and model update.
2. Change the responsible C++ model/controller and add narrowly scoped tests.
3. Update all QML callers, model role names, property notifications and type
   registrations affected by the API.
4. Check the active-pane route as well as inactive panes. Tasks must outlive the
   pane that submitted them.
5. If state shape changes, keep saved layouts recoverable and expose invalid
   state rather than silently losing it.

Directory workers must not mutate Qt models from worker threads. Queue results
to the owning thread and reject stale generations after navigation. Keep
cancellation and lifetime handling intact; a stale result must not target a
destroyed controller.

The central workspace is NOT a recursive QML SplitView. C++ computes rectangles
from a binary tree; QML uses Repeaters for panes and dividers. The outer
SplitView controls sidebar widths. Preserve pane identities when changing
layouts so histories and selection are not accidentally exchanged.

## Validation

Use the existing `coretests` and `apptests` targets and CTest names `core` and
`app`. Cover changed history, filtering, selection, stale scans, layout
geometry, persistence and command routing. For a C++ API used by QML, also run
`qml_smoke`; compilation alone does not prove QML argument conversion works.
Use the `hexplorer-build-release` skill for environment and command details.
