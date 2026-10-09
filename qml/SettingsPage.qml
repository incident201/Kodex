import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Controls.ScrollView {
    id: page
    clip: true
    contentWidth: availableWidth
    Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
    property string note: ""
    function save() {
        if (Preferences.save({executable: executable.text, arguments: codexArguments.text, proxyMode: proxyMode.keys[proxyMode.currentIndex], proxyUrl: proxyUrl.text, noProxy: noProxy.text, fallbackProxy: fallbackProxy.text, language: language.keys[language.currentIndex], updateMode: updateMode.currentIndex === 1 ? "external" : "builtin", updater: updater.text, updaterArguments: updaterArguments.text}, proxyPassword.text)) {
            proxyPassword.text = ""; note = i18n("Settings saved. A running daemon keeps its current environment until restarted.")
        }
    }
    ColumnLayout {
        width: page.availableWidth; spacing: Kirigami.Units.largeSpacing
        Kirigami.Heading { text: i18n("Account"); level: 2 }
        Controls.Label { text: App.state.account || i18n("Not signed in"); Layout.fillWidth: true }
        Flow {
            Layout.fillWidth: true; spacing: Kirigami.Units.smallSpacing
            Controls.Button { text: i18n("Sign in with ChatGPT"); visible: !App.state.signedIn; enabled: Connection.ready && !App.state.loginPending; onClicked: App.login() }
            Controls.Button { text: i18n("Use device code"); visible: !App.state.signedIn; enabled: Connection.ready && !App.state.loginPending; onClicked: App.login(true) }
            Controls.Button { text: i18n("Cancel login"); visible: App.state.loginPending || false; onClicked: App.cancelLogin() }
            Controls.Button { text: i18n("Sign out"); icon.name: "system-log-out"; visible: App.state.signedIn || false; onClicked: logoutDialog.open() }
        }
        Controls.Label { text: App.state.loginInfo; visible: text.length > 0; font.bold: true }
        Controls.Label { text: App.state.limits || i18n("Usage limits unavailable"); wrapMode: Text.Wrap; Layout.fillWidth: true }
        Kirigami.Separator { Layout.fillWidth: true }
        Kirigami.Heading { text: i18n("Codex and proxy"); level: 2 }
        Kirigami.FormLayout {
            id: codexForm
            Layout.fillWidth: true
            twinFormLayouts: [updatesForm]
            Controls.TextField { id: executable; Kirigami.FormData.label: i18n("Executable:"); text: Preferences.values.executable; Layout.fillWidth: true }
            Controls.TextField { id: codexArguments; Kirigami.FormData.label: i18n("Arguments:"); text: Preferences.values.arguments; Layout.fillWidth: true; placeholderText: i18n("Optional, quoted arguments supported") }
            Controls.ComboBox { id: proxyMode; Kirigami.FormData.label: i18n("Proxy:"); property var keys: ["inherit", "none", "custom"]; model: [i18n("Inherit environment"), i18n("No proxy"), i18n("Custom proxy")]; currentIndex: keys.indexOf(Preferences.values.proxyMode) }
            Controls.TextField { id: proxyUrl; Kirigami.FormData.label: i18n("Proxy URL:"); text: Preferences.values.proxyUrl; placeholderText: "http://host:port or socks5h://host:port"; enabled: proxyMode.currentIndex === 2; Layout.fillWidth: true }
            Controls.TextField { id: proxyPassword; Kirigami.FormData.label: i18n("Proxy password:"); echoMode: TextInput.Password; placeholderText: i18n("Stored in KWallet"); enabled: proxyMode.currentIndex === 2; Layout.fillWidth: true }
            Controls.TextField { id: noProxy; Kirigami.FormData.label: i18n("Bypass proxy:"); text: Preferences.values.noProxy; enabled: proxyMode.currentIndex === 2; Layout.fillWidth: true }
            Controls.TextField { id: fallbackProxy; Kirigami.FormData.label: i18n("Update fallback proxy:"); text: Preferences.values.fallbackProxy; Layout.fillWidth: true }
            Controls.ComboBox { id: language; Kirigami.FormData.label: i18n("Language:"); property var keys: ["", "ru", "en"]; model: [i18n("System language"), "Русский", "English"]; currentIndex: keys.indexOf(Preferences.values.language) }
        }
        Controls.Label { Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7; text: i18n("A wrapper may override proxy settings. Changes apply to newly started processes; restart the daemon to apply them to Codex. Language changes require restarting Kodex.") }
        Kirigami.Separator { Layout.fillWidth: true }
        Kirigami.Heading { text: i18n("Updates"); level: 2 }
        Controls.Label { text: i18n("CLI: %1 · daemon: %2", Maintenance.state.version || "—", Maintenance.state.daemonVersion || "—"); Layout.fillWidth: true }
        Kirigami.FormLayout {
            id: updatesForm
            Layout.fillWidth: true
            twinFormLayouts: [codexForm]
            Controls.ComboBox { id: updateMode; Kirigami.FormData.label: i18n("Update method:"); model: [i18n("Standalone installer"), i18n("External updater")]; currentIndex: Preferences.values.updateMode === "external" ? 1 : 0 }
            Controls.TextField { id: updater; Kirigami.FormData.label: i18n("Updater executable:"); text: Preferences.values.updater; enabled: updateMode.currentIndex === 1; Layout.fillWidth: true }
            Controls.TextField { id: updaterArguments; Kirigami.FormData.label: i18n("Updater arguments:"); text: Preferences.values.updaterArguments; enabled: updateMode.currentIndex === 1; Layout.fillWidth: true }
        }
        Flow {
            Layout.fillWidth: true; spacing: Kirigami.Units.smallSpacing
            Controls.Button { text: i18n("Check version"); onClicked: Maintenance.versions() }
            Controls.Button { text: i18n("Update Codex"); enabled: !Maintenance.state.busy; onClicked: Maintenance.update() }
            Controls.Button { text: i18n("Restart daemon…"); enabled: !Maintenance.state.busy; onClicked: restartDialog.open() }
        }
        Controls.Label { Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.7; text: i18n("External scripts can override proxy settings. Updates never restart the daemon automatically.") }
        Controls.ProgressBar { Layout.fillWidth: true; indeterminate: true; visible: Maintenance.state.busy }
        Controls.ScrollView {
            visible: Maintenance.state.log.length > 0; Layout.fillWidth: true; Layout.preferredHeight: 160
            Controls.TextArea { text: Maintenance.state.log; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; textFormat: TextEdit.PlainText; font.family: "monospace" }
        }
        Kirigami.Separator { Layout.fillWidth: true }
        Kirigami.Heading { text: i18n("Remote control"); level: 2 }
        Controls.Label { text: i18n("Pair your phone with Codex on this computer."); Layout.fillWidth: true; wrapMode: Text.Wrap }
        Controls.Label { text: (Maintenance.state.serverName || "") + " · " + Maintenance.state.remote; Layout.fillWidth: true; opacity: 0.7 }
        Flow {
            Layout.fillWidth: true; spacing: Kirigami.Units.smallSpacing
            Controls.Button { text: i18n("Enable remote control"); enabled: !Maintenance.state.busy; onClicked: Maintenance.remote(true) }
            Controls.Button { text: i18n("Disable remote control"); enabled: !Maintenance.state.busy; onClicked: Maintenance.remote(false) }
            Controls.Button { text: i18n("Create pairing code"); enabled: !Maintenance.state.busy; onClicked: Maintenance.pair() }
        }
        Controls.TextArea { visible: Maintenance.state.pairing.length > 0; text: Maintenance.state.pairing; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; Layout.fillWidth: true; textFormat: TextEdit.PlainText }
        Controls.Label { text: Maintenance.state.pairingStatus || ""; visible: text.length > 0; Layout.fillWidth: true }
        Kirigami.Separator { Layout.fillWidth: true }
        Kirigami.Heading { text: "MCP"; level: 2 }
        RowLayout {
            Controls.Button { text: i18n("Refresh"); onClicked: App.refreshMcp() }
            Controls.Button { text: i18n("Reload configuration"); onClicked: App.reloadMcp() }
        }
        Repeater {
            model: App.mcp
            delegate: RowLayout {
                required property var record
                Layout.fillWidth: true
                Controls.Label { text: record.name + " · " + (record.authStatus || ""); Layout.fillWidth: true; elide: Text.ElideRight }
                Controls.Button { text: i18n("Authenticate"); onClicked: App.loginMcp(record.name) }
            }
        }
    }
    Controls.Dialog {
        id: restartDialog; title: i18n("Restart the shared daemon?"); modal: true; anchors.centerIn: parent
        standardButtons: Controls.Dialog.Ok | Controls.Dialog.Cancel
        Controls.Label { width: 380; wrapMode: Text.Wrap; text: i18n("This interrupts active tasks in every connected client, including CLI and remote control. Reconnect after the restart to restore history.") }
        onAccepted: Maintenance.restart()
    }
    Controls.Dialog {
        id: logoutDialog; title: i18n("Sign out of Codex?"); modal: true; anchors.centerIn: parent
        standardButtons: Controls.Dialog.Ok | Controls.Dialog.Cancel
        Controls.Label { width: 350; wrapMode: Text.Wrap; text: i18n("This signs out the shared Codex account, including other clients.") }
        onAccepted: App.logout()
    }
}
