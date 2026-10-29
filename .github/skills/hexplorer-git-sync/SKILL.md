---
name: hexplorer-git-sync
description: "Apply this repository's post-commit origin synchronization requirement. Use whenever committing changes on a primary branch, finishing repository changes, or troubleshooting a failed origin push."
---

# HExplorer primary-branch synchronization

## Required repository policy

For this repository, every successful commit on a primary branch must be
followed by synchronization to the same branch on `origin`. This applies to all
changes, including source, documentation, configuration and Skills.

This policy authorizes the post-commit push, not automatic commits. Do not
create commits, switch branches, or merge feature branches merely to satisfy
this policy. It does not automatically require pushing feature branches.

## Identify the branch and remote

Before committing, inspect the current branch, working tree and `origin`:

```powershell
git status --short
git branch --show-current
git remote get-url origin
git symbolic-ref --quiet --short refs/remotes/origin/HEAD
```

Treat the repository's configured default branch and any explicitly designated
primary branches as primary branches. Do not infer that a feature branch is a
primary branch because it tracks an origin branch. If origin/HEAD is absent,
use `git ls-remote --symref origin HEAD` to discover the remote default branch.
If primary-branch identity remains ambiguous, ask rather than guessing.

## Synchronize after a successful commit

For the verified primary branch, replace `<primary-branch>` below with its
exact branch name. Use explicit source and destination refs:

```powershell
git push origin "refs/heads/<primary-branch>:refs/heads/<primary-branch>"
```

Do not use force, force-with-lease, `--mirror`, or `--all`. Push only the intended
branch to the existing origin; never rewrite origin or switch GitHub accounts
without authorization.

After the push succeeds, compare the local branch tip with the live origin tip:

```powershell
git rev-parse "refs/heads/<primary-branch>"
git ls-remote --exit-code origin "refs/heads/<primary-branch>"
```

Only report synchronization complete when the SHAs match. A successful local
commit is not evidence of a successful push.

## Failure handling

If origin is missing, authentication fails, permission is denied, branch
protection blocks the push, or the remote rejects a non-fast-forward update:

- Preserve the local commit and report that synchronization is incomplete.
- Explain the specific blocker and the action needed to resolve it.
- Do not reset, rebase, merge, force-push, bypass protection, or create a fork
  as an automatic workaround.
- Fetch/read remote state when needed for diagnosis; request authorization
  before integrating divergent history or changing the publication workflow.

If new commits arrive on origin during verification, report the differing tips
rather than retrying destructively or claiming the branches are synchronized.
