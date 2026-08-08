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
fm-piano1|24 MB|FreePats FM Piano 1 — classic DX-style FM electric piano, SFZ+FLAC (CC0)
upright-piano|34 MB|FreePats Upright Piano KW — Kawai upright, velocity layers, SFZ+FLAC (CC0)
old-piano-fb|39 MB|FreePats Old Piano FB — worn 1920s upright, honky-tonk character, SFZ+FLAC (CC0)
freepats-synth-choir|7 MB|FreePats Synth Pad Choir — GM #92 choir pad, SFZ+FLAC (CC0)
legato-vocal|160 MB|SFZ legato vocal tutorial — solo voice, vowel sustains + transitions + syllables, SFZ+FLAC (CC0)
avl-drumkits|29 MB|AVL Drumkits 1.0 — Black Pearl + Red Zeppelin acoustic kits + percussion, GM-ish SFZ maps (CC-BY-SA)
sm-drums|2.2 GB|SM MegaReaper Drumkit — deep-sampled acoustic kit, up to 127 vel layers + 4 RR, SFZ (royalty-free)
big-rusty-drums|600 MB|Karoryfer Big Rusty Drums — characterful 60s acoustic kit, chokes + RR, SFZ (CC0)
muldjord-kit|350 MB|Muldjord Kit — Tama Superstar rock/metal kit, DrumGizmo port, per-mic + stereo SFZ (CC-BY 4.0)
drs-kit|740 MB|DRS Kit — Sonor kit, DrumGizmo port, brushes variant included, SFZ (CC-BY 4.0)
naked-drums|1.3 GB|Naked Drums — modern multi-mic kit, 10 round robins, GM + TD-12 maps, SFZ (CC-BY 4.0)
virtuosity-drums|1.2 GB|Virtuosity Drums — Versilian/Karoryfer deep-sampled kit, 400+ SFZ programs (CC0)
swirly-drums|840 MB|Karoryfer Swirly Drums — punk/indie brushes kit + unusual percussion, SFZ (CC0)
unruly-drums|660 MB|Karoryfer Unruly Drums — experimental kit where every drum is a snare, SFZ (CC0)
frankensnare|340 MB|Karoryfer Frankensnare — snare collection from tiny to huge, SFZ (CC0)
gogodze-phu|135 MB|Karoryfer Gogodze Phu Vol II — variable-fidelity kit, lo-fi to hi-fi, SFZ (CC0)
latin-percussion|6 MB|Latin Percussion — bongos/congas/cajon/claves/guiro/agogo, GM-mapped SFZ for bossa & samba (CC0)
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
  if [ ! -d "$dest" ]; then
    git clone --depth 1 https://github.com/sgossner/VSCO-2-CE.git "$dest"
  fi
  # The SFZ mappings live on the repo's SFZ branch; overlay them at the root
  # (their default_path entries are relative to the sample tree root).
  if ! ls "$dest"/*.sfz >/dev/null 2>&1; then
    echo "  overlaying SFZ mappings (SFZ branch) ..."
    git -C "$dest" fetch --depth 1 origin SFZ
    git -C "$dest" checkout FETCH_HEAD -- "*.sfz" 2>/dev/null ||       git -C "$dest" checkout FETCH_HEAD -- $(git -C "$dest" ls-tree -r --name-only FETCH_HEAD | grep '\.sfz$')
  fi
  echo "done: $dest ($(ls "$dest"/*.sfz 2>/dev/null | wc -l | tr -d ' ') instruments)"
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

get_avl_drumkits() {
  need unzip
  local dest="$DEST_ROOT/avl-drumkits"
  if find "$dest" -name "*.sfz" -print -quit 2>/dev/null | grep -q .; then
    echo "already present: $dest"
    return
  fi
  mkdir -p "$dest"
  local file="$dest/avl-drumkits-sfz.zip"
  # Canonical SFZ package from the author's site (Glen MacArthur, bandshed.net).
  [ -f "$file" ] || fetch_zip "http://www.bandshed.net/sounds/sfz/AVL_Drumkits_1.0.zip" "$file"
  echo "  extracting ..."
  unzip -oq "$file" -d "$dest"
  echo "done: $dest"
}

get_sm_drums() {
  need git
  local dest="$DEST_ROOT/sm-drums"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/SMDrums.git "$dest"
  echo "done: $dest"
}

get_muldjord_kit() {
  need git
  local dest="$DEST_ROOT/muldjord-kit"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/DrumGizmo.MuldjordKit.git "$dest"
  echo "done: $dest"
}

get_drs_kit() {
  need git
  local dest="$DEST_ROOT/drs-kit"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/DrumGizmo.DRSKit.git "$dest"
  echo "done: $dest"
}

get_naked_drums() {
  need git
  local dest="$DEST_ROOT/naked-drums"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/WilkinsonAudio.NakedDrums.git "$dest"
  echo "done: $dest"
}

get_virtuosity_drums() {
  need git
  local dest="$DEST_ROOT/virtuosity-drums"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/virtuosity_drums.git "$dest"
  echo "done: $dest"
}

get_swirly_drums() {
  need git
  local dest="$DEST_ROOT/swirly-drums"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/karoryfer.swirly-drums.git "$dest"
  echo "done: $dest"
}

get_unruly_drums() {
  need git
  local dest="$DEST_ROOT/unruly-drums"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/karoryfer.unruly-drums.git "$dest"
  echo "done: $dest"
}

get_frankensnare() {
  need git
  local dest="$DEST_ROOT/frankensnare"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/karoryfer.frankensnare.git "$dest"
  echo "done: $dest"
}

get_latin_percussion() {
  need curl
  local dest="$DEST_ROOT/latin-percussion"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  mkdir -p "$dest"
  local file="$dest/latin.zip"
  # Generated GM mapping over CC0 VCSL samples, hosted with the SappKit repo.
  fetch_zip "https://github.com/localteampartners/sappkit/releases/download/samples-v1/SappKit-Latin-Percussion.zip" "$file"
  echo "  extracting ..."
  unzip -oq "$file" -d "$dest"
  rm -f "$file"
  echo "done: $dest"
}

get_gogodze_phu() {
  need git
  local dest="$DEST_ROOT/gogodze-phu"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/karoryfer.gogodze-phu-vol-ii.git "$dest"
  echo "done: $dest"
}

get_big_rusty_drums() {
  need git
  local dest="$DEST_ROOT/big-rusty-drums"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/sfzinstruments/karoryfer.big-rusty-drums.git "$dest"
  echo "done: $dest"
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

get_fm_piano1() {
  need git
  local dest="$DEST_ROOT/fm-piano1"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/freepats/fm-piano1.git "$dest"
  echo "done: $dest"
}

get_upright_piano() {
  need git
  local dest="$DEST_ROOT/upright-piano"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/freepats/upright-piano-KW.git "$dest"
  echo "done: $dest"
}

get_old_piano_fb() {
  need git
  local dest="$DEST_ROOT/old-piano-fb"
  [ -d "$dest" ] && { echo "already present: $dest"; return; }
  git clone --depth 1 https://github.com/freepats/old-piano-FB.git "$dest"
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
      fm-piano1) get_fm_piano1 ;;
      upright-piano) get_upright_piano ;;
      old-piano-fb) get_old_piano_fb ;;
      freepats-synth-choir) get_freepats_synth_choir ;;
      legato-vocal) get_legato_vocal ;;
      avl-drumkits) get_avl_drumkits ;;
      sm-drums) get_sm_drums ;;
      big-rusty-drums) get_big_rusty_drums ;;
      muldjord-kit) get_muldjord_kit ;;
      drs-kit) get_drs_kit ;;
      naked-drums) get_naked_drums ;;
      virtuosity-drums) get_virtuosity_drums ;;
      swirly-drums) get_swirly_drums ;;
      unruly-drums) get_unruly_drums ;;
      frankensnare) get_frankensnare ;;
      gogodze-phu) get_gogodze_phu ;;
      latin-percussion) get_latin_percussion ;;
      all) get_sonatina; get_vpo; get_vsco2_ce; get_salamander;
           get_fm_piano1; get_upright_piano; get_old_piano_fb;
           get_freepats_synth_choir; get_legato_vocal;
           get_avl_drumkits; get_sm_drums; get_big_rusty_drums ;;
      *) echo "unknown library '$NAME' — run: $0 list"; exit 2 ;;
    esac
    ;;
  *)
    echo "usage: $0 list | get <name|all> [--dest DIR]"; exit 2 ;;
esac
