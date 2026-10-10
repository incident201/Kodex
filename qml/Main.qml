/*
 * SPDX-FileCopyrightText: 2026 incident201 <58225046+incident201@users.noreply.github.com>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import org.kde.kirigami as Kirigami

Kirigami.ApplicationWindow {
    id: root
    width: 1320; height: 880
    minimumWidth: 850; minimumHeight: 600
    title: App.state.title ? App.state.title + " — Kodex" : "Kodex"
    property bool showChanges: true
    // Narrow windows hide the Git panel; the toolbar toggle can still show it.
    readonly property bool wide: width >= Kirigami.Units.gridUnit * 60
    onWideChanged: showChanges = wide
    Component.onCompleted: showChanges = wide
    property bool settingsOpen: false
    property string errorText: ""
    property string draftKey: ""
    property int requestPage: 0
    function report(message) { errorText = message }
    function tail(text) {
        const limit = 60000
        return text.length > limit ? i18np("… %1 earlier character hidden …", "… %1 earlier characters hidden …", text.length - limit) + "\n" + text.slice(-limit) : text
    }
    function shortTime(seconds) {
        if (!seconds) return ""
        const date = new Date(seconds * 1000), now = new Date()
        if (date.toDateString() === now.toDateString()) return date.toLocaleTimeString(Qt.locale(), "HH:mm")
        return date.toLocaleDateString(Qt.locale(), date.getFullYear() === now.getFullYear() ? "d MMM" : "dd.MM.yyyy")
    }
    function settings() { settingsOpen = true }
    Connections { target: App; function onMessageAccepted(text, threadId) { if (composer.text === text && threadId === App.state.threadId) { composer.text = ""; Preferences.saveDraft(root.draftKey, "") } } function onError(message) { root.report(message) } function onNavigate(page) { if (page === "settings") root.settings(); else if (page === "model") modelSelect.popup.open(); else if (page === "permissions") permissions.popup.open() } }
    Connections { target: Preferences; function onError(message) { root.report(message) } }
    Connections { target: Repository; function onError(message) { root.report(message) } }
    Connections { target: Maintenance; function onError(message) { root.report(message) } }
    Connections { target: Connection; function onError(message) { root.report(message) } }
    Connections {
        target: App
        function onChanged() {
            const key = App.state.threadId || App.state.projectId
            if (key !== root.draftKey) {
                Preferences.saveDraft(root.draftKey, composer.text)
                root.draftKey = key
                composer.text = Preferences.draft(key)
            }
        }
    }
    onClosing: event => {
        Preferences.saveDraft(draftKey, composer.text)
        if (Repository.state.busy || Maintenance.state.busy) {
            event.accepted = false
            root.report(i18n("Wait for the current operation to finish before closing."))
        }
    }
    Shortcut { sequence: "Ctrl+N"; onActivated: App.newThread() }
    Shortcut { sequence: "Ctrl+,"; onActivated: root.settings() }
    Shortcut { sequence: "Ctrl+Shift+D"; onActivated: root.showChanges = !root.showChanges }
    FolderDialog { id: folderPicker; title: i18n("Add project"); onAccepted: App.addProject(selectedFolder) }
    FileDialog { id: attachDialog; title: i18n("Attach files"); fileMode: FileDialog.OpenFiles; onAccepted: App.attachFiles(selectedFiles) }
    function showImage(source, name) { imageViewer.source = source; imageViewer.caption = name || ""; imageViewer.open() }
    DropArea {
        id: dropTarget
        anchors.fill: parent; z: 100
        enabled: !!App.state.threadId && !App.state.archived
        onDropped: drop => { if (drop.hasUrls) { App.attachFiles(drop.urls); drop.acceptProposedAction() } }
        Rectangle {
            anchors.fill: parent; visible: dropTarget.containsDrag
            color: Qt.alpha(Kirigami.Theme.highlightColor, 0.15); border.width: 2; border.color: Kirigami.Theme.highlightColor
            Controls.Label { anchors.centerIn: parent; text: i18n("Drop files to attach"); font.bold: true; font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.4 }
        }
    }
    Controls.Popup {
        id: imageViewer
        objectName: "imageViewer"
        property string source: ""
        property string caption: ""
        parent: Controls.Overlay.overlay
        anchors.centerIn: parent
        width: parent ? parent.width * 0.9 : 0; height: parent ? parent.height * 0.9 : 0
        modal: true; focus: true; padding: Kirigami.Units.smallSpacing
        closePolicy: Controls.Popup.CloseOnEscape | Controls.Popup.CloseOnPressOutside
        contentItem: ColumnLayout {
            spacing: Kirigami.Units.smallSpacing
            Image {
                Layout.fillWidth: true; Layout.fillHeight: true
                // The large decode is released when the viewer closes.
                source: imageViewer.opened ? imageViewer.source : ""; asynchronous: true; fillMode: Image.PreserveAspectFit; sourceSize: Qt.size(4096, 4096)
                TapHandler { onTapped: imageViewer.close() }
            }
            RowLayout {
                Controls.Label { text: imageViewer.caption; Layout.fillWidth: true; elide: Text.ElideMiddle; opacity: 0.7 }
                Controls.Button { icon.name: "document-open"; text: i18n("Open in viewer"); visible: imageViewer.source.startsWith("file:"); onClicked: App.openLink(imageViewer.source) }
                Controls.Button { icon.name: "window-close"; text: i18n("Close"); onClicked: imageViewer.close() }
            }
        }
    }
    Controls.Dialog {
        id: renameDialog; title: i18n("Rename"); anchors.centerIn: parent; modal: true
        property bool project: false
        footer: DialogButtons {}
        Controls.TextField { id: renameField; width: 360; onAccepted: renameDialog.accept() }
        onAccepted: project ? App.renameProject(renameField.text) : App.renameThread(renameField.text)
    }
    pageStack.initialPage: Kirigami.Page {
        padding: 0
        title: "Kodex"
        globalToolBarStyle: Kirigami.ApplicationHeaderStyle.None
        ColumnLayout {
            anchors.fill: parent; spacing: 0
            Controls.ToolBar {
                Layout.fillWidth: true
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: Kirigami.Units.largeSpacing; anchors.rightMargin: Kirigami.Units.largeSpacing
                    Kirigami.Heading { text: "Kodex"; level: 2 }
                    Rectangle { width: Kirigami.Units.smallSpacing * 2; height: width; radius: width / 2; color: Connection.ready ? Kirigami.Theme.positiveTextColor : Kirigami.Theme.neutralTextColor }
                    Controls.Label { text: Connection.status; opacity: 0.65; elide: Text.ElideRight; Layout.fillWidth: true }
                    Controls.ToolButton { icon.name: "view-refresh"; text: i18n("Reconnect"); display: Controls.AbstractButton.IconOnly; onClicked: Connection.connectServer(); Controls.ToolTip.text: text; Controls.ToolTip.visible: hovered }
                    Controls.ToolButton { icon.name: "view-split-left-right"; text: i18n("Changes"); checkable: true; checked: root.showChanges; onClicked: root.showChanges = checked }
                    Controls.ToolButton { icon.name: "settings-configure"; text: i18n("Settings"); onClicked: root.settings() }
                }
            }
            Kirigami.InlineMessage {
                Layout.fillWidth: true; visible: root.errorText.length > 0
                text: root.errorText; type: Kirigami.MessageType.Error; showCloseButton: true
                onVisibleChanged: if (!visible) root.errorText = ""
            }
            Controls.SplitView {
                Layout.fillWidth: true; Layout.fillHeight: true
                ColumnLayout {
                    Controls.SplitView.preferredWidth: Kirigami.Units.gridUnit * 17; Controls.SplitView.minimumWidth: Kirigami.Units.gridUnit * 12
                    spacing: Kirigami.Units.smallSpacing
                    RowLayout {
                        Layout.margins: Kirigami.Units.smallSpacing; Layout.leftMargin: Kirigami.Units.largeSpacing
                        Kirigami.Heading { text: i18n("Projects"); level: 3; Layout.fillWidth: true }
                        Controls.ToolButton { icon.name: "list-add"; text: i18n("Add project"); display: Controls.AbstractButton.IconOnly; onClicked: folderPicker.open(); enabled: Connection.ready }
                    }
                    ListView {
                        Layout.fillWidth: true; Layout.preferredHeight: Math.min(contentHeight, 230); clip: true
                        model: App.projects
                        delegate: Controls.ItemDelegate {
                            required property var record
                            width: ListView.view.width; text: record.name; font.bold: record.id === App.state.projectId
                            icon.name: "folder"; onClicked: App.selectProject(record.id)
                            Controls.ToolTip.text: record.path || ""; Controls.ToolTip.visible: hovered
                        }
                    }
                    Kirigami.Separator { Layout.fillWidth: true }
                    Controls.ItemDelegate {
                        objectName: "unassignedSessions"
                        Layout.fillWidth: true; text: i18n("Sessions without a project"); font.bold: App.state.unassigned
                        icon.name: "document-open-recent"; enabled: Connection.ready; onClicked: App.showUnassigned()
                        Controls.ToolTip.text: i18n("Codex sessions from any folder that belong to no project"); Controls.ToolTip.visible: hovered
                    }
                    RowLayout {
                        Layout.margins: Kirigami.Units.smallSpacing
                        Controls.ToolButton { icon.name: "system-file-manager"; text: i18n("Open folder"); display: Controls.AbstractButton.IconOnly; enabled: !!App.state.cwd; onClicked: App.openFolder(); Controls.ToolTip.text: text; Controls.ToolTip.visible: hovered }
                        Controls.ToolButton { icon.name: "edit-rename"; text: i18n("Rename project"); display: Controls.AbstractButton.IconOnly; enabled: !!App.state.projectId; onClicked: { renameDialog.project = true; renameField.text = App.state.projectName; renameDialog.open() } }
                        Item { Layout.fillWidth: true }
                        Controls.Button { objectName: "newTaskButton"; text: i18n("New task"); icon.name: "list-add"; enabled: Connection.ready && !!App.state.cwd && !App.state.loading; onClicked: App.newThread() }
                    }
                    Kirigami.Separator { Layout.fillWidth: true }
                    RowLayout {
                        Layout.leftMargin: Kirigami.Units.largeSpacing; Layout.rightMargin: Kirigami.Units.smallSpacing
                        Kirigami.Heading { text: i18n("Tasks"); level: 3; Layout.fillWidth: true }
                        Controls.CheckBox { objectName: "archivedTasksToggle"; text: i18n("Archived tasks"); checked: App.state.archived; onToggled: App.loadThreads(checked) }
                    }
                    // The scroll bar sits beside the rows instead of being drawn over their dates.
                    SlimScrollView {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
                        ListView {
                            clip: true
                            model: App.threads
                            delegate: Controls.ItemDelegate {
                                id: threadItem
                                required property var record
                                readonly property real time: record.updatedAt || record.recencyAt || 0
                                objectName: "thread-" + record.id
                                width: ListView.view.width; text: record.name || (record.preview || "").replace(/\s+/g, " ").trim() || i18n("Untitled task")
                                highlighted: record.id === App.state.threadId
                                onClicked: App.selectThread(record.id)
                                readonly property string folder: App.state.unassigned && record.cwd ? Preferences.shortPath(record.cwd) : ""
                                Controls.ToolTip.text: (folder ? text + "\n" + folder : text) + (time ? "\n" + new Date(time * 1000).toLocaleString(Qt.locale(), Locale.ShortFormat) : "")
                                Controls.ToolTip.visible: hovered
                                Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
                                contentItem: RowLayout {
                                    spacing: Kirigami.Units.smallSpacing
                                    Kirigami.Icon { source: threadItem.record.status && threadItem.record.status.type === "active" ? "media-playback-start" : "dialog-messages"; selected: threadItem.highlighted; implicitWidth: Kirigami.Units.iconSizes.small; implicitHeight: implicitWidth }
                                    ColumnLayout {
                                        Layout.fillWidth: true; spacing: 0
                                        Controls.Label { text: threadItem.text; Layout.fillWidth: true; elide: Text.ElideRight; color: threadItem.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor }
                                        Controls.Label { visible: !!threadItem.folder; text: threadItem.folder; Layout.fillWidth: true; elide: Text.ElideMiddle; font: Kirigami.Theme.smallFont; opacity: 0.7; color: threadItem.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor }
                                    }
                                    Controls.Label { text: root.shortTime(threadItem.time); font: Kirigami.Theme.smallFont; opacity: 0.7; color: threadItem.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor }
                                }
                            }
                        }
                    }
                    Controls.Button { visible: !!App.state.threadsCursor; text: i18n("Load more"); Layout.fillWidth: true; onClicked: App.loadThreads(App.state.archived, App.state.threadsCursor) }
                    Controls.Label { Layout.fillWidth: true; Layout.margins: Kirigami.Units.largeSpacing; text: App.state.account || i18n("Not signed in"); elide: Text.ElideRight; opacity: 0.65 }
                }
                ColumnLayout {
                    Controls.SplitView.fillWidth: true; Controls.SplitView.minimumWidth: 350
                    spacing: 0
                    RowLayout {
                        Layout.fillWidth: true; Layout.margins: Kirigami.Units.largeSpacing
                        Controls.Label { Layout.fillWidth: true; text: App.state.title || (App.state.threadId ? i18n("Untitled task") : i18n("Start a task")); font.bold: true; elide: Text.ElideRight }
                        Controls.Label { visible: App.state.archived; text: i18n("Archived"); opacity: 0.65 }
                        Controls.ToolButton { icon.name: "edit-rename"; enabled: !!App.state.threadId; onClicked: { renameDialog.project = false; renameField.text = App.state.title; renameDialog.open() } Controls.ToolTip.text: i18n("Rename"); Controls.ToolTip.visible: hovered }
                        Controls.ToolButton { objectName: "archiveTaskButton"; icon.name: App.state.archived ? "archive-remove" : "archive-insert"; enabled: !!App.state.threadId && !App.state.busy; onClicked: App.archiveThread(!App.state.archived); Controls.ToolTip.text: App.state.archived ? i18n("Restore") : i18n("Archive"); Controls.ToolTip.visible: hovered }
                    }
                    // The scroll bar sits beside the list; drawn over it, its edge line cuts through message bubbles.
                    Item {
                        Layout.fillWidth: true; Layout.fillHeight: true
                        ListView {
                            id: chat
                            objectName: "chatView"
                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom; right: parent.right; rightMargin: chatScroll.implicitWidth }
                            clip: true; spacing: Kirigami.Units.largeSpacing
                            model: App.messages; reuseItems: true
                            property bool follow: true
                            onMovementEnded: follow = atYEnd
                            onContentHeightChanged: if (follow) positionViewAtEnd()
                            onHeightChanged: if (follow) Qt.callLater(() => positionViewAtEnd())
                            header: Controls.Button { text: i18n("Load earlier messages"); visible: !!App.state.historyCursor; height: visible ? implicitHeight : 0; onClicked: { chat.follow = false; App.loadHistory() } }
                            delegate: Item {
                                id: message
                                required property var record
                                width: chat.width
                                height: bubble.height
                                readonly property bool plainMessage: ["agentMessage", "userMessage", "plan", "planProgress", "notice"].includes(record.type)
                                // Pictures Codex opened or made are shown right away; other steps stay collapsed.
                                readonly property bool expandedByDefault: plainMessage || record.type === "imageView" || record.type === "imageGeneration"
                                readonly property bool user: record.type === "userMessage"
                                readonly property bool activity: !plainMessage
                                readonly property var pictures: record.images || []
                                readonly property bool code: record.type === "commandExecution" || record.type === "fileChange"
                                property bool expanded: expandedByDefault
                                // reuseItems keeps a manually toggled state; restore the default for the new record.
                                ListView.onReused: { expanded = Qt.binding(() => expandedByDefault); pooled = false }
                                // A pooled delegate is off screen; dropping its pictures frees them until it is shown again.
                                property bool pooled: false
                                ListView.onPooled: pooled = true
                                HoverHandler { id: messageHover }
                                Rectangle {
                                    id: bubble
                                    // Long lines are hard to read on wide windows.
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: Math.min(parent.width - 2 * Kirigami.Units.largeSpacing, Kirigami.Units.gridUnit * 50)
                                    height: content.implicitHeight + 2 * inset
                                    readonly property real inset: message.user || message.activity ? Kirigami.Units.mediumSpacing : 0
                                    radius: Kirigami.Units.cornerRadius
                                    color: message.user ? Qt.alpha(Kirigami.Theme.highlightColor, 0.12) : "transparent"
                                    border.width: message.activity ? 1 : 0
                                    border.color: Qt.alpha(Kirigami.Theme.textColor, 0.15)
                                    ColumnLayout {
                                        id: content
                                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: bubble.inset }
                                        spacing: Kirigami.Units.smallSpacing
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 24
                                            TapHandler { enabled: message.activity; onTapped: message.expanded = !message.expanded }
                                            Controls.ToolButton { visible: message.activity; Layout.maximumHeight: 24; Layout.maximumWidth: 24; icon.name: message.expanded ? "arrow-down" : "arrow-right"; onClicked: message.expanded = !message.expanded; Accessible.name: message.expanded ? i18n("Collapse") : i18n("Expand") }
                                            Controls.Label { text: ({userMessage: i18n("You"), agentMessage: "Codex", commandExecution: i18n("Command"), fileChange: i18n("File changes"), reasoning: i18n("Reasoning"), plan: i18n("Plan"), mcpToolCall: i18n("MCP tool"), contextCompaction: i18n("Context"), imageView: i18n("Image"), imageGeneration: i18n("Generated image")})[message.record.type] || i18n("Activity"); font.bold: true; opacity: 0.7 }
                                            // Collapsed steps show their first line, e.g. the command that ran.
                                            Controls.Label { Layout.fillWidth: true; text: message.activity && !message.expanded ? message.record.summary || "" : ""; elide: Text.ElideRight; font.family: message.code ? "monospace" : root.font.family; opacity: 0.6 }
                                            Controls.Label { text: ({completed: i18n("Done"), inProgress: i18n("Running"), failed: i18n("Failed"), declined: i18n("Declined"), interrupted: i18n("Stopped")})[message.record.status] || ""; opacity: message.record.status === "failed" ? 1 : 0.6; color: message.record.status === "failed" ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor }
                                            Controls.ToolButton { Layout.maximumHeight: 24; opacity: messageHover.hovered || activeFocus ? 1 : 0; icon.name: "edit-copy"; onClicked: App.copy(message.record.text || ""); Controls.ToolTip.text: i18n("Copy"); Controls.ToolTip.visible: hovered }
                                        }
                                        Flow {
                                            objectName: "messageImages"
                                            Layout.fillWidth: true; spacing: Kirigami.Units.smallSpacing
                                            visible: message.expanded && message.pictures.length > 0
                                            Repeater {
                                                model: message.expanded && !message.pooled ? message.pictures : []
                                                delegate: Item {
                                                    id: picture
                                                    required property var modelData
                                                    readonly property real fit: thumb.implicitWidth > 0 ? Math.min(1, 360 / thumb.implicitWidth, 240 / thumb.implicitHeight) : 1
                                                    implicitWidth: thumb.status === Image.Error ? failed.implicitWidth : Math.max(48, thumb.implicitWidth * fit)
                                                    implicitHeight: thumb.status === Image.Error ? failed.implicitHeight : Math.max(48, thumb.implicitHeight * fit)
                                                    Image {
                                                        id: thumb
                                                        anchors.fill: parent; visible: status !== Image.Error
                                                        source: picture.modelData.src; asynchronous: true; fillMode: Image.PreserveAspectFit
                                                        // Decode to the size shown, not the size of the file.
                                                        sourceSize: Qt.size(Math.ceil(360 * Screen.devicePixelRatio), Math.ceil(240 * Screen.devicePixelRatio))
                                                        TapHandler { onTapped: root.showImage(picture.modelData.src, picture.modelData.name) }
                                                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                                                    }
                                                    RowLayout {
                                                        id: failed; visible: thumb.status === Image.Error; spacing: Kirigami.Units.smallSpacing
                                                        Kirigami.Icon { source: "image-missing"; implicitWidth: 16; implicitHeight: 16 }
                                                        Controls.Label { text: picture.modelData.name ? i18n("Image unavailable: %1", picture.modelData.name) : i18n("Image unavailable"); opacity: 0.7 }
                                                    }
                                                }
                                            }
                                        }
                                        Controls.Label {
                                            visible: message.expanded && message.record.type === "imageView"
                                            Layout.fillWidth: true; text: message.record.text || ""; elide: Text.ElideMiddle
                                            font: Kirigami.Theme.smallFont; opacity: 0.65
                                        }
                                        Controls.TextArea {
                                            Layout.fillWidth: true; visible: message.expanded && message.record.type !== "imageView" && (message.record.text || "").length > 0
                                            leftPadding: 0; rightPadding: 0; topPadding: 0; bottomPadding: 0
                                            readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap
                                            font.family: message.code ? "monospace" : root.font.family
                                            textFormat: message.code ? TextEdit.PlainText : TextEdit.RichText
                                            // Hidden cards skip layout; very long output shows its tail, Copy keeps everything.
                                            text: !message.expanded ? "" : textFormat === TextEdit.PlainText ? root.tail(message.record.text || "") : RichText.markdown(message.record.text || "", Kirigami.Theme.backgroundColor.hslLightness < 0.5)
                                            background: null
                                            onLinkActivated: link => App.openLink(link)
                                        }
                                    }
                                }
                            }
                            Controls.ScrollBar.vertical: SlimScrollBar { id: chatScroll; parent: chat.parent; anchors { top: chat.top; bottom: chat.bottom; left: chat.right } }
                            Kirigami.PlaceholderMessage {
                                anchors.centerIn: parent; width: parent.width - 40; visible: App.messages.count === 0 && !App.state.loading
                                icon.name: "dialog-messages"; text: i18n("Your workspace, with Codex")
                                explanation: App.state.threadId ? i18n("Describe what you want to build or change.") : App.state.unassigned && App.threads.count > 0 ? i18n("Choose a session on the left to continue it.") : i18n("Add a project and create a task to get started.")
                            }
                            Controls.BusyIndicator { anchors.centerIn: parent; running: App.state.loading; visible: running }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; visible: App.requests.count > 0
                        RowLayout {
                            visible: App.requests.count > 1
                            Controls.ToolButton { text: "‹"; enabled: root.requestPage > 0; onClicked: --root.requestPage }
                            Controls.Label { text: i18n("Request %1 of %2", Math.min(root.requestPage + 1, App.requests.count), App.requests.count) }
                            Controls.ToolButton { text: "›"; enabled: root.requestPage < App.requests.count - 1; onClicked: ++root.requestPage }
                        }
                        Repeater { model: App.requests; delegate: RequestCard { required property var record; required property int index; request: record; visible: index === Math.min(root.requestPage, App.requests.count - 1); maximumHeight: Math.max(180, Math.min(380, root.height * (root.width < 1000 ? 0.34 : 0.43))); Layout.fillWidth: true } }
                    }
                    Controls.Frame {
                        objectName: "planDecision"
                        visible: App.state.planReady && !App.state.busy && !App.state.archived
                        Layout.fillWidth: true; Layout.leftMargin: Kirigami.Units.largeSpacing; Layout.rightMargin: Kirigami.Units.largeSpacing
                        ColumnLayout {
                            anchors.fill: parent
                            Controls.Label { Layout.fillWidth: true; wrapMode: Text.Wrap; text: i18n("The plan is ready. Implement it?") }
                            Flow { Layout.fillWidth: true; spacing: Kirigami.Units.smallSpacing
                                Controls.Button { objectName: "keepPlanningButton"; text: i18n("Keep planning"); onClicked: App.keepPlanning() }
                                Controls.Button { objectName: "implementPlanButton"; icon.name: "media-playback-start"; text: i18n("Implement plan"); enabled: Connection.ready; onClicked: App.implementPlan() }
                            }
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true; Layout.margins: Kirigami.Units.largeSpacing
                        Flow {
                            Layout.fillWidth: true
                            enabled: !App.state.archived
                            spacing: Kirigami.Units.smallSpacing
                            InlineSelector {
                                id: modelSelect
                                objectName: "modelSelector"; symbol: "tools-wizard"
                                label: App.state.model || i18n("Model"); value: App.state.model
                                entries: Array.from({length: App.models.count}, (_, i) => { const m = App.models.get(i); return {title: m.displayName, value: m.model} })
                                onSelected: value => App.setOption("model", value)
                            }
                            InlineSelector {
                                objectName: "effortSelector"; symbol: "speedometer"
                                label: App.state.effort || i18n("Reasoning"); value: App.state.effort
                                entries: App.state.efforts.map(e => ({title: e, value: e}))
                                onSelected: value => App.setOption("effort", value)
                            }
                            InlineSelector {
                                id: permissions; objectName: "permissionsSelector"; symbol: "security-high"; value: App.state.permission
                                property var labels: [i18n("Session settings"), i18n("Read only"), i18n("Ask me"), i18n("Approve for me"), i18n("Full access")]
                                property var modes: ["inherit", "read", "workspace", "auto", "full"]
                                label: labels[Math.max(0, modes.indexOf(value))]
                                entries: modes.map((mode, i) => ({title: labels[i], value: mode}))
                                onSelected: value => App.setOption("permission", value)
                            }
                            Controls.ToolButton { objectName: "planToggle"; text: i18n("Plan"); icon.name: "view-pim-tasks"; icon.width: 14; icon.height: 14; flat: true; checkable: true; checked: App.state.plan; onClicked: App.setOption("plan", checked); opacity: checked ? 1 : 0.65 }
                            Controls.ToolButton { objectName: "fastToggle"; text: "Fast"; icon.name: "flash"; icon.width: 14; icon.height: 14; flat: true; checkable: true; checked: App.state.fast; enabled: App.state.fastAvailable; onClicked: App.setOption("fast", checked); opacity: checked ? 1 : 0.65; Controls.ToolTip.text: i18n("Available when the model supports a fast service tier"); Controls.ToolTip.visible: hovered }
                        }
                        Controls.Frame {
                            Layout.fillWidth: true
                            ColumnLayout {
                                anchors.fill: parent
                                Flow {
                                    objectName: "attachmentList"
                                    Layout.fillWidth: true; spacing: Kirigami.Units.smallSpacing; visible: App.attachments.count > 0
                                    Repeater {
                                        model: App.attachments
                                        delegate: Rectangle {
                                            id: chip
                                            required property var record
                                            objectName: "attachment-" + record.name
                                            implicitWidth: chipRow.implicitWidth + Kirigami.Units.smallSpacing * 2
                                            implicitHeight: Math.max(chipRow.implicitHeight, 40) + Kirigami.Units.smallSpacing
                                            radius: Kirigami.Units.cornerRadius
                                            color: Qt.alpha(Kirigami.Theme.textColor, 0.07)
                                            border.width: 1; border.color: Qt.alpha(Kirigami.Theme.textColor, 0.15)
                                            RowLayout {
                                                id: chipRow
                                                anchors.centerIn: parent; spacing: Kirigami.Units.smallSpacing
                                                Image {
                                                    visible: chip.record.kind === "image"
                                                    source: visible ? chip.record.url : ""; asynchronous: true; fillMode: Image.PreserveAspectCrop
                                                    sourceSize: Qt.size(96, 96); Layout.preferredWidth: 36; Layout.preferredHeight: 36
                                                }
                                                Kirigami.Icon {
                                                    visible: chip.record.kind !== "image"
                                                    source: chip.record.kind === "folder" ? "inode-directory" : chip.record.icon; fallback: "text-x-generic"
                                                    Layout.preferredWidth: 28; Layout.preferredHeight: 28
                                                }
                                                ColumnLayout {
                                                    spacing: 0
                                                    Controls.Label { text: chip.record.name; elide: Text.ElideMiddle; Layout.maximumWidth: Kirigami.Units.gridUnit * 12 }
                                                    Controls.Label { visible: !!chip.record.sizeText; text: chip.record.sizeText; font: Kirigami.Theme.smallFont; opacity: 0.65 }
                                                }
                                                Controls.ToolButton {
                                                    objectName: "removeAttachment"; icon.name: "window-close"; display: Controls.AbstractButton.IconOnly
                                                    text: i18n("Remove attachment"); onClicked: App.removeAttachment(chip.record.id)
                                                    Controls.ToolTip.text: text; Controls.ToolTip.visible: hovered
                                                }
                                            }
                                        }
                                    }
                                }
                                SlimScrollView {
                                    Layout.fillWidth: true; Layout.preferredHeight: Math.min(Math.max(composer.implicitHeight, 75), 180)
                                    Controls.TextArea {
                                        id: composer; placeholderText: App.state.busy ? i18n("Add a clarification…") : i18n("Message Codex…  / for commands")
                                        objectName: "messageEditor"
                                        wrapMode: TextEdit.Wrap; selectByMouse: true
                                        background: null
                                        Keys.onPressed: event => {
                                            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) { if (sendButton.enabled) sendButton.clicked(); event.accepted = true }
                                            // Copied files and screenshots become attachments; any other clipboard is pasted as text.
                                            else if (event.matches(StandardKey.Paste) && App.pasteAttachments()) event.accepted = true
                                        }
                                    }
                                }
                                Flow {
                                    Layout.fillWidth: true; spacing: 4; visible: composer.text.startsWith("/") && !composer.text.includes(" ")
                                    Repeater { model: ["/plan", "/compact", "/model", "/permissions", "/fast", "/new", "/resume", "/review", "/status", "/mcp", "/help"]; delegate: Controls.Button { required property string modelData; visible: modelData.startsWith(composer.text); text: modelData; onClicked: { composer.text = modelData; composer.forceActiveFocus() } } }
                                }
                                RowLayout {
                                    id: composerFooter
                                    Layout.fillWidth: true
                                    property bool compact: width < 480
                                    Controls.ToolButton {
                                        objectName: "attachButton"; icon.name: "mail-attachment"; text: i18n("Attach files…"); display: Controls.AbstractButton.IconOnly
                                        enabled: Connection.ready && !!App.state.threadId && !App.state.archived
                                        onClicked: attachDialog.open()
                                        Controls.ToolTip.text: i18n("Attach files or paste an image (Ctrl+V)"); Controls.ToolTip.visible: hovered
                                    }
                                    Controls.Label {
                                        text: composerFooter.compact ? (App.state.contextPercent < 0 ? "—" : Math.round(App.state.contextPercent) + "%") : (App.state.contextPercent < 0 ? i18n("Context: unavailable") : i18n("Context: %1%", Math.round(App.state.contextPercent)))
                                        opacity: 0.65
                                        Controls.ToolTip.visible: contextHover.hovered
                                        Controls.ToolTip.text: App.state.limits || i18n("Usage limits unavailable")
                                        HoverHandler { id: contextHover }
                                    }
                                    Controls.ProgressBar { from: 0; to: 100; value: Math.max(0, App.state.contextPercent); Layout.preferredWidth: 65; visible: App.state.contextPercent >= 0 && !composerFooter.compact }
                                    Item { Layout.fillWidth: true }
                                    Controls.BusyIndicator { running: App.state.busy; visible: running; implicitWidth: 24; implicitHeight: 24 }
                                    Controls.ToolButton { objectName: "stopButton"; icon.name: "media-playback-stop"; visible: App.state.busy; text: i18n("Stop"); display: composerFooter.compact ? Controls.AbstractButton.IconOnly : Controls.AbstractButton.TextBesideIcon; onClicked: App.interrupt(); Controls.ToolTip.text: text; Controls.ToolTip.visible: hovered }
                                    Controls.Button { id: sendButton; objectName: "sendButton"; icon.name: "mail-send"; text: App.state.busy ? i18n("Steer") : i18n("Send"); display: composerFooter.compact ? Controls.AbstractButton.IconOnly : Controls.AbstractButton.TextBesideIcon; enabled: Connection.ready && !!App.state.threadId && !App.state.archived && (composer.text.trim().length > 0 || App.attachments.count > 0) && !App.state.loading; onClicked: { if (App.send(composer.text)) chat.follow = true } Controls.ToolTip.text: text; Controls.ToolTip.visible: hovered }
                                }
                            }
                        }
                    }
                }
                GitPanel { visible: root.showChanges; Controls.SplitView.preferredWidth: 350; Controls.SplitView.minimumWidth: 230 }
            }
        }
    }
    Controls.Dialog {
        id: settingsDialog
        visible: root.settingsOpen; onClosed: root.settingsOpen = false
        onOpened: Maintenance.versions()
        anchors.centerIn: parent; width: Math.min(root.width - 60, 830); height: Math.min(root.height - 60, 800)
        modal: true; title: i18n("Settings")
        footer: Controls.DialogButtonBox {
            Controls.Button { text: i18n("Save settings"); icon.name: "document-save"; Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.ApplyRole }
            Controls.Button { text: i18n("Close"); icon.name: "window-close"; Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.RejectRole }
            onApplied: settingsPage.save()
        }
        ColumnLayout {
            anchors.fill: parent
            // The main window message is hidden behind the modal dialog.
            Kirigami.InlineMessage {
                Layout.fillWidth: true; visible: root.errorText.length > 0
                text: root.errorText; type: Kirigami.MessageType.Error; showCloseButton: true
                onVisibleChanged: if (!visible) root.errorText = ""
            }
            Kirigami.InlineMessage { Layout.fillWidth: true; visible: settingsPage.note.length > 0; text: settingsPage.note; type: Kirigami.MessageType.Positive; showCloseButton: true; onVisibleChanged: if (!visible) settingsPage.note = "" }
            SettingsPage { id: settingsPage; Layout.fillWidth: true; Layout.fillHeight: true }
        }
    }
}
