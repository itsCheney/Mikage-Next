# RFVPRuntime host framework

Mikage Next runs FVP games with [rfvp](https://github.com/xmoezzz/rfvp), a Rust reimplementation of the FVP engine, built as an embeddable iOS dynamic framework.

Pinned input:

- `Source/` submodule: `itsCheney/rfvp`, branch `mikage`, based on upstream `main` (062fec8). The branch drops the Microsoft fonts upstream embeds (MS Gothic, MS Mincho, MS PGothic, MS PMincho; they may not be redistributed) and the host registers system fonts instead. It also adds the `rfvp_mikage_*` calls in `crates/rfvp/src/mikage_host.rs`; `MIKAGE.md` in the submodule lists the changes.

rfvp isn't an SDL program. It renders with wgpu into a `CAMetalLayer`, plays audio through kira (CoreAudio), decodes MP4 movies with VideoToolbox and WMV and MPEG-2 movies in Rust. `scripts/build-rfvp-ios.sh` builds the `rfvp` crate as a dynamic library for the device and Apple Silicon simulator, wraps each in `RFVPRuntime.framework` with the C header (`rfvp.h`) and a module map, checks the exports, then creates `build/RFVPRuntime.xcframework`. It needs Rust with the `aarch64-apple-ios` and `aarch64-apple-ios-sim` targets, nothing else from the other engines. It stays a framework of its own rather than sharing one with siglus_rs: the two pin different wgpu and kira versions.

`App/Engine/FVPSession.swift` gives the engine its own landscape window with a `CAMetalLayer` view, steps it from a display link and reuses the SDL engines' orientation, lifecycle and menu overlay code, as the Siglus session does. PingFang stands in for the four MS faces, with Hiragino Sans and Apple SD Gothic Neo behind it for missing characters (`App/Engine/SystemFontExport.swift`, shared with Siglus). A touch is a left click; a long press is a right click, and so is the game menu's Back tile, which FVP games use for their menu and to step back. Two fingers dragged up and down turn the mouse wheel, which opens and scrolls the backlog; the Backlog command is one wheel step up. Holding Skip holds Ctrl. A three-finger touch opens the Mikage menu when that setting is on. Saves go to the game folder's `savedata`, in rfvp's format. Going to the background pauses sounds and writes the global save; leaving the game writes it too.

A game folder needs a `.hcb` script or a `.bch` translation patch. Each one is a startup file; patches come first. The script's text encoding (Shift-JIS, GBK or UTF-8) comes from the game's page, or automatically: GBK for `.bch` files and names with GBK, else Shift-JIS.

Known limits: auto mode, quick save and load, and the save and load screens have no engine-level hook in FVP, so those menu tiles stay off; the Mikage screenshot may come out black (the Metal layer is drawn outside UIKit); a long press releases the left button before its right click, as with Siglus; saves of the original Windows engine can't be loaded.

rfvp is MPL-2.0, which allows combining with Mikage's GPL-3.0-or-later. A few WMA and VC-1 decoder files in `crates/na_wmv_player` are LGPL-2.1-or-later (translated from FFmpeg). The licenses are in the submodule.
