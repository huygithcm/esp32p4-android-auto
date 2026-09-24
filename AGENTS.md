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

## Multi-agent workflow for this repository

The user requests multi-agent work in this folder. For substantial tasks with
independent subtasks, use parallel agents when the client supports delegation.
Keep trivial edits with the primary agent. Respect the runtime's lower limits.

- The primary agent coordinates up to three concurrent subagents, assigns
  explicit file ownership, integrates results and performs final verification.
- Use `code_mapper` for read-only exploration, `implementer` for scoped edits,
  and `reviewer` for independent read-only review. If custom roles are not
  exposed by the client, pass these responsibilities in subagent task prompts.
- Only one agent may edit a given file at a time. Assign disjoint scopes before
  parallel edits; serialize shared-header, build-config and integration changes.
  All agents must preserve user/Claude changes already present in the tree.
- Only the primary agent updates `COLLABORATION_LOG.md`; workers return their
  changed files, verification results and handoff notes to it.
- Serialize builds that share output directories, generated files, simulator
  instances, serial ports or hardware. Workers must coordinate these resources
  with the primary agent before use. Subagents must not spawn more agents.
- Use the parent model and reasoning settings unless the user requests another
  setup. Project settings do not bypass sandbox, approval or trust restrictions.

Project configuration: `.codex/config.toml`; role definitions:
`.codex/agents/*.toml`. Start a new Codex session in this folder to reload them.
See `docs/MULTI_AGENT.md` for usage and client limitations.
