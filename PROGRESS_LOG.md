# proton-wine PROGRESS_LOG (branch `feat/winewayland-desktop-11.0-2`)

Newest entry at the top.

## 2026-09-14: HDR10 — the monitor's EDID for Windows (versionCode 10); VK_EXT_swapchain_colorspace was never hidden

**Ask:** make HDR10 (already proven on the user's Galaxy Fold with DXVK v3.1 on versionCode 9)
work with DXVK 2.x and vkd3d-proton, and tell Windows the screen's real brightness. Evidence
given: with DXVK 2.4.1-gplasync God of War shows no HDR option and `GoW_d3d11.log` prints
`VK_EXT_swapchain_colorspace — extension supported : 0`; `strings` finds the name twice in the v9
`winevulkan.dll` and never in `winevulkan.so`.

### 1. VK_EXT_swapchain_colorspace: not missing, and not what DXVK's HDR depends on (no code change)
- **Wine exposes it already, at the instance level where the spec puts it** (`vk.xml:21961`,
  `type="instance"`). The host→guest instance filter is in win32u (`vulkan.c:4144-4180`,
  `vulkan_init_once` → `client_extensions`) and the PE side (`winevulkan/loader.c:358-365, 473-516`),
  not in `winevulkan.so`, which has no extension-name table at all. v9 binaries from the device:
  `VK_EXT_swapchain_colorspace` ×1 in `win32u.so`, ×2 in `winevulkan.dll`, ×0 in `winevulkan.so` —
  exactly the counts of `VK_EXT_surface_maintenance1`, which DXVK's own "Enabled instance
  extensions" list shows reaching the game. Turnip reports both from the same
  `#ifdef TU_USE_WSI_PLATFORM` block (`tu_device.cc:185-187` at 7cda7850). make_vulkan skips neither.
- **DXVK looks for it in the DEVICE list** — since commit `4335eccae9` (2022-12-18): at `4c0cbbef`
  (= the user's v2.4.1-293-gplasync) `dxvk_extensions.h:315` (DxvkDeviceExtensions),
  `dxvk_adapter.cpp:892-893` (`m_deviceExtensions` = vkEnumerateDeviceExtensionProperties, :728),
  enabled in vkCreateDevice (:999, :1110-1111) and logged (:1286-1287). No Mesa driver can list an
  instance extension there, so that line says 0 on every Mesa system — including the Fold's
  **working** v3.1 HDR session (`extSwapchainColorSpace : 0` in `gow2/GoW_d3d11.log`).
- **Nothing in DXVK reads the flag.** Grep of `src/` in v2.1, 2.2, 2.3, 4c0cbbef, 2.5.3, 2.6.2,
  2.7.1, 3.1: only the adapter/device-info files that enable and log it. DXGI HDR is
  `dxgi.enableHDR` (DXVK_HDR=1) → `DXGI_OUTPUT_DESC1.ColorSpace` (`dxgi_output.cpp:234`,
  `dxgi_monitor.cpp:91-95`), plus `CheckColorSpaceSupport` → `Presenter::supportsColorSpace` →
  `vkGetPhysicalDeviceSurfaceFormatsKHR` (`dxvk_presenter.cpp:384-397, 429-467`), which Mesa's
  Wayland WSI fills from `wp_color_manager_v1` whatever the instance enabled
  (`wsi_wl_display_determine_colorspaces`). vkd3d-proton v3.0.1 never names the extension:
  `dxgi_vk_swap_chain_CheckColorSpaceSupport` (`swapchain.c:1266`) reads surface formats too, and
  D3D12 titles get their DXGI output from DXVK's `dxgi.dll`.
- So a winevulkan change could only flip DXVK's log line to 1 (by listing an instance extension
  among device extensions and stripping it again before the host's vkCreateDevice) — it cannot
  change what a game sees. Not done.
- **Why GoW showed no HDR option on 2.4.1 is NOT explained by DXVK's source:** GetDesc1, the
  monitor colour-space logic, ValidateColorSpaceSupport, D3D11 CheckColorSpaceSupport and
  `env::getEnvVar` are identical between 4c0cbbef and v3.1. Open; needs a controlled Fold A/B
  (same shortcut, DXVK 2.4.1 vs v3.1, `DXVK_HDR=1` confirmed in the session log's
  `session environment:` line, game restarted after the change).

### 2. The EDID (what versionCode 10 adds)
Before: `winewayland.drv` gives its monitor no EDID, win32u writes `BAD_EDID`
(`sysparams.c:2024-2030`), DXVK's `readMonitorEdidFromKey` fails ("Failed to get EDID reg key
size" in every Fold dxgi log) and `NormalizeDisplayMetadata` (`wsi_edid.h:38-56`) substitutes
1499 / 799 / 0.01 nits (HDR) or 270 / 270 / 0.5 (SDR).

Now (`dlls/winewayland.drv/wayland_edid.c` + `.h`, `display.c`): when any of
`BANNER_WAYLAND_HDR_MAX_NITS`, `_MAX_AVG_NITS`, `_MIN_NITS` (decimal nits, the app's per-session
contract) parses, `wayland_add_device_monitor` hands win32u a 256-byte EDID; none set = no EDID,
exactly as before; X11 untouched (win32u rebuilds the monitor keys on every device update, so an
X11 session never inherits it). One `winewayland: HDR10 monitor description (EDID) for Windows:
max … (EDID …), max frame-average …, min … nits` line per process; a malformed value is named
and ignored.
- Base block, EDID 1.4: vendor `WAY` (unassigned in hwdata's PnP list), product 1, 2026, digital
  10 bpc DisplayPort, gamma 2.2, Display P3 primaries + D65 (what DXVK assumes for HDR without an
  EDID), preferred DTD = the output's current mode (CVT-RB proportions; size clamped to 4095,
  pixel clock to 655.35 MHz), name "Wayland", two dummy descriptors, one extension.
- CTA-861 rev 3 block: Colorimetry (BT.2020 RGB) + HDR Static Metadata (traditional SDR + SMPTE
  ST 2084 EOTFs, type 1; desired content max, max frame-average, min luminance). CTA-861.3 coding:
  max = 50·2^(cv/32) (cv = round(32·log2(n/50)), 1..255, 0 = not given), min = max·(cv/255)²/100;
  trailing absent values left out, the min only with a max (libdisplay-info fails a min without
  one). 1351 nits → cv 152 = 1345.43 nits (the coding's ~2 % step); min 0 → "not given".
- win32u picks up id `WAY0001`, name "Wayland" and the preferred mode from it.

### Verified off-device against the real parsers (`scratchpad/hdr10/harness/roundtrip.cpp`)
The committed `wayland_edid.c`, linked with libdisplay-info at `275e6459` (the submodule both
DXVK 4c0cbbef and v3.1 build), DXVK's own `src/wsi/wsi_edid.cpp` (byte-identical in both) and
win32u's `get_monitor_info_from_edid` extracted verbatim from `sysparams.c`: 13 scenarios, 0
failures — no libdisplay-info conformance failure (this revision returns "" for none; its own
reference EDIDs print the same empty "FAIL" in `di-edid-decode`), `supportsST2084 = 1`, DXVK's
luminances equal the encoder's decode, all 255 max codes and 255×15 min codes round-trip, the
env parser takes "1351", " 0.05 ", "0", "1." and rejects "", "-1", "1e3", "12,5", "inf", "nan".
Fold contract (1351 / 1351 / 0) → `DXGI_OUTPUT_DESC1` with DXVK_HDR=1: MaxLuminance 1345.43,
MaxFullFrameLuminance 1345.43, MinLuminance 0.01 (DXVK's own stand-in for "not given"), primaries
(0.6797, 0.3203) (0.2646, 0.6904) (0.1504, 0.0596) white (0.3125, 0.3291). Both changed files
syntax-check clean with gcc and with clang `--target=aarch64-linux-android28`, `-Wall -Wextra`.

### Build
- Commits on `fix/wayland-hdr-edid` (off `459bf7a8a7c`): `612401793ce` winewayland EDID,
  `05569528f36` ci versionCode 9 → 10 + one description sentence (both profiles).
- CI run 34909438885 (workflow_dispatch, headSha `05569528f36` verified), artifact
  `proton-arm64ec-sdk28` → `proton-11.0-2-arm64ec.wcp`. **Green.** wcp sha256
  `31d165a58026de7d94f4e6a097c3b918bd72e1256793b70cbccd04c5f56f8a60` (117,477,213 B), profile
  `Proton 11.0-2.1-arm64ec` versionCode 10 with the HDR sentence. Against v9: `winewayland.so`
  differs (carries the three variable names and the `HDR10 monitor description (EDID)` line),
  `win32u.so` and `winevulkan.so` byte-identical, `winevulkan.dll` 4 bytes (PE timestamp and
  checksum, same size).
- No Pocket FIT test this round (lead, 2026-09-14): the user tests on the Fold from the lead's
  Gamehub-Components test release. Not merged into `feat/winewayland-desktop-11.0-2` yet
  (fast-forward once the Fold has spoken, as with versionCode 9).

### Carry into v8 (all seven parents)
- Cherry-pick `612401793ce` (`dlls/winewayland.drv/wayland_edid.c` + `wayland_edid.h` new,
  `display.c` hunk, one `Makefile.in` SOURCES line). `git merge-tree` against the parents as they
  are today: clean on proton_11.0, proton_11.0-2, proton_11.3-GE, proton_11.5-GE, proton_11.6-GE;
  on proton_10.0 and proton_10.34-GE only the `Makefile.in` line conflicts (its context line
  `wayland_data_device.c` does not exist there — add `wayland_edid.c` to SOURCES by hand),
  `display.c` merges. It goes on top of the Wayland support itself, like everything on this list.
- The CI commit `05569528f36` is NOT carried.
- Optional, not in this build: Proton's winex11 sets `gdi_monitor.hdr_enabled` from DXVK_HDR=1
  (DisplayConfig ADVANCED_COLOR_INFO → advancedColorSupported/Enabled); winewayland never does.
  Parity would be one line in `wayland_add_device_monitor` (suggest: only when the EDID is given
  AND DXVK_HDR=1). Not needed by DXVK or vkd3d-proton.

## 2026-09-14: native OpenGL black with sound on phones with no DRM node (versionCode 9)

**Bug (user's own Adreno 840, standard Bannerlator, `Proton-11.0-2.1-arm64ec-8`, Turnip
a8xx-white; also reported on Adreno 830):** Wizardry (native OpenGL) plays sound behind a
black window. Session log: `feedback ready: 8 format/modifier pairs, main device 0:0`, then
`0 GPU frames from games | ~293 window redraws` every 10 s. Vulkan/DXVK games are fine.

### Root cause (Mesa source at 7cda7850, the tree libEGL/libgallium come from)
1. The phone gives apps no `/dev/dri` node, so the compositor's dma-buf feedback names
   `main_device` 0:0 (`dmabuf_render_node()` finds nothing to `stat`).
2. `default_dmabuf_feedback_main_device()` → `loader_get_render_node(0:0)` → NULL →
   `fd_render_gpu` stays -1 (this build has no wl_drm fallback: `HAVE_BIND_WL_DISPLAY` off).
3. `dri2_initialize_wayland_drm()` builds the kopper screen anyway (fd -1 means "no DRM" in
   `kopper_init_screen` → `pipe_loader_vk_probe_dri` → `zink_create_screen`), then
   **`dri2_setup_device(disp, false)` (platform_wayland.c:2752) fails**:
   `loader_is_device_render_capable(-1)` is false and `dri_query_compatible_render_only_device_fd(-1)`
   gives -1 (egl_dri2.c:862-868).
4. `eglInitialize` retries with `Zink=FALSE, ForceSoftware=TRUE` → the wl_shm software path,
   which this build cannot draw (gallium = zink only, no LLVM) → black.
The Pocket FIT never hit it: its `/dev/dri/renderD128` (msm display node) opens and is
describable, so step 2 gets a real fd.

### Fix (Banners-Turnip `wayland`, `patches/wayland/egl_wayland_no_drm_node.py`)
In `dri2_initialize_wayland_drm()` only: when the display is kopper and there is no usable
render node (none from the feedback, or one `loader_is_device_render_capable()` /
`_eglFindDevice()` cannot place — exactly when the stock `dri2_setup_device` call would fail),
close the fd, build the kopper screen with fd -1 (zink on the one Vulkan device, as X11's
`LIBGL_KOPPER_DRI2` path does), and skip `dri2_setup_device` (no EGLDevice, as Android's
pure-swrast path does; the software EGLDevice would make win32u report the display as
unaccelerated). A node that works keeps the stock path. One warning line each on the no-node
path and on the software fallback. See `android/wayland-deps/TURNIP.md`.

### What changed in this repo
Only `android/wayland-deps/usr/lib/libEGL.so.1` (sha256 `a9b5f3ad…55519`, from Banners-Turnip
`wayland` `644f1a5c`, run 34877806759, artifact `banner-mesa-wayland`). Every other library -
the eight Turnips, libgallium (Zink), libGLESv2, libdrm, libwayland - stays the bytes of
versionCode 7's run 34804055227: same Mesa commit, same flags, and the new libEGL's imports and
the old libgallium's exports are identical symbol sets (checked with `nm -D`), so nothing on the
Vulkan side can differ from versionCode 8. Plus TURNIP.md and the versionCode/description.

### Build and device proof
- CI run 34879146480 (workflow_dispatch on `fix/wayland-gl-no-drm-node`, headSha `aeaff6e8ee3`),
  artifact `proton-arm64ec-sdk28` → `proton-11.0-2-arm64ec.wcp`, sha256
  `bce6e7cc0e8b4251e9cc1b58a11c3efe6a485857ec02b270a7bb9a61940e6961` (117,467,136 B), profile
  `Proton 11.0-2.1-arm64ec` versionCode 9. Full diff against the v8 wcp: `lib/libEGL.so.1`,
  `profile.json`, and build timestamps in PE/.a files (every PE diff ≤ 12 bytes, same size); every
  unix-side library, the Turnips and winevulkan included, is byte-identical.
- **User's Adreno 840 (standard app, pre-release 7): v9 renders Wizardry** ("it works on a840").
  The wcp is attached to pre-release 7 as `proton-11.0-2.1-arm64ec-wayland-v9.wcp` in place of v8.
- Pocket FIT (Adreno 750, node present), pre-release-7-equivalent app, throwaway container on -9:
  Wizardry `feedback ready: 8 format/modifier pairs, main device 226:128` → `is presenting GPU frames
  through Wayland: 1280x720, format XR24, qcom_compressed` → `300 GPU frames from games` per 10 s;
  Wine `+wgl`: `accelerated: 1`, `zink Vulkan 1.4(Turnip Adreno (TM) 750 (MESA_TURNIP))`, no
  `wayland-egl:` line (stock DRM path). Insane 2 (DXVK) `143.4 fps | ~16k GPU frames` per 10 s.
- Pocket FIT with the compositor forced to advertise `main device 0:0` (debug app switch
  `BANNER_WAYLAND_NO_RENDER_NODE=1`, the A840's condition): on -8, `0 GPU frames from games | 300
  window redraws`, Wine's display EGLDevice = Mesa's software one (`accelerated: 0`) = black; on -9,
  `MESA-EGL: warning: wayland-egl: the compositor names no DRM render node this process can open;
  running zink on the Vulkan device without one`, display device `0x0` (no EGLDevice), `accelerated:
  1`, `zink … Turnip Adreno (TM) 750`, `300 GPU frames from games` per 10 s, HUD "OpenGL 30.0 fps".

### Carry into v8 (all seven parents)
- `android/wayland-deps/usr/lib/libEGL.so.1` from Banners-Turnip `wayland` at or after
  `644f1a5c` (the patch is applied on every build there, so any later run carries it; the v8
  port vendors one run's full set as before).
- `android/wayland-deps/TURNIP.md`: the "Since versionCode 9 the render node is optional"
  paragraph and the libEGL provenance sentence (reword the latter if v8 vendors one full run).
- No Wine source change. The CI commit (versionCode 8 → 9, description sentence) is NOT carried.

## 2026-09-14: XP Start menu "Control Panel" did nothing (versionCode 8)

**Bug (user, 2026-09-14 08:31, container 7 on `Proton-11.0-2.1-arm64ec-7`):** in the
Wine XP desktop, Start menu → Control Panel opens nothing, and the entry has a plain
folder icon. People use Control Panel → Add/Remove Programs to uninstall software.

### Root cause (from source, and from the user's own session logs)
1. The XP Start menu entry was `xp_add_folder(CSIDL_CONTROLS)`, which runs
   `ShellExecuteEx(SEE_MASK_IDLIST)` on the Control Panel pidl. The classic Start menu
   does the same.
2. shell32 can't run a virtual folder in-process. `SHELL_translate_idlist` starts
   `explorer.exe ::{20D04FE0-…}\::{21EC2020-…}` instead.
3. In that child, `make_explorer_window` ran `GetFullPathNameW` on the name. ntdll
   treats any string whose second character is `:` as drive-relative, so the name
   became `::\{20d04fe0-…}\::{21ec2020-…}`. `ILCreateFromPathW` could not parse it,
   explorer logged `Failed to create PIDL`, and the process exited without opening
   a window. Upstream Wine master has the same code, so this is a stock Wine bug,
   not something the XP desktop introduced.
4. The icon: the prefix's `HKCR\CLSID\{21EC2020…}` has only a `LocalizedString`
   (from `shell32.rgs`) and no `DefaultIcon`. `folders.c` therefore falls back to
   `IDI_SHELL_FOLDER`.

Evidence from the user's 08:30:18 session:
- `Wayland-logs/wayland-2026-09-14_08-30-18.log`: the start menu closes at
  08:30:26.454. A second `explorer.exe (pid 19233)` connects at 08:30:26.532 and
  disconnects at 08:30:26.590. It never opens a window.
- `bannerlator/wayland/previous/2026-09-14_08-30-26/wine_debug.log`:
  `00f4:fixme:exec:SHELL_execute flags ignored: 0x00000004`,
  `01a8:fixme:shell:DllGetClassObject failed for CLSID={00000000-…}`,
  `01a8:err:explorer:make_explorer_window Failed to create PIDL for L"::\\{20d04fe0-3aea-1069-a2d8-08002b30309d}\\::{21ec2020-3aea-1069-a2dd-08002b30309d}".`

### Fix (commits on this branch)
- `2b01f10fcd7` explorer: open `::{CLSID}` shell namespace paths instead of mangling
  them (`programs/explorer/explorer.c`, `make_explorer_window`). Names that start
  with `::` are now passed through unchanged. File paths still go through
  `GetFullPathNameW`.
- `6b8452edae4` explorer: the XP Start menu Control Panel entry now starts
  `control.exe`, with shell32's Control Panel icon (`programs/explorer/startmenu.c`,
  new `xp_add_control_panel()`, icon id 36). The icon is the one control.exe's own
  window uses. The `CSIDL_CONTROLS` folder is kept as a fallback if control.exe is
  missing.
- `f71f4a2a583` ci: versionCode 7 → 8, plus one sentence added to the description.
  The layer installs as `Proton-11.0-2.1-arm64ec-8`, next to `-7`.

CI run 34846379023 (workflow_dispatch; headSha `f71f4a2a583` verified), green.
The wcp is `proton-11.0-2-arm64ec.wcp` from artifact `proton-arm64ec-sdk28`:
sha256 `de8dcad0227dbfb0f32fd766a6c34497bd69787b9a62459657a146895a4c5d09`,
117,465,780 bytes. profile.json reads Proton `11.0-2.1-arm64ec`, versionCode 8.
explorer.exe carries the UTF-16 string `control.exe` once in each of aarch64-windows
and i386-windows; v7 carries it zero times. The XP taskbar marker is unchanged.
No release, tag or catalog change.

### Device proof (AYANEO Pocket FIT, Bannerlator pubg `com.tencent.ig`, Wayland sessions)
The test layer was hand-installed into its own slot, `contents/Proton/11.0-2.1-arm64ec-8`,
with app ownership and label. `-7` was left untouched. Both tests ran on throwaway
containers, each created through the app UI (Wayland, Turnip r4, FEX 2609-stable).

**Before: `Proton-11.0-2.1-arm64ec-7`, container "ZZ CPL v7"**
- The Start menu shows Control Panel with a plain folder icon, same as the user's
  screenshot.
- Clicking it: at 09:04:58.667 an `explorer.exe (pid 11470)` connects. It disconnects
  at .720 and opens no window. Wine log:
  `01ac:err:explorer:make_explorer_window Failed to create PIDL for L"::\\{20d04fe0-…}\\::{21ec2020-…}"`.
- The same layer run from Start-menu `.bat` launchers (the Run box can't be typed into
  from the test harness):
  - `control` opens "Wine Control Panel" (Add/Remove Programs, Display Settings,
    Game Controllers, Internet Settings).
  - `control appwiz.cpl` opens "Add/Remove Programs".
  - `rundll32 shell32.dll,Control_RunDLL` opens "Wine Control Panel".
  - `explorer ::{20D04FE0-…}\::{21EC2020-…}` and `explorer ::{20D04FE0-…}`: explorer
    exits after about 56 ms with the same ERR. No window.
- The other XP Start menu entries all open: My Documents, My Pictures and My Music
  (Wine Explorer windows), My Computer (wfm), Task Manager, Wine Configuration, and
  the All Programs entries.

**After: `Proton-11.0-2.1-arm64ec-8`, container "ZZ CPL V8"**
- The Start menu shows Control Panel with the shell32 Control Panel icon (a grey panel
  with the Wine glass), in the same style as the Task Manager icon below it.
- Clicking it: at 09:38:50.001 `control.exe (pid 23917)` connects, and at 09:38:50.096
  the "Wine Control Panel" window opens (Add/Remove Programs, Display Settings, Game
  Controllers, Internet Settings). No ERR in the log.
- Double-clicking Add/Remove Programs in that window: `rundll32.exe (pid 24125)`
  opens the "Add/Remove Programs" window (Install…, list, Modify…/Remove).
- `explorer ::{20D04FE0-…}\::{21EC2020-…}` opens a "Control Panel" Explorer window
  listing the four applets with their comments. `explorer ::{20D04FE0-…}` opens
  "My Computer" (Control Panel, C:, D:, E:, F:, Z:). No PIDL ERR either time.
- Regression check: My Documents still opens its Explorer window.

The explorer fix covers every `ShellExecute` of a virtual folder: the XP My Computer
entry when wfm.exe is absent, the Control Panel fallback when control.exe is missing,
and any shortcut or program that opens a `::{CLSID}` folder. The classic Start menu
doesn't need it, because its Control Panel is a cascade (see below).

### Did it ever work before v7? Yes. v7 broke it.
Device check on the pre-XP layer `Proton-11.0-6-arm64ec-6` (X11 container "ZZ CPL v6",
stock Wine classic taskbar):
- Start → **"Control Panel ▶" is a submenu** listing Add/Remove Programs, Display
  Settings, Game Controllers and Internet Settings.
- Clicking Add/Remove Programs opens the "Add/Remove Programs" window. It runs through
  the Control Panel folder's own execute hook, so the only log line is
  `SHELL_execute flags ignored: 0x00000004` and no explorer child starts.

Stock `add_shell_item` turns a folder item into a cascade and never executes the
folder itself. v7's XP Start menu replaced that cascade with one flat item that
executes the Control Panel folder. That is the one path that reaches the stock
`GetFullPathNameW` bug in explorer.c, so the user's "since v7" is exact. The explorer
bug itself is old (the v6 source `349547afa45`, Proton 9 `feat/p9-combined` and
upstream master all have it), but the stock menu never hit it.

### Carry into v8 (all seven AIO layers)
All seven v7 parents have both halves of the bug: the XP entry via
`xp_add_folder(CSIDL_CONTROLS)` and the explorer `GetFullPathNameW` call. On the
five Wine-11 parents, `programs/explorer/{explorer,startmenu}.c` are byte-identical
to this branch before the fix. On the two Wine-10 parents, only unrelated
explorer.c hunks differ. A `git merge-tree` simulation cherry-picks both commits
cleanly onto all seven. When Wayland is folded into v8, carry:
`2b01f10fcd7` (explorer.c) and `6b8452edae4` (startmenu.c).
Do not carry the CI commit; each parent stamps its own versionCode.

Optional follow-up (not done): shell32 still has no `DefaultIcon` for the Control
Panel CLSID, so the classic menu and Explorer's My Computer view still draw it as a
folder. The fix would be a `folders.c` fallback to `IDI_SHELL_CONTROL_PANEL`.
