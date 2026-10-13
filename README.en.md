# Mikage Next

[简体中文](README.md) · English

Mikage Next is a visual novel player for iPhone and iPad. It keeps all your games in one library and runs each one with the engine it was made for. KiriKiri (KAG / TJS) games play today, through an embedded build of [KRKRSDL3](https://github.com/itsCheney/krkrsdl3). ONScripter, Ren’Py and Artemis are next: their games are already recognized and listed, but stay disabled until their runtimes land.

Mikage ships no game content. You bring games you own.

## What it does

**Library**
- Every engine has its own folder in the app’s Documents: `krkr/`, `ons/`, `renpy/` and `artemis/`. Put a game folder in one of them with the Files app, or import a folder with the + button in the Library.
- The library rescans on launch, when the app returns to the foreground, and on pull to refresh. Folders keep their original names; Mikage’s own records live in Application Support.
- For KiriKiri, Mikage picks `startup.tjs` at the game root if there is one, otherwise the first XP3 among `启动游戏.xp3`, `startup.xp3`, `start.xp3`, `boot.xp3` and `data.xp3` that passes its signature check.
- Covers come from an image in the game folder, a picture you choose, or artwork from Bangumi or VNDB. A cover you set yourself is never replaced.
- Sort by recently played, date added, name or size. Games whose folder disappeared are kept as “missing” so you can relink or remove them.

**Playing**
- Games start inside the app and can be quit and relaunched, or switched for another game, without restarting Mikage.
- An in-game menu (floating button or three-finger tap) offers a screenshot, a performance overlay, “Mark Finished” and Quit. Quit goes through the game’s own exit confirmation, so it saves the way the game expects.
- The game pauses fully in the background and resumes where it was.
- Rendering is native Metal by default, including Emote animations, D3D layers and the built-in transitions. Software compositing presented through Metal is available as a fallback in Settings › Advanced.
- Saves stay in the game’s own `savedata/` folder.
- Options for the letterbox fill (game cover or black), honoring Silent Mode, and skipping translation-group credit videos.

**Bangumi and VNDB**
- Match a game to its Bangumi subject and/or VNDB entry to get its cover, synopsis, tags, developer, release date and screenshots. None of this needs an account.
- Sign in to Bangumi (or paste an access token) and add a VNDB token to sync your status, rating and comments. Mikage can move a game to “Playing” after a session and record start and finish dates on VNDB. Tokens are kept in the device Keychain.
- Title display: Bangumi Chinese name, original title, romanized title or folder name.

**Storage**
- Settings › Storage shows what Mikage uses: game files, saves, cover cache and diagnostic logs, with games sorted largest first.
- Per game: show in Files, export saves, remove the game but keep its saves (they come back when you import it again), or delete everything.
- Clear logs, and free space in one step by removing finished games while keeping their saves.

**Languages:** English, 简体中文 and 日本語.

## Status

| Engine | Detected | Plays |
| --- | --- | --- |
| KiriKiri (KRKRSDL3) | Yes | Yes |
| ONScripter | Yes | Not yet |
| Ren’Py | Yes | Not yet |
| Artemis | Yes | Not yet |

ZIP import and Wi-Fi upload aren’t available yet. The plan for the next engines is in [docs/PHASE-2-MULTI-ENGINE-PLAN.md](docs/PHASE-2-MULTI-ENGINE-PLAN.md).

## Building

Requirements: macOS with Xcode 26, CMake, XcodeGen and Git. vcpkg is bootstrapped by the runtime build. The first build compiles FFmpeg, SDL3 and the other runtime dependencies, so it takes a while.

```sh
git clone --recursive https://github.com/itsCheney/Mikage-Next.git
cd Mikage-Next
brew install cmake xcodegen
bash scripts/build-krkr-ios.sh   # builds build/KRKRRuntime.xcframework
xcodegen generate
open Mikage.xcodeproj
```

Choose the Mikage scheme, set your own signing team, and run. The deployment target is iOS 16.

To use Bangumi website sign-in instead of pasted tokens, register an app at [bgm.tv/dev/app](https://bgm.tv/dev/app) and pass `BANGUMI_APP_ID` and `BANGUMI_APP_SECRET` as build settings (see `project.yml`). Left empty, Bangumi falls back to access tokens.

Every push to `main` or `beta` also runs the [iOS build workflow](.github/workflows/ios.yml) on GitHub Actions. It runs the tests, builds the runtime and the app, and uploads an unsigned `Mikage-unsigned-<run>.ipa`. Sign it yourself before installing.

## Branches

- `main`: stable.
- `beta`: testing ahead of `main`.

## Repository layout

| Path | Contents |
| --- | --- |
| `App/` | The SwiftUI app: library, game details, player, settings, storage, Bangumi/VNDB linking, string catalog |
| `App/Engine/` | The bridge that hosts the KiriKiri runtime inside the app |
| `Sources/VNCore/` | Swift package: library scanning and import, path safety, Bangumi and VNDB clients, matching |
| `Engine/KRKRRuntime/` | The KRKRSDL3 runtime, pinned as a submodule ([krkrsdl3_build](https://github.com/itsCheney/krkrsdl3_build) → [krkrsdl3](https://github.com/itsCheney/krkrsdl3)) |
| `Tests/` | Swift unit tests and native runtime tests |
| `scripts/` | Build scripts and diagnostic analysis tools |
| `docs/` | Design notes, investigations and performance baselines |

## License

Mikage Next is free software, released under the [GNU General Public License v3.0 or later](LICENSE).

The bundled KiriKiri runtime carries its own licenses: see [Engine/KRKRRuntime/KRKRSDL3-LICENSE.txt](Engine/KRKRRuntime/KRKRSDL3-LICENSE.txt) and the notices inside the runtime submodule. Read them before distributing builds.

KiriKiri, ONScripter, Ren’Py, Artemis, Bangumi and VNDB are the work of their respective authors. Mikage is not affiliated with any of them.
