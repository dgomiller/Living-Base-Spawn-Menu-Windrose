# LivingBaseSpawnMenu

A compiled C++ [UE4SS](https://docs.ue4ss.com/) companion mod for **LivingBase** (a Lua mod for
[Windrose](https://store.steampowered.com/), Kraken Express, UE 5.6) — adds a real, clickable, always-on-top
GUI window on top of LivingBase's keyboard-only placement/live-edit system: a categorized spawn tree,
held-repeat movement buttons, and a precise typed coordinate editor.

This mod is **inert on its own**. It has no game logic of its own — it never touches Unreal reflection for
actual gameplay classes. Every real action (spawning, moving, despawning) is requested through small text
files that LivingBase's own Lua side reads and acts on; this mod is UI only. See *Architecture* below.

## Requirements

- **[LivingBase](https://github.com/dgomiller/Living-Base-Enhanced-Windrose)** installed and enabled — this mod has nothing to do without it.
- UE4SS (RE-UE4SS build) with the same `[EngineVersionOverride] MajorVersion=5, MinorVersion=6` LivingBase
  itself requires.

## Using it

Press **`-`** in-game to open/close the window (starts closed each session). Press **`=`** to steal OS
focus for it if it's already open. Full usage docs — the spawn tree, the move panel, the coordinate editor,
target lock, keyboard shortcuts while the window has focus — live in the shipped `help.txt` (also readable
from the window's own **Instructions** tab) and in LivingBase's own end-user README.

## Architecture

- **`StandaloneWindow.cpp`** — the actual window: its own OS thread, its own D3D11 device/swapchain, its
  own Dear ImGui context (`WS_EX_TOPMOST`, so it renders on top of the game). Deliberately independent of
  both Windrose's own D3D12 pipeline and UE4SS's own GUI console/thread — see this file's own comments for
  the earlier approaches that were tried and rejected (a raw D3D12 present-hook overlay conflicts with
  NVIDIA Streamline's swapchain wrapper; UE4SS's own `register_tab` console works but confines content
  inside UE4SS's devtools window instead of a clean player-facing one).
- **`SpawnMenu.cpp` / `MoveMenu.cpp` / `CoordsMenu.cpp`** — the three panels: the categorized spawn tree,
  the D-pad/despawn/undo/delete-all move panel, and the precise X/Y/Z/Rotation editor.
- **`MenuStatus.cpp`** — the one Lua → C++ direction in the whole bridge: polls a small status file
  LivingBase writes (keys-enabled state, restore-lock state, current target-lock info, a couple of
  one-shot toggle sequence counters) so the window can reflect live game state it has no other way to know.
- **The bridge is entirely file-based**, all inside LivingBase's own mod folder:
  - `spawn_menu.ini` — the category tree this mod reads to build the Tools tab (auto-generated,
    hand-curatable, LivingBase → C++ direction).
  - `spawn_request.txt` / `move_request.txt` — one-shot action requests, C++ → LivingBase direction.
  - `spawn_menu_status.txt` — live status, LivingBase → C++ direction (see `MenuStatus.cpp` above).
  - `spawn_menu_history.txt` — a running log of every on-screen toast this session, for the Instructions
    tab's History pane.

## Building

Requires an Epic Games account linked to your GitHub account (for RE-UE4SS's private engine-header
submodule access), CMake, and a C++ toolchain (Visual Studio with the Desktop C++ workload).

```
git submodule update --init --recursive
cmake -S . -B Output -G "Visual Studio 17 2022"
cmake --build Output --config Game__Shipping__Win64
```

Deploy the built `Output/Mod/Game__Shipping__Win64/LivingBaseSpawnMenu.dll` to
`.../ue4ss/Mods/LivingBaseSpawnMenu/dlls/main.dll` in the game install (plus an empty `enabled.txt` and
`Mod/assets/help.txt` alongside it), or use the pre-built copy already bundled into LivingBase's own repo
for a combined 2.0.0+ release.

## License

**All rights reserved by default**, except for the specific permissions in [`LICENSE`](LICENSE) — nothing
beyond what's listed there is implied.

**Permitted, without needing to ask:** modify for personal use; reuse this project's code in your own
separate project, with credit; convert/port to other games, with credit.

**Not permitted:** reuploading/rehosting this project (modified or unmodified) anywhere other than the
original author's own page(s)/repo(s); selling it or using it in anything sold or monetized, in whole or in
part (Nexus Mods' own Donation Points system is exempt).

This covers this mod's own code only. **Windrose** and its game assets, class names, and intellectual
property belong to Kraken Express — this is an unofficial, unaffiliated mod. UE4SS/RE-UE4SS (the
`RE-UE4SS` submodule) is a separate open-source project with its own license.
