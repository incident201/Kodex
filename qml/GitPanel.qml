import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: panel
    spacing: Kirigami.Units.smallSpacing
    property string selectedPath: ""
    property bool stagedDiff: false
    property string diffText: tabs.currentIndex === 0 ? Repository.state.diff : App.state.turnDiff
    Connections { target: Repository; function onCommitted() { commitMessage.clear() } }
    RowLayout {
        Layout.fillWidth: true; Layout.margins: Kirigami.Units.largeSpacing
        Kirigami.Heading { text: i18n("Changes"); level: 3; Layout.fillWidth: true }
        Controls.ToolButton { icon.name: "view-refresh"; onClicked: Repository.refresh(); Controls.ToolTip.text: i18n("Refresh"); Controls.ToolTip.visible: hovered }
    }
    RowLayout {
        Layout.leftMargin: Kirigami.Units.largeSpacing; Layout.rightMargin: Kirigami.Units.largeSpacing
        Kirigami.Icon { visible: Repository.state.repository; source: "vcs-branch"; implicitWidth: Kirigami.Units.iconSizes.small; implicitHeight: implicitWidth; opacity: 0.65 }
        Controls.Label { Layout.fillWidth: true; elide: Text.ElideRight; text: Repository.state.repository ? Repository.state.branch + (Repository.state.upstreamName ? " → " + Repository.state.upstreamName : "") : i18n("Not a Git repository"); opacity: 0.65 }
    }
    Controls.TabBar {
        id: tabs; Layout.fillWidth: true
        Controls.TabButton { text: i18n("Working tree") }
        Controls.TabButton { text: i18n("Codex diff") }
    }
    ListView {
        visible: tabs.currentIndex === 0; Layout.fillWidth: true; Layout.preferredHeight: Math.min(contentHeight + 5, 210); clip: true
        model: Repository.files
        delegate: Controls.ItemDelegate {
            required property var record
            objectName: "file-" + record.path
            width: ListView.view.width
            highlighted: panel.selectedPath === record.path
            contentItem: RowLayout {
                Controls.CheckBox { checked: record.staged; enabled: !Repository.state.busy; onClicked: Repository.stage(record.path, record.staged); Controls.ToolTip.text: checked ? i18n("Unstage file") : i18n("Stage file"); Controls.ToolTip.visible: hovered }
                // Porcelain XY codes are cryptic; show one colored letter with a description.
                Controls.Label {
                    readonly property string code: record.untracked ? "?" : (["DD", "AU", "UD", "UA", "DU", "AA", "UU"].includes(record.status) ? "U" : record.status.trim()[0])
                    text: code === "?" ? "N" : code === "U" ? "!" : code
                    font.family: "monospace"; font.bold: true
                    horizontalAlignment: Text.AlignHCenter; Layout.preferredWidth: Kirigami.Units.gridUnit
                    color: code === "?" || code === "A" ? Kirigami.Theme.positiveTextColor : code === "D" || code === "U" ? Kirigami.Theme.negativeTextColor : code === "M" ? Kirigami.Theme.neutralTextColor : Kirigami.Theme.textColor
                    HoverHandler { id: statusHover }
                    Controls.ToolTip.visible: statusHover.hovered
                    Controls.ToolTip.text: ({"?": i18n("Untracked"), A: i18n("Added"), D: i18n("Deleted"), M: i18n("Modified"), R: i18n("Renamed"), C: i18n("Copied"), U: i18n("Conflict")})[code] || record.status
                }
                Controls.Label { text: record.path; Layout.fillWidth: true; elide: Text.ElideMiddle }
            }
            onClicked: { panel.selectedPath = record.path; Repository.diff(record.path, panel.stagedDiff) }
        }
        Controls.ScrollBar.vertical: Controls.ScrollBar {}
    }
    Controls.CheckBox { Layout.leftMargin: Kirigami.Units.largeSpacing; visible: tabs.currentIndex === 0 && Repository.state.repository; text: i18n("Show staged diff"); checked: panel.stagedDiff; onToggled: { panel.stagedDiff = checked; if (panel.selectedPath) Repository.diff(panel.selectedPath, checked) } }
    Controls.Label {
        visible: panel.diffText.length === 0
        Layout.fillWidth: true; Layout.margins: Kirigami.Units.largeSpacing; wrapMode: Text.Wrap; opacity: 0.65
        text: tabs.currentIndex === 1 ? i18n("Codex has not provided a separate diff. File changes are available in Working tree.") : Repository.state.repository ? i18n("Select a file to view its diff.") : i18n("This folder is not a Git repository. Codex patches appear in the Codex diff tab.")
    }
    Controls.ScrollView {
        Layout.fillWidth: true; Layout.fillHeight: true; clip: true
        Controls.ScrollBar.horizontal.policy: panel.diffText.length > 0 ? Controls.ScrollBar.AsNeeded : Controls.ScrollBar.AlwaysOff
        Controls.TextArea {
            text: panel.diffText.length > 0 ? RichText.code(panel.diffText,"diff",Kirigami.Theme.backgroundColor.hslLightness < 0.5) : ""
            readOnly: true; selectByMouse: true; font.family: "monospace"; font.pointSize: 10
            wrapMode: TextEdit.NoWrap; textFormat: TextEdit.RichText; background: null
        }
    }
    Controls.ToolButton {
        id: gitActions
        visible: Repository.state.repository && panel.height < 650
        Layout.fillWidth: true; text: i18n("Git actions"); icon.name: checked ? "arrow-down" : "arrow-up"
        checkable: true
    }
    ColumnLayout {
        visible: Repository.state.repository && (panel.height >= 650 || gitActions.checked)
        Layout.fillWidth: true; Layout.margins: Kirigami.Units.largeSpacing
        enabled: Repository.state.repository && !Repository.state.busy
        Controls.TextField { id: commitMessage; Layout.fillWidth: true; placeholderText: i18n("Commit message") }
        Controls.Button { text: i18n("Commit staged"); icon.name: "vcs-commit"; Layout.fillWidth: true; enabled: commitMessage.text.trim().length > 0; onClicked: Repository.commit(commitMessage.text) }
        GridLayout {
            Layout.fillWidth: true
            columns: panel.width < 330 ? 1 : 2
            Controls.Button { text: i18n("Push"); icon.name: "vcs-push"; Layout.fillWidth: true; enabled: Repository.state.upstream; onClicked: Repository.push() }
            Controls.Button { text: i18n("Sync"); icon.name: "view-refresh"; Layout.fillWidth: true; enabled: Repository.state.upstream; onClicked: Repository.sync(); Controls.ToolTip.text: i18n("Fetch, fast-forward, then push"); Controls.ToolTip.visible: hovered }
        }
    }
    Controls.BusyIndicator { Layout.alignment: Qt.AlignHCenter; running: Repository.state.busy; visible: running }
}
