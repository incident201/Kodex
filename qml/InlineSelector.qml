import QtQuick
import QtQuick.Controls as Controls
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

Controls.ToolButton {
    id: control
    property var entries: []
    property string value: ""
    property string label: ""
    property string symbol: ""
    property alias popup: menu
    signal selected(string value)
    // The KDE desktop style paints ToolButton.text in its native background.
    // Keep the accessible label separate when supplying custom content.
    text: ""
    Accessible.name: label
    implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
    implicitHeight: Math.max(contentItem.implicitHeight + topPadding + bottomPadding, 28)
    flat: true
    leftPadding: Kirigami.Units.smallSpacing
    rightPadding: Kirigami.Units.smallSpacing
    onClicked: menu.open()
    contentItem: RowLayout {
        spacing: Kirigami.Units.smallSpacing
        Kirigami.Icon { visible: control.symbol.length > 0; source: control.symbol; implicitWidth: 14; implicitHeight: 14; opacity: 0.7 }
        Controls.Label { text: control.label; elide: Text.ElideRight; Layout.maximumWidth: 185; opacity: control.enabled ? 0.8 : 0.4 }
        Kirigami.Icon { source: "arrow-down"; implicitWidth: 10; implicitHeight: 10; opacity: 0.45 }
    }
    Controls.Menu {
        id: menu
        parent: control
        x: 0
        y: -height - Kirigami.Units.smallSpacing
        Instantiator {
            model: control.entries
            delegate: Controls.MenuItem {
                required property var modelData
                objectName: "option-" + control.objectName + "-" + modelData.value
                text: modelData.title
                checkable: true
                checked: control.value === modelData.value
                onTriggered: control.selected(modelData.value)
            }
            onObjectAdded: (index, object) => menu.insertItem(index, object)
            onObjectRemoved: (index, object) => menu.removeItem(object)
        }
    }
}
