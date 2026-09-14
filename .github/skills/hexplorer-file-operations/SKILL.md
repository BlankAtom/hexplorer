---
name: hexplorer-file-operations
description: "Implement or troubleshoot HExplorer background copy, move, recycle, delete and ZIP extraction, including collisions, cancellation, Windows drag/drop and cut/paste. Use when changing file-operation correctness or native transfer semantics."
---

# HExplorer file operations and native transfers

Paths are workspace-root relative. Read `Qt\src\operations\TaskManager.h`
for the current contract and `FileOperationWorker.*` for actual execution.
Also inspect `Qt\src\core\AppController.*`,
`Qt\src\platform\NativeServices.*`, and `Qt\qml\FilePane.qml` for callers.

## Contracts to preserve

- TaskManager serializes background workers. Qt model changes and user-facing
  signals run on the model/UI thread.
- Operations are `copy`, `move`, `recycle`, `delete`, `extract`.
  Conflict policies are `ask`, `overwrite`, `skip`, `rename`.
- Statuses distinguish queued, running, waiting, completed, partial, failed and
  cancelled. Partial success is not success and cancellation is not rollback.
- Cancellation must wake a worker waiting on a conflict, and shutdown must join
  workers safely. Do not add pretend pause/resume controls.
- Each task retains its own source and destination; it must not consult the
  active pane midway through execution.

## Data-loss safeguards

1. Validate local paths, same-file/hardlink identity, source descendants and
   destination ancestors before mutation. Reject unsupported reparse points.
2. Use native no-overwrite rename for absent-target same-volume moves. Never
   treat Qt rename's possible copy fallback as an atomic native move.
3. Copy fallbacks use QSaveFile with direct-write fallback disabled. Commit the
   destination before deleting a move source; revalidate source identity and
   observed content metadata before removal.
4. Preserve the current permissions/mtime guarantees and surface failures.
   Do not claim ACL, sparse-file or extended-attribute preservation.
   Unsupported alternate streams must not be silently discarded.
5. On cancellation, remove only the current uncommitted temporary output.
   Keep completed work and report the true outcome.
6. Recycle must never silently degrade to permanent deletion. The UI confirms
   deletion; native worker dialogs must not bypass application coordination.

Existing directories merge; overwrite does not replace a file with a directory
or vice versa. Follow the current worker's target identity checks rather than
adding a second collision implementation in QML.

ZIP extraction uses commit-pinned miniz configured in `Qt\cmake\Archive.cmake`.
Preserve checks for traversal, absolute paths, ADS, Windows reserved names,
links, duplicate/ambiguous names and destination reparse ancestors. Current
limits are 100,000 entries, 10 GiB uncompressed data and UTF-8 names. Any change
to limits or supported formats requires matching tests and documentation.

## Native drag/drop and clipboard

- Same-volume drops prefer move, cross-volume drops copy; Ctrl requests copy,
  Shift requests move. Unsupported actions must be rejected honestly.
- QML DropArea only queues a task; it does not complete the transfer.
- For external sources, acknowledge Copy only when it is offered. Never
  acknowledge Move merely because an asynchronous move was queued: some
  sources may delete data themselves. Reject external move-only offers.
- Owned drags may acknowledge Move because NativeServices never deletes source
  files based on the returned drop action. Evaluate `ownsDrag()` synchronously.
- Preserve `QList<QUrl>` for QML incoming URL conversion and the C++ modifier
  bridge; Qt 6.8 DragEvent has no QML `modifiers`.
- Clear a cut clipboard only on fully successful move and exact matching URL
  set. Never clear newer clipboard content or claim a partial move completed.

## Validation

Use temporary fixture directories, never real user files. Extend
`Qt\tests\operationtests.cpp` for affected conflict, identity, cancellation,
metadata and extraction behavior. Malicious archive fixtures may need binary
filename patching because the ZIP writer itself rejects unsafe names.
Remove test junctions/symlinks themselves without traversing their targets.

Run CTest `operations`; add `app`, `platform`, and `qml_smoke` when transfer
integration changes. Test source preservation on failure, not just destination
existence on success. Progress byte/rate values represent logical work, not
physical device throughput, especially for native renames.
