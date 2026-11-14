# Changelog

## 0.1.0 — unreleased

First development version.

- Dock on each monitor (bottom, left or right), sized per monitor DPI, with
  app bar space reservation or overlay placement, hidden during full-screen apps.
- Pinned items launched through the shell; icons from the Windows shell.
- Notification-area menu: dock, wallpaper, start with Windows, settings file,
  reload, data folder, exit.
- Settings file with validation, defaults and preservation of invalid files.
- Per-user startup registration without administrator rights.
- Crash guard with automatic safe mode, Shift-at-start and `--safe-mode`.
- Theme system (`theme.json` + optional stylesheet) and the Miku theme.
- Phone-to-desktop wallpaper adaptation with restore of the previous wallpaper.
- Reduced-motion support.
- Per-user installer (Inno Setup): no administrator rights, Start menu entry,
  optional startup and desktop shortcut, clean uninstall that restores the
  wallpaper and offers to keep settings. Installer commands `--exit`,
  `--set-startup on|off` and `--uninstall-cleanup`.
- Runtime self-test (`--self-test`, `--data-dir`), run on Windows in CI with
  screenshots. It found and fixed two startup bugs: the message window failed
  to create (MDU exited immediately), and docks could stay empty when WebView2
  started quickly.
