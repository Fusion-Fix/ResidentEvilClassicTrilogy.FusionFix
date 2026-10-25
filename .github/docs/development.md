# Building and developing Fusion Fix

## Building and debugging

Install Visual Studio 2026 with the C++ desktop workload and initialize the repository submodules. Copy `.env.example` to `.env`, enter your six installation roots and run `premake5.bat`. Local paths are ignored by Git. Regenerate after changing them.

Open the solution for your game: `build/RE1Classic.FusionFix.slnx`, `build/RE2Classic.FusionFix.slnx`, or `build/RE3Nemesis.FusionFix.slnx`. GOG is the primary debugging/testing target and the default configuration in each solution. Each contains four GOG/Steam Debug/Release configurations:

| Game | Steam | GOG |
| --- | --- | --- |
| Resident Evil | RE1-Steam-Debug / RE1-Steam-Release | RE1-GOG-Debug / RE1-GOG-Release |
| Resident Evil 2 | RE2-Steam-Debug / RE2-Steam-Release | RE2-GOG-Debug / RE2-GOG-Release |
| Resident Evil 3 | RE3-Steam-Debug / RE3-Steam-Release | RE3-GOG-Debug / RE3-GOG-Release |

Each solution produces its corresponding `RE1Classic.FusionFix.asi`, `RE2Classic.FusionFix.asi`, or `RE3Nemesis.FusionFix.asi`. Builds write the ASI and symbols to `bin/<configuration>/`, install Win32 Ultimate ASI Loader as `version.dll` for Steam or `dsound.dll` for GOG, and copy the ASI/PDB into `plugins/` beside the executable of the selected game installation. Translations remain editable under `text/` and `settings/text/` in this repository and are embedded as ASI resources; no external text files are needed. The plugin writes its settings file on the first start. Steam deployments cover every installed language folder. Existing DirectDraw and input wrappers remain in place. Deployment stops if an unrelated DLL already occupies the loader name.

Debugging starts the English main executable with its own folder as the working directory. RE2 defaults to Leon. Optional `<GAME>_<STORE>_EXE` entries select Claire, another language or the Mercenaries executable using a path relative to the installation root:

```dotenv
RE2_STEAM_EXE=english/ClaireU.exe
RE2_GOG_EXE=ClaireU.exe
RE3_STEAM_EXE=japanese/Bio3_PC.exe
```

Press F5 to launch the selected executable. `InitializeASI` and `Init` are useful breakpoints. The loader calls `InitializeASI`; pattern callbacks initialize features when the native code is available. RE1/RE2 portable registry redirection starts before those callbacks. Steam configurations supply the appropriate SteamAppId and SteamGameId. Steam must be running, and stock DRM executables may reject a debugger. GOG configurations launch their DRM-free game executable directly.

If an installation root is unset, that configuration still builds, but has no automatic deployment or game debugger command. For local deployment, `data/version.dll` must contain the Win32 Ultimate ASI Loader. CI downloads that loader; it is not tracked in Git.

## Release layout

Build all six Release configurations, then run `release.bat` to embed symbols, sign when configured, and create six ZIPs in `releases/`:

- `RE1Classic.FusionFix.Steam.zip`
- `RE1Classic.FusionFix.GOG.zip`
- `RE2Classic.FusionFix.Steam.zip`
- `RE2Classic.FusionFix.GOG.zip`
- `RE3Nemesis.FusionFix.Steam.zip`
- `RE3Nemesis.FusionFix.GOG.zip`

To package only RE3, run `powershell -File release.ps1 -Game RE3`.

Extract the archive for your game/store into its installation root. Steam archives contain the appropriate language folders with `version.dll` and `FusionFixHub.exe` beside each executable and the ASI in its `plugins/` subfolder. All menu languages and English fallback text are embedded in the ASI. GOG archives contain root `dsound.dll` and `FusionFixHub.exe`, and the ASI in `plugins/`. The archives contain no settings file: the plugin writes `<plugin>.cfg` with the defaults on the first start. RE2's Leon and Claire share the same installation and plugin. Package contents are defined by `games.json`; no installation paths are included.

Native implementation files are isolated in `source/RE1`, `source/RE2`, and `source/RE3`; shared helpers and modules are compiled across all projects.

## Portable settings

RE1 and RE2 redirect their CAPCOM registry reads and writes to `RE1Classic.Game.ini` or `RE2Classic.Game.ini` beside the executable. Calls from loaded wrappers share the profile; unrelated registry keys retain their native behavior.

The first launch creates the file from embedded defaults and imports existing native settings, excluding installation and save paths. The profile uses UTF-16; existing ANSI or UTF-8 profiles are converted on launch. Sections retain native registry value types: `[DWORD]`, `[STRING]`, `[BINARY]`, and `[NONE]`. Native option and key-binding changes are written back to this file.

RE2's `[PATHS] Save Path` defaults to a relative `Saves\` directory. RE1 uses its native `SAVE` directory. Setting `PortableMode = 0` in the plugin's settings file restores native registry behavior on the next launch.

## Settings

The settings follow [ASI Settings](https://github.com/ThirteenAG/FusionFixUtils/tree/main/asi_settings) (the `external/FusionFixUtils` submodule):

- `settings/RE1Classic.FusionFix.json`, `settings/RE2Classic.FusionFix.json` and `settings/RE3Nemesis.FusionFix.json` describe each plugin's settings: type, default, page and presets. `settings/text/<language>.json` holds their texts (English required; French, German, Italian, Spanish and Japanese), shared by the three plugins. `source/resources/Settings.rc` embeds them.
- `source/FixSettings.ixx` loads them with `asi_settings::Plugin`, which reads and writes `<plugin>.cfg` next to the plugin, exports `AsiSettings_GetApi` and serves the live link, so FusionFixHub and the overlay change values while the game runs. The code's settings follow every change; the native menu stores its changes through the same plugin.
- Presets: Vanilla, Recommended (the defaults) and Enhanced set only the settings whose values differ between them (widescreen, the RE3 wobble fix, skip intro, skip doors, fast load). The pause menu's **Preset** row switches them.
- `source/PluginPages.ixx` shows other plugins' settings: when another loaded plugin has a description with menu settings and no other menu host has the role, the pause menu gets a **Plugins** row. It lists those plugins; each page has the plugin's preset row, its menu settings (compatibility settings last) and **Restore defaults**. Values are read and changed through the plugin's `AsiSettings_GetApi` table.
- `source/Overlay.cpp` adds the settings overlay of FusionFixUtils (Ctrl+F10, or Back + Start) on the Direct3D 9 device of the games' DirectDraw wrapper; the game pauses while it is open.
- The build validates the descriptions and translations before compiling and the embedded ones in the built plugin (`asi-settings-validate`, built from the submodule); a warning or error fails the build.

A new setting is one entry in the description, its texts in the translations, and its binding in `ClassicGame.ixx` or `RE3/Game.ixx`.

## Release packages and secrets

The workflow (`.github/workflows/msvc.yml`) builds all six Release configurations and uploads the six ZIPs as artifacts; a run started with *release* publishes them as a GitHub release (ZIPs only). It needs these repository (or organization) secrets:

- `HUB_RELEASE_TOKEN`: a read-only token for the releases of the private `ThirteenAG/FusionFixHub-App` repository (fine-grained, contents: read). Each package ships the latest signed `FusionFixHub.exe`; a release run fails without it, other runs build without the hub.
- `CODE_SIGNING_PFX` and `CODE_SIGNING_PASSWORD`: sign the plugins on `main`.

The settings descriptions are published to fusionfix.io with the run's GitHub Actions OIDC token (no secret).


## Documentation and release pages

The public overview lives in `readme.md`. `.github/docs/release.md` contains the matching GitHub Release body and is passed to the release action through `bodyFile`. Keep feature descriptions, installation steps, defaults, and limitations in sync. The support block belongs at the top of the README and the bottom of the release body.

README showcase images use repository-relative paths. The release body uses absolute raw GitHub URLs so images resolve on release pages. Add the planned animated WebP files under `.github/docs/video/` before publishing; see the [capture checklist](video/README.md).
