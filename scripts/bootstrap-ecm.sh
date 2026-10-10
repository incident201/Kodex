#!/bin/sh

# SPDX-FileCopyrightText: 2026 incident201 <58225046+incident201@users.noreply.github.com>
#
# SPDX-License-Identifier: GPL-3.0-or-later

# Optional, local-only ECM bootstrap for systems without extra-cmake-modules.
set -eu
kodex_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
kodex_deps="$kodex_root/.deps"
mkdir -p "$kodex_deps"
curl --fail --location --proto '=https' --proto-redir '=https' \
    https://download.kde.org/stable/frameworks/6.30/extra-cmake-modules-6.30.0.tar.xz \
    --output "$kodex_deps/ecm.tar.xz"
printf '%s  %s\n' 22c9f7ff930dae7329faebf2942d2424f4a298c43bb3f11e217be6b12f227f6e "$kodex_deps/ecm.tar.xz" | sha256sum --check
tar -xf "$kodex_deps/ecm.tar.xz" -C "$kodex_deps"
cmake -S "$kodex_deps/extra-cmake-modules-6.30.0" -B "$kodex_deps/ecm-build" \
    -DBUILD_TESTING=OFF -DBUILD_DOC=OFF -DCMAKE_INSTALL_PREFIX="$kodex_deps/usr"
cmake --install "$kodex_deps/ecm-build"
