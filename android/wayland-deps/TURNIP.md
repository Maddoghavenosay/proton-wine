# Wayland Turnip

`usr/lib/libvulkan_freedreno_wayland.so` is OUR build: `libvulkan_freedreno.so` from the
Banners-Turnip `wayland` branch (`build_wayland.sh`, workflow "Build Wayland variant"), Mesa
26.3.0-devel at `7cda7850`, NDK r29, API 29: Turnip with the KGSL backend and the Wayland WSI,
built Linux-style on bionic like Termux's. The containers' own Vulkan drivers (the wrapper,
adrenotools builds) have no Wayland WSI, so winewayland points `VK_ICD_FILENAMES` at
`share/vulkan/icd.d/banner_wayland_turnip.json` when it runs on the Bannerlator compositor.
`usr/lib/libdrm.so` (Termux 2.4.134) is what it was linked against and is bundled next to it.

Why ours and not Termux's `mesa-vulkan-icd-freedreno` 26.0.6-3, which this file used to be: with
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
