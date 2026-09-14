# proton-wine PROGRESS_LOG (branch `feat/winewayland-desktop-11.0-2`)

Newest entry at the top.

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

CI run 34846379023 (workflow_dispatch; headSha `f71f4a2a583` verified). No release,
tag or catalog change.

### Carry into v8 (all seven AIO layers)
The seven v7 parents have the same bug (see the report). When Wayland is folded
into v8, carry the two source commits above:
`2b01f10fcd7` (explorer.c, ~10 lines) and `6b8452edae4` (startmenu.c, ~20 lines).
Do not carry the CI commit; each parent stamps its own versionCode.
