# Dark Paint (`mspaint-dark`)

A comprehensive, production-grade dark mode for Microsoft Paint on Windows 10 and 11, implemented as a [Windhawk](https://windhawk.net/) mod.

![Dark Paint Screenshot](assets/screenshot.png)

## Features

- **Classic Win32 Ribbon Theming (Windows 10)**:
  - Pitch-black title bar and non-client frame via DWM composition attributes.
  - Fully themed Windows Ribbon framework (tabs, command bar, QAT, application menu).
  - Dark status bar with high-contrast icons, dark trackbar slider, and custom dark size gripper.
  - Zero visual stutter or white flashing during window resizing and movement.
- **Dark Canvas Spawning**:
  - Automatically spawns new canvases with a dark background (`#202020` by default) instead of blinding white.
  - Synchronizes Color 1 (Pencil = White) and Color 2 (Eraser = Dark Canvas) upon document creation and reset.
- **Themed Modal Dialogs**:
  - Custom dark save/close confirmation prompt replacing the bright white TaskDialog.
  - Full keyboard navigation support (Enter, Escape, Tab navigation).
- **Modern WinUI / XAML Support (Windows 11)**:
  - Automatically requests Dark Application theme for Windows 11 Paint.
- **Dynamic Symbol Resolution**:
  - Automatically hooks internal symbols via Microsoft Symbol Server (`WindhawkUtils::HookSymbols`), ensuring portability across Windows cumulative updates without hardcoded offsets.

## Settings

The mod provides configurable options in Windhawk:

| Setting | Default | Description |
| :--- | :--- | :--- |
| `darkCanvas` | `true` | Automatically spawn new canvases with a dark background. |
| `canvasColor` | `#202020` | Hex color for the canvas background. |
| `workspaceColor` | `#282828` | Hex color for the workspace surrounding the canvas. |
| `ribbonBrightness` | `38` | Brightness level of the ribbon bar (0 to 100). |

## Installation

1. Install [Windhawk](https://windhawk.net/) if you haven't already.
2. In Windhawk, open the **Developer Channel** or go to **Advanced -> Create Local Mod**.
3. Copy the contents of [`mspaint-dark.wh.cpp`](mspaint-dark.wh.cpp) into the editor.
4. Click **Compile Mod** and enable it.

## License

[MIT](LICENSE)
