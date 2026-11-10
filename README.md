# HExplorer workspace

The Qt/C++/QML desktop application lives in [`Qt`](Qt/README.md).
The installed Qt SDK stays in `.tools` as a shared development dependency.

```text
Qt/
  CMakeLists.txt
  src/           C++ models, controllers, tasks and platform integration
  qml/           Fluent Qt Quick interface
  tests/         QtTest coverage
  scripts/       Windows build and packaging
  assets/
  cmake/
  packaging/
  build/         Generated build tree
  dist/          Portable application and ZIP
.github/skills/  Project-local agent skills
.tools/         Shared SDK, not application source
```

## Build and run

From PowerShell at the workspace root:

```powershell
.\Qt\scripts\build.ps1 -Test -Package
.\Qt\dist\HExplorer\HExplorer.exe
```

Pass `-QtRoot 'C:\Qt\6.8.3\msvc2022_64'` to use another SDK.
See `Qt\README.md` for prerequisites, shortcuts, architecture and limitations.

The application version is in `Qt\VERSION.txt`. On `main`, Release Please
automatically maintains a version PR: `feat:` changes propose a minor release,
`fix:` changes a patch. Merging that Release PR creates a matching `v` tag and
calls the Windows build/release workflow directly, publishing the portable ZIP,
per-user installer and SHA-256 checksums only after validation. Manual workflow
runs only produce artifacts; manually pushed matching tags also support
publication. Repository owners must allow Actions to create pull requests.
See `Qt\README.md` for setup, conventional-commit conventions and recovery.
In-app version/update controls are under **View > Settings > About and updates**;
startup checks are opt-in.

## Solution skills

Each skill is a directory under `.github\skills` with a `SKILL.md` entry point:

| Skill | Use for |
|---|---|
| `hexplorer-architecture` | C++/QML boundaries, navigation, models and workspace state |
| `hexplorer-fluent-ui` | Fluent controls, dynamic panes, sidebars, focus and previews |
| `hexplorer-file-operations` | Copy/move/extract safety, native drag/drop and clipboard |
| `hexplorer-build-release` | MSVC/Qt builds, targeted tests and standalone Windows packaging |
| `hexplorer-git-sync` | Synchronize every successful primary-branch commit with the same branch on origin |

Repository policy: after every successful commit on a primary branch, push that
branch to `origin` and verify that the local and remote tips match. This covers
code, documentation, configuration and Skills. It does not authorize automatic
commits or force pushes; report blocked synchronization explicitly.

Skills describe the current implementation and its constraints. They do not
install dependencies, start background services, or add runtime application
features merely by being present.
