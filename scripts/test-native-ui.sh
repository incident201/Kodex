#!/bin/sh
# Real, opt-in Codex turns in isolated projects, using independent KDE palettes.
set -eu
kodex_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
kodex_test_dir=$(mktemp -d /tmp/kodex-native-ui-XXXXXX)
trap 'rm -rf "$kodex_test_dir"' EXIT HUP INT TERM
case "${1:-both}" in
    Light|Dark) kodex_themes="$1" ;;
    both) kodex_themes="Light Dark" ;;
    *) printf 'Usage: %s [Light|Dark|both]\n' "$0" >&2; exit 2 ;;
esac
for kodex_theme in $kodex_themes; do
    kodex_theme_dir="$kodex_test_dir/$kodex_theme"
    mkdir -p "$kodex_theme_dir/config" "$kodex_theme_dir/data" "$kodex_theme_dir/cache"
    cp "/usr/share/color-schemes/Breeze$kodex_theme.colors" "$kodex_theme_dir/config/kdeglobals"
    printf '[Runtime]\nlanguage=ru\n' > "$kodex_theme_dir/config/kodexrc"
    XDG_CONFIG_HOME="$kodex_theme_dir/config" XDG_DATA_HOME="$kodex_theme_dir/data" \
    XDG_CACHE_HOME="$kodex_theme_dir/cache" QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
    QT_QPA_PLATFORMTHEME=kde KODEX_TEST_THEME="$kodex_theme" KODEX_LIVE_INTERACTIVE=1 \
        "$kodex_root/build/kodex_interactive_tests" visualDiffThemes
done
