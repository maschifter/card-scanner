#!/usr/bin/env bash
# Offline benchmark over every game in mobile-example's ground-truth image
# set, merged into one JSON on the Desktop. 
#
# Usage: tools/benchmark-all.sh
#   Overrides via environment: WARMUP=2 ITERATIONS=10 OUT=<file>, plus
#   BIN=<harness> CONFIG=<config.json> IMAGES_ROOT=<dir of game dirs>
#   MANIFEST=<file> (a prepared ground truth instead of the generated one;
#   MANIFEST= disables it, leaving timings only) PYTHON=<interpreter>.
#   CARD_SCANNER_EP applies as usual: CoreML is the macOS default, and
#   CARD_SCANNER_EP=cpu puts every model back on CPU (see OnnxSession).
#
# Needs jq, python3 for the generated ground truth, and a build configured
# with -DCARDSCANNER_BENCHMARK=1. Runs on macOS, Linux, and Windows under
# git-bash, where it finds the .exe in its per-config build directory.
set -euo pipefail

APP_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CONFIG="${CONFIG:-$APP_DIR/config.json}"
IMAGES_ROOT="${IMAGES_ROOT:-$APP_DIR/../mobile-example/assets/games_images}"

# Windows puts an .exe suffix on the binary, and MSVC is a multi-config
# generator, so the build lands in a per-config subdirectory rather than
# directly in build/. Single-config generators (Make, Ninja) keep the plain
# path macOS and Linux use. BIN= still overrides all of it.
case "${OSTYPE:-}" in
  msys* | cygwin* | win32) EXE=".exe" ;;
  *) EXE="" ;;
esac
if [[ -z "${BIN:-}" ]]; then
  for candidate in \
      "$APP_DIR/build/desktop_card_scanner$EXE" \
      "$APP_DIR/build/Release/desktop_card_scanner$EXE" \
      "$APP_DIR/build/RelWithDebInfo/desktop_card_scanner$EXE" \
      "$APP_DIR/build/Debug/desktop_card_scanner$EXE"; do
    if [[ -x "$candidate" ]]; then
      BIN="$candidate"
      break
    fi
  done
fi
if [[ -z "${BIN:-}" || ! -x "$BIN" ]]; then
  echo "error: desktop_card_scanner$EXE not found under $APP_DIR/build;" \
      "configure with -DCARDSCANNER_BENCHMARK=1 and build, or pass BIN=" >&2
  exit 1
fi

# python3 is the name on macOS and Linux; the python.org installer for Windows
# ships python only.
PYTHON="${PYTHON:-}"
if [[ -z "$PYTHON" ]]; then
  for candidate in python3 python; do
    if command -v "$candidate" > /dev/null; then
      PYTHON="$candidate"
      break
    fi
  done
fi

WARMUP="${WARMUP:-2}"
ITERATIONS="${ITERATIONS:-10}"
# USERPROFILE is the Windows home. HOME is whatever the git-bash running this
# set, which need not be the account the real Desktop sits under.
DESKTOP_DIR="${HOME:-.}/Desktop"
if [[ -n "${USERPROFILE:-}" && -d "$USERPROFILE/Desktop" ]]; then
  DESKTOP_DIR="$USERPROFILE/Desktop"
fi
OUT="${OUT:-$DESKTOP_DIR/benchmark_desktop_all_$(date +%Y-%m-%dT%H-%M-%S).json}"

command -v jq > /dev/null || { echo "error: jq is required" >&2; exit 1; }

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
if [[ -z "${MANIFEST+set}" ]]; then
  [[ -n "$PYTHON" ]] ||
      { echo "error: python3 is required (or pass PYTHON=, MANIFEST=)" >&2; exit 1; }
  MANIFEST="$TMP/manifest.json"
  "$PYTHON" "$APP_DIR/tools/gen-benchmark-manifest.py" > "$MANIFEST"
fi
GROUND_TRUTH=()
if [[ -n "$MANIFEST" ]]; then
  GROUND_TRUTH=(--manifest "$MANIFEST")
fi

for dir in "$IMAGES_ROOT"/*/; do
  game="$(basename "$dir")"
  echo "== $game"
  # Model-loading chatter goes to stderr; parked in a log so it stays out of
  # the way but a failure still says what went wrong.
  "$BIN" "$CONFIG" --benchmark "$dir" "${GROUND_TRUTH[@]}" \
      --warmup "$WARMUP" --iterations "$ITERATIONS" \
      --out "$TMP/$game.json" > /dev/null 2> "$TMP/$game.log" ||
      { cat "$TMP/$game.log" >&2; exit 1; }
  # The manifest already names the game; this only fills images it missed.
  jq --arg g "$game" 'map(if .game == "" then .game = $g else . end)' \
      "$TMP/$game.json" > "$TMP/$game.tagged.json"
done

jq -s 'add' "$TMP"/*.tagged.json > "$OUT"

jq -r '[.[] | select(.isWarmup | not)] |
  "\(length) measured record(s), avg ms:" +
  "  yolo \([.[].yoloMs] | add / length * 10 | round / 10)" +
  "  preproc \([.[].preprocMs] | add / length * 100 | round / 100)" +
  "  embed \([.[].embedMs] | add / length * 10 | round / 10)" +
  "  dbSearch \([.[].dbSearchMs] | add / length * 10 | round / 10)" +
  "  total \([.[].totalMs] | add / length * 10 | round / 10)"' "$OUT"

jq -r '[.[] | select(.isWarmup | not)] | group_by(.outcome) |
  "outcome: " + (map("\(.[0].outcome) \(length)") | join(", ")) +
  (if any(.[]; .[0].outcome == "correct")
   then "  ->  \((map(select(.[0].outcome == "correct")) | flatten | length)
         * 1000 / ([.[] | length] | add) | round / 10)% hit rate"
   else "" end)' "$OUT"
echo "-> $OUT"
