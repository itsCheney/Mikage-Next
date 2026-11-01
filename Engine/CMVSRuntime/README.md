# CMVSRuntime host framework

Mikage Next runs CMVS (Purple software) games with [MikageCMVS](https://github.com/itsCheney/MikageCMVS), Mikage's fork of [cmvs-rs](https://github.com/Shizuku-in/cmvs-rs), a CMVS runtime written in Rust, built as an embeddable iOS dynamic framework.

Pinned input:

- `Source/` submodule: `itsCheney/MikageCMVS`, branch `mikage`. It reads the game's `data/pack` CPZ archives and PS3 scripts and runs them; nothing is converted ahead of time. The C interface is `crates/cmvs-mobile` with the header `platform/ios/include/cmvs_mobile.h`; `MIKAGE.md` in the submodule lists what the fork adds to upstream.

MikageCMVS isn't an SDL program. It draws with wgpu into the `CAMetalLayer` of a view and has no audio output of its own: the session pulls its mixer from an `AVAudioSourceNode`, and movies only advance while their soundtrack is pulled. `scripts/build-cmvs-ios.sh` builds the `cmvs-mobile` crate as a dynamic library for the device and Apple Silicon simulator, wraps each in `CMVSRuntime.framework` with the header and a module map, checks the exports, then creates `build/CMVSRuntime.xcframework`. It needs Rust with the `aarch64-apple-ios` and `aarch64-apple-ios-sim` targets, nothing else. It stays a framework of its own: the other Rust engines pin other wgpu versions.

`App/Engine/CMVSSession.swift` gives the engine its own landscape window with a `CAMetalLayer` view, steps it from a display link and reuses the SDL engines' orientation, lifecycle and menu overlay code, as the YU-RIS session does. The game's 1280x720 screen is fitted into the view with black bars. PingFang (Traditional on Traditional Chinese devices) draws all text, whichever font the game names; Hiragino Sans is registered after it. A touch is the left button; a long press is a right click, and so is the game menu's Back tile. Two fingers dragged up and down turn the mouse wheel, which opens the backlog; the Backlog command is one wheel step up. Holding Skip holds Ctrl. A three-finger touch opens the Mikage menu when that setting is on. Going to the background stops the game's clock and its sound.

A game folder needs `data/pack/start.ps3` and its `*.cpz` packs. The engine build is picked from the game's `cmvs*.exe` when it is a recorded one (else the 2016 base), and commands the runtime has not verified for that build still run; one it can't run stops the game with the script and command in the error. Chinese patches in the game folder are read by the runtime itself: 茶组's `Pk/*.pk` (アマツツミ) and Longinus's `Longinus.dat` (アオイトリ), so the patch launcher is not a startup file. The text encoding comes from the game's page, or automatically: GBK with a patch, else Shift-JIS. Saves go to the game folder's `savedata`.

Known limits: tested headless with アマツツミ and アオイトリ (Japanese and Chinese), not yet on a device; other patch layouts need their own reader in the runtime; saves of the original Windows engine can't be loaded; the Mikage screenshot may come out black (the Metal layer is drawn outside UIKit).

MikageCMVS is MIT OR Apache-2.0, which combines with Mikage's GPL-3.0-or-later. The licenses are in the submodule.
