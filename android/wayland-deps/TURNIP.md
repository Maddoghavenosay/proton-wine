# Wayland Turnip

Three drivers, all OUR build: `libvulkan_freedreno.so` from the Banners-Turnip `wayland` branch
(`build_wayland.sh`, workflow "Build Wayland variant"), Mesa 26.3.0-devel at `7cda7850`, NDK r29,
API 29: Turnip with the KGSL backend and the Wayland WSI, built Linux-style on bionic like Termux's.
The containers' own Vulkan drivers (the wrapper, adrenotools builds) have no Wayland WSI, so
winewayland points `VK_ICD_FILENAMES` at one of these when it runs on the Bannerlator compositor.
`usr/lib/libdrm.so` (Termux 2.4.134) is what they were linked against and is bundled next to them.

## Variants

One Mesa tree, one set of flags; the variants are the Android release matrix's per-GPU patches
(`turnip_build_combined_test.yml`), applied the way `build_turnip.sh` applies them, on top of the
shared Wayland patches. The build refuses a variant whose patch does not apply or that leaves
`freedreno_devices.py` unchanged, checks that the three share one SONAME and NEEDED set, and that
the GPU names really are in the binary (`FD710` only in a7xx, `Adreno (TM) 825` only in a8xx).

| variant | `usr/lib/` | ICD manifest (`usr/share/vulkan/icd.d/`) | patches on top of the shared tree | Adreno |
| --- | --- | --- | --- | --- |
| plain | `libvulkan_freedreno_wayland.so` | `banner_wayland_turnip.json` | none | 6xx, 730/740/750 (upstream also lists 722) |
| a7xx | `libvulkan_freedreno_wayland_a7xx.so` | `banner_wayland_turnip_a7xx.json` | `patches/a710-720.py`: FD710/FD720/FD722 entries with per-GPU magic regs (replaces upstream's 722) | 710/720/722 |
| a8xx | `libvulkan_freedreno_wayland_a8xx.so` | `banner_wayland_turnip_a8xx.json` | `patches/tu8_kgsl_26.patch` (u_gralloc UBWC hack + drm-shim ids; neither file is compiled in this build) + `fix_a8xx_dev_info.py` (`disable_gmem` dev-info property) + `apply_a8xx_gpus.py` (A825 entry, A810 KGSL chip id + `disable_gmem`, extra A829 ids) | 830/840 (8 Elite: also 810/825/829) |

Known limit of the a8xx recipe, same as on the Android matrix: `fix_a8xx_dev_info.py` looks for the
literal `disable_gmem` in `tu_cmd_buffer.cc`, upstream already has an unrelated
`cmd->state.rp.disable_gmem`, so the script believes its check is present and never inserts the
`dev_info.props.disable_gmem` read. A810's `disable_gmem = True` is therefore inert; the other
a8xx changes are real.

## How winewayland picks one (`dlls/winewayland.drv/waylanddrv_main.c`, `use_bundled_drivers`)

Evaluated once per process on the Bannerlator compositor, first match wins:

1. `BANNER_WAYLAND_VK_ICD=<absolute path>` — the ICD manifest of a driver the app manages (an
   imported one). Taken if it is readable; logged at ERR level as
   `winewayland: Vulkan driver <path> (app-selected)`. Not absolute / not readable: ERR, fall
   through.
2. `BANNER_WAYLAND_VK_VARIANT=a7xx` or `a8xx` — the bundled manifest above. If that file is
   missing from the wcp: ERR ("… is missing (path), using the plain one"), fall through. Any other
   value: ERR ("unknown BANNER_WAYLAND_VK_VARIANT=…"), fall through (`plain` is accepted silently).
3. The plain bundled manifest — today's behaviour.

The chosen manifest becomes `VK_ICD_FILENAMES` and is logged as
`winewayland: Vulkan driver <manifest>` (the app greps for that prefix); its `library_path`,
resolved against the manifest's directory, is `dlopen`ed `RTLD_NODELETE` so the loader can never
unload the driver mid-process (winevulkan pins itself alongside, see its loader.c). Zink/OpenGL is
independent of this choice: it always goes through the bundled EGL (`BANNER_WAYLAND_GL=0` turns
it off) and reaches the same driver through the imagefs Vulkan loader.

## Why our build

Why ours and not Termux's `mesa-vulkan-icd-freedreno` 26.0.6-3, which the plain file used to be: with
Termux's driver, any program that destroys a Vulkan device and creates another (the AIO Graphics
Test switching backends; DiRT Rally 2.0's probe device before its real one) progressively starves
and then hangs; and OpenGL through Zink died after a few seconds. Neither happens with this build:
all eight AIO backends switch fluidly in one launch and OpenGL holds at ~230 fps.

What the recipe needed (all in build_wayland.sh):
- `-Dfreedreno-kmds=msm,kgsl`. With `kgsl` alone Mesa's meson decides the system has no KMS/DRM,
  drops libdrm and never compiles `wsi_common_drm.c`; the Wayland WSI still asks for DRM images, so
  `vkCreateSwapchainKHR` walks into a compiled-out branch and the guest dies with an access
  violation. That was the 2026-09-12 "crashes in vkCreateSwapchainKHR" result.
- With libdrm present Mesa also builds the VK_KHR_display WSI, which stops threads with
  `pthread_cancel`; bionic has none. `patches/wayland/no_pthread_cancel.py` does what Termux's
  0006 does (a SIGUSR2 handler that `pthread_exit`s), written against the source text.
- Termux 0014: the KGSL timestamp wait no longer asserts on an unexpected errno.
- Built unstripped (`-Dstrip=false`, resolvable crash addresses) with `-Db_ndebug=true`.
Termux's other patches were checked: 0000/0002/0018 are applied inline; 0008/0015 only act when
`__TERMUX__` is defined; 0003 only affects the wl_shm path.

# OpenGL (EGL + Zink)

`usr/lib/libEGL.so.1`, `libGLESv2.so.2` and `libgallium-26.3.0-devel.so` are Mesa 26.3.0-devel at
7cda7850edd103ace21aac37d416d2fdf7a282e1 (the Banners-Turnip release commit), built by the
Banners-Turnip `wayland` branch (`build_wayland.sh`): EGL on the Wayland platform with Zink, no LLVM,
no GLX, as a Linux-style build on bionic like Termux's Mesa, with EGL patched to take its kopper (Zink)
path for a Wayland display without a DRM device. winewayland points Mesa at Zink
(`MESA_LOADER_DRIVER_OVERRIDE=zink`, `WINE_USE_EGL=1`; NOT `LIBGL_ALWAYS_SOFTWARE`, which makes
Zink demand a CPU Vulkan device) when these are present.
Zink opens `libvulkan.so.1`, the imagefs Vulkan loader, which follows `VK_ICD_FILENAMES` to the
Wayland Turnip above.

`libwayland-client.so` and `libwayland-egl.so` are Termux's libwayland 1.25.0-1, the version these
libraries were linked against.
