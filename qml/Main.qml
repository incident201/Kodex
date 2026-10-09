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
        return text.length > limit ? i18n("… %1 earlier characters hidden …", text.length - limit) + "\n" + text.slice(-limit) : text
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
    Controls.Dialog {
        id: renameDialog; title: i18n("Rename"); anchors.centerIn: parent; modal: true
        property bool project: false
        standardButtons: Controls.Dialog.Ok | Controls.Dialog.Cancel
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
                    Controls.SplitView.preferredWidth: 240; Controls.SplitView.minimumWidth: 185
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
                    RowLayout {
                        Layout.margins: Kirigami.Units.smallSpacing
                        Controls.ToolButton { icon.name: "system-file-manager"; text: i18n("Open folder"); display: Controls.AbstractButton.IconOnly; enabled: !!App.state.cwd; onClicked: App.openFolder(); Controls.ToolTip.text: text; Controls.ToolTip.visible: hovered }
                        Controls.ToolButton { icon.name: "edit-rename"; text: i18n("Rename project"); display: Controls.AbstractButton.IconOnly; enabled: !!App.state.cwd; onClicked: { renameDialog.project = true; renameField.text = App.state.projectName; renameDialog.open() } }
                        Item { Layout.fillWidth: true }
                        Controls.Button { objectName: "newTaskButton"; text: i18n("New task"); icon.name: "list-add"; enabled: Connection.ready && !!App.state.cwd && !App.state.loading; onClicked: App.newThread() }
                    }
                    Kirigami.Separator { Layout.fillWidth: true }
                    RowLayout {
                        Layout.leftMargin: Kirigami.Units.largeSpacing; Layout.rightMargin: Kirigami.Units.smallSpacing
                        Kirigami.Heading { text: i18n("Tasks"); level: 3; Layout.fillWidth: true }
                        Controls.CheckBox { objectName: "archivedTasksToggle"; text: i18n("Archived tasks"); checked: App.state.archived; onToggled: App.loadThreads(checked) }
                    }
                    ListView {
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                        model: App.threads
                        delegate: Controls.ItemDelegate {
                            id: threadItem
                            required property var record
                            readonly property real time: record.updatedAt || record.recencyAt || 0
                            objectName: "thread-" + record.id
                            width: ListView.view.width; text: record.name || record.preview || i18n("Untitled task")
                            highlighted: record.id === App.state.threadId
                            onClicked: App.selectThread(record.id)
                            Controls.ToolTip.text: time ? text + "\n" + new Date(time * 1000).toLocaleString(Qt.locale(), Locale.ShortFormat) : text
                            Controls.ToolTip.visible: hovered
                            Controls.ToolTip.delay: Kirigami.Units.toolTipDelay
                            contentItem: RowLayout {
                                spacing: Kirigami.Units.smallSpacing
                                Kirigami.Icon { source: threadItem.record.status && threadItem.record.status.type === "active" ? "media-playback-start" : "dialog-messages"; selected: threadItem.highlighted; implicitWidth: Kirigami.Units.iconSizes.small; implicitHeight: implicitWidth }
                                Controls.Label { text: threadItem.text; Layout.fillWidth: true; elide: Text.ElideRight; color: threadItem.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor }
                                Controls.Label { text: root.shortTime(threadItem.time); font: Kirigami.Theme.smallFont; opacity: 0.7; color: threadItem.highlighted ? Kirigami.Theme.highlightedTextColor : Kirigami.Theme.textColor }
                            }
                        }
                        Controls.ScrollBar.vertical: Controls.ScrollBar {}
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
                    ListView {
                        id: chat
                        objectName: "chatView"
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: Kirigami.Units.largeSpacing
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
                            readonly property bool expandedByDefault: ["agentMessage", "userMessage", "plan", "planProgress", "notice"].includes(record.type)
                            readonly property bool user: record.type === "userMessage"
                            readonly property bool activity: !expandedByDefault
                            readonly property bool code: record.type === "commandExecution" || record.type === "fileChange"
                            property bool expanded: expandedByDefault
                            // reuseItems keeps a manually toggled state; restore the default for the new record.
                            ListView.onReused: expanded = Qt.binding(() => expandedByDefault)
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
                                        Controls.Label { text: ({userMessage: i18n("You"), agentMessage: "Codex", commandExecution: i18n("Command"), fileChange: i18n("File changes"), reasoning: i18n("Reasoning"), plan: i18n("Plan"), mcpToolCall: i18n("MCP tool"), contextCompaction: i18n("Context")})[message.record.type] || i18n("Activity"); font.bold: true; opacity: 0.7 }
                                        // Collapsed steps show their first line, e.g. the command that ran.
                                        Controls.Label { Layout.fillWidth: true; text: message.activity && !message.expanded ? message.record.summary || "" : ""; elide: Text.ElideRight; font.family: message.code ? "monospace" : root.font.family; opacity: 0.6 }
                                        Controls.Label { text: ({completed: i18n("Done"), inProgress: i18n("Running"), failed: i18n("Failed"), declined: i18n("Declined"), interrupted: i18n("Stopped")})[message.record.status] || ""; opacity: message.record.status === "failed" ? 1 : 0.6; color: message.record.status === "failed" ? Kirigami.Theme.negativeTextColor : Kirigami.Theme.textColor }
                                        Controls.ToolButton { Layout.maximumHeight: 24; opacity: messageHover.hovered || activeFocus ? 1 : 0; icon.name: "edit-copy"; onClicked: App.copy(message.record.text || ""); Controls.ToolTip.text: i18n("Copy"); Controls.ToolTip.visible: hovered }
                                    }
                                    Controls.TextArea {
                                        Layout.fillWidth: true; visible: message.expanded
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
                        Controls.ScrollBar.vertical: Controls.ScrollBar {}
                        Kirigami.PlaceholderMessage {
                            anchors.centerIn: parent; width: parent.width - 40; visible: App.messages.count === 0 && !App.state.loading
                            icon.name: "dialog-messages"; text: i18n("Your workspace, with Codex")
                            explanation: App.state.threadId ? i18n("Describe what you want to build or change.") : i18n("Add a project and create a task to get started.")
                        }
                        Controls.BusyIndicator { anchors.centerIn: parent; running: App.state.loading; visible: running }
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
                                objectName: "modelSelector"
                                label: App.state.model || i18n("Model"); value: App.state.model
                                entries: Array.from({length: App.models.count}, (_, i) => { const m = App.models.get(i); return {title: m.displayName, value: m.model} })
                                onSelected: value => App.setOption("model", value)
                            }
                            InlineSelector {
                                objectName: "effortSelector"
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
                            Controls.ToolButton { objectName: "planToggle"; text: i18n("Plan"); flat: true; checkable: true; checked: App.state.plan; onClicked: App.setOption("plan", checked); opacity: checked ? 1 : 0.65 }
                            Controls.ToolButton { objectName: "fastToggle"; text: "Fast"; flat: true; checkable: true; checked: App.state.fast; enabled: App.state.fastAvailable; onClicked: App.setOption("fast", checked); opacity: checked ? 1 : 0.65; Controls.ToolTip.text: i18n("Available when the model supports a fast service tier"); Controls.ToolTip.visible: hovered }
                        }
                        Controls.Frame {
                            Layout.fillWidth: true
                            ColumnLayout {
                                anchors.fill: parent
                                Controls.ScrollView {
                                    Layout.fillWidth: true; Layout.preferredHeight: Math.min(Math.max(composer.implicitHeight, 75), 180)
                                    Controls.TextArea {
                                        id: composer; placeholderText: App.state.busy ? i18n("Add a clarification…") : i18n("Message Codex…  / for commands")
                                        objectName: "messageEditor"
                                        wrapMode: TextEdit.Wrap; selectByMouse: true
                                        background: null
                                        Keys.onPressed: event => { if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && (event.modifiers & Qt.ControlModifier)) { if (sendButton.enabled) sendButton.clicked(); event.accepted = true } }
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
                                    Controls.Button { id: sendButton; objectName: "sendButton"; icon.name: "mail-send"; text: App.state.busy ? i18n("Steer") : i18n("Send"); display: composerFooter.compact ? Controls.AbstractButton.IconOnly : Controls.AbstractButton.TextBesideIcon; enabled: Connection.ready && !!App.state.threadId && !App.state.archived && composer.text.trim().length > 0 && !App.state.loading; onClicked: { if (App.send(composer.text)) chat.follow = true } Controls.ToolTip.text: text; Controls.ToolTip.visible: hovered }
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
            standardButtons: Controls.DialogButtonBox.Close
            Controls.Button { text: i18n("Save settings"); icon.name: "document-save"; Controls.DialogButtonBox.buttonRole: Controls.DialogButtonBox.ApplyRole }
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
