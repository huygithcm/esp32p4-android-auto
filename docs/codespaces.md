# Persistent P4 development in GitHub Codespaces

Create a Codespace from the `codex/p4-workspace-setup` branch using this
repository's `.devcontainer/devcontainer.json`. Select a 2-core machine first.
The first setup downloads ESP-IDF v5.5.3 and the ESP32-P4/ESP32 toolchains;
allow several minutes. No firmware build runs automatically.

The default ESP-IDF terminal activates the SDK when opened at the repo root.
In another terminal:

```bash
. scripts/workspace_env.sh
scripts/build_board.sh waveshare build
# For Guition:
scripts/build_board.sh jc4880 build
```

SDK, toolchain caches and the repository live under `/workspaces`, so they
survive a dev-container rebuild. A deleted Codespace removes these files.

## Retention and cost settings

In https://github.com/codespaces, open the Codespace's menu and select
**Keep codespace** to exempt a personal Codespace from automatic deletion.
This is a per-Codespace setting, not a devcontainer.json property. Storage
continues consuming your allowance or incurring charges while kept.
Set idle timeout to 30 minutes in https://github.com/settings/codespaces and
explicitly stop the Codespace when finished. Closing the browser does not stop it.
Review usage and spending limits in GitHub Billing before extended use.

Default inactive retention is 30 days; Keep codespace retains it until manually
deleted unless an organization policy disallows that option.

## Durable timeline

Use Git commits for the development timeline. Commit and push changes at the
end of a work session; Codespace storage alone is not a backup.
VS Code's file Timeline can show Git history for committed files.
ChatGPT/Codex chat history is separate and is not copied into Codespaces.
Record decisions and verified results in `docs/progress.md`.

```bash
git status
git add <changed-files>
git commit -m "Describe the completed change"
git push
```

## References

- https://docs.github.com/en/codespaces/about-codespaces/understanding-the-codespace-lifecycle
- https://docs.github.com/en/codespaces/setting-your-user-preferences/configuring-automatic-deletion-of-your-codespaces
