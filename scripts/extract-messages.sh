#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 incident201 <58225046+incident201@users.noreply.github.com>
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Regenerates po/kodex.pot from the sources, the desktop entry and the AppStream
# metadata, then merges it into every po/<lang>/kodex.po. Run it from anywhere:
#
#   scripts/extract-messages.sh
#
# Requires GNU gettext (xgettext, msgmerge, msgcat).
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"
domain=kodex
pot="po/${domain}.pot"
app_id=io.github.incident201.kodex

# The KDE keywords of KI18n, for C++ and for the JavaScript of QML files.
keywords=(-kde -ci18n -ki18n:1 -ki18nc:1c,2 -ki18np:1,2 -ki18ncp:1c,2,3
          -kki18n:1 -kki18nc:1c,2 -kki18np:1,2 -kki18ncp:1c,2,3
          -kI18N_NOOP:1 -kI18N_NOOP2:1c,2)
common=(--from-code=UTF-8 --package-name=Kodex --package-version=0.1.0 --copyright-holder=incident201
        --msgid-bugs-address=https://github.com/incident201/Kodex/issues --sort-by-file)

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

xgettext "${common[@]}" "${keywords[@]}" -C -o "$work/cpp.pot" \
    $(find src -name '*.cpp' -o -name '*.h' | sort)
xgettext "${common[@]}" "${keywords[@]}" -L JavaScript -o "$work/qml.pot" \
    $(find qml -name '*.qml' | sort)
xgettext "${common[@]}" -L Desktop -o "$work/desktop.pot" "data/${app_id}.desktop"
xgettext "${common[@]}" -o "$work/metainfo.pot" "data/${app_id}.metainfo.xml"

msgcat --use-first --sort-by-file -o "$pot" \
    "$work/cpp.pot" "$work/qml.pot" "$work/desktop.pot" "$work/metainfo.pot"
# A template has no creation date noise and no language.
sed -i -e '/^"POT-Creation-Date:/d' "$pot"

for catalog in po/*/"${domain}.po"; do
    msgmerge --quiet --update --backup=none --no-fuzzy-matching "$catalog" "$pot"
done
echo "Updated $pot and $(ls po/*/"${domain}.po" | wc -l) catalog(s)."
