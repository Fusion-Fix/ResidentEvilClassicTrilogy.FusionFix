<div align="center">

<img width="600" src="https://raw.githubusercontent.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/refs/heads/main/source/resources/logo.svg">

**Widescreen presentation. Alternate controls. Less waiting.**

Improvements for the original **Resident Evil**, **Resident Evil 2**, and **Resident Evil 3: Nemesis** on **Steam and GOG**.

[Download](https://github.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/releases/latest) · [Features](#features) · [Installation](#installation) · [Settings](#settings) · [Report an issue](https://github.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/issues)

</div>

---

## Installation

| Game | Steam | GOG |
|:--|:--:|:--:|
| **Resident Evil** | [Download](https://github.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/releases/latest/download/RE1Classic.FusionFix.Steam.zip) | [Download](https://github.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/releases/latest/download/RE1Classic.FusionFix.GOG.zip) |
| **Resident Evil 2** | [Download](https://github.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/releases/latest/download/RE2Classic.FusionFix.Steam.zip) | [Download](https://github.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/releases/latest/download/RE2Classic.FusionFix.GOG.zip) |
| **Resident Evil 3: Nemesis** | [Download](https://github.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/releases/latest/download/RE3Nemesis.FusionFix.Steam.zip) | [Download](https://github.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/releases/latest/download/RE3Nemesis.FusionFix.GOG.zip) |

1. Download the archive for your **game and store**.
2. Extract it into the game's installation folder, preserving the folders in the ZIP.
   - **Steam:** use **Manage → Browse local files**. Extract into the root containing the language folders, such as `english`. Steam can redirect a copied executable to the installed game, so use the folder Steam opens.
   - **GOG:** extract beside `ResidentEvil.exe`, `LeonU.exe` / `ClaireU.exe`, or `ResidentEvil3.exe`.
3. Launch the game normally. Press **Esc** or controller **Start** to open Fusion Fix.
4. Enable **Widescreen** and **Alternate controls** in the menu if you want them; both are off by default.

The loader is included. Plugins and their INIs live in `plugins/` beside the executable; Steam archives place them inside each language folder. RE2's Leon and Claire share one plugin. Keep a copy of your INI when updating if you want to retain your settings.

## Features

### Widescreen with a moving frame

Fill your display while keeping the original proportions. Pan-and-scan follows the character vertically, and the **right stick** lets you look toward either edge of the original background. Let go to return smoothly to automatic framing.

The scene is enlarged and cropped vertically from its original 4:3 image. Backgrounds, foreground masks, characters, and effects stay aligned. Inventory screens, menus, and text retain their complete 4:3 layout.

<p align="center">
  <img src="https://raw.githubusercontent.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/refs/heads/main/.github/docs/video/re3-pan-and-scan.webp" width="800" alt="Resident Evil 3: widescreen gameplay with vertical pan-and-scan framing">
</p>

**16:10 works automatically.** The INI-only `MaxAspectRatio` defaults to `16:9`; wider displays have side borders unless you raise it, for example to `21:9`.

### Alternate controls, familiar cameras

Move relative to the screen with the keyboard or left stick. **Tilt gently to walk; tilt fully to run.** Holding a direction through a camera change preserves your heading until you release or change direction.

The D-pad retains tank controls. Aiming, interactions, and scripted movement retain their native behavior. Keyboard players can choose hold or toggle behavior for walking and running.

<p align="center">
  <img src="https://raw.githubusercontent.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/refs/heads/main/.github/docs/video/re2-alternate-controls.webp" width="960" alt="Resident Evil 2: alternate movement controls in the police station">
</p>

### Remaster-style button layout

The default keyboard and XInput layout follows **Resident Evil HD Remaster Type A**, with the same buttons in tank and Alternate movement modes. Set `HDControls = 0` in the plugin INI to restore native bindings. Legacy controllers retain their native button mappings.

| Action | Keyboard / mouse | Controller |
|:--|:--|:--|
| Move | **WASD** | **Left stick**; **D-pad** always uses tank movement |
| Run | **Shift** | **X**; full stick tilt also runs with Alternate controls |
| Aim / attack | **Right / left mouse button** | **LT / RT** |
| Examine / confirm | **F / Enter** | **A** |
| Reload while aiming | **R** | **B** |
| Change aiming target | **C** | **LB** |
| Quick turn | **Q** | **Right-stick click**, or **X + down** |
| Map / status | **M / N** | **RB / Y** |
| Back in native menus | **Esc / Backspace** | **B** |
| Fusion Fix menu | **Esc** outside native menus | **Start** |

Quick turn is added to RE1 and RE2; RE3 uses its native turn animation. Manual reload tops up compatible weapons using ammunition in the inventory and the game's reload animation. Weapons without a native reload animation retain their original behavior. The right stick still controls vertical widescreen framing.

### Steadier geometry in Resident Evil 3

The **RE3-only wobble fix** preserves subpixel model coordinates to reduce visible polygon snapping. Toggle it with **F2** to compare the same scene.

<p align="center">
  <img src="https://raw.githubusercontent.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/refs/heads/main/.github/docs/video/re3-wobble-fix.webp" width="960" alt="Resident Evil 3: side-by-side comparison of polygon wobble with the fix disabled and enabled">
</p>

### Settings without leaving the game

Press **Esc** or controller **Start** to pause. Each game has its own menu styling, with **Display**, **Controls**, and **Game options** pages. The selected item explains its effect, and the button hints follow keyboard or controller input. Changes are saved automatically to the INI, and all menu text follows the game's language. **Esc / B** returns to the previous page; **Start** resumes the game.

<p align="center">
  <img src="https://raw.githubusercontent.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/refs/heads/main/.github/docs/video/fusion-fix-menu.webp" width="960" alt="The Fusion Fix menu: presentation, controls, and loading options">
</p>

| Shortcut | Action |
|:--|:--|
| **Esc / controller Start** | Open the pause menu; Esc goes back, Start resumes |
| **F2** | Toggle the wobble fix in RE3 |
| **F3** | Toggle widescreen / 4:3 |
| **F4** | Toggle alternate controls |

Function-key toggles apply to the current session. Use the menu or INI to save your preference.

### Less waiting between rooms

Skip door animations while retaining room loading. Skip startup warnings, logos, and the opening movie to reach the main menu sooner. Both options can be switched off.

<p align="center">
  <img src="https://raw.githubusercontent.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/refs/heads/main/.github/docs/video/door-skip.webp" width="960" alt="Resident Evil: moving between rooms with door animations skipped">
</p>

**Auto Load** can take you straight into a selected save on startup. **Fast Load** skips recap captions after loading; story subtitles remain intact. The menu's **Load game** action is available from the title menu.

### Movie presentation

Movies scale uniformly to the selected scene viewport, with cropping where needed to fill it. Widescreen presentation avoids stretching and unnecessary nested borders.

<p align="center">
  <img src="https://raw.githubusercontent.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/refs/heads/main/.github/docs/video/re2-movie-presentation.webp" width="960" alt="Resident Evil 2: widescreen movie presentation">
</p>

### Portable settings for Resident Evil 1 & 2

Native game settings are stored beside the executable instead of depending on the game's CAPCOM registry entries. **Portable Mode is enabled by default.** Existing settings are imported on first use, while installation and save paths follow the local game folder.

- **RE1:** `RE1Classic.Game.ini`, with saves in the native `SAVE/` folder.
- **RE2:** `RE2Classic.Game.ini`, with a relative `Saves\` path configurable under `[PATHS]`.

This makes settings and save paths portable; Steam still requires its normal client and ownership checks.

## Settings

Edit the matching INI in `plugins/`, or use the in-game menu. The defaults keep the original 4:3 presentation and tank controls, enable intro/door skipping, and enable RE3's wobble fix.

<details>
<summary><strong>All options and defaults</strong></summary>

| Option in `[MAIN]` | Default | What it does |
|:--|:--:|:--|
| `PanAndScan` | `0` | Widescreen framing with automatic and right-stick vertical panning |
| `MaxAspectRatio` | `16:9` | INI-only widescreen limit; accepts a ratio or decimal |
| `HDControls` | `1` | Remaster keyboard / XInput bindings, quick turn and manual reload; restart after changing |
| `AlternateControls` | `0` | Screen-relative keyboard and left-stick movement |
| `KeyboardRunMode` | `0` | Choose Shift hold/toggle behavior; see below |
| `SkipIntro` | `1` | Skip startup warnings, logos, and the opening movie |
| `SkipDoor` | `1` | Skip door animations |
| `FastLoad` | `0` | Skip recap captions after loading |
| `AutoLoad` | `0` | Load the selected save on startup |
| `LoadSlot` | `0` | Select Latest or a native save entry; see below |
| `PortableMode` | `1` | RE1/RE2 only: keep native settings in an INI; restart after changing |
| `WobbleFix` | `1` | RE3 only: stabilize model geometry |

For alternate keyboard controls, `KeyboardRunMode` selects:

| Value | Movement |
|:--:|:--|
| `0` | Walk by default; hold Shift to run |
| `1` | Walk by default; press Shift to toggle running |
| `2` | Run by default; hold Shift to walk |
| `3` | Run by default; press Shift to toggle walking |

Default INIs: [RE1](https://github.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/blob/main/data/plugins/RE1Classic.FusionFix.ini) · [RE2](https://github.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/blob/main/data/plugins/RE2Classic.FusionFix.ini) · [RE3](https://github.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/blob/main/data/plugins/RE3Nemesis.FusionFix.ini).

</details>

<details>
<summary><strong>Save selection and Latest</strong></summary>

| Game | `LoadSlot = 0` | Explicit selection |
|:--|:--|:--|
| RE1 | Latest save by file modification time | `1–8`: native slot |
| RE2 | Newest save compatible with the running character's campaign | `1` and above: entry in the native list, newest first |
| RE3 | Latest reliably dated save | `1–15`: first memory card; `16–30`: second memory card |

Missing, incompatible, or unreadable saves retain the game's normal selection or error handling. Old RE3 memory cards have no reliable timestamps; if Latest cannot determine the newest save, it opens the native slot picker.

</details>

## How to report crashes

If the game crashes, include a **crash dump (`.dmp`)** in your [issue report](https://github.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/issues). Include the game, Steam/GOG version, language, Fusion Fix version, other installed mods, and steps to reproduce it.

<details>
<summary><strong>Collect a crash dump or a freeze dump</strong></summary>

**Ultimate ASI Loader (recommended)**

The bundled [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) can generate crash dumps and logs.

1. Open the folder containing the executable that actually runs the game. On Steam, this is the selected language folder inside the Steam installation, rather than the launcher folder.
2. Create a folder named exactly `CrashDumps` beside that executable.
3. Reproduce the crash and open `CrashDumps`.
4. Attach the newest `.dmp` file and its `.log` file, if present. The log alone does not replace the dump.

**Windows crash dumps (fallback)**

If the loader does not create a dump:

1. Press **Win + R**, enter `regedit`, and press Enter.
2. Navigate to `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\Windows Error Reporting`.
3. Create a key named `LocalDumps`, then close Registry Editor.
4. Reproduce the crash and find the newest game dump in `%LOCALAPPDATA%\CrashDumps`.

This fallback requires administrator rights and enables crash dumps for all programs.

**Hangs or freezes**

Open Task Manager with **Ctrl + Shift + Esc**, find the game process, right-click it, and select **Create memory dump file** (under **Details** on some Windows versions). Use the location shown by Task Manager.

Dumps can be large. If you cannot attach one to the issue, upload it to a file-sharing service and include a link accessible to the maintainers.

</details>

---

[Build and development guide](https://github.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/blob/main/.github/docs/development.md) · [Source code](https://github.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix) · [GPL-3.0-or-later](https://github.com/Fusion-Fix/ResidentEvilClassicTrilogy.FusionFix/blob/main/license)

<p align="center"> <a href="https://patreon.fusionfix.io/" target="_blank"><picture><source media="(max-width: 768px) and (prefers-color-scheme: dark)" srcset="https://fusionlegacyinitiative.com/sponsors-progress/sponsors-progress-wfp-mobile-dark.svg"><source media="(max-width: 768px)" srcset="https://fusionlegacyinitiative.com/sponsors-progress/sponsors-progress-wfp-mobile.svg"><source media="(prefers-color-scheme: dark)" srcset="https://fusionlegacyinitiative.com/sponsors-progress/sponsors-progress-wfp-dark.svg"><img width="100%" src="https://fusionlegacyinitiative.com/sponsors-progress/sponsors-progress-wfp.svg"></picture></a> <br /> <a href="https://github.com/sponsors/ThirteenAG"><picture><source media="(prefers-color-scheme: dark)" srcset="https://thirteenag.github.io/img/buttons/github-dark.svg"><img src="https://thirteenag.github.io/img/buttons/github.svg" width="250"></picture></a> <a href="https://ko-fi.com/thirteenag"><picture><source media="(prefers-color-scheme: dark)" srcset="https://thirteenag.github.io/img/buttons/kofi-dark.svg"><img src="https://thirteenag.github.io/img/buttons/kofi.svg" width="250"></picture></a> <a href="https://paypal.me/SergeyP13"><picture><source media="(prefers-color-scheme: dark)" srcset="https://thirteenag.github.io/img/buttons/paypal-dark.svg"><img src="https://thirteenag.github.io/img/buttons/paypal.svg" width="250"></picture></a> <a href="https://www.patreon.com/ThirteenAG"><picture><source media="(prefers-color-scheme: dark)" srcset="https://thirteenag.github.io/img/buttons/patreon-dark.svg"><img src="https://thirteenag.github.io/img/buttons/patreon.svg" width="250"></picture></a> <a href="https://boosty.to/thirteenag"><picture><source media="(prefers-color-scheme: dark)" srcset="https://thirteenag.github.io/img/buttons/boosty-dark.svg"><img src="https://thirteenag.github.io/img/buttons/boosty.svg" width="250"></picture></a><br><br> </p>
