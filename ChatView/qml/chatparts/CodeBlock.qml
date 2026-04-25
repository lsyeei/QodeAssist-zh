// Copyright (C) 2024-2026 Petr Mironychev
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import ChatView
import UIControls
// import Qt.labs.platform as Platform

Rectangle {
    id: root

    property string code: ""
    property string language: ""
    property bool expanded: false

    property alias codeFontFamily: codeText.font.family
    property alias codeFontSize: codeText.font.pointSize
    readonly property real collapsedHeight: copyButton.height + 10

    color: palette.alternateBase
    border.color: root.color.hslLightness > 0.5 ? Qt.darker(root.color, 1.3)
                                                : Qt.lighter(root.color, 1.3)
    border.width: 2
    radius: 4
    implicitWidth: parent.width - 10
    clip: true

    Behavior on implicitHeight {
        NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
    }

    ChatUtils {
        id: utils
    }

    HoverHandler {
        id: hoverHandler
        enabled: true
    }

    MouseArea {
        id: header

        width: parent.width
        height: root.collapsedHeight
        cursorShape: Qt.PointingHandCursor
        onClicked: root.expanded = !root.expanded

        Row {
            id: headerRow

            anchors {
                verticalCenter: parent.verticalCenter
                left: parent.left
                leftMargin: 10
            }
            spacing: 6

            Text {
                text: root.language ? qsTr("Code (%1)").arg(root.language) :
                                      qsTr("Code")
                font.pixelSize: 12
                font.bold: true
                color: palette.text
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.expanded ? "∨" : "∧"//▼▶
                font.pixelSize: 12
                font.bold: true
                color: palette.mid
            }
        }
    }

    TextEdit {
        id: codeText

        anchors {
            left: parent.left
            right: parent.right
            top: header.bottom
        }
        leftPadding: 20
        rightPadding: 20
        topPadding: 10
        bottomPadding: 10
        text: root.code
        readOnly: true
        selectByMouse: true
        color: parent.color.hslLightness > 0.5 ? "black" : "white"
        wrapMode: TextEdit.Wrap
        selectionColor: palette.highlight

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.RightButton
            onClicked: contextMenu.popup()
        }
    }

    // Platform.Menu {
    Menu{
        id: contextMenu

        width: 130
        background: Rectangle{
            anchors.fill: parent
            border{
                width: 1
                color: palette.light
            }
            radius: 6
            color: palette.alternateBase
        }
        padding: 2
        palette: root.palette

        // Platform.MenuItem {
        MenuItem{
            text: qsTr("Copy")
            icon{
                source: "qrc:/qt/qml/ChatView/icons/edit-copy.svg"
                width: 15
                height: 15
                color: palette.brightText
            }
            padding: 2
            hoverEnabled: true
            background: Rectangle {
                color: parent.hovered ? palette.highlight : "transparent"
                z: -1
            }
            // width: parent.width
            onTriggered: {
                const textToCopy = codeText.selectedText || root.code
                utils.copyToClipboard(textToCopy)
            }
        }

        // Platform.MenuSeparator {}
        MenuSeparator{
            padding: 0
        }

        // Platform.MenuItem {
        MenuItem{
            id: collapseMenuId
            text: root.expanded ? qsTr("Collapse") : qsTr("Expand")
            icon{
                source: "qrc:/qt/qml/ChatView/icons/list-collapse.svg"
                width: 15
                height: 15
                color: palette.brightText
            }
            padding: 2
            background: Rectangle {
                anchors.fill: parent
                color: parent.hovered ? palette.highlight : "transparent"
                z: -1
            }
            onTriggered: root.expanded = !root.expanded
        }
    }

    // QoAButton {
    ToolButton {
        id: copyButton

        anchors.right: parent.right
        anchors.rightMargin: 5

        y: 5
        // text: qsTr("Copy")
        // flat: true
        icon{
            source: "qrc:/qt/qml/ChatView/icons/edit-copy.svg"
            width: 15
            height: 15
        }
        background : Rectangle {
            implicitWidth: 24
            implicitHeight: 24
            opacity: 0.3
            color: palette.alternateBase
        }
        ToolTip.text: qsTr("Copy")
        ToolTip.delay: 250
        ToolTip.visible: hovered

        onClicked: {
            utils.copyToClipboard(root.code)
            // text = qsTr("Copied")
            copyTimer.start()
        }

        Timer {
            id: copyTimer
            interval: 2000
            // onTriggered: parent.text = qsTr("Copy")
        }
    }

    states: [
        State {
            when: !root.expanded
            PropertyChanges {
                target: root
                implicitHeight: root.collapsedHeight
            }
        },
        State {
            when: root.expanded
            PropertyChanges {
                target: root
                implicitHeight: header.height + codeText.implicitHeight + 10
            }
        }
    ]
}