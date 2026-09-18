# Shared workspace instructions

This repository is edited by the user, Claude, and Codex in the same working
tree. The canonical shared root is:

`C:\esp32p4-android-auto`

Before changing files:

1. Read `CLAUDE.md` and `COLLABORATION_LOG.md`.
2. Run `git status --short --branch` and inspect diffs in files that will be
   touched.
3. Treat every pre-existing modification as owned by another collaborator
   unless ownership is explicitly known. Do not reset, revert, overwrite, or
   reformat it.

While working:

- When the user requests an interactive simulator window, launch it outside
  the sandbox (request the required tool approval) with `-WindowStyle Normal`
  and `SDL_VIDEODRIVER=windows`. Do not use a sandbox launch for visible UI.
  Build/headless tests may stay sandboxed. A running process/window handle is
  not proof that the user can see it. See `docs/SIMULATOR_WINDOWS_LAUNCH.md`.

- Use repository-relative paths in source, scripts, and documentation. Only
  use the canonical absolute root when an absolute path is unavoidable.
- Keep changes narrowly scoped and re-check the working tree before editing a
  file that another collaborator may also be changing.
- Do not run `git pull`, rebase, checkout, reset, clean, or stash over a dirty
  working tree without explicit coordination.
- Add an entry to `COLLABORATION_LOG.md` for material work, including files
  changed, checks run, status, and any handoff or unresolved issue.
- Do not claim another collaborator's uncommitted changes as your own.

Before handing off:

1. Run the relevant tests or document why they were not run.
2. Run `git status --short --branch` again.
3. Update `COLLABORATION_LOG.md` so the next collaborator sees the current
   state without reconstructing it from Git history.
