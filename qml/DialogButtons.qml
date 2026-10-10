/*
 * SPDX-FileCopyrightText: 2026 incident201 <58225046+incident201@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick.Controls as Controls

// OK and Cancel in the language chosen in Kodex. The standard buttons of Qt follow the
// system language instead, which differs when the language is overridden in the settings.
Controls.DialogButtonBox {
    Controls.Button {
        text: i18n("OK"); icon.name: "dialog-ok"
        Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.AcceptRole
    }
    Controls.Button {
        text: i18n("Cancel"); icon.name: "dialog-cancel"
        Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.RejectRole
    }
}
