# SiglusRuntime host framework

Mikage Next runs SiglusEngine games with [siglus_rs](https://github.com/xmoezzz/siglus_rs), a Rust reimplementation of SiglusEngine, built as an embeddable iOS dynamic framework.

Pinned input:

- `Source/` submodule: `itsCheney/siglus_rs`, branch `mikage`, based on the upstream `1.0.0` tag. The branch drops the Microsoft MS PGothic font upstream embeds (it may not be redistributed); the host registers system fonts instead. It also adds the `siglus_mikage_*` calls in `crates/siglus_scene_vm/src/mikage_host.rs`.

siglus_rs isn't an SDL program. It renders with wgpu into a `CAMetalLayer` and plays audio through kira (CoreAudio). `scripts/build-siglus-ios.sh` builds the `siglus_scene_vm` crate as a dynamic library for the device and Apple Silicon simulator, wraps each in `SiglusRuntime.framework` with the C header (`siglus.h`) and a module map, checks the exports, then creates `build/SiglusRuntime.xcframework`. It needs Rust with the `aarch64-apple-ios` and `aarch64-apple-ios-sim` targets, nothing else from the other engines.

`App/Engine/SiglusSession.swift` gives the engine its own landscape window with a `CAMetalLayer` view, steps it from a display link and reuses the SDL engines' orientation, lifecycle and menu overlay code. Hiragino Sans and Hiragino Mincho stand in for MS Gothic and MS Mincho. A touch is a left click; a long press is a right click, and so is the game menu's Back tile, which Siglus games use for their system menu and to step back. A three-finger touch opens the Mikage menu when that setting is on. Saves go to the game folder's `savedata`, in the original format. Leaving the game, or the app going to the background, writes the global save.

A game folder needs `Scene.pck` or `Gameexe.dat`. Retail games need a second-layer key; siglus_rs works it out on first launch and writes `key.toml` into the game folder.

Known limits: the Mikage screenshot may come out black (the Metal layer is drawn outside UIKit); no backlog or save tiles in the Mikage menu yet; audio is muted, not paused, in the background.

siglus_rs is MPL-2.0, which allows combining with Mikage's GPL-3.0-or-later. A few WMV/WMA decoder files are LGPL-2.1-or-later. The licenses are in the submodule.
