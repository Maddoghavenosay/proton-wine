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

## Layers in this release

| Layer | Base | Installs as | Asset |
|---|---|---|---|
| **Proton 11.0-1** | Valve [Proton 11.0-1](https://github.com/ValveSoftware/Proton/releases/tag/proton-11.0-1) (Wine 11.0) | `11.0-1-arm64ec-8` | `proton-11.0-1-arm64ec.wcp` |
| **Proton 11.0-2** | Valve Proton 11.0-2 (Wine 11.0) — the Wayland/HDR base line | `11.0-2-arm64ec-8` · `11.0-2-x86_64-8` | `proton-11.0-2-arm64ec.wcp` · `proton-11.0-2-x86_64.wcp` |
| **GE-Proton 11.0-3** | GE [GE-Proton11-3](https://github.com/GloriousEggroll/proton-ge-custom/releases/tag/GE-Proton11-3) tier on Valve 11.0-1 | `11.0-3-arm64ec-8` | `GE-proton-11.0-3-arm64ec.wcp` |
| **GE-Proton 11.0-5** | GE [GE-Proton11-5](https://github.com/GloriousEggroll/proton-ge-custom/releases/tag/GE-Proton11-5) tier on Valve 11.0-1 | `11.0-5-arm64ec-8` | `GE-proton-11.0-5-arm64ec.wcp` |
| **GE-Proton 11.0-6** | GE [GE-Proton11-6](https://github.com/GloriousEggroll/proton-ge-custom/releases/tag/GE-Proton11-6) tier on Valve 11.0-1 | `11.0-6-arm64ec-8` | `GE-proton-11.0-6-arm64ec.wcp` |
| **GE-Proton 11.0-7** | GE [GE-Proton11-7](https://github.com/GloriousEggroll/proton-ge-custom/releases/tag/GE-Proton11-7) tier | `11.0-7-arm64ec-8` · `11.0-7-x86_64-8` | `GE-proton-11.0-7-arm64ec.wcp` · `GE-proton-11.0-7-x86_64.wcp` |
| **GE-Proton 11.0-7.1** | GE-Proton11-7 tier on ValveSoftware/wine `46b29104` (bleeding-edge base) | `11.0-7.1-arm64ec-8` · `11.0-7.1-x86_64-8` | `GE-proton-11.0-7.1-arm64ec.wcp` · `GE-proton-11.0-7.1-x86_64.wcp` |
| **Proton-CachyOS 11.0-20260703** | `wine-cachyos` `b5f2dc7b590` (Valve experimental-11.0 + CachyOS patches) | `11.0-20260703-arm64ec-8` · `11.0-20260703-x86_64-8` | `proton-cachyos-11.0-20260703-arm64ec.wcp` · `proton-cachyos-11.0-20260703-x86_64.wcp` |

Every arm64ec layer carries the full Wayland + HDR10 stack. The four x86_64 (box64) layers carry everything except Wayland.

<details>
<summary><b>What the Wayland stack is, per layer</b> (tap to expand)</summary>

<br>

| | |
|---|---|
| **Wine driver** | `lib/wine/aarch64-unix/winewayland.so` · `lib/wine/aarch64-windows/winewayland.drv` · `lib/wine/i386-windows/winewayland.drv` |
| **Turnip drivers** | `libvulkan_freedreno_wayland{,_a7xx,_a8xx,_a8xx_perf,_a8xx_gen8,_a8xx_smxz,_a8xx_white,_a8xx_upstream}.so` with matching `banner_wayland_turnip*.json` ICDs |
| **Wayland libs** | `libwayland-client.so` · `libwayland-egl.so` |
| **HDR10** | built EDID (CTA-861.3) · DXGI · DisplayConfig advanced colour · builtin `amd_ags_x64` |
| **Protocol** | `banner-desktop-v1` (the compositor's own protocol for desktop placement and stacking) |

The app detects Wayland capability by **files**, never by layer name, so the gate lights up on every layer here the moment it is installed.

</details>

---

*versionCode 8 · built 2026-09-17 · pre-release, untested on device. Report problems against the layer name and slot shown on the container card.*
