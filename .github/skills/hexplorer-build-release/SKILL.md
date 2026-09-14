---
name: hexplorer-build-release
description: "Build, test, run or package HExplorer on Windows with Qt 6.8+, MSVC, CMake and Ninja. Use for missing DLLs, QML smoke failures, source relocation, targeted CTest runs and standalone portable ZIP releases."
---

# HExplorer Windows build and release

## Paths and prerequisites

The workspace contains `Qt\CMakeLists.txt`. The shared default SDK is
`.tools\Qt\6.8.3\msvc2022_64` at the workspace root. Application build output is
`Qt\build`; deployment output is `Qt\dist`. Do not confuse the source directory
named `Qt` with the installed SDK under `.tools\Qt`.

Use Qt 6.8+, MSVC desktop C++ tools, CMake 3.24+ and Ninja. Inspect the existing
script before issuing commands:

```powershell
.\Qt\scripts\build.ps1
.\Qt\scripts\build.ps1 -QtRoot 'C:\Qt\6.8.3\msvc2022_64'
```

The script locates Visual Studio via vswhere and initializes vcvars64 in the
same cmd.exe process as CMake/build. Do not initialize vcvars in one tool call
and assume it survives in another. Prefer tools already installed. Install
dependencies only when needed, not on every build.

## Targeted validation

The existing target / CTest-name pairs are:

| Target | CTest name | Covers |
|---|---|---|
| `coretests` | `core` | Directory models, navigation, selection, layout persistence |
| `apptests` | `app` | Command routing, clipboard, drop negotiation |
| `operationtests` | `operations` | Copy/move/delete/ZIP and failure safeguards |
| `platformtests` | `platform` | Native services, icons and preview contracts |
| `HExplorer` | `qml_smoke` | UI loading, selection, drops, previews, layout changes |

After building, use the appropriate selector with Qt DLLs on PATH:

```powershell
$env:PATH = (Join-Path $PWD '.tools\Qt\6.8.3\msvc2022_64\bin') + ';' + $env:PATH
ctest --test-dir .\Qt\build -R '^(app|qml_smoke)$' --output-on-failure
```

Resolve `ctest` from the installed CMake tools if it is not on PATH. Run commands
from the workspace root or adjust paths explicitly. Use the smallest affected
set. The release/all-tests command is:

```powershell
.\Qt\scripts\build.ps1 -Test -Package
```

For GUI diagnostics read `Qt\build\qml-smoke.log`. QtTest supports
`-o -,txt` or `-o '<absolute-file-path>,txt'`; quote the full latter argument.
Do not pass unsupported `qmllint --syntax-only` to this Qt version.

## Portable packaging

Expected outputs:

```text
Qt\dist\HExplorer\HExplorer.exe
Qt\dist\HExplorer-<version>-win64.zip
```

Use CMake's existing Qt QML deployment script and CPack. Keep
`CMAKE_INSTALL_BINDIR:STRING=.` relative: declaring it as a PATH can turn `.`
into an absolute source path and break deployment. Qt DLLs, qt.conf and
app-local MSVC runtime libraries must be located consistently with the EXE.
Normalize Windows environment paths with `file(TO_CMAKE_PATH ...)` before
generating CMake install commands.

After packaging, extract the ZIP into a named temporary directory under
`Qt\build`, find the packaged executable, and run `--smoke-test` with a log path.
Remove the SDK from that process's PATH so it cannot hide missing runtime DLLs.
Check its exit code and log. Inspect a `--smoke-screenshot` when UI changed.
Then remove only the explicitly identified temporary extraction directory.

A package is not ready merely because the build-tree executable starts.
Do not claim code signing, installer integration or complete license compliance:
the current output is an unsigned portable development release. Keep NOTICE,
miniz license and redistribution caveats in the package.

## Relocation and running the app

CMake caches, Ninja files, dependency sub-builds and generated deploy scripts
contain absolute paths. After moving sources, create a clean build tree rather
than editing generated cache paths. Preserve existing outputs until the new
build succeeds, then clean up only known generated artifacts. Leave the shared
SDK in place; its installed metadata may also contain absolute paths.

To run interactively, launch `Qt\dist\HExplorer\HExplorer.exe` with its directory
as working directory, confirm the process remains alive and has a responsive
window. Keep the process attached to the session unless survival beyond the
session was explicitly requested. Never terminate unrelated processes by name.
