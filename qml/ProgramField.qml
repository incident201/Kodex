/*
 * SPDX-FileCopyrightText: 2026 incident201 <58225046+incident201@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

// A program path: editable text, a file picker, and a line showing what will actually run.
ColumnLayout {
    id: field
    property alias text: input.text
    property alias placeholderText: input.placeholderText
    property string dialogTitle
    readonly property string resolved: Preferences.resolveProgram(input.text)
    spacing: Kirigami.Units.smallSpacing
    RowLayout {
        Layout.fillWidth: true
        Controls.TextField { id: input; Layout.fillWidth: true; Layout.minimumWidth: Kirigami.Units.gridUnit * 20 }
        Controls.Button {
            text: i18n("Browse…"); icon.name: "document-open"
            onClicked: { picker.currentFolder = Preferences.dialogFolder(input.text); picker.open() }
        }
    }
    Controls.Label {
        visible: field.enabled && input.text.trim().length > 0
        Layout.fillWidth: true; wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        color: field.resolved ? Kirigami.Theme.textColor : Kirigami.Theme.negativeTextColor
        opacity: field.resolved ? 0.7 : 1
        text: !field.resolved ? i18n("Not found or not executable")
            : field.resolved === input.text.trim() ? i18n("Executable file found")
            : i18n("Will run: %1", field.resolved)
    }
    FileDialog {
        id: picker; title: field.dialogTitle; fileMode: FileDialog.OpenFile
        onAccepted: input.text = Preferences.localPath(selectedFile)
    }
}
