#!/bin/bash
# Apply the GloriousEggroll (GE-Proton) game-fixes tier on top of the Valve proton
# source. This runs AFTER the GameNative bionic/android patches have been applied
# in the build-step scripts — the game-fixes tier was verified to apply cleanly on
# the bionic-patched tree in that order.
#
#   Tier: game-fixes  -- GE per-game compat fixes. Low conflict.
#   (ge-video-rework is NOT included in this tier; it is a separate port.)
#
# GE applies its patches with `patch -Np1` (fuzz tolerated), so we match that.
#
# Fail-hard contract:
#   * a patch that does not apply is a FATAL error (CI surfaces the conflict);
#   * after EACH patch a source token unique to that fix must be present in the
#     file it targets (fuzz can land a hunk, but a rename/refactor can also make
#     `patch` "succeed" on the wrong function — the marker catches that);
#   * a patch file with no registered marker is FATAL too: adding a GE fix means
#     adding its marker below, so nothing can ride in unverified.
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(dirname "$SCRIPT_DIR")"
GE_DIR="$ROOT/android/ge-patches"

# One marker per patch: "<target file>|<literal token that only exists once applied>"
marker_for() {
  case "$1" in
    0001-win32u-Avoid-zero-WM_ACTIVATEAPP-lparam-on-first-for.patch)
      echo "dlls/win32u/input.c|get_activateapp_thread_id" ;;
    ai-limit-dx12-compute-shader-fallback.patch)
      echo "dlls/ntdll/loader.c|ai_limit_compute_thunk" ;;
    assettocorsa-hud.patch)
      echo "dlls/dwrite/font.c|244210" ;;
    black-desert-keep-fullscreen-on-focus-loss.patch)
      echo "dlls/win32u/input.c|WINE_BLACK_DESERT_KEEP_FULLSCREEN" ;;
    battlenet-launcher-in-process-gpu.patch)
      echo "dlls/kernelbase/process.c|Battle.net Launcher.exe" ;;
    dai_xinput.patch)
      echo "dlls/win32u/input.c|GameLoop" ;;
    eac_60101_timeout.patch)
      echo "dlls/ntdll/unix/server.c|EAC_LAUNCHERDIR" ;;
    maplestory-kernelbase-charprev-null.patch)
      echo "dlls/kernelbase/string.c|if (!start) return NULL;" ;;
    maplestory-spi-stickykeys-filterkeys.patch)
      echo "dlls/win32u/sysparams.c|WINE_SPI_WARN(SPI_SETSTICKYKEYS)" ;;
    max-payne-cpu-detection.patch)
      echo "dlls/ntdll/loader.c|patch_max_payne_cpu_detection" ;;
    nascar25-protector.patch)
      echo "dlls/ntdll/unix/signal_x86_64.c|n25_repaired = 1;" ;;
    pso2_hack.patch)
      echo "dlls/ntdll/unix/file.c|WINE_NO_OPEN_FILE_SEARCH" ;;
    silence-starcitizen-unsupported-os.patch)
      echo "dlls/user32/msgbox.c|Star Citizen" ;;
    vgsoh.patch)
      echo "dlls/kernelbase/file.c|218210" ;;
    *) echo "" ;;
  esac
}

apply_dir() {
  local dir="$1" tier="$2"
  [ -d "$dir" ] || { echo "GE: tier '$tier' dir missing ($dir) — skipping"; return 0; }
  local n=0 fail=0
  for p in "$dir"/*.patch; do
    [ -e "$p" ] || continue
    n=$((n+1))
    local name; name="$(basename "$p")"
    local marker; marker="$(marker_for "$name")"
    if [ -z "$marker" ]; then
      echo "GE[$tier]: FATAL: no verification marker registered for $name (add it to marker_for in $0)"
      fail=$((fail+1))
      continue
    fi
    local file="${marker%%|*}" token="${marker#*|}"
    echo "GE[$tier]: applying $name"
    if ! patch -Np1 --fuzz=3 --no-backup-if-mismatch < "$p"; then
      echo "GE[$tier]: FATAL: failed to apply $name"
      fail=$((fail+1))
      continue
    fi
    if grep -qF -- "$token" "$ROOT/$file"; then
      echo "GE[$tier]:   ok    marker '$token' present in $file"
    else
      echo "GE[$tier]: FATAL: $name applied but marker '$token' is NOT in $file"
      fail=$((fail+1))
    fi
  done
  echo "GE[$tier]: applied+verified $((n-fail))/$n patches"
  return $fail
}

echo "=== Applying GE-Proton patch tiers ==="
rc=0
apply_dir "$GE_DIR/game-fixes" "game-fixes" || rc=$?

if [ "$rc" -ne 0 ]; then
  echo "=== GE patch application had $rc failure(s) — refusing to build a partial GE layer ==="
  exit 1
fi
echo "=== GE patches applied and verified ==="
