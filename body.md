Test build of **GE-Proton 11.0-7** as bionic **arm64ec** and **x86_64** layers for Bannerlator / WinNative / Winlator-bionic, made from GloriousEggroll's **[GE-Proton11-7](https://github.com/GloriousEggroll/proton-ge-custom/releases/tag/GE-Proton11-7)** release. It comes in two versions:

- **GE-Proton 11.0-7**: our v7 **GE-Proton 11.0-6** layer with GE-Proton11-7's game fixes.
- **GE-Proton 11.0-7.1** (experimental): the same v7 stack moved onto the newer Valve Wine that GE-Proton11-7 is built on.

Both arm64ec layers are attached, plus the **x86_64 (box64)** build of GE-Proton 11.0-7. The x86_64 build of GE-Proton 11.0-7.1 is still building and will be added here.

> 🧪 **Pre-release for testing.** Neither layer has been booted on a device yet. This release is not Latest. Only **GE-Proton 11.0-7 arm64ec** is also in the in-app catalog, via the winlator-contents v7 release. Install the others from file. Each one installs into its own slot next to your existing layers, so nothing is overwritten.

## What's new in GE-Proton 11.0-7 compared to v7 GE-Proton 11.0-6

GE-Proton11-7's game-fix tier, rebuilt on our v7 GE 11.0-6 layer:

- 🎮 **Black Desert Online:** stays fullscreen when the game loses focus (`black-desert-keep-fullscreen-on-focus-loss`, in `win32u`). Active on arm64ec.
- 🎮 **Max Payne:** fixes the new-game crash caused by the game's CPU detection in `rlmfc.dll` (`max-payne-cpu-detection`, in 32-bit `ntdll`). Active on arm64ec.
- **AI LIMIT** DX12 compute-shader fallback and **NASCAR 25** protector fix: both are included, but both are x86_64-only code, so they do nothing in an arm64ec layer. GE's NASCAR 25 patch was adapted to our base, where the syscall trap lives in a different function.
- **Assetto Corsa HUD:** now uses GE-Proton11-7's version of the patch. The resulting source is identical to what the 11-6 version produced.
- **Dragon Age: Inquisition XInput** (`dai_xinput`): kept. GE removed it in 11-7 because its new Sony/XInput controller stack replaces it, and that stack is not part of these layers.
- **Not included:** `return-to-krondor-text-bitmap-readback`. It works around a Wine change (`624a73bc`) that this layer's base doesn't have (the 11.0-7.1 base does, so it is included there), so the text readback it fixes should already work.

**Everything else is identical to v7 GE-Proton 11.0-6.** The layer was compared file by file against the v7 `GE-proton-11.0-6-arm64ec.wcp`:

| | |
|---|---|
| Files | both have the same 2,227 entries, with nothing added, removed or re-linked |
| Byte-identical | 729 |
| Identical except PE build stamps (timestamp, checksum, debug ID) | 1,494 |
| Real code changes | 4 |

The 4 changes:
- `win32u.so`: Black Desert fix.
- 32-bit `ntdll.dll`: Max Payne fix.
- arm64ec `ntdll.dll`: the AI LIMIT patch adds an unused pointer to Wine's module record and an empty call. No behaviour change.
- `profile.json`: name and description.

GE-Proton11-7's other changes are **not** in the 11.0-7 layer. That includes the newer Wine base, Wayland rendering, the media/video rework, DualSense audio and haptics, Steam overlay fixes and protonfixes. Only the game-fix tier carries over to a layer on this base. The 11.0-7.1 build moves to GE-Proton11-7's Wine base.

## Layers

<details>
<summary><b>GE-Proton 11.0-7</b> &nbsp;·&nbsp; arm64ec · Wine 11 · versionCode <code>7</code> &nbsp;·&nbsp; ✅ attached</summary>

<br>

| | |
|---|---|
| **Base** | GloriousEggroll **[GE-Proton11-7](https://github.com/GloriousEggroll/proton-ge-custom/releases/tag/GE-Proton11-7)** game-fix tier on Valve **[Proton 11.0-1](https://github.com/ValveSoftware/Proton/releases/tag/proton-11.0-1)** (Wine 11.0-1), the same base as our v7 GE-Proton 11.0-6 |
| **Installs as** | `11.0-7-arm64ec-7` (a new slot next to `11.0-6-arm64ec-7`) |
| **ntdll fix** | `RtlIsEcCode` bounds check (Denuvo unwind loop, *NFS Heat*) |
| **EA fixes** | ws2_32 dual-stack DNS (`AI_V4MAPPED`) · nsiproxy default route (`WINE_ANDROID_GATEWAY`) · gdiplus span clamp (installer wizard) |
| **DirectAudio** | v1.3.2, opt-in via registry `Audio=directaudio`; mic capture opt-in via `BANNER_AUDIO_DIRECT_MIC=1` |
| **XInput fix** | the update thread survives transient wait failures, so controllers no longer die mid-game |
| **Wine XP desktop** | Luna taskbar and start menu · XP window frames · `winexp.msstyles` visual style: Blue / Olive Green / Silver, or navy / moss / graphite in dark mode |
| **Assets** | `GE-proton-11.0-7-arm64ec.wcp` (one file for 4 KB and 16 KB page devices) · `GE-proton-11.0-7-x86_64.wcp` (box64, 4 KB pages) |

**Android compatibility fixes:** SD-card boot (`noexec` / `force_anon`) · drive-root copy guard · `C.UTF-8` locale
**Runtime:** realized-font-handle cap `32768` · `WINEVMEMMAXSIZE` cap · fast-yield gate · FEX-unixlib load-by-name loader · XRandR / XRender · esync (fsync is compiled out because Android blocks it)
**Build:** `-g0 -O2` release build, `llvm-strip` on the PE DLLs/EXEs and the unix `.so` loaders · zstd-compressed `.wcp` · Android API 28
**Inherited bionic base:** the Winlator-bionic / GameNative Android patch set every layer is built on:
- winex11 driver (window, keyboard, mouse, OpenGL, bitblt)
- preloader
- clipboard bridge
- Android DNS resolver
- links open in the Android browser
- MIDI
- winemenubuilder
- wow64 syscall path

**GE game fixes (13):**
- **Carried from 11-6:** `WM_ACTIVATEAPP` · `assettocorsa-hud` · `dai_xinput` · `eac_60101_timeout` · `maplestory` ×2 · `pso2_hack` · `silence-starcitizen` · `vgsoh`
- **New in 11-7:** `black-desert-keep-fullscreen-on-focus-loss` · `max-payne-cpu-detection` · `ai-limit-dx12-compute-shader-fallback` (x86_64-only) · `nascar25-protector` (x86_64-only)

**Build verification:** the build refuses to package unless all 20 shipped-feature source checks and all 13 GE patch checks pass, and the built binaries pass the layer verifier. That covers the XP desktop, DirectAudio 1.3.2, `RtlIsEcCode`, the font cap, the XInput fix, the EA network fixes and the GE markers.

**sha256 (arm64ec):** `3c4d2c1f1473570b84f186c17fc1fc8cfb47b9d285ba5b004ee981e09f1b008c` (94,590,448 bytes)

**x86_64 (box64) build**, `GE-proton-11.0-7-x86_64.wcp`, installs as `11.0-7-x86_64-7`. It has the same source, patches and checks as the arm64ec layer, built for x86_64 at Android API 28 with 4 KB pages, the same way as v7's Proton 11.0-2 x86_64 layer:
- **Checks:** all 20 shipped-feature source checks, the GE tier (13/13) and the layer verifier passed on x86_64.
- **GE fixes that differ on x86_64:**
  - **AI LIMIT's DX12 fix is active** (confirmed in the x86_64 `ntdll.dll`).
  - **NASCAR 25's fix is compiled in**, but it sits in a Linux syscall trap that box64 never reaches, so don't expect it to do anything.
- **Built from:** `proton_11.7-GE` [`11c3486c`](https://github.com/The412Banner/proton-wine/commit/11c3486cc39a1ad730630c5c4554417d90d9909f) · CI run [35084480801](https://github.com/The412Banner/proton-wine/actions/runs/35084480801) (green).
- **sha256:** `2bb5c22ebf0ccfa2b054bad62023ad96273fadcb430f0ea059d641254054ab06` (61,980,430 bytes)

> ⚠️ **32-bit programs currently crash on our x86_64 layers under box64.** On v7 Proton 11.0-2 x86_64, every 32-bit Windows program tested crashed as soon as it started; that includes every Visual C++ redistributable installer, even the `_x64` ones. The same programs work on arm64ec. The desktop and 64-bit programs do run and render. Until this is fixed, use arm64ec for 32-bit games and installers; that also covers the Max Payne fix, which only helps a 32-bit game. This x86_64 build has not been booted on a device.

</details>

<details>
<summary><b>GE-Proton 11.0-7.1 (new Valve base, experimental)</b> &nbsp;·&nbsp; arm64ec · Wine 11 · versionCode <code>1</code> &nbsp;·&nbsp; ✅ attached</summary>

<br>

| | |
|---|---|
| **Base** | Valve Wine **[`46b29104`](https://github.com/ValveSoftware/wine/commit/46b29104e3741fe23bf5e2547196a253aab88c89)** (2026-09-15), the Wine GloriousEggroll **[GE-Proton11-7](https://github.com/GloriousEggroll/proton-ge-custom/releases/tag/GE-Proton11-7)** is built on. It is 313 commits newer than the Valve base of our v7 Proton 11.0-2 layer, and close to Proton Experimental. Our v7 Proton 11.0-2 layer was merged onto it with no conflicts. |
| **Installs as** | `11.0-7.1-arm64ec-1` (its own slot; does not replace GE-Proton 11.0-7 or 11.0-6) |
| **ntdll fix** | `RtlIsEcCode` bounds check (Denuvo unwind loop, *NFS Heat*) |
| **EA fixes** | ws2_32 dual-stack DNS (`AI_V4MAPPED`) · nsiproxy default route (`WINE_ANDROID_GATEWAY`) · gdiplus span clamp · Android DNS resolver |
| **DirectAudio** | v1.3.2, opt-in via registry `Audio=directaudio`; mic capture opt-in via `BANNER_AUDIO_DIRECT_MIC=1` |
| **XInput fix** | the update thread survives transient wait failures. Re-ported onto Valve's reworked xinput, which still ended the thread on a failure. |
| **Wine XP desktop** | Luna taskbar and start menu · XP window frames · `winexp.msstyles` visual style: Blue / Olive Green / Silver, or navy / moss / graphite in dark mode |
| **Asset** | `GE-proton-11.0-7.1-arm64ec.wcp` (one file for 4 KB and 16 KB page devices) |

**Android compatibility fixes:** SD-card boot (`noexec` / `force_anon`) · drive-root copy guard · `C.UTF-8` locale
**Runtime:** realized-font-handle cap `32768` · `WINEVMEMMAXSIZE` cap · fast-yield gate · FEX-unixlib load-by-name loader · XRandR / XRender · esync (fsync is compiled out because Android blocks it)
**Build:** `-g0 -O2` release build, `llvm-strip` on the PE DLLs/EXEs and the unix `.so` loaders · zstd-compressed `.wcp` · Android API 28
**Inherited bionic base:** the same Winlator-bionic / GameNative Android patch set as GE-Proton 11.0-7. 52 of its 54 patches applied unchanged. The other two were re-ported onto Valve's rewritten code: the XInput fix, and the winepulse timer guard, which now also frees the device name the new code allocates.

**GE game fixes (13):** GE-Proton11-7's own versions, which match this Wine base, plus our `dai_xinput`.
- **Carried from 11-6:** `WM_ACTIVATEAPP` · `assettocorsa-hud` · `dai_xinput` · `eac_60101_timeout` · `maplestory` ×2 · `pso2_hack` · `silence-starcitizen` · `vgsoh`
- **New in 11-7:** `black-desert-keep-fullscreen-on-focus-loss` · `max-payne-cpu-detection` · `ai-limit-dx12-compute-shader-fallback` (x86_64-only, inactive on arm64ec)
- **Also new in 11-7, only in this layer:** `return-to-krondor-text-bitmap-readback`. This newer base carries the Wine change it works around (`win32u: Reject DDBs that are not 1-bit or 32-bit for GetDIBits()`), so the fix is needed here.
- **Not included:** `nascar25-protector`. It hooks a Linux syscall handler that only GE's Wayland patch set adds, and it is x86_64-only code.
- These patches are applied at GE's own fuzz level (2), so a patch cannot land in the wrong place and still pass.

**Newer-base changes worth watching while testing** (from Valve's commits since our 11.0-2 base):
- **Thread suspend reworked:** Valve replaced its earlier WoW64/ARM64 suspend workarounds with a simpler upstream approach and changed the arm64ec hand-off to FEX. This is the area most likely to change launch reliability (TF2 / Brawlhalla) and Rockstar Social Club. Test with a recent FEXCore.
- **.NET:** AnyCPU programs now run as x64 under FEX instead of native ARM64.
- **X11:** keyboard text input goes through `Xutf8LookupString`, fullscreen windows are no longer force-activated when mapped, and window-state updates are locked while applied. Check focus and fullscreen behaviour.
- **New files:** Valve's `igd10iumd64.dll` (both architectures) and a WebView2 (`msedgewebview2`) no-sandbox workaround in `kernelbase`.

**Not included from GE-Proton11-7:** Wayland, DualSense audio/haptics, Steam client/overlay patches, protonfixes (GE's Python launcher), the media/video rework, and GE's wine-staging / hotfix patches. Some of the hotfixes may come in a later build: the clipboard stale-handle fix, Warcraft III login, the Diablo IV power-status freeze, and the process-exit hang.

**Build verification:**
- All 54 Android patches applied, and all 21 shipped-feature source checks passed.
- The GE tier applied and verified 13/13.
- The layer verifier passed on the built binaries: XP desktop, DirectAudio 1.3.2, `RtlIsEcCode` bytes, font cap, XInput fix, EA fixes, GE markers.
- The wcp has 2,248 entries against 2,246 in v7 Proton 11.0-2. The only additions are the two `igd10iumd64.dll` files, and nothing is missing.

**sha256:** `acb6dc12060c0ae79747d8bd375dd4dc271cc6e3494a73663c2da063b41445a7` (96,722,063 bytes)

</details>

## Installation

Download the `.wcp` and install it from file on the app's **Contents** screen. Then create a new arm64ec container on it, or switch a test container to it. Your existing layers and containers are not touched.

## Source

- **GloriousEggroll GE-Proton11-7:** [release](https://github.com/GloriousEggroll/proton-ge-custom/releases/tag/GE-Proton11-7) · [source at the tag](https://github.com/GloriousEggroll/proton-ge-custom/tree/GE-Proton11-7) · [game-patches](https://github.com/GloriousEggroll/proton-ge-custom/tree/GE-Proton11-7/patches/game-patches)
- **This layer (GE-Proton 11.0-7):** branch [`proton_11.7-GE`](https://github.com/The412Banner/proton-wine/tree/proton_11.7-GE) at [`07471201`](https://github.com/The412Banner/proton-wine/commit/07471201867cca8fbd79f282663cec48df102470), built from our v7 `proton_11.6-GE` `d4e97500` · CI run [35076092489](https://github.com/The412Banner/proton-wine/actions/runs/35076092489) (green)
- **GE-Proton 11.0-7.1 (new base):** branch [`wip/proton_11.7-GE-valve`](https://github.com/The412Banner/proton-wine/tree/wip/proton_11.7-GE-valve) at [`b11baa2d`](https://github.com/The412Banner/proton-wine/commit/b11baa2d3f3861dbbd2c2c190bbaa5497ad345e7): our v7 `proton_11.0-2` `777342a9` with Valve `46b29104` merged in, then the two re-ports and the GE tier · CI run [35076546926](https://github.com/The412Banner/proton-wine/actions/runs/35076546926) (green)
- **Valve Wine used by GE-Proton11-7:** [`ValveSoftware/wine@46b29104`](https://github.com/ValveSoftware/wine/commit/46b29104e3741fe23bf5e2547196a253aab88c89)
