# YurisRuntime host framework

Mikage Next runs YU-RIS games with [MikageYURIS](https://github.com/itsCheney/MikageYURIS), a YU-RIS runtime written in Rust, built as an embeddable iOS dynamic framework.

Pinned input:

- `Source/` submodule: `itsCheney/MikageYURIS`, branch `mikage`. It reads a game's YPF archives and compiled scripts and runs them; nothing is converted ahead of time. The C interface is `crates/yuris-mobile` with the header `platform/ios/include/yuris_mobile.h`; `docs/engine/player.md` in the submodule describes it.

MikageYURIS isn't an SDL program. It draws with wgpu into the `CAMetalLayer` of a view and has no audio output of its own: the session pulls its mixer (44.1 kHz stereo) from an `AVAudioSourceNode`. `scripts/build-yuris-ios.sh` builds the `yuris-mobile` crate as a dynamic library for the device and Apple Silicon simulator, wraps each in `YurisRuntime.framework` with the header and a module map, checks the exports, then creates `build/YurisRuntime.xcframework`. It needs Rust with the `aarch64-apple-ios` and `aarch64-apple-ios-sim` targets, nothing else. It stays a framework of its own: the other Rust engines pin other wgpu versions.

`App/Engine/YurisSession.swift` gives the engine its own landscape window with a `CAMetalLayer` view, steps it from a display link and reuses the SDL engines' orientation, lifecycle and menu overlay code, as the Siglus and FVP sessions do. The game's screen is fitted into the view with black bars. PingFang (Traditional on Traditional Chinese devices) draws all text, whichever font the game names; Hiragino Sans is registered after it (`App/Engine/SystemFontExport.swift`). A touch is the left button; a long press is a right click, and so is the game menu's Back tile. Two fingers dragged up and down turn the mouse wheel, which opens and scrolls the backlog; the Backlog command is one wheel step up. Holding Skip holds Ctrl. A three-finger touch opens the Mikage menu when that setting is on. Going to the background stops the game's clock and its sound.

A game folder needs its `*.ypf` packs, in the folder or in its `pac` folder (or an extracted `ysbin` folder). Translation patches that come as an executable packed with Enigma Virtual Box (`erewhon_chs.exe`, which names an external `.mys` package) are startup files of their own: the runtime mounts the packed files over the game's. The text encoding comes from the game's page, or automatically: GBK when the startup file or the folder is named for a Chinese release, else Shift-JIS. Saves go to the game folder's `savedata`.

Known limits: only Erewhon (CLOCKUP, 2018, engine 481) with its Chinese patch has been tested; movies, masks and CGACT transitions are not supported yet; auto mode, quick save and load, and the save and load screens have no engine-level hook, so those menu tiles stay off; saves of the original Windows engine can't be loaded; the Mikage screenshot may come out black (the Metal layer is drawn outside UIKit).

MikageYURIS is GPL-3.0-or-later, except `crates/yuris-format/src/evb.rs` (MPL-2.0, from ethornell-rs); both allow combining with Mikage's GPL-3.0-or-later. The licenses are in the submodule.
