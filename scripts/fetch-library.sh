#!/usr/bin/env bash
# fetch-library.sh — download curated free sample libraries for the Sapp
# instruments (SappOrchestra, and future SappSounds-based products).
#
#   ./scripts/fetch-library.sh list
#   ./scripts/fetch-library.sh get <name> [--dest DIR]
#   ./scripts/fetch-library.sh get all [--dest DIR]
#
# Default destination: ~/Samples/<name>. Every library here is free to
# download and use; licenses vary per library — check LICENSE/readme files
# after download before redistribution. Samples are never committed to git.

set -euo pipefail

DEST_ROOT="${HOME}/Samples"
CMD="${1:-list}"
NAME="${2:-}"
if [ "${3:-}" = "--dest" ] && [ -n "${4:-}" ]; then DEST_ROOT="$4"; fi

libraries() {
cat <<'EOF'
sonatina|2.6 GB|Sonatina Symphonic Orchestra — full orchestra, 747 SFZ instruments, FLAC (CC Sampling Plus 1.0)
vpo|1.3 GB|Virtual Playing Orchestra 3 — orchestra with velocity layers + round robins, WAV (free)
vsco2-ce|3.3 GB|VSCO 2 Community Edition — chamber orchestra + percussion, public domain (CC0)
salamander|707 MB|Salamander Grand Piano — Yamaha C5, 16 velocity layers, SFZ+FLAC (CC-BY 3.0)
freepats-synth-choir|7 MB|FreePats Synth Pad Choir — GM #92 choir pad, SFZ+FLAC (CC0)
legato-vocal|160 MB|SFZ legato vocal tutorial — solo voice, vowel sustains + transitions + syllables, SFZ+FLAC (CC0)
EOF
}

need() { command -v "$1" >/dev/null || { echo "error: $1 required"; exit 1; }; }

fetch_zip() {  # url, dest-file
  echo "  downloading $(basename "$2") ..."
  curl -L --progress-bar -o "$2" "$1"
}

get_sonatina() {
  need git
  local dest="$DEST_ROOT/sonatina"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/peastman/sso.git "$dest"
  echo "done: $dest  (instruments in 'Sonatina Symphonic Orchestra/')"
}

get_vpo() {
  need unzip
  local dest="$DEST_ROOT/vpo"
  mkdir -p "$dest"
  if [ ! -d "$dest/Virtual-Playing-Orchestra3" ]; then
    [ -f "$dest/vpo-waves.zip" ] || fetch_zip \
      "https://virtualplaying.com/go/virtual-playing-orchestra-v3-2-wave-files-archive/" \
      "$dest/vpo-waves.zip"
    [ -f "$dest/vpo-standard-scripts.zip" ] || fetch_zip \
      "https://virtualplaying.com/go/virtual-playing-orchestra-v3-3-standard-scripts/" \
      "$dest/vpo-standard-scripts.zip"
    [ -f "$dest/vpo-performance-scripts.zip" ] || fetch_zip \
      "https://virtualplaying.com/go/virtual-playing-orchestra-v3-3-performance-scripts/" \
      "$dest/vpo-performance-scripts.zip"
    echo "  extracting (waves first, then SFZ scripts on top) ..."
    unzip -oq "$dest/vpo-waves.zip" -d "$dest"
    unzip -oq "$dest/vpo-standard-scripts.zip" -d "$dest"
    unzip -oq "$dest/vpo-performance-scripts.zip" -d "$dest"
  fi
  echo "done: $dest"
}

get_vsco2_ce() {
  need git
  local dest="$DEST_ROOT/vsco2-ce"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sgossner/VSCO-2-CE.git "$dest"
  echo "done: $dest"
}

get_freepats_synth_choir() {
  need git
  local dest="$DEST_ROOT/freepats-synth-choir"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/freepats/synth-pad-choir.git "$dest"
  echo "done: $dest"
}

get_legato_vocal() {
  need git
  local dest="$DEST_ROOT/legato-vocal"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/legato_vocal_tutorial.git "$dest"
  echo "done: $dest  (playable programs in 'Programs/')"
}

get_salamander() {
  need curl
  local dest="$DEST_ROOT/salamander"
  [ -d "$dest/SalamanderGrandPiano" ] && { echo "already present: $dest"; return; }
  mkdir -p "$dest"
  # freepats hosts the canonical SFZ package; archive.org mirrors it.
  local url="https://freepats.zenvoid.org/Piano/SalamanderGrandPiano/SalamanderGrandPiano-SFZ+FLAC-V3+20200602.tar.gz"
  local file="$dest/salamander.tar.gz"
  [ -f "$file" ] || curl -fL --progress-bar -o "$file" "$url"
  echo "  extracting ..."
  tar -xzf "$file" -C "$dest"
  echo "done: $dest"
}

case "$CMD" in
  list)
    printf "%-12s %-8s %s\n" "NAME" "SIZE" "DESCRIPTION"
    libraries | while IFS='|' read -r n s d; do printf "%-12s %-8s %s\n" "$n" "$s" "$d"; done
    echo
    echo "usage: $0 get <name|all> [--dest DIR]   (default dest: ~/Samples)"
    ;;
  get)
    mkdir -p "$DEST_ROOT"
    case "$NAME" in
      sonatina) get_sonatina ;;
      vpo) get_vpo ;;
      vsco2-ce) get_vsco2_ce ;;
      salamander) get_salamander ;;
      freepats-synth-choir) get_freepats_synth_choir ;;
      legato-vocal) get_legato_vocal ;;
      all) get_sonatina; get_vpo; get_vsco2_ce; get_salamander;
           get_freepats_synth_choir; get_legato_vocal ;;
      *) echo "unknown library '$NAME' — run: $0 list"; exit 2 ;;
    esac
    ;;
  *)
    echo "usage: $0 list | get <name|all> [--dest DIR]"; exit 2 ;;
esac
