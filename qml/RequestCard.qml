import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Controls.Frame {
    id: card
    required property var request
    property real maximumHeight: 380
    property var answers: ({})
    property bool question: request.method === "item/tool/requestUserInput"
    property bool mcpForm: request.method === "mcpServer/elicitation/request"
    property var schema: request.requestedSchema || ({})
    property var fields: Object.keys(schema.properties || {})
    property bool supportedForm: !mcpForm || request.mode === "url" || request.mode === "form" || request.mode === "openai/form"
    implicitHeight: Math.min(maximumHeight, body.implicitHeight + heading.implicitHeight + actions.implicitHeight + topPadding + bottomPadding + 4 * Kirigami.Units.largeSpacing)
    function put(key, value) { let a = Object.assign({}, answers); a[key] = value; answers = a }
    function valid() {
        if (question) return (request.questions || []).every(q => answers[q.id] && answers[q.id].trim().length > 0)
        if (mcpForm && request.mode !== "url") return (schema.required || []).every(k => answers[k] !== undefined && answers[k] !== "")
        return supportedForm
    }
    ColumnLayout {
        anchors.fill: parent
        Kirigami.Heading { id: heading; level: 3; text: card.question ? i18n("Codex has a question") : i18n("Approval required"); Layout.fillWidth: true }
        Controls.ScrollView {
            id: scroll
            objectName: "questionScroll"
            Layout.fillWidth: true; Layout.fillHeight: true; clip: true
            contentWidth: availableWidth
            Controls.ScrollBar.horizontal.policy: Controls.ScrollBar.AlwaysOff
            ColumnLayout {
                id: body
                width: scroll.availableWidth
                spacing: Kirigami.Units.largeSpacing
                Controls.Label { text: card.request.reason || card.request.message || card.request.command || ""; visible: text.length > 0; wrapMode: Text.Wrap; Layout.fillWidth: true }
                Controls.TextArea {
                    visible: !card.question && text.length > 0
                    text: card.request.command || card.request.operationPreview || ""
                    readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; font.family: "monospace"
                    textFormat: TextEdit.PlainText; background: null; Layout.fillWidth: true
                }
                Controls.Label { visible: !card.question && !!card.request.cwd; text: i18n("Directory: %1", card.request.cwd || ""); wrapMode: Text.Wrap; Layout.fillWidth: true; opacity: 0.65 }
                Controls.ToolButton { visible: !!card.request.threadId && card.request.threadId !== App.state.threadId; text: i18n("Open task"); onClicked: App.selectThread(card.request.threadId) }
                Repeater {
                    model: card.question ? card.request.questions : []
                    delegate: ColumnLayout {
                        id: questionBlock
                        required property var modelData
                        Layout.fillWidth: true
                        Controls.Label { text: questionBlock.modelData.question; Layout.fillWidth: true; wrapMode: Text.Wrap; font.bold: true }
                        Controls.ButtonGroup { id: optionGroup }
                        Repeater {
                            model: questionBlock.modelData.options || []
                            delegate: RowLayout {
                                id: optionRow
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                spacing: Kirigami.Units.smallSpacing
                                Controls.RadioButton {
                                    id: option
                                    objectName: "questionOption-" + questionBlock.modelData.id + "-" + optionRow.index
                                    text: ""
                                    Accessible.name: optionRow.modelData.label
                                    Controls.ButtonGroup.group: optionGroup
                                    Layout.alignment: Qt.AlignTop
                                    Layout.preferredWidth: implicitHeight
                                    onClicked: card.put(questionBlock.modelData.id, optionRow.modelData.label)
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    Layout.topMargin: Math.max(0, (option.implicitHeight - optionLabel.implicitHeight) / 2)
                                    spacing: 2
                                    Controls.Label { id: optionLabel; text: optionRow.modelData.label; Layout.fillWidth: true; wrapMode: Text.Wrap }
                                    Controls.Label { text: optionRow.modelData.description || ""; visible: text.length > 0; Layout.fillWidth: true; wrapMode: Text.Wrap; opacity: 0.65 }
                                    TapHandler { onTapped: { option.checked = true; card.put(questionBlock.modelData.id, optionRow.modelData.label) } }
                                }
                            }
                        }
                        Controls.TextField {
                            objectName: "questionAnswer-" + questionBlock.modelData.id
                            Layout.fillWidth: true; placeholderText: i18n("Your answer…")
                            echoMode: questionBlock.modelData.isSecret ? TextInput.Password : TextInput.Normal
                            onTextEdited: { optionGroup.checkState = Qt.Unchecked; card.put(questionBlock.modelData.id, text) }
                        }
                    }
                }
                Repeater {
                    model: card.mcpForm ? card.fields : []
                    delegate: ColumnLayout {
                        id: field
                        required property string modelData
                        property var spec: card.schema.properties[modelData]
                        property var choices: spec.enum || (spec.oneOf || []).map(v => v.const) || []
                        Layout.fillWidth: true
                        Controls.Label { text: (field.spec.title || field.modelData) + ((card.schema.required || []).includes(field.modelData) ? " *" : ""); Layout.fillWidth: true; wrapMode: Text.Wrap }
                        Controls.CheckBox { visible: field.spec.type === "boolean"; text: field.spec.description || ""; onToggled: card.put(field.modelData, checked); Component.onCompleted: if (visible) card.put(field.modelData, field.spec.default || false) }
                        Controls.ComboBox { visible: field.choices.length > 0; model: field.choices; currentIndex: -1; Layout.fillWidth: true; onActivated: card.put(field.modelData, currentText) }
                        Controls.TextField {
                            visible: field.spec.type !== "boolean" && field.choices.length === 0
                            Layout.fillWidth: true; placeholderText: field.spec.description || ""
                            onTextEdited: {
                                let value = text
                                if (field.spec.type === "number" || field.spec.type === "integer") value = Number(text)
                                if (field.spec.type === "array") value = text.split(",").map(s => s.trim())
                                card.put(field.modelData, value)
                            }
                        }
                    }
                }
                Controls.Button { visible: card.mcpForm && card.request.mode === "url"; text: i18n("Open in browser"); icon.name: "internet-web-browser"; onClicked: App.openLink(card.request.url) }
                Controls.Label { visible: !card.supportedForm; text: i18n("This request type is not supported. You can decline or cancel it."); wrapMode: Text.Wrap; Layout.fillWidth: true }
                Controls.ToolButton { id: detailsButton; visible: !card.question; text: i18n("Details"); checkable: true; icon.name: checked ? "arrow-down" : "arrow-right" }
                Controls.TextArea { visible: detailsButton.checked; text: card.request.details; readOnly: true; selectByMouse: true; wrapMode: TextEdit.Wrap; font.family: "monospace"; textFormat: TextEdit.PlainText; Layout.fillWidth: true }
            }
        }
        Flow {
            id: actions
            Layout.fillWidth: true; spacing: Kirigami.Units.smallSpacing
            Controls.Button { objectName: "submitAnswers-" + card.request.id; visible: card.question; text: i18n("Submit answers"); enabled: card.valid(); onClicked: App.answer(card.request.id, "accept", card.answers) }
            Repeater {
                model: card.question ? [] : (card.request.availableDecisions || ["accept", "decline", "cancel"])
                delegate: Controls.Button {
                    required property var modelData
                    property string label: typeof modelData === "string" ? modelData : JSON.stringify(modelData)
                    objectName: "approval-" + card.request.id + "-" + label
                    text: label === "accept" ? i18n("Approve") : label === "acceptForSession" ? i18n("Approve for session") : label === "decline" ? i18n("Decline") : label === "cancel" ? i18n("Cancel") : modelData.acceptWithExecpolicyAmendment ? i18n("Approve and remember rule") : modelData.applyNetworkPolicyAmendment ? (modelData.applyNetworkPolicyAmendment.network_policy_amendment.action === "allow" ? i18n("Allow this host") : i18n("Block this host")) : label
                    enabled: label !== "accept" || card.valid()
                    Controls.ToolTip.text: modelData.acceptWithExecpolicyAmendment ? i18n("Remember rule: %1", modelData.acceptWithExecpolicyAmendment.execpolicy_amendment.join(" ")) : ""
                    Controls.ToolTip.visible: hovered && Controls.ToolTip.text.length > 0
                    onClicked: App.answer(card.request.id, modelData, card.answers)
                }
            }
        }
    }
}
