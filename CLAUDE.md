# CLAUDE.md

Guidance for working on MDU (Miku Desktop Unit), a Windows 11 dock and theming
layer. C++23 + Win32 + WebView2 natively, TypeScript/HTML/CSS for the UI.

## Non-negotiables

- Stable releases only. No alpha, beta, preview, RC or nightly dependencies,
  SDKs or tools. Pin versions and verify downloads by hash (`cmake/Dependencies.cmake`).
- Coexist with Explorer. Never replace the shell, never hide or modify the
  taskbar, never require administrator rights for normal operation.
- Windows must stay usable and recoverable if MDU fails: no self-restart, the
  crash guard decides safe mode, Shift at start forces safe mode.
- No telemetry and no network requests. WebView2 may only show
  `https://mdu.example/` (mapped to `web/`) and load theme files from
  `https://themes.mdu.example/` (mapped to `themes/`).
- The frontend is untrusted. The dock page sends only messages listed in
  `src/ui/bridge.cpp`, the settings page only those in
  `src/ui/settings_protocol.cpp`; anything they reference (such as an item to
  launch) is looked up in native configuration. Never accept paths or commands
  from a page: files are chosen through native dialogs. All WebView2 windows
  go through `ui/webview_host` for the same lockdown.
- Nothing theme-specific in core code. Visual choices belong in
  `themes/<id>/theme.json` and `theme.css`.
- Never commit third-party artwork. The Miku wallpaper is user-supplied and
  gitignored (`themes/*/wallpaper/*.jpg|png|...`). Never modify the user's
  original wallpaper file; adaptations go to `%LOCALAPPDATA%\MDU\cache\`.
- Respect reduced motion and per-monitor DPI; never assume one monitor, one
  resolution or one scaling factor.

## Layout

- `src/` is split into a **portable core** (no Windows headers; built as
  `mdu_core` on every OS and unit-tested) and **Windows code** (built only into
  `mdu.exe`). Portable: `app/crash_guard`, `dock/dock_layout`, `settings/config`,
  `ui/bridge`, `ui/settings_protocol`, `ui/theme`, `wallpaper/adapt`,
  `utilities/{file_io,log,base64}`.
  Keep logic that can be tested in the portable part.
- `cmake/App.cmake` defines the Windows target and copies `web/` and `themes/`
  next to the executable (the same layout the installer will use).
- `tests/` uses a tiny in-house harness (`tests/test_main.h`), not a framework.
- `installer/mdu.iss` is the per-user Inno Setup installer. It relies on the
  `mdu.exe --exit`, `--set-startup` and `--uninstall-cleanup` commands
  (`src/app/commands.cpp`); keep those in sync. CI installs and uninstalls it.

## Build and verify

```sh
# Any OS: portable core + tests, ASan/UBSan, warnings as errors
cmake --preset core-tests && cmake --build --preset core-tests && ctest --preset core-tests

# Frontend type-check
cd web && npm ci && npm run check
```

```powershell
# Windows: full application
cmake --preset windows-msvc
cmake --build --preset windows-release
ctest --preset windows-release
```

The MSVC build cannot run in a Linux container. GitHub Actions
(`.github/workflows/build.yml`) builds the Windows app on every push; check its
result before claiming the Windows code compiles. A MinGW syntax check is a
useful early signal but its SDK headers are older than the Windows 11 SDK
(no `DWMWA_WINDOW_CORNER_PREFERENCE`, no `wrl/implements.h`), so those errors
are expected there.

Runtime behaviour can only be verified on real Windows. The CI job runs
`mdu.exe --self-test` (src/app/app_self_test.cpp) on the runner and uploads the
report and screenshots as `mdu-self-test`; read them before claiming runtime
behaviour works. The runner has one monitor, so multi-monitor, mixed-DPI and
hot-plug behaviour remain unverified until tested on real hardware. When adding
a user-visible feature, add a self-test check for it if it can be observed.

## Conventions

- Warnings are errors in presets. Fix them; do not suppress them broadly.
- Explicit error handling: log with `mdu::log`, tell the user through
  `App::notify` when they need to act.
- Win32 handles and COM objects are owned by RAII types or `ComPtr`.
- Event-driven only: no polling timers. The single timer debounces display changes.
- Keep files focused; prefer a small new file over growing a large one.
