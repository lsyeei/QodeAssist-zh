/*
 * Copyright (C) 2024-2026 Petr Mironychev
 *
 * This file is part of QodeAssist.
 *
 * QodeAssist is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * QodeAssist is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with QodeAssist. If not, see <https://www.gnu.org/licenses/>.
 */

import QtQuick
import ChatView
import QtQuick.Controls
import QtQuick.Layouts
import UIControls

Rectangle {
    id: root

    property alias msgModel: msgCreator.model
    property alias messageAttachments: attachmentsModel.model
    property alias messageImages: imagesModel.model
    property string chatFilePath: ""
    property string textFontFamily: Qt.application.font.family
    property string codeFontFamily: {
        switch (Qt.platform.os) {
        case "windows":
            return "Consolas";
        case "osx":
            return "Menlo";
        case "linux":
            return "DejaVu Sans Mono";
        default:
            return "monospace";
        }
    }
    property int textFontSize: Qt.application.font.pointSize
    property int codeFontSize: Qt.application.font.pointSize
    property int textFormat: 0

    property bool isUserMessage: false
    property int messageIndex: -1

    signal resetChatToMessage(int index)
    signal openFileRequested(string filePath)

    height: msgColumn.implicitHeight + 10
    radius: 8
    // color: isUserMessage ? palette.alternateBase
    //                      : palette.base
    color: palette.base

    HoverHandler {
        id: mouse
    }

    ColumnLayout {
        id: msgColumn

        x: 5
        width: parent.width - x
        anchors.fill: parent
        anchors.verticalCenter: parent.verticalCenter
        // anchors.centerIn: parent
        // anchors.right: parent.right
        anchors.rightMargin: 20
        spacing: 5

        Repeater {
            id: msgCreator
            delegate: Loader {
                id: msgCreatorDelegate
                // Fix me:
                // why does `required property MessagePart modelData` not work?
                required property var modelData

                Layout.preferredWidth: root.width
                sourceComponent: {
                    // If `required property MessagePart modelData` is used
                    // and conversion to MessagePart fails, you're left
                    // with a nullptr. This tests that to prevent crashing.
                    if(!modelData) {
                        return undefined;
                    }

                    switch(modelData.type) {
                        case MessagePartType.Text: return isUserMessage?userMsgComponent:textComponent;
                        case MessagePartType.Code: return codeBlockComponent;
                        default: return textComponent;
                    }
                }

                Component {
                    id: userMsgComponent
                    UserMessageComponent{
                        itemData: msgCreatorDelegate.modelData
                    }
                }

                Component {
                    id: textComponent
                    TextComponent {
                        itemData: msgCreatorDelegate.modelData
                    }
                }

                Component {
                    id: codeBlockComponent
                    CodeBlockComponent {
                        itemData: msgCreatorDelegate.modelData
                    }
                }
            }
        }

        Flow {
            id: attachmentsFlow

            Layout.fillWidth: true
            visible: attachmentsModel.model && attachmentsModel.model.length > 0
            leftPadding: 10
            rightPadding: 10
            spacing: 5

            Repeater {
                id: attachmentsModel

                delegate: AttachmentComponent {
                    required property int index
                    required property var modelData

                    itemData: modelData
                }
            }
        }

        Flow {
            id: imagesFlow

            Layout.fillWidth: true
            visible: imagesModel.model && imagesModel.model.length > 0
            leftPadding: 10
            rightPadding: 10
            spacing: 10

            Repeater {
                id: imagesModel

                delegate: ImageComponent {
                    required property int index
                    required property var modelData

                    itemData: modelData
                }
            }
        }
    }

    component UserMessageComponent : Item {
        required property var itemData
        width: parent.width
        height: mainLayout.implicitHeight

        ColumnLayout{
            id: mainLayout
            anchors.fill: parent
            spacing: 5
            Layout.alignment: Qt.AlignTop
            Item{height:15}
        RowLayout{
            id: textRow
            spacing: 0
            width: parent.width
            Item{
                Layout.fillWidth: true
            }

            Rectangle{
                id: editorPanel
                border.color: root.color.hslLightness > 0.5 ? Qt.darker(root.color, 1.3)
                                                            : Qt.lighter(root.color, 1.3)
                border.width: 2
                color: palette.alternateBase
                clip: true
                height: editorId.implicitHeight + 20
                width: editorId.implicitWidth + 30
                radius: 8
                TextBlock {
                    id: editorId
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.horizontalCenter: parent.horizontalCenter
                    height: implicitHeight
                    verticalAlignment: Text.AlignVCenter
                    Layout.alignment: Qt.AlignVCenter
                    Layout.maximumWidth: parent.width - 15
                    Layout.fillHeight: true
                    text: itemData.text
                    font.family: root.textFontFamily
                    font.pointSize: root.textFontSize
                    textFormat: {
                        if (root.textFormat == 0) {
                            return Text.MarkdownText
                        } else if (root.textFormat == 1) {
                            return Text.RichText
                        } else {
                            return Text.PlainText
                        }
                    }
                    ChatUtils {
                        id: utils
                    }
                }
            }
            Rectangle {
                id: userMessageMarker

                anchors.top: editorPanel.top
                anchors.right: editorPanel.right
                anchors.rightMargin: -3
                width: 3
                height: editorPanel.height
                color: "#92BD6C"
                radius: editorPanel.radius
            }
            Item{ width: 10}
        }
        RowLayout{
            id: toolRow
            Layout.alignment: Qt.AlignRight
            // height: 30
            width: parent.width
            spacing: 5
            ToolButton {
                id: stopButtonId
                icon {
                    source: "qrc:/qt/qml/ChatView/icons/undo-changes-button.svg"
                    height: 15
                    width: 15
                    color: palette.highlightedText
                }
                background: Rectangle{
                    anchors.fill: parent
                    radius: 5
                    z : -1
                    color: palette.base
                    border{
                        width: 1
                        color: palette.brightText
                    }
                }

                visible: mouse.hovered
                onClicked: function() {
                    root.resetChatToMessage(root.messageIndex)
                }
                ToolTip {
                    visible: stopButtonId.hovered
                    text: qsTr("Reset chat to this message and edit")
                    delay: 500
                }
            }
            Item{width: 10; height: 25;}
        }
        }
        MouseArea{
            anchors.fill: editorPanel
            hoverEnabled: true
            onEntered: stopButtonId.visible = true
            onExited: stopButtonId.visible = false
        }
    }

    component TextComponent : TextBlock {
        required property var itemData
        height: implicitHeight + 10
        verticalAlignment: Text.AlignVCenter
        rightPadding: 10
        // text: textFormat == Text.MarkdownText ? utils.getSafeMarkdownText(itemData.text)
        //                                       : itemData.text
        text: itemData.text
        font.family: root.textFontFamily
        font.pointSize: root.textFontSize
        textFormat: {
            if (root.textFormat == 0) {
                return Text.MarkdownText
            } else if (root.textFormat == 1) {
                return Text.RichText
            } else {
                return Text.PlainText
            }
        }

        onLinkActivated: function(link) {
            if (link.startsWith("file://")) {
                var filePath = link.replace(/^file:\/\//, "")
                root.openFileRequested(filePath)
            } else {
                Qt.openUrlExternally(link)
            }
        }

        ChatUtils {
            id: utils
        }
    }

    component CodeBlockComponent : CodeBlock {
        id: codeblock

        required property var itemData
        anchors {
            left: parent.left
            leftMargin: 10
            right: parent.right
            rightMargin: 10
        }

        code: itemData.text
        language: itemData.language
        codeFontFamily: root.codeFontFamily
        codeFontSize: root.codeFontSize
    }

    component AttachmentComponent : Rectangle {
        required property var itemData

        height: attachFileText.implicitHeight + 8
        width: attachFileText.implicitWidth + 16
        radius: 4
        color: attachFileMouseArea.containsMouse ? Qt.lighter(palette.button, 1.1) : palette.button
        border.width: 1
        border.color: palette.mid

        Behavior on color { ColorAnimation { duration: 100 } }

        FileItem {
            id: fileItem
            filePath: itemData.filePath || ""
        }

        Text {
            id: attachFileText

            anchors.centerIn: parent
            text: (itemData.fileName || "")
            color: palette.buttonText
            font.pointSize: root.textFontSize - 1
        }

        MouseArea {
            id: attachFileMouseArea

            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.PointingHandCursor

            onClicked: (mouse) => {
                if (mouse.modifiers & Qt.ShiftModifier) {
                    fileItem.openFileInExternalEditor()
                } else {
                    fileItem.openFileInEditor()
                }
            }

            QoAToolTip {
                visible: attachFileMouseArea.containsMouse
                text: qsTr("Click: Open in Qt Creator\nShift+Click: Open in System Editor")
                delay: 500
            }
        }
    }

    component ImageComponent : Rectangle {
        required property var itemData

        readonly property int maxImageWidth: Math.min(400, root.width - 40)
        readonly property int maxImageHeight: 300

        width: Math.min(imageDisplay.implicitWidth, maxImageWidth) + 16
        height: imageDisplay.implicitHeight + fileNameText.implicitHeight + 16
        radius: 4
        color: imageMouseArea.containsMouse ? Qt.lighter(palette.base, 1.05) : palette.base
        border.width: 1
        border.color: palette.mid

        Behavior on color { ColorAnimation { duration: 100 } }

        FileItem {
            id: imageFileItem
            filePath: itemData.filePath || ""
        }

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 4

            Image {
                id: imageDisplay

                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: parent.parent.maxImageWidth
                Layout.maximumHeight: parent.parent.maxImageHeight

                source: itemData.imageUrl ? itemData.imageUrl : ""

                sourceSize.width: parent.parent.maxImageWidth
                sourceSize.height: parent.parent.maxImageHeight
                fillMode: Image.PreserveAspectFit
                cache: true
                asynchronous: true
                smooth: true
                mipmap: true

                BusyIndicator {
                    anchors.centerIn: parent
                    running: imageDisplay.status === Image.Loading
                    visible: running
                }

                Text {
                    anchors.centerIn: parent
                    text: qsTr("Failed to load image")
                    visible: imageDisplay.status === Image.Error
                    color: palette.placeholderText
                }
            }

            Text {
                id: fileNameText

                Layout.fillWidth: true
                text: itemData.fileName || ""
                color: palette.text
                font.pointSize: root.textFontSize - 1
                elide: Text.ElideMiddle
                horizontalAlignment: Text.AlignHCenter
            }
        }

        MouseArea {
            id: imageMouseArea

            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton
            cursorShape: Qt.PointingHandCursor

            onClicked: (mouse) => {
                if (mouse.modifiers & Qt.ShiftModifier) {
                    imageFileItem.openFileInExternalEditor()
                } else {
                    imageFileItem.openFileInEditor()
                }
            }

            QoAToolTip {
                visible: imageMouseArea.containsMouse
                text: qsTr("Click: Open in Qt Creator\nShift+Click: Open in System Editor")
                delay: 500
            }
        }
    }
}