/*
 * SPDX-FileCopyrightText: 2026 incident201 <58225046+incident201@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami

// Draws only the handle. The native style also paints an edge line along the whole bar,
// which showed up as a gray stripe next to the content.
T.ScrollBar {
    id: control
    implicitWidth: horizontal ? 0 : Kirigami.Units.smallSpacing * 3
    implicitHeight: horizontal ? Kirigami.Units.smallSpacing * 3 : 0
    padding: Kirigami.Units.smallSpacing / 2
    hoverEnabled: true
    minimumSize: 0.06
    visible: policy !== T.ScrollBar.AlwaysOff && size < 1.0
    contentItem: Rectangle {
        implicitWidth: 6; implicitHeight: 6
        radius: Math.min(width, height) / 2
        color: Kirigami.Theme.textColor
        opacity: control.pressed ? 0.5 : control.hovered ? 0.38 : 0.25
        Behavior on opacity { NumberAnimation { duration: Kirigami.Units.shortDuration } }
    }
}
