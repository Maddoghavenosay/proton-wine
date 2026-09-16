# Proton-CachyOS 11.0-20260703 — arm64ec + x86_64 bionic layers (test)

Build of the CachyOS Wine fork as Bannerlator bionic compatibility layers, like the GE-Proton 11.0-7 layers. Source: `CachyOS/wine-cachyos` pin `b5f2dc7b590` (release `cachyos-11.0-20260703-slr`), branch `wip/proton_11.0-cachyos` @ `1233149df3a`, CI run 35132184963.

**Two files in total:**

| File | Arch | Size | sha256 |
|---|---|---|---|
| `proton-cachyos-11.0-20260703-arm64ec.wcp` | arm64ec (native FEX/arm64ec) | 96,756,146 B | `4e469755bb8bc1dca1107030389328d4984d80d62177ccd3c8fa95c9d040c422` |
| `proton-cachyos-11.0-20260703-x86_64.wcp` | x86_64 (box64) | 63,880,925 B | `3de3f16e39fa5a13df92a8611ea85c990f022b36e1cf8e288119244c827eb73b` |

## What it carries

Ported the full v7 Android stack (XP taskbar/controls, EA network, font cap, controller fix, RtlIsEcCode, DirectAudio 1.3.2, build hardening) onto the CachyOS tree. CachyOS's own picks differ from GE:

- **Newer mmdevapi audio ABI** — upstream unixlib-direct + system-thread drivers (`PROTON_MMDEV_FAKE_EXCLUSIVE`, DualSense/container audio device ids). DirectAudio was ported to this ABI (`WINE_MMDEVAPI_SYSTEM_THREADS`).
- **ws2_32 AF_UNIX sockets** + staging tier (winecfg staging tab, HideWineExports).
- **Anticheat-kernel stub DLLs** (ntoskrnl line).
- Font smoothing, ole32 clipboard fix, mscms/atiadlxx stubs, wine_build "(CachyOS)" stamp.

**Deliberately not carried:** PipeWire driver, upstream Wayland, the AMD-only `_sauce` tier (FSR4/MLFG), gstreamer/ffmpeg media — all inert or desktop-only on Android. No GE game-fixes in this line.

## Fixes vs the first build

Round 1 surfaced a real CachyOS bug — `mmdevapi/devenum.c` `PropVariantInit(&pv)` on a `PROPVARIANT*` (stack clobber on the driver-failure path, hit per endpoint during device enumeration, from CachyOS `84289b7a71e`). Fixed in `1233149df3a` (`PropVariantInit(pv)`) and rebuilt; these files are the fixed build.

## Notes

- arm64ec: pick an installed FEXCore (e.g. `FEXCore-2609-stable-unix`) — new containers can default to an uninstalled nightly.
- x86_64: use box64 0.4.5 Hybrid (Bionic) from Contents → Nightlies for best controller + x86_64 compatibility.
- Test-only build; not on the in-app catalog. Not device-tested yet — first boot to desktop not confirmed.
- Low-latency DXVK/VKD3D companion components are on the separate `cachyos-low-latency` Nightlies release.