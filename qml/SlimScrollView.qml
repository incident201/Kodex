/*
 * SPDX-FileCopyrightText: 2026 incident201 <58225046+incident201@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick
import QtQuick.Controls

// A ScrollView whose bars draw only their handles; see SlimScrollBar.
ScrollView {
    id: view
    ScrollBar.vertical: SlimScrollBar {
        parent: view
        x: view.mirrored ? 0 : view.width - width
        y: view.topPadding
        height: view.availableHeight
        active: view.ScrollBar.horizontal.active
    }
    ScrollBar.horizontal: SlimScrollBar {
        parent: view
        x: view.leftPadding
        y: view.height - height
        width: view.availableWidth
        active: view.ScrollBar.vertical.active
    }
}
