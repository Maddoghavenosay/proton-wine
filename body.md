Consolidated build of the Proton / GE-Proton layers at **versionCode 8**, rebuilt from the [`build-bionic-layers-20260911-xp`](https://github.com/The412Banner/proton-wine/releases/tag/build-bionic-layers-20260911-xp) sources — every layer is its v7 build plus **Wayland and HDR10**: Wine's own `winewayland.drv` for Bannerlator's embedded Wayland compositor, eight bundled Wayland Turnip drivers, and HDR10 screens reported to Windows. Nothing else was added, removed or rebased. Each layer is a single `.wcp` that runs on both 4 KB- and 16 KB-page devices.

**Two things changed about the set itself:** the **Proton-CachyOS** layer joins it, and **GE-Proton 11.0-7**, **11.0-7.1** and **CachyOS** now ship an **x86_64 (box64)** build alongside `Proton 11.0-2`, so there are four x86_64 layers instead of one.

> ⚠️ **PRE-RELEASE — NOT DEVICE-TESTED.** Every layer here was built green in CI, `verify-layer` passed on every leg, and each `.wcp` was unpacked and checked to contain `winewayland.so`, both `winewayland.drv` builds, the eight Wayland Turnip ICDs and the right `versionCode 8` identity. **No game has been launched on any of them.** The Wayland and HDR10 work itself was proven on device, but on the **11.0-2 Wayland line** (`11.0-2.1-arm64ec-16`) — not on these rebuilt layers. Treat this as a build to test, not a build to rely on.
>
> These wcp install into a new coexisting `<version>-arm64ec-8` slot next to v7's `-7` — nothing is overwritten, and any container already on a `-7` layer is offered this as an in-place update with a revert snapshot.
>
> ⛔ **Proton 10.0-4 and GE-Proton 10.0-34 are NOT in this release.** They stay at v7. Their Wine 10 base sits at GDI driver interface version 102 against this work's 109 and shares no history with the 11.x line, so Wayland there is a backport rather than a port. Deferred deliberately.

## What's new

- 🌊 **Wayland on every 11.x layer** — Wine's `winewayland.drv` (unix `winewayland.so` plus the `aarch64-windows` and `i386-windows` PE drivers) so a layer can drive Bannerlator's embedded Wayland compositor directly instead of going through the X server. Each layer also carries its own **eight Wayland Turnip drivers** with an app-selectable game driver: `plain` (upstream, Adreno 6xx/730/740/750), `a7xx`, `a8xx`, `a8xx-perf`, `a8xx-gen8`, `a8xx-smxz`, `a8xx-white` and `a8xx-upstream`. The layers still run as ordinary X11 Proton layers when Wayland is off.
- 🌈 **HDR10 reported to Windows** — the screen's real peak, frame-average and black level are described to the guest through a built EDID (CTA-861.3), DXGI and DisplayConfig, plus the builtin `amd_ags_x64`, so a game's own HDR option stops being greyed out instead of seeing DXVK's stand-in values.
- 🧩 **Proton-CachyOS joins the set** — the `wine-cachyos` layer (`11.0-20260703`), previously a standalone test build, is now part of the consolidated release with the same Wayland and HDR10 work. Its Wayland driver is larger than the others' because it keeps CachyOS's own upstream Wayland additions (`alpha-modifier`, `color-management`, `content-type`) alongside ours.
- 🧱 **Three more x86_64 layers** — GE-Proton 11.0-7, 11.0-7.1 and CachyOS now build an x86_64 (box64) leg as well as arm64ec. **The x86_64 layers do not have Wayland** — `winewayland` is arm64ec-only — but they are rebuilt at versionCode 8 with everything else, including the XP Start-menu Control Panel fix. Their descriptions say so rather than claiming Wayland.
- 🏷️ **Wine component identity corrected** — GE-Proton 11.0-3, 11.0-5, 11.0-6 and 11.0-7 were emitting their `proton-wine-*.wcp.xz` companion under Proton 11.0-1's name and version, so four different builds collided on one identity and one filename. Each now carries its own. The layer `.wcp` files themselves were never affected.
- 🔢 **versionCode → 8** on every layer in this release — new coexisting install slot `<version>-arm64ec-8` (and `-x86_64-8`) next to v7's `-7`.
- ♻️ **Carried over from v7** — Wine XP desktop (Luna taskbar, start menu, XP window frames, `winexp.msstyles`, dark mode) · XP Start-menu **Control Panel** fix · XInput update-thread fix · `RtlIsEcCode` bounds check · DirectAudio v1.3.2 · ws2_32 dual-stack DNS · nsiproxy default route · gdiplus span clamp · realized-font-handle cap `32768` · GE game-fix tiers · SD-card boot fix (`noexec` / `force_anon`) · drive-root copy fix · `C.UTF-8` locale · `WINEVMEMMAXSIZE` cap · fast-yield gate · FEX-unixlib loader · XRandR / XRender · one wcp per layer (4 KB + 16 KB pages).

**Scope of the changes versus v7:** the `winewayland.drv` driver and its Wayland protocol sources, `win32u` (display, vulkan surface and window handling), `winevulkan`, `ntdll`'s loader, `amd_ags_x64` (HDR/AGS reporting), `explorer`, the wserver window-station code, and the bundled Wayland Turnip drivers and their loader libraries. No FEX, DXVK, audio or input changes.

## Layers

<details>
<summary><b>GE-Proton 11.0-7.1</b> &nbsp;·&nbsp; arm64ec + <b>x86_64</b> · Wine 11 (bleeding-edge Valve base) · versionCode <code>8</code></summary>

<br>

| | |
|---|---|
| **Base** | GloriousEggroll **[GE-Proton11-7](https://github.com/GloriousEggroll/proton-ge-custom/releases/tag/GE-Proton11-7)** game-fix tier on **ValveSoftware/wine `46b29104`** (bleeding-edge, 2026-09-15) |
| **Installs as** | `11.0-7.1-arm64ec-8` · `11.0-7.1-x86_64-8` |
| **Wayland** *(arm64ec only)* | `winewayland.drv` (unix `.so` + `aarch64-windows` and `i386-windows` PE) · 8 bundled Wayland Turnip drivers (`plain`, `a7xx`, `a8xx`, `a8xx-perf`, `a8xx-gen8`, `a8xx-smxz`, `a8xx-white`, `a8xx-upstream`) · `banner-desktop-v1` protocol |
| **HDR10** *(arm64ec only)* | screen peak / frame-average / black level reported to Windows through a built CTA-861.3 EDID, DXGI and DisplayConfig advanced colour, plus the builtin `amd_ags_x64` |
| **ntdll fix** | `RtlIsEcCode` bounds check (Denuvo unwind loop) |
| **EA fixes** | ws2_32 dual-stack DNS · nsiproxy default route · gdiplus span clamp |
| **DirectAudio** | v1.3.2 (vendored source) — opt-in via registry `Audio=directaudio`; mic capture opt-in via `BANNER_AUDIO_DIRECT_MIC=1` |
| **XInput fix** | update thread survives transient wait failures (controllers no longer die mid-game) |
| **Wine XP desktop** | Luna taskbar + start menu (with the Control Panel fix) · XP window frames · `winexp.msstyles` visual style — Blue / Olive Green / Silver, navy / moss / graphite in dark mode |
| **Assets** | `GE-proton-11.0-7.1-arm64ec.wcp` · `GE-proton-11.0-7.1-x86_64.wcp` (4 KB + 16 KB pages) |

**Android compatibility fixes** — SD-card boot (`noexec` / `force_anon`) · drive-root copy · `C.UTF-8` locale
**Runtime** — realized-font-handle cap `32768` · `WINEVMEMMAXSIZE` cap · fast-yield gate · FEX-unixlib loader · XRandR / XRender
**Build** — `-g0 -O2` release build, `llvm-strip` on both the PE DLLs/EXEs and the unix `.so` loaders · zstd-compressed `.wcp` · ccache in CI (build speed only, not in the layer)
**Inherited bionic base** — the Winlator-bionic / GameNative Android patch set every layer is built on: esync/fsync, winex11 driver (window/keyboard/mouse/OpenGL/bitblt), preloader, clipboard, winemenubuilder, MIDI, DNS resolver, wow64 syscall path
**GE game-fixes** — `ai-limit` · `assettocorsa` · `black-desert` · `dai_xinput` · `eac` · `maplestory` · `max-payne` · `pso2` · `return-to-krondor` · `silence-starcitizen` · `vgsoh` · `WM_ACTIVATEAPP`

> ℹ️ This layer keeps its own install slot (`11.0-7.1`) apart from the current-base GE 11.0-7, so the two can sit side by side. Its Wine base is newer than every other layer here, which is why its `win32u` needed a hand-merge to take the Wayland work.

</details>

<details>
<summary><b>GE-Proton 11.0-7</b> &nbsp;·&nbsp; arm64ec + <b>x86_64</b> · Wine 11 · versionCode <code>8</code></summary>

<br>

| | |
|---|---|
| **Base** | GloriousEggroll **[GE-Proton11-7](https://github.com/GloriousEggroll/proton-ge-custom/releases/tag/GE-Proton11-7)** game-fix tier on Valve **[Proton 11.0-1](https://github.com/ValveSoftware/Proton/releases/tag/proton-11.0-1)** (Wine 11.0-1) |
| **Installs as** | `11.0-7-arm64ec-8` · `11.0-7-x86_64-8` |
| **Wayland** *(arm64ec only)* | `winewayland.drv` (unix `.so` + `aarch64-windows` and `i386-windows` PE) · 8 bundled Wayland Turnip drivers (`plain`, `a7xx`, `a8xx`, `a8xx-perf`, `a8xx-gen8`, `a8xx-smxz`, `a8xx-white`, `a8xx-upstream`) · `banner-desktop-v1` protocol |
| **HDR10** *(arm64ec only)* | screen peak / frame-average / black level reported to Windows through a built CTA-861.3 EDID, DXGI and DisplayConfig advanced colour, plus the builtin `amd_ags_x64` |
| **ntdll fix** | `RtlIsEcCode` bounds check (Denuvo unwind loop) |
| **EA fixes** | ws2_32 dual-stack DNS · nsiproxy default route · gdiplus span clamp |
| **DirectAudio** | v1.3.2 (vendored source) — opt-in via registry `Audio=directaudio`; mic capture opt-in via `BANNER_AUDIO_DIRECT_MIC=1` |
| **XInput fix** | update thread survives transient wait failures (controllers no longer die mid-game) |
| **Wine XP desktop** | Luna taskbar + start menu (with the Control Panel fix) · XP window frames · `winexp.msstyles` visual style — Blue / Olive Green / Silver, navy / moss / graphite in dark mode |
| **Assets** | `GE-proton-11.0-7-arm64ec.wcp` · `GE-proton-11.0-7-x86_64.wcp` (4 KB + 16 KB pages) |

**Android compatibility fixes** — SD-card boot (`noexec` / `force_anon`) · drive-root copy · `C.UTF-8` locale
**Runtime** — realized-font-handle cap `32768` · `WINEVMEMMAXSIZE` cap · fast-yield gate · FEX-unixlib loader · XRandR / XRender
**Build** — `-g0 -O2` release build, `llvm-strip` on both the PE DLLs/EXEs and the unix `.so` loaders · zstd-compressed `.wcp` · ccache in CI (build speed only, not in the layer)
**Inherited bionic base** — the Winlator-bionic / GameNative Android patch set every layer is built on: esync/fsync, winex11 driver (window/keyboard/mouse/OpenGL/bitblt), preloader, clipboard, winemenubuilder, MIDI, DNS resolver, wow64 syscall path
**GE game-fixes** — `maplestory` · `dai_xinput` · `eac` · `pso2` · `assettocorsa` · `silence-starcitizen` · `vgsoh` · `WM_ACTIVATEAPP` · `black-desert fullscreen` · `max-payne cpu detection` · `ai-limit dx12 compute fallback`

</details>

<details>
<summary><b>GE-Proton 11.0-6</b> &nbsp;·&nbsp; arm64ec · Wine 11 · versionCode <code>8</code></summary>

<br>

| | |
|---|---|
| **Base** | GloriousEggroll **[GE-Proton11-6](https://github.com/GloriousEggroll/proton-ge-custom/releases/tag/GE-Proton11-6)** game-fix tier on Valve **[Proton 11.0-1](https://github.com/ValveSoftware/Proton/releases/tag/proton-11.0-1)** (Wine 11.0-1) |
| **Installs as** | `11.0-6-arm64ec-8` |
| **Wayland** | `winewayland.drv` (unix `.so` + `aarch64-windows` and `i386-windows` PE) · 8 bundled Wayland Turnip drivers (`plain`, `a7xx`, `a8xx`, `a8xx-perf`, `a8xx-gen8`, `a8xx-smxz`, `a8xx-white`, `a8xx-upstream`) · `banner-desktop-v1` protocol |
| **HDR10** | screen peak / frame-average / black level reported to Windows through a built CTA-861.3 EDID, DXGI and DisplayConfig advanced colour, plus the builtin `amd_ags_x64` |
| **ntdll fix** | `RtlIsEcCode` bounds check (Denuvo unwind loop) |
| **EA fixes** | ws2_32 dual-stack DNS · nsiproxy default route · gdiplus span clamp |
| **DirectAudio** | v1.3.2 (vendored source) — opt-in via registry `Audio=directaudio`; mic capture opt-in via `BANNER_AUDIO_DIRECT_MIC=1` |
| **XInput fix** | update thread survives transient wait failures (controllers no longer die mid-game) |
| **Wine XP desktop** | Luna taskbar + start menu (with the Control Panel fix) · XP window frames · `winexp.msstyles` visual style — Blue / Olive Green / Silver, navy / moss / graphite in dark mode |
| **Assets** | `GE-proton-11.0-6-arm64ec.wcp` (4 KB + 16 KB pages) |

**Android compatibility fixes** — SD-card boot (`noexec` / `force_anon`) · drive-root copy · `C.UTF-8` locale
**Runtime** — realized-font-handle cap `32768` · `WINEVMEMMAXSIZE` cap · fast-yield gate · FEX-unixlib loader · XRandR / XRender
**Build** — `-g0 -O2` release build, `llvm-strip` on both the PE DLLs/EXEs and the unix `.so` loaders · zstd-compressed `.wcp` · ccache in CI (build speed only, not in the layer)
**Inherited bionic base** — the Winlator-bionic / GameNative Android patch set every layer is built on: esync/fsync, winex11 driver (window/keyboard/mouse/OpenGL/bitblt), preloader, clipboard, winemenubuilder, MIDI, DNS resolver, wow64 syscall path
**GE game-fixes** — `maplestory` · `dai_xinput` · `eac` · `pso2` · `assettocorsa` · `silence-starcitizen` · `vgsoh` · `WM_ACTIVATEAPP`

> ℹ️ GE dropped its `battlenet` workaround upstream in GE-Proton11-6, so this layer's game-fix set is the 11.0-5 tier minus `battlenet`. This is the layer the *NFS Heat* / Denuvo fix and the v7 Wine XP desktop were device-proven on.

</details>

<details>
<summary><b>GE-Proton 11.0-5</b> &nbsp;·&nbsp; arm64ec · Wine 11 · versionCode <code>8</code></summary>

<br>

| | |
|---|---|
| **Base** | GloriousEggroll **[GE-Proton11-5](https://github.com/GloriousEggroll/proton-ge-custom/releases/tag/GE-Proton11-5)** game-fix tier on Valve **[Proton 11.0-1](https://github.com/ValveSoftware/Proton/releases/tag/proton-11.0-1)** (Wine 11.0-1) |
| **Installs as** | `11.0-5-arm64ec-8` |
| **Wayland** | `winewayland.drv` (unix `.so` + `aarch64-windows` and `i386-windows` PE) · 8 bundled Wayland Turnip drivers (`plain`, `a7xx`, `a8xx`, `a8xx-perf`, `a8xx-gen8`, `a8xx-smxz`, `a8xx-white`, `a8xx-upstream`) · `banner-desktop-v1` protocol |
| **HDR10** | screen peak / frame-average / black level reported to Windows through a built CTA-861.3 EDID, DXGI and DisplayConfig advanced colour, plus the builtin `amd_ags_x64` |
| **ntdll fix** | `RtlIsEcCode` bounds check (Denuvo unwind loop) |
| **EA fixes** | ws2_32 dual-stack DNS · nsiproxy default route · gdiplus span clamp |
| **DirectAudio** | v1.3.2 (vendored source) — opt-in via registry `Audio=directaudio`; mic capture opt-in via `BANNER_AUDIO_DIRECT_MIC=1` |
| **XInput fix** | update thread survives transient wait failures (controllers no longer die mid-game) |
| **Wine XP desktop** | Luna taskbar + start menu (with the Control Panel fix) · XP window frames · `winexp.msstyles` visual style — Blue / Olive Green / Silver, navy / moss / graphite in dark mode |
| **Assets** | `GE-proton-11.0-5-arm64ec.wcp` (4 KB + 16 KB pages) |

**Android compatibility fixes** — SD-card boot (`noexec` / `force_anon`) · drive-root copy · `C.UTF-8` locale
**Runtime** — realized-font-handle cap `32768` · `WINEVMEMMAXSIZE` cap · fast-yield gate · FEX-unixlib loader · XRandR / XRender
**Build** — `-g0 -O2` release build, `llvm-strip` on both the PE DLLs/EXEs and the unix `.so` loaders · zstd-compressed `.wcp` · ccache in CI (build speed only, not in the layer)
**Inherited bionic base** — the Winlator-bionic / GameNative Android patch set every layer is built on: esync/fsync, winex11 driver (window/keyboard/mouse/OpenGL/bitblt), preloader, clipboard, winemenubuilder, MIDI, DNS resolver, wow64 syscall path
**GE game-fixes** — `battlenet` · `maplestory` · `dai_xinput` · `eac` · `pso2` · `assettocorsa` · `silence-starcitizen` · `vgsoh` · `WM_ACTIVATEAPP`

</details>

<details>
<summary><b>GE-Proton 11.0-3</b> &nbsp;·&nbsp; arm64ec · Wine 11 · versionCode <code>8</code></summary>

<br>

| | |
|---|---|
| **Base** | GloriousEggroll **[GE-Proton11-3](https://github.com/GloriousEggroll/proton-ge-custom/releases/tag/GE-Proton11-3)** game-fix tier on Valve **[Proton 11.0-1](https://github.com/ValveSoftware/Proton/releases/tag/proton-11.0-1)** (Wine 11.0-1) |
| **Installs as** | `11.0-3-arm64ec-8` |
| **Wayland** | `winewayland.drv` (unix `.so` + `aarch64-windows` and `i386-windows` PE) · 8 bundled Wayland Turnip drivers (`plain`, `a7xx`, `a8xx`, `a8xx-perf`, `a8xx-gen8`, `a8xx-smxz`, `a8xx-white`, `a8xx-upstream`) · `banner-desktop-v1` protocol |
| **HDR10** | screen peak / frame-average / black level reported to Windows through a built CTA-861.3 EDID, DXGI and DisplayConfig advanced colour, plus the builtin `amd_ags_x64` |
| **ntdll fix** | `RtlIsEcCode` bounds check (Denuvo unwind loop) |
| **EA fixes** | ws2_32 dual-stack DNS · nsiproxy default route · gdiplus span clamp |
| **DirectAudio** | v1.3.2 (vendored source) — opt-in via registry `Audio=directaudio`; mic capture opt-in via `BANNER_AUDIO_DIRECT_MIC=1` |
| **XInput fix** | update thread survives transient wait failures (controllers no longer die mid-game) |
| **Wine XP desktop** | Luna taskbar + start menu (with the Control Panel fix) · XP window frames · `winexp.msstyles` visual style — Blue / Olive Green / Silver, navy / moss / graphite in dark mode |
| **Assets** | `GE-proton-11.0-3-arm64ec.wcp` (4 KB + 16 KB pages) |

**Android compatibility fixes** — SD-card boot (`noexec` / `force_anon`) · drive-root copy · `C.UTF-8` locale
**Runtime** — realized-font-handle cap `32768` · `WINEVMEMMAXSIZE` cap · fast-yield gate · FEX-unixlib loader · XRandR / XRender
**Build** — `-g0 -O2` release build, `llvm-strip` on both the PE DLLs/EXEs and the unix `.so` loaders · zstd-compressed `.wcp` · ccache in CI (build speed only, not in the layer)
**Inherited bionic base** — the Winlator-bionic / GameNative Android patch set every layer is built on: esync/fsync, winex11 driver (window/keyboard/mouse/OpenGL/bitblt), preloader, clipboard, winemenubuilder, MIDI, DNS resolver, wow64 syscall path
**GE game-fixes** — `battlenet` · `maplestory` · `dai_xinput` · `eac` · `pso2` · `assettocorsa` · `silence-starcitizen` · `vgsoh` · `WM_ACTIVATEAPP`

</details>

<details>
<summary><b>Proton 11.0-2</b> &nbsp;·&nbsp; arm64ec + <b>x86_64</b> · Wine 11 · versionCode <code>8</code></summary>

<br>

| | |
|---|---|
| **Base** | Valve **Proton 11.0-2** (Wine 11.0) — **this is the line the Wayland and HDR10 work was developed and device-proven on** |
| **Installs as** | `11.0-2-arm64ec-8` · `11.0-2-x86_64-8` |
| **Wayland** *(arm64ec only)* | `winewayland.drv` (unix `.so` + `aarch64-windows` and `i386-windows` PE) · 8 bundled Wayland Turnip drivers (`plain`, `a7xx`, `a8xx`, `a8xx-perf`, `a8xx-gen8`, `a8xx-smxz`, `a8xx-white`, `a8xx-upstream`) · `banner-desktop-v1` protocol |
| **HDR10** *(arm64ec only)* | screen peak / frame-average / black level reported to Windows through a built CTA-861.3 EDID, DXGI and DisplayConfig advanced colour, plus the builtin `amd_ags_x64` |
| **ntdll fix** | `RtlIsEcCode` bounds check (Denuvo unwind loop) |
| **EA fixes** | ws2_32 dual-stack DNS · nsiproxy default route · gdiplus span clamp |
| **DirectAudio** | v1.3.2 (vendored source) — opt-in via registry `Audio=directaudio`; mic capture opt-in via `BANNER_AUDIO_DIRECT_MIC=1` |
| **XInput fix** | update thread survives transient wait failures (controllers no longer die mid-game) |
| **Wine XP desktop** | Luna taskbar + start menu (with the Control Panel fix) · XP window frames · `winexp.msstyles` visual style — Blue / Olive Green / Silver, navy / moss / graphite in dark mode |
| **Assets** | `proton-11.0-2-arm64ec.wcp` · `proton-11.0-2-x86_64.wcp` (4 KB + 16 KB pages) |

**Android compatibility fixes** — SD-card boot (`noexec` / `force_anon`) · drive-root copy · `C.UTF-8` locale
**Runtime** — realized-font-handle cap `32768` · `WINEVMEMMAXSIZE` cap · fast-yield gate · FEX-unixlib loader · XRandR / XRender
**Build** — `-g0 -O2` release build, `llvm-strip` on both the PE DLLs/EXEs and the unix `.so` loaders · zstd-compressed `.wcp` · ccache in CI (build speed only, not in the layer)
**Inherited bionic base** — the Winlator-bionic / GameNative Android patch set every layer is built on: esync/fsync, winex11 driver (window/keyboard/mouse/OpenGL/bitblt), preloader, clipboard, winemenubuilder, MIDI, DNS resolver, wow64 syscall path
**GE game-fixes** — none (plain Proton)

> ⚠️ The **x86_64** wcp carries every v8 fix **except Wayland** (`winewayland` is arm64ec-only). Proton 11 under box64 also still does not render a window — the arm64ec layer is the one to use.

</details>

<details>
<summary><b>Proton 11.0-1</b> &nbsp;·&nbsp; arm64ec · Wine 11 · versionCode <code>8</code></summary>

<br>

| | |
|---|---|
| **Base** | Valve **[Proton 11.0-1](https://github.com/ValveSoftware/Proton/releases/tag/proton-11.0-1)** (Wine 11.0-1), stock |
| **Installs as** | `11.0-1-arm64ec-8` |
| **Wayland** | `winewayland.drv` (unix `.so` + `aarch64-windows` and `i386-windows` PE) · 8 bundled Wayland Turnip drivers (`plain`, `a7xx`, `a8xx`, `a8xx-perf`, `a8xx-gen8`, `a8xx-smxz`, `a8xx-white`, `a8xx-upstream`) · `banner-desktop-v1` protocol |
| **HDR10** | screen peak / frame-average / black level reported to Windows through a built CTA-861.3 EDID, DXGI and DisplayConfig advanced colour, plus the builtin `amd_ags_x64` |
| **ntdll fix** | `RtlIsEcCode` bounds check (Denuvo unwind loop) |
| **EA fixes** | ws2_32 dual-stack DNS · nsiproxy default route · gdiplus span clamp |
| **DirectAudio** | v1.3.2 (vendored source) — opt-in via registry `Audio=directaudio`; mic capture opt-in via `BANNER_AUDIO_DIRECT_MIC=1` |
| **XInput fix** | update thread survives transient wait failures (controllers no longer die mid-game) |
| **Wine XP desktop** | Luna taskbar + start menu (with the Control Panel fix) · XP window frames · `winexp.msstyles` visual style — Blue / Olive Green / Silver, navy / moss / graphite in dark mode |
| **Assets** | `proton-11.0-1-arm64ec.wcp` (4 KB + 16 KB pages) |

**Android compatibility fixes** — SD-card boot (`noexec` / `force_anon`) · drive-root copy · `C.UTF-8` locale
**Runtime** — realized-font-handle cap `32768` · `WINEVMEMMAXSIZE` cap · fast-yield gate · FEX-unixlib loader · XRandR / XRender
**Build** — `-g0 -O2` release build, `llvm-strip` on both the PE DLLs/EXEs and the unix `.so` loaders · zstd-compressed `.wcp` · ccache in CI (build speed only, not in the layer)
**Inherited bionic base** — the Winlator-bionic / GameNative Android patch set every layer is built on: esync/fsync, winex11 driver (window/keyboard/mouse/OpenGL/bitblt), preloader, clipboard, winemenubuilder, MIDI, DNS resolver, wow64 syscall path
**GE game-fixes** — none (plain Proton)

</details>

<details>
<summary><b>Proton-CachyOS 11.0-20260703</b> &nbsp;·&nbsp; arm64ec + <b>x86_64</b> · Wine 11 (CachyOS) · versionCode <code>8</code></summary>

<br>

| | |
|---|---|
| **Base** | **[wine-cachyos](https://github.com/CachyOS/wine-cachyos) `b5f2dc7b590`** (release `cachyos-11.0-20260703-slr`: Valve Proton experimental-11.0 + the CachyOS patch set) + our full v7 Android stack |
| **Installs as** | `11.0-20260703-arm64ec-8` · `11.0-20260703-x86_64-8` |
| **Wayland** *(arm64ec only)* | `winewayland.drv` (unix `.so` + `aarch64-windows` and `i386-windows` PE) · 8 bundled Wayland Turnip drivers (`plain`, `a7xx`, `a8xx`, `a8xx-perf`, `a8xx-gen8`, `a8xx-smxz`, `a8xx-white`, `a8xx-upstream`) · `banner-desktop-v1` protocol |
| **HDR10** *(arm64ec only)* | screen peak / frame-average / black level reported to Windows through a built CTA-861.3 EDID, DXGI and DisplayConfig advanced colour, plus the builtin `amd_ags_x64` |
| **ntdll fix** | `RtlIsEcCode` bounds check (Denuvo unwind loop) |
| **EA fixes** | ws2_32 dual-stack DNS · nsiproxy default route · gdiplus span clamp |
| **DirectAudio** | v1.3.2 (vendored source) — opt-in via registry `Audio=directaudio`; mic capture opt-in via `BANNER_AUDIO_DIRECT_MIC=1` |
| **XInput fix** | update thread survives transient wait failures (controllers no longer die mid-game) |
| **Wine XP desktop** | Luna taskbar + start menu (with the Control Panel fix) · XP window frames · `winexp.msstyles` visual style — Blue / Olive Green / Silver, navy / moss / graphite in dark mode |
| **Assets** | `proton-cachyos-11.0-20260703-arm64ec.wcp` · `proton-cachyos-11.0-20260703-x86_64.wcp` (4 KB + 16 KB pages) |

**Android compatibility fixes** — SD-card boot (`noexec` / `force_anon`) · drive-root copy · `C.UTF-8` locale
**Runtime** — realized-font-handle cap `32768` · `WINEVMEMMAXSIZE` cap · fast-yield gate · FEX-unixlib loader · XRandR / XRender
**Build** — `-g0 -O2` release build, `llvm-strip` on both the PE DLLs/EXEs and the unix `.so` loaders · zstd-compressed `.wcp` · ccache in CI (build speed only, not in the layer)
**Inherited bionic base** — the Winlator-bionic / GameNative Android patch set every layer is built on: esync/fsync, winex11 driver (window/keyboard/mouse/OpenGL/bitblt), preloader, clipboard, winemenubuilder, MIDI, DNS resolver, wow64 syscall path
**GE game-fixes** — none (CachyOS patch set, not a GE tier)

> ℹ️ **New to the consolidated release** — previously a standalone test build. Its Wayland driver is larger than the other layers' because CachyOS carries its own upstream Wayland additions (`alpha-modifier`, `color-management`, `content-type`) that were merged **alongside** ours rather than replacing them. Also ships CachyOS's expanded `ntoskrnl` anti-cheat API surface and an esync/NTSYNC-tuned scheduler.

</details>

---

*versionCode 8 · built 2026-09-17 · **pre-release, not tested on device**. Proton 10.0-4 and GE-Proton 10.0-34 are not in this release and stay at v7. Report problems against the layer name and slot shown on the container card.*
