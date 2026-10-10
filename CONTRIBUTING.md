# Contributing to Kodex

Thank you for helping. This page explains how to build, test and translate Kodex and
what a change should look like.

## Building and testing

See the [README](README.md#build-and-run) for the requirements. A development build:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

The regular tests need neither Codex nor a network. Please add or update a test in
`tests/test_core.cpp` for every behavior change; the fake app-server is
`tests/fake_codex.py`. The live and interactive tests use a real Codex account and are
described in the README.

For a change to the interface, run the application with `--screenshot` and look at the
result in a light and a dark Breeze theme.

## Code style

- C++23, formatted with the `.clang-format` of this repository. Run
  `cmake --build build --target clang-format` before you commit.
- The code builds with the KDE compiler settings of Extra CMake Modules. Use `Q_EMIT`,
  `Q_SIGNALS` and `Q_SLOTS` instead of the Qt keywords.
- Everything the user can read goes through `i18n()`; see below.
- QML only presents data. Put logic in the C++ backend, where it can be tested.
- Source code, comments, commit messages and documentation are in English.

## Licensing

Kodex follows the [REUSE](https://reuse.software/) specification. Every file has an SPDX
copyright and license header, or an entry in `REUSE.toml`. New source files use:

```
SPDX-FileCopyrightText: <year> <name> <email>
SPDX-License-Identifier: GPL-3.0-or-later
```

Check your change with `reuse lint`. By contributing you agree to license your work under
the license of the file you change.

## Translations

User-visible strings use KDE's `ki18n` (`i18n()`, `i18np()` for plurals, `i18nc()` when
a short string needs a context). The translation domain is `kodex`; the catalogs are
`po/<language>/kodex.po` and the template is `po/kodex.pot`.

To refresh the template and merge it into the catalogs after you changed strings:

```sh
scripts/extract-messages.sh
```

To add a language, for example German:

```sh
mkdir po/de
msginit --no-translator --locale=de --input=po/kodex.pot --output=po/de/kodex.po
```

Translate `po/de/kodex.po` with any PO editor such as Lokalize, then build. The build
compiles the catalog with `ki18n_install` and also translates the desktop entry and the
AppStream metadata from the same file. Keep placeholders such as `%1` and the plural
forms of the header intact, and check the file with `msgfmt --check -o /dev/null`.

## Reporting problems

Open an issue on GitHub. Please include the Kodex and Codex versions (`kodex --version`,
`codex --version`) and the steps to reproduce. Never paste tokens or the contents of
`auth.json`.

## Code of conduct

This project follows the [KDE Community Code of Conduct](CODE_OF_CONDUCT.md).
