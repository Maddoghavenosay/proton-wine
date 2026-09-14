# proton-wine PROGRESS_LOG (branch `feat/winewayland-desktop-11.0-2`)

Newest entry at the top.

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
