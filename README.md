# MDU — Miku Desktop Unit

MDU is a free, open-source desktop layer for Windows 11: a themeable dock on
every monitor, a desktop wallpaper adapted from a phone wallpaper, and a theme
system to tie them together. It runs alongside Windows Explorer and never
replaces the shell; the normal taskbar stays available at all times.

The first theme, **Miku**, follows a Miku / Monster Energy phone theme: dark
surfaces, muted olive accents and a grey backdrop.

> Status: early development (0.1.0). The dock, tray menu, startup registration,
> crash recovery, wallpaper adaptation and the installer are implemented. A
> settings window is not built yet; settings are edited in a JSON file.

## Installing

Run `MDU-Setup-<version>.exe`. It installs for your user only, with no
administrator prompt, into `%LOCALAPPDATA%\Programs\MDU`, and adds a Start menu
entry. It offers to start MDU when you sign in (on by default) and to create a
desktop shortcut.

Uninstall from **Settings → Apps**. The uninstaller stops MDU, puts your
previous wallpaper back if MDU replaced it, removes the startup entry, and
deletes caches, logs and browser data. It asks whether to delete your settings
too; a silent uninstall (`/VERYSILENT`) keeps them.

## Features

- **Dock on each monitor**, at the bottom, left or right edge. It reserves its
  space like the taskbar does (maximized windows stop above it) or floats over
  windows (`"placement": "overlay"`). It hides automatically while a full-screen
  app runs on that monitor.
- **Per-monitor DPI and hot-plugging.** Each dock is sized for its own
  monitor's scaling and follows monitors being added, removed or rearranged.
- **Wallpaper adaptation.** A portrait phone wallpaper is shown whole and
  unstretched on landscape monitors. Its edges are blended into the image's own
  background colour and Windows letterboxes it with that colour, so it fills
  any aspect ratio without cropping the subject. The original file is never
  modified, and your previous wallpaper is restored when you turn this off.
- **Themes** are folders with a `theme.json` (colours, sizes, fonts, motion
  timing, wallpaper) and an optional stylesheet. Nothing about Miku is
  hard-coded into the application.
- **Respects Windows settings**: reduced motion ("Animation effects" off), per-
  monitor scaling, Focus Assist for notifications.
- **Private by design**: no telemetry, no network requests. The dock UI is
  local content served from the install folder; WebView2 is locked to it.

## Using MDU

MDU lives in the notification area. Right-click (or click) its icon for:
show/hide the dock, use the theme wallpaper, start with Windows, edit or reload
settings, open the data folder, and exit.

### Settings

`%LOCALAPPDATA%\MDU\config.json` is created on first start. Edit it, then
choose **Reload settings** from the tray menu. Comments are allowed.

```jsonc
{
  "startWithWindows": true,
  "theme": "miku",
  "motion": "system",            // "system", "reduced" or "full"
  "dock": {
    "enabled": true,
    "edge": "bottom",            // "bottom", "left" or "right"
    "monitors": "all",           // "all" or "primary"
    "placement": "reserve",      // "reserve" or "overlay"
    "iconSize": 48,              // 24-128, in DIPs
    "margin": 4
  },
  "wallpaper": {
    "enabled": false,
    "image": ""                  // empty = the theme's wallpaper; or an absolute path
  },
  "pinned": [
    { "id": "explorer", "name": "File Explorer", "target": "%WINDIR%\\explorer.exe" },
    { "id": "settings", "name": "Settings", "target": "ms-settings:" },
    { "id": "code", "name": "VS Code", "target": "code.exe", "arguments": "--new-window" }
  ]
}
```

`target` can be a program, document, folder, shortcut (`.lnk`) or URI; it is
opened exactly as Explorer would open it. Invalid values are ignored with a
note in the log, and a file that is not valid JSON is kept as
`config.json.invalid-<time>` while MDU runs with defaults.

### The Miku wallpaper

The Miku wallpaper is third-party artwork with trademarked branding, so it is
**not included** in this repository. Put your copy of the phone wallpaper at
`themes\miku\wallpaper\mobile.jpg` (in the source tree before building, or next
to `mdu.exe` in an existing build), then enable **Use theme wallpaper**.

### Recovery

- If MDU closes unexpectedly three times within ten minutes, it starts in
  **safe mode**: only the tray icon runs, with an option to restart normally.
- Hold **Shift** while MDU starts, or run `mdu.exe --safe-mode`, to force safe
  mode.
- MDU never restarts itself after a crash, so it cannot get stuck in a loop,
  and it never changes Explorer: if MDU is not running, Windows looks and works
  exactly as before (apart from the wallpaper, which stays until you change it
  or turn the MDU wallpaper off).

Logs are in `%LOCALAPPDATA%\MDU\logs\` (`mdu.log`, and `mdu.log.old` from the
previous run).

## Building

Requirements:

- Windows 11 with the Microsoft Edge WebView2 Runtime (included with Windows 11)
- Visual Studio 2022 or newer with the "Desktop development with C++" workload
  (MSVC, Windows 11 SDK, CMake 3.26+)
- Node.js 20 or newer (only to compile the TypeScript frontend)

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-release
ctest --preset windows-release
.\build\windows\Release\mdu.exe
```

The platform-independent core (configuration, layout, theme parsing, message
validation, wallpaper adaptation) also builds and tests on Linux and macOS:

```sh
cmake --preset core-tests
cmake --build --preset core-tests
ctest --preset core-tests
```

Every push is built and tested by GitHub Actions; the Windows job uploads a
ready-to-run build as the `mdu-windows-x64` artifact.

### Runtime self-test

`mdu.exe --self-test <report.json> [--data-dir <folder>]` starts the real
application in an isolated data folder, then checks it from the outside:
docks are created, visible, drawn and transparent, reserve their screen space,
launch an item when it is clicked in the UI, reject malformed and malicious
messages, refuse to navigate away from local content, and apply and restore the
wallpaper. It writes a JSON report, screenshots (`screen.png`, `dock-N.png`)
and exits with 0 (all passed), 3 (a check failed) or 2 (timed out). It never
changes the startup registration and always restores the previous wallpaper.

CI runs it on every push; the results are uploaded as the `mdu-self-test`
artifact, so you can see what MDU looks like on Windows without a test machine.

### Building the installer

Requires [Inno Setup](https://jrsoftware.org/isinfo.php) 6.3 or newer.

```powershell
cmake --install build\windows --config Release --prefix staging
iscc /DSourceDir=$PWD\staging /DAppVersion=0.1.0 installer\mdu.iss
```

CI builds the installer on every push, installs it silently on the runner,
checks the installed files, shortcut and startup entry, runs the self-test from
the installed copy, uninstalls, and checks that everything except the settings
file is gone. The installer is uploaded as the `mdu-setup` artifact.

### Dependencies

All versions are pinned and downloads are verified by SHA-256.

| Dependency | Version | Why |
| --- | --- | --- |
| [Microsoft WebView2 SDK](https://www.nuget.org/packages/Microsoft.Web.WebView2) | 1.0.4314.35 | Renders the UI with the WebView2 runtime built into Windows 11. Linked statically, no extra DLL. |
| [nlohmann/json](https://github.com/nlohmann/json) | 3.12.0 | JSON for settings, themes and UI messages. |
| [TypeScript](https://www.typescriptlang.org/) | 7.0.2 | Build-time only, compiles the frontend. |
| [Inno Setup](https://jrsoftware.org/isinfo.php) | 6.7.1 in CI (6.3+ supported) | Build-time only, creates the installer. CI uses the copy preinstalled on the GitHub Windows runner and logs its version. Free for non-commercial use, which covers MDU; commercial redistribution would need an Inno Setup license. |

Everything else is the Windows SDK: Win32, Shell, DWM, WIC and COM.

## Project layout

```
src/
  main.cpp           entry point, single instance, safe-mode switch
  app/               application lifecycle, tray menu, crash guard
  dock/              dock window (WebView2 host), app bar, layout maths
  settings/          configuration file and paths
  ui/                theme loading, frontend message protocol, WebView2 environment
  wallpaper/         wallpaper adaptation and IDesktopWallpaper integration
  windows/           monitors, startup registration, launching, icons, tray icon
  utilities/         logging, strings, files, small helpers
web/                 dock frontend (HTML, CSS, TypeScript)
themes/miku/         the Miku theme
tests/               unit tests for the portable core
installer/           Inno Setup script
```

## Contributing

Issues and pull requests are welcome. Please keep changes focused, build with
warnings as errors (the presets do this), add tests for core logic, and avoid
new dependencies unless they clearly pay for themselves. Never commit artwork
you do not have the right to redistribute.

## License

MIT — see [LICENSE](LICENSE). Hatsune Miku is a character of Crypton Future
Media; Monster Energy is a trademark of Monster Energy Company. MDU is not
affiliated with either, and ships none of their artwork.
