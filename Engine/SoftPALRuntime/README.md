# SoftPALRuntime host framework

Mikage Next runs SoftPAL (PAL engine) games with [MikageSoftPAL](https://github.com/itsCheney/MikageSoftPAL), Mikage's fork of [sena-rs](https://github.com/xmoezzz/sena-rs), a SoftPAL runtime written in Rust, built as an embeddable iOS dynamic framework.

Pinned input:

- `Source/` submodule: `itsCheney/MikageSoftPAL`, branch `mikage`. It reads the game's PAC archives (`data.pac` with the script, `bgm.pac`, `st.pac`...) and runs the script; nothing is converted ahead of time. The C interface is `crates/sena-mobile` with the header `platform/ios/include/sena_mobile.h`; `MIKAGE.md` in the submodule lists what the fork adds to upstream.

MikageSoftPAL isn't an SDL program. It draws with wgpu into the `CAMetalLayer` of a view and has no audio output of its own: the session pulls its mixer from an `AVAudioSourceNode`. Button reactions and voice waits finish as their sound plays, so the sound is pulled whenever the game runs. `scripts/build-softpal-ios.sh` builds the `sena-mobile` crate as a dynamic library for the device and Apple Silicon simulator, wraps each in `SoftPALRuntime.framework` with the header and a module map, checks the exports, then creates `build/SoftPALRuntime.xcframework`. It needs Rust with the `aarch64-apple-ios` and `aarch64-apple-ios-sim` targets, nothing else. It stays a framework of its own: the other Rust engines pin other wgpu versions.

`App/Engine/SoftPALSession.swift` gives the engine its own landscape window with a `CAMetalLayer` view, steps it from a display link and reuses the SDL engines' orientation, lifecycle and menu overlay code, as the CMVS session does. The game's screen (SYSTEM.INI, usually 1280x720) is fitted into the view with black bars. The runtime bundles no font (upstream's embedded one was a Microsoft font): text the game's own bitmap font doesn't cover is drawn with PingFang (Traditional on Traditional Chinese devices), Hiragino Sans when PingFang can't be loaded. A touch is the left button; a long press is a right click, and so is the game menu's Back tile. Two fingers dragged up and down turn the mouse wheel, which opens the backlog; the Backlog command is one wheel step up. Holding Skip holds Ctrl. A three-finger touch opens the Mikage menu when that setting is on. Going to the background stops the game's clock and its sound. The Mikage screenshot draws the runtime's last frame.

A game folder needs `data.pac` (a PAC archive). The text encoding comes from the game's page, or automatically: Shift-JIS until the runtime reads a translation patch. The language of the multi-language releases follows the folder's Chinese archives and then the game's own setting. Saves go to the game folder's `savedata`; the first launch copies the original engine's `save` folder there, so its saves load.

Known limits: upstream checks its work against three engine builds, recognized by their executables (one is 恋×シンアイ彼女's), and Mikage tested 恋×シンアイ彼女 headless, not yet on a device. Older builds can stop at their start-up checks (魔女こいにっき does); translation patches that hook the Windows executable (XmoeLinker, Longinus) are not read yet.

MikageSoftPAL is MPL-2.0, which combines with Mikage's GPL-3.0-or-later. The license is in the submodule.
