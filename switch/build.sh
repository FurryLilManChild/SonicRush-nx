#!/usr/bin/env bash
# SonicRush-nx Switch build (cloud).
#
# Steps:
#   1. check the upstream checkouts are on their pinned commits, verify the ROM
#   2. apply switch/patches to ndsrecomp (idempotent)
#   3. generate the native ARM9 and ARM7 programs (tools/nrecomp) and shaders
#   4. generate FreeBIOS banks plus the retail-BIOS stubs
#   5. cross-compile the runner for Switch and package sonicrush-nx.nro
#   6. check the artifact before it goes anywhere
#
# Environment (defaults match the cloud workspace):
#   NDSRECOMP_ROOT  ndsrecomp checkout (pinned 179ac63, submodules initialised)
#   RECOMP_UI_ROOT  recomp-ui checkout (3f1d6f1)
#   SR_ROM          verified Sonic Rush USA ROM (ASCE)
#   NDS_RECOMPILE   host nds_recompile tool (FreeBIOS banks only)
#   BUILD_DIR       build output
#   DKRUN           command that runs its arguments inside devkitPro
#   JOBS            parallel compile jobs (2 fits 7 GB of RAM)
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
NDSRECOMP_ROOT="${NDSRECOMP_ROOT:-/home/claude/src/ndsrecomp}"
RECOMP_UI_ROOT="${RECOMP_UI_ROOT:-/home/claude/src/mph/third_party/recomp-ui}"
SR_ROM="${SR_ROM:-/home/claude/private/sonicrush-usa.nds}"
NDS_RECOMPILE="${NDS_RECOMPILE:-/home/claude/build/recompiler/nds_recompile}"
BUILD_DIR="${BUILD_DIR:-/home/claude/build/nx}"
DKRUN="${DKRUN:-/home/claude/dk.sh}"
JOBS="${JOBS:-2}"
VERSION="${SRNX_VERSION:-switch-test1}"
TITLE="${SRNX_TITLE:-SonicRush-nx}"
APP="${SRNX_APP:-sonicrush-nx}"

NDSRECOMP_PIN=179ac63d9e4055e1218d61b3fd03c34a94880a6e
ROM_SHA1=bc617530bb9b6e4179f2e54d7816c38676812dde

say() { echo "[sonicrush-nx build] $*"; }
die() { echo "[sonicrush-nx build] ERROR: $*" >&2; exit 1; }

STAMP="$(mktemp)"  # build start; the NRO must be newer than this
say "1/6 checking pins and ROM"
[ "$(git -C "$NDSRECOMP_ROOT" rev-parse HEAD)" = "$NDSRECOMP_PIN" ] || die "ndsrecomp is not on $NDSRECOMP_PIN"
[ -f "$SR_ROM" ] || die "ROM not found at $SR_ROM"
[ "$(sha1sum "$SR_ROM" | cut -c1-40)" = "$ROM_SHA1" ] || die "ROM SHA-1 does not match Sonic Rush USA (ASCE)"
[ -x "$NDS_RECOMPILE" ] || die "nds_recompile not found at $NDS_RECOMPILE"

say "2/6 applying patches"
for p in "$HERE"/patches/*.patch; do
  if git -C "$NDSRECOMP_ROOT" apply --reverse --check "$p" 2>/dev/null; then
    say "  already applied: $(basename "$p")"
  else
    git -C "$NDSRECOMP_ROOT" apply "$p" || die "patch failed: $(basename "$p")"
    say "  applied: $(basename "$p")"
  fi
done

say "3/6 native program and shaders"
NATIVE_DIR="$BUILD_DIR/nrgen"
# Runtime roots found by boot tests (derived from the game; never committed).
export NR_EXTRA_ROOTS="${NR_EXTRA_ROOTS:-$(dirname "$BUILD_DIR")/nrroots.txt}"
export NR7_EXTRA_ROOTS="${NR7_EXTRA_ROOTS:-$(dirname "$BUILD_DIR")/nrroots7.txt}"
export NR_HOOKS="${NR_HOOKS:-$HERE/../tools/nrecomp/hooks/srnx.hooks}"
python3 "$HERE/../tools/nrecomp/gen_all.py" "$SR_ROM" "$NATIVE_DIR" || die "native ARM9 generation failed"
python3 "$HERE/../tools/nrecomp/gen_all7.py" "$SR_ROM" "$NATIVE_DIR" || die "native ARM7 generation failed"
DEKO_DIR="$BUILD_DIR/deko-shaders"
DKRUN="$DKRUN" "$HERE/tools/build_deko_shaders.sh" "$HERE/src/deko/shaders" "$DEKO_DIR" >/dev/null || die "deko3d shader build failed"

say "4/6 BIOS banks"
GEN="$BUILD_DIR/gen"
mkdir -p "$GEN"
"$NDS_RECOMPILE" --config "$NDSRECOMP_ROOT/bios/freebios9.toml" \
  --bin "$NDSRECOMP_ROOT/third_party/freebios/drastic_bios_arm9.bin" --out "$GEN" --bank freebios_arm9 >/dev/null
"$NDS_RECOMPILE" --config "$NDSRECOMP_ROOT/bios/freebios7.toml" \
  --bin "$NDSRECOMP_ROOT/third_party/freebios/drastic_bios_arm7.bin" --out "$GEN" --bank freebios_arm7 >/dev/null
cp "$HERE"/stubs/*.c "$GEN"/

say "5/6 Switch configure and build"
COMPAT="$HERE/compat"
INC="$COMPAT;/opt/devkitpro/libnx/include;/opt/devkitpro/portlibs/switch/include"
# private hbmenu icon (never in the repo): SRNX_ICON, or the default private path
ICON="${SRNX_ICON:-}"   # 0.5.0: release icon from the repo unless one is given
[ -f "$ICON" ] || ICON=""
"$DKRUN" "cmake -S '$HERE' -B '$BUILD_DIR/switch' -G Ninja -DSRNX_ICON='$ICON' \
  -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DMPHNX_VERSION='$VERSION' \
  -DMPHNX_TITLE='$TITLE' \
  -DMPHNX_APP_NAME='$APP' \
  -DMPHNX_ONLINE=OFF \
  -DMPHNX_PIE_LIBS=ON \
  -DMPHNX_LEAN=ON \
  -DMPHNX_PERF_BANK_DIR= \
  -DMPHNX_NATIVE_DIR='$NATIVE_DIR' \
  -DMPHNX_DEKO_SHADERS='$DEKO_DIR/nx_deko_shaders.c' \
  -DNDSRECOMP_ROOT='$NDSRECOMP_ROOT' \
  -DNDS_GENERATED_DIR='$GEN' \
  -DNDS_TITLE_BANK_DIR= \
  -DNDS_TITLE_ROM_SHA1= \
  -DNDS_RECOMP_UI_ROOT='$RECOMP_UI_ROOT' \
  -DNDSRECOMP_COMPILE_JOBS=$JOBS -DNDSRECOMP_LINK_JOBS=1 \
  '-DCMAKE_C_STANDARD_INCLUDE_DIRECTORIES=$INC' \
  '-DCMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES=$INC'" > "$BUILD_DIR/configure.log" 2>&1 || { tail -40 "$BUILD_DIR/configure.log"; die "configure failed"; }
grep -q "deko3d GPU 3D renderer" "$BUILD_DIR/configure.log" || die "deko3d renderer not configured"
grep -q "native ARM9 program" "$BUILD_DIR/configure.log" || die "native program not configured"
"$DKRUN" "ninja -C '$BUILD_DIR/switch' -j$JOBS nds_runner_nro"

say "6/6 checking the artifact"
NRO="$BUILD_DIR/switch/$APP.nro"
ELF="$BUILD_DIR/switch/runner/nds_runner.elf"
[ -f "$NRO" ] || die "no NRO produced"
[ "$NRO" -nt "$STAMP" ] || die "NRO was not rebuilt by this run (stale file)"
SIZE=$(stat -c %s "$NRO")
say "  $APP.nro: $((SIZE / 1048576)) MB"
[ "$SIZE" -gt $((20 * 1048576)) ] || die "NRO is suspiciously small (game code missing?)"
"$DKRUN" "aarch64-none-elf-nm -C '$ELF' > '$BUILD_DIR/switch/nds_runner.syms'"
for sym in SDL_CreateRenderer SDL_OpenAudioDevice SDL_GameControllerOpen SDL_RenderPresent \
           nds_upstream_main g_dispatch_freebios_arm9 dkDeviceCreate nx_present_frame \
           __wrap_pthread_create __libnx_exception_handler \
           nr_resume_arm9 nr_install nr_tab_static nr_overlays N_02000800_a nr7_install g_nx_interp_any_pc; do
  grep -q "$sym" "$BUILD_DIR/switch/nds_runner.syms" || die "symbol missing from ELF: $sym"
done
if grep -q "g_validation_\|g_dispatch_mph_" "$BUILD_DIR/switch/nds_runner.syms"; then
  die "legacy title banks linked into the native build (cartridge bytes)"
fi
cat > "$BUILD_DIR/switch/game.toml" <<TOML
# SonicRush-nx native build: Sonic Rush USA
[game]
name = "Sonic Rush"
id = "ASCE"
region = "USA"
revision = 0
sha1 = "$ROM_SHA1"

[display]
screen_layout = "stacked"
supersampling = 1
internal_resolution = 1
antialiasing = 8
adaptive_widescreen = "none"
adaptive_guest_culling = false

[system]
startup_mode = "automatic"

[cartridge]
save_type = "eeprom"
save_size = 8192
TOML
say "  renderer, audio, input, native game code, FreeBIOS and Switch layer all present"
sha256sum "$NRO"
