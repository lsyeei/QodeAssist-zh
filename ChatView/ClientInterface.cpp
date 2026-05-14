// Copyright (C) 2024-2026 Petr Mironychev
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ClientInterface.hpp"

#include <LLMQore/BaseClient.hpp>

#include <projectexplorer/buildconfiguration.h>
#include <projectexplorer/target.h>
#include <texteditor/textdocument.h>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QUuid>

#include <context/TokenUtils.hpp>

#include <coreplugin/editormanager/editormanager.h>
#include <coreplugin/editormanager/ieditor.h>
#include <coreplugin/idocument.h>
#include <projectexplorer/project.h>
#include <projectexplorer/projectexplorer.h>
#include <projectexplorer/projectmanager.h>

#include <texteditor/textdocument.h>
#include <texteditor/texteditor.h>

#include <LLMQore/ToolsManager.hpp>

#include "tools/TodoTool.hpp"

#include "ChatAssistantSettings.hpp"
#include "ChatSerializer.hpp"
#include "GeneralSettings.hpp"
#include "Logger.hpp"
#include "ProvidersManager.hpp"
#include "ToolsSettings.hpp"
#include <RulesLoader.hpp>
#include <context/ChangesManager.h>

namespace QodeAssist::Chat {

ClientInterface::ClientInterface(
    ChatModel *chatModel, PluginLLMCore::IPromptProvider *promptProvider, QObject *parent)
    : QObject(parent)
    , m_chatModel(chatModel)
    , m_promptProvider(promptProvider)
    , m_contextManager(new Context::ContextManager(this))
    , m_compressor(new ChatCompressor(this))
{}

ClientInterface::~ClientInterface()
{
    cancelRequest();
}

void ClientInterface::sendMessage(
    const QString &message,
    const QList<QString> &attachments,
    const QList<QString> &linkedFiles,
    bool useTools,
    bool useThinking)
{
    cancelRequest();
    m_accumulatedResponses.clear();

    Context::ChangesManager::instance().archiveAllNonArchivedEdits();

    QList<QString> imageFiles;
    QList<QString> textFiles;

    for (const QString &filePath : attachments) {
        if (isImageFile(filePath)) {
            imageFiles.append(filePath);
        } else {
            textFiles.append(filePath);
        }
    }

    QList<Context::ContentFile> storedAttachments;
    if (!textFiles.isEmpty() && !m_chatFilePath.isEmpty()) {
        auto attachFiles = m_contextManager->getContentFiles(textFiles);
        for (const auto &file : attachFiles) {
            QString storedPath;
            if (ChatSerializer::saveContentToStorage(
                    m_chatFilePath, file.filename, file.content.toUtf8().toBase64(), storedPath)) {
                Context::ContentFile storedFile;
                storedFile.filename = file.filename;
                storedFile.content = storedPath;
                storedAttachments.append(storedFile);
                LOG_MESSAGE(QString("Stored text file %1 as %2").arg(file.filename, storedPath));
            }
        }
    } else if (!textFiles.isEmpty()) {
        LOG_MESSAGE(QString("Warning: Chat file path not set, cannot save %1 text file(s)")
                        .arg(textFiles.size()));
    }

    QList<ChatModel::ImageAttachment> imageAttachments;
    if (!imageFiles.isEmpty() && !m_chatFilePath.isEmpty()) {
        for (const QString &imagePath : imageFiles) {
            QString base64Data = encodeImageToBase64(imagePath);
            if (base64Data.isEmpty()) {
                continue;
            }

            QString storedPath;
            QFileInfo fileInfo(imagePath);
            if (ChatSerializer::saveContentToStorage(
                    m_chatFilePath, fileInfo.fileName(), base64Data, storedPath)) {
                ChatModel::ImageAttachment imageAttachment;
                imageAttachment.fileName = fileInfo.fileName();
                imageAttachment.storedPath = storedPath;
                imageAttachment.mediaType = getMediaTypeForImage(imagePath);
                imageAttachments.append(imageAttachment);

                LOG_MESSAGE(QString("Stored image %1 as %2").arg(fileInfo.fileName(), storedPath));
            }
        }
    } else if (!imageFiles.isEmpty()) {
        LOG_MESSAGE(QString("Warning: Chat file path not set, cannot save %1 image(s)")
                        .arg(imageFiles.size()));
    }

    m_chatModel->addMessage(message, ChatModel::ChatRole::User, "", storedAttachments, imageAttachments);

    auto &chatAssistantSettings = Settings::chatAssistantSettings();

    auto providerName = Settings::generalSettings().caProvider();
    auto provider = PluginLLMCore::ProvidersManager::instance().getProviderByName(providerName);

    if (!provider) {
        LOG_MESSAGE(QString("No provider found with name: %1").arg(providerName));
        return;
    }

    auto templateName = Settings::generalSettings().caTemplate();
    auto promptTemplate = m_promptProvider->getTemplateByName(templateName);

    if (!promptTemplate) {
        LOG_MESSAGE(QString("No template found with name: %1").arg(templateName));
        return;
    }

    PluginLLMCore::ContextData context;

    const bool isToolsEnabled = useTools;

    if (chatAssistantSettings.useSystemPrompt()) {
        QString systemPrompt = chatAssistantSettings.systemPrompt();

        const QString lastRoleId = chatAssistantSettings.lastUsedRoleId();
        if (!lastRoleId.isEmpty()) {
            const Settings::AgentRole role = Settings::AgentRolesManager::loadRole(lastRoleId);
            if (!role.id.isEmpty())
                systemPrompt = systemPrompt + "\n\n" + role.systemPrompt;
        }

        // 添加工具调用限制
        systemPrompt.append(tr("\n\n# calling tools rules \n\n"));
        systemPrompt.append(QString(tr("- No more than %1 tool calls allowed\n"))
                                .arg(Settings::toolsSettings().maxToolContinuations.value()));
        systemPrompt.append(tr("- Every tool call must explicitly specify its calling reason\n"));
        systemPrompt.append(tr("- If no more tools are needed, please answer directly\n"));

        auto project = PluginLLMCore::RulesLoader::getActiveProject();

        if (project) {
            systemPrompt += QString("\n# Active project name: %1").arg(project->displayName());
            systemPrompt += QString("\n# Active Project path: %1")
                                .arg(project->projectDirectory().toUrlishString());

            if (auto target = project->activeTarget()) {
                if (auto buildConfig = target->activeBuildConfiguration()) {
                    systemPrompt += QString("\n# Active Build directory: %1")
                    .arg(buildConfig->buildDirectory().toUrlishString());
                }
            }

            QString projectRules
                = PluginLLMCore::RulesLoader::loadRulesForProject(project, PluginLLMCore::RulesContext::Chat);

            if (!projectRules.isEmpty()) {
                systemPrompt += QString("\n# Project Rules\n\n") + projectRules;
            }
        } else {
            systemPrompt += QString("\n# No active project in IDE");
        }

        if (!linkedFiles.isEmpty()) {
            systemPrompt = getSystemPromptWithLinkedFiles(systemPrompt, linkedFiles);
        }
        context.systemPrompt = systemPrompt;
    }

    QVector<PluginLLMCore::Message> messages;
    for (const auto &msg : m_chatModel->getChatHistory()) {
        if (msg.role == ChatModel::ChatRole::Tool || msg.role == ChatModel::ChatRole::FileEdit) {
            continue;
        }

        PluginLLMCore::Message apiMessage;
        apiMessage.role = msg.role == ChatModel::ChatRole::User ? "user" : "assistant";
        apiMessage.content = msg.content;

        if (!msg.attachments.isEmpty() && !m_chatFilePath.isEmpty()) {
            apiMessage.content += "\n\nAttached files:";
            for (const auto &attachment : msg.attachments) {
                QString fileContent = ChatSerializer::loadContentFromStorage(m_chatFilePath, attachment.content);
                if (!fileContent.isEmpty()) {
                    QString decodedContent = QString::fromUtf8(QByteArray::fromBase64(fileContent.toUtf8()));
                    apiMessage.content += QString("\n\nFile: %1\n```\n%2\n```")
                                              .arg(attachment.filename, decodedContent);
                }
            }
        }

        apiMessage.isThinking = (msg.role == ChatModel::ChatRole::Thinking);
        apiMessage.isRedacted = msg.isRedacted;
        apiMessage.signature = msg.signature;

        if (provider->capabilities().testFlag(PluginLLMCore::ProviderCapability::Image)
            && !m_chatFilePath.isEmpty() && !msg.images.isEmpty()) {
            auto apiImages = loadImagesFromStorage(msg.images);
            if (!apiImages.isEmpty()) {
                apiMessage.images = apiImages;
            }
        }

        messages.append(apiMessage);
    }

    if (!imageFiles.isEmpty()
        && !provider->capabilities().testFlag(PluginLLMCore::ProviderCapability::Image)) {
        LOG_MESSAGE(QString("Provider %1 doesn't support images, %2 ignored")
                        .arg(provider->name(), QString::number(imageFiles.size())));
    }

    context.history = messages;

    QJsonObject payload{
        {"model", Settings::generalSettings().caModel()}, {"stream", true}};

    provider->prepareRequest(
        payload,
        promptTemplate,
        context,
        PluginLLMCore::RequestType::Chat,
        useTools,
        useThinking);

    provider->client()->setMaxToolContinuations(
        Settings::toolsSettings().maxToolContinuations());

    // 准确计算token数量，判断是否超出阈值
    QString payloadStr = QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact));
    int accurateTokens = Context::TokenUtils::estimateTokens(payloadStr);
    emit tokenCount(accurateTokens);
    // qDebug() << __FUNCTION__ << "字符数量：" << payloadStr.length()
    //          << "计算的token数量：" << accurateTokens
    //          << ";token 阈值：" << m_chatModel->tokensThreshold();
    if (accurateTokens > m_chatModel->tokensThreshold()) {
        LOG_MESSAGE(QString("Accurate token check: %1 exceeds threshold %2, starting compression")
                        .arg(accurateTokens)
                        .arg(m_chatModel->tokensThreshold()));

        // 使用ChatCompressor同步压缩历史对话（排除最后2轮）
        QString summary = m_compressor->compressHistorySync(m_chatModel);
        if (!summary.isEmpty()) {
            int splitIndex = m_compressor->splitIndex();

            // 创建摘要消息
            ChatModel::Message summaryMessage;
            summaryMessage.role = ChatModel::ChatRole::System;
            summaryMessage.content = summary;
            summaryMessage.id = QUuid::createUuid().toString(QUuid::WithoutBraces);

            // 替换内存中的消息（会通知视图更新）
            m_chatModel->applyCompression(summaryMessage, splitIndex);

            LOG_MESSAGE(QString("Compression applied, replaced %1 messages with summary, length: %2")
                            .arg(splitIndex)
                            .arg(summary.length()));

            // 不在此保存文件，由调用方（ChatRootView）在适当时机保存
            // 重建context.history和payload（因为模型已更新）
            context.history.reset();
            QList<PluginLLMCore::Message> newMessages;
            for (const auto &msg : m_chatModel->getChatHistory()) {
                if (msg.role == ChatModel::ChatRole::Tool || msg.role == ChatModel::ChatRole::FileEdit) {
                    continue;
                }
                PluginLLMCore::Message apiMessage;
                apiMessage.role = msg.role == ChatModel::ChatRole::User ? "user" : "assistant";
                apiMessage.content = msg.content;
                apiMessage.isThinking = (msg.role == ChatModel::ChatRole::Thinking);
                apiMessage.isRedacted = msg.isRedacted;
                apiMessage.signature = msg.signature;
                newMessages.append(apiMessage);
            }
            context.history = newMessages;

            // 重建payload
            payload = QJsonObject{{"model", Settings::generalSettings().caModel()}, {"stream", true}};
            provider->prepareRequest(
                payload,
                promptTemplate,
                context,
                PluginLLMCore::RequestType::Chat,
                useTools,
                useThinking);

            LOG_MESSAGE("Payload rebuilt after compression");
        } else {
            LOG_MESSAGE("Compression failed or skipped, proceeding with current messages");
        }

        // 压缩完成后继续请求（不return）
    }

    connect(
        provider->client(),
        &::LLMQore::BaseClient::chunkReceived,
        this,
        &ClientInterface::handlePartialResponse,
        Qt::UniqueConnection);
    connect(
        provider->client(),
        &::LLMQore::BaseClient::requestCompleted,
        this,
        &ClientInterface::handleFullResponse,
        Qt::UniqueConnection);
    connect(
        provider->client(),
        &::LLMQore::BaseClient::requestFailed,
        this,
        &ClientInterface::handleRequestFailed,
        Qt::UniqueConnection);
    connect(
        provider->client(),
        &::LLMQore::BaseClient::toolStarted,
        this,
        &ClientInterface::handleToolExecutionStarted,
        Qt::UniqueConnection);
    connect(
        provider->client(),
        &::LLMQore::BaseClient::toolResultReady,
        this,
        &ClientInterface::handleToolExecutionCompleted,
        Qt::UniqueConnection);
    connect(
        provider->client(),
        &::LLMQore::BaseClient::thinkingBlockReceived,
        this,
        &ClientInterface::handleThinkingBlockReceived,
        Qt::UniqueConnection);

    const QString customEndpoint = Settings::generalSettings().caCustomEndpoint();
    const QString endpoint = !customEndpoint.isEmpty() ? customEndpoint
                                                       : promptTemplate->endpoint();
    auto requestId
        = provider->sendRequest(QUrl(Settings::generalSettings().caUrl()), payload, endpoint);
    QJsonObject request{{"id", requestId}};

    m_activeRequests[requestId] = {request, provider};

    emit requestStarted(requestId);

    if (provider->capabilities().testFlag(PluginLLMCore::ProviderCapability::Tools)
        && provider->toolsManager()) {
        if (auto *todoTool = qobject_cast<QodeAssist::Tools::TodoTool *>(
                provider->toolsManager()->tool("todo_tool"))) {
            todoTool->setCurrentSessionId(m_chatFilePath);
        }
    }
}

void ClientInterface::clearMessages()
{
    const auto providerName = Settings::generalSettings().caProvider();
    auto *provider = PluginLLMCore::ProvidersManager::instance().getProviderByName(providerName);

    if (provider && !m_chatFilePath.isEmpty()
        && provider->capabilities().testFlag(PluginLLMCore::ProviderCapability::Tools)
        && provider->toolsManager()) {
        if (auto *todoTool = qobject_cast<QodeAssist::Tools::TodoTool *>(
                provider->toolsManager()->tool("todo_tool"))) {
            todoTool->clearSession(m_chatFilePath);
        }
    }

    m_chatModel->clear();
}

void ClientInterface::cancelRequest()
{
    QSet<PluginLLMCore::Provider *> providers;
    for (auto it = m_activeRequests.begin(); it != m_activeRequests.end(); ++it) {
        const auto &value = it.value();
        if (value.provider) {
            providers.insert(value.provider);
        }
    }

    for (auto *provider : providers) {
        disconnect(provider->client(), nullptr, this, nullptr);
    }

    for (const auto &key : m_activeRequests.keys()) {
        const auto &value = m_activeRequests.value(key);
        if (value.provider) {
            value.provider->cancelRequest(key);
        }
    }

    m_activeRequests.clear();
    m_accumulatedResponses.clear();
    m_awaitingContinuation.clear();

    LOG_MESSAGE("All requests cancelled and state cleared");
}

void ClientInterface::handleLLMResponse(const QString &response, const QJsonObject &request)
{
    const auto message = response.trimmed();

    if (!message.isEmpty()) {
        QString messageId = request["id"].toString();
        m_chatModel->addMessage(message, ChatModel::ChatRole::Assistant, messageId);
    }
}

QString ClientInterface::getCurrentFileContext() const
{
    auto currentEditor = Core::EditorManager::currentEditor();
    if (!currentEditor) {
        LOG_MESSAGE("No active editor found");
        return QString();
    }

    auto textDocument = qobject_cast<TextEditor::TextDocument *>(currentEditor->document());
    if (!textDocument) {
        LOG_MESSAGE("Current document is not a text document");
        return QString();
    }

    QString fileInfo = QString("Language: %1\nFile: %2\n\n")
                           .arg(textDocument->mimeType(), textDocument->filePath().toFSPathString());

    QString content = textDocument->document()->toPlainText();

    LOG_MESSAGE(QString("Got context from file: %1").arg(textDocument->filePath().toFSPathString()));

    return QString("Current file context:\n%1\nFile content:\n%2").arg(fileInfo, content);
}

QString ClientInterface::getSystemPromptWithLinkedFiles(
    const QString &basePrompt, const QList<QString> &linkedFiles) const
{
    QString updatedPrompt = basePrompt;

    if (!linkedFiles.isEmpty()) {
        updatedPrompt += "\n\nLinked files for reference:\n";

        auto contentFiles = m_contextManager->getContentFiles(linkedFiles);
        for (const auto &file : contentFiles) {
            updatedPrompt += QString("\nFile: %1\nContent:\n%2\n").arg(file.filename, file.content);
        }
    }

    return updatedPrompt;
}

Context::ContextManager *ClientInterface::contextManager() const
{
    return m_contextManager;
}

void ClientInterface::handlePartialResponse(const QString &requestId, const QString &partialText)
{
    auto it = m_activeRequests.find(requestId);
    if (it == m_activeRequests.end())
        return;

    if (m_awaitingContinuation.remove(requestId)) {
        m_accumulatedResponses[requestId].clear();
        LOG_MESSAGE(
            QString("Cleared accumulated responses for continuation request %1").arg(requestId));
    }

    m_accumulatedResponses[requestId] += partialText;

    const RequestContext &ctx = it.value();
    handleLLMResponse(m_accumulatedResponses[requestId], ctx.originalRequest);
}

void ClientInterface::handleFullResponse(const QString &requestId, const QString &fullText)
{
    auto it = m_activeRequests.find(requestId);
    if (it == m_activeRequests.end())
        return;

    const RequestContext &ctx = it.value();

    QString finalText = !fullText.isEmpty() ? fullText : m_accumulatedResponses[requestId];

    QString applyError;
    bool applySuccess
        = Context::ChangesManager::instance().applyPendingEditsForRequest(requestId, &applyError);

    if (!applySuccess) {
        LOG_MESSAGE(QString("Some edits for request %1 were not auto-applied: %2")
                        .arg(requestId, applyError));
    }

    LOG_MESSAGE(
        "Message completed. Final response for message " + ctx.originalRequest["id"].toString()
        + ": " + finalText);
    emit messageReceivedCompletely();

    m_activeRequests.erase(it);
    m_accumulatedResponses.remove(requestId);
    m_awaitingContinuation.remove(requestId);
}

void ClientInterface::handleRequestFailed(const QString &requestId, const QString &error)
{
    auto it = m_activeRequests.find(requestId);
    if (it == m_activeRequests.end())
        return;

    LOG_MESSAGE(QString("Chat request %1 failed: %2").arg(requestId, error));
    qDebug() << "Chat request " << requestId
             << "failed: " << error
             << "\n original Request:" << it.value().originalRequest;
    emit errorOccurred(error);

    m_activeRequests.erase(it);
    m_accumulatedResponses.remove(requestId);
    m_awaitingContinuation.remove(requestId);
}

void ClientInterface::handleThinkingBlockReceived(
    const QString &requestId, const QString &thinking, const QString &signature)
{
    if (!m_activeRequests.contains(requestId)) {
        LOG_MESSAGE(QString("Ignoring thinking block for non-chat request: %1").arg(requestId));
        return;
    }

    if (m_awaitingContinuation.remove(requestId)) {
        m_accumulatedResponses[requestId].clear();
        LOG_MESSAGE(
            QString("Cleared accumulated responses for continuation request %1").arg(requestId));
    }

    if (thinking.isEmpty()) {
        m_chatModel->addRedactedThinkingBlock(requestId, signature);
    } else {
        m_chatModel->addThinkingBlock(requestId, thinking, signature);
    }
}

void ClientInterface::handleToolExecutionStarted(
    const QString &requestId, const QString &toolId, const QString &toolName)
{
    if (!m_activeRequests.contains(requestId)) {
        LOG_MESSAGE(QString("Ignoring tool execution start for non-chat request: %1").arg(requestId));
        return;
    }

    m_chatModel->addToolExecutionStatus(requestId, toolId, toolName);
    m_awaitingContinuation.insert(requestId);
}

void ClientInterface::handleToolExecutionCompleted(
    const QString &requestId,
    const QString &toolId,
    const QString &toolName,
    const QString &toolOutput)
{
    if (!m_activeRequests.contains(requestId)) {
        LOG_MESSAGE(QString("Ignoring tool execution result for non-chat request: %1").arg(requestId));
        return;
    }

    m_chatModel->updateToolResult(requestId, toolId, toolName, toolOutput);
}

bool ClientInterface::isImageFile(const QString &filePath) const
{
    static const QSet<QString> imageExtensions = {"png", "jpg", "jpeg", "gif", "webp", "bmp", "svg"};

    QFileInfo fileInfo(filePath);
    QString extension = fileInfo.suffix().toLower();

    return imageExtensions.contains(extension);
}

QString ClientInterface::getMediaTypeForImage(const QString &filePath) const
{
    static const QHash<QString, QString> mediaTypes
        = {{"png", "image/png"},
           {"jpg", "image/jpeg"},
           {"jpeg", "image/jpeg"},
           {"gif", "image/gif"},
           {"webp", "image/webp"},
           {"bmp", "image/bmp"},
           {"svg", "image/svg+xml"}};

    QFileInfo fileInfo(filePath);
    QString extension = fileInfo.suffix().toLower();

    if (mediaTypes.contains(extension)) {
        return mediaTypes[extension];
    }

    QMimeDatabase mimeDb;
    QMimeType mimeType = mimeDb.mimeTypeForFile(filePath);
    return mimeType.name();
}

QString ClientInterface::encodeImageToBase64(const QString &filePath) const
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        LOG_MESSAGE(QString("Failed to open image file: %1").arg(filePath));
        return QString();
    }

    QByteArray imageData = file.readAll();
    file.close();

    return imageData.toBase64();
}

QVector<PluginLLMCore::ImageAttachment> ClientInterface::loadImagesFromStorage(
    const QList<ChatModel::ImageAttachment> &storedImages) const
{
    QVector<PluginLLMCore::ImageAttachment> apiImages;

    for (const auto &storedImage : std::as_const(storedImages)) {
        QString base64Data
            = ChatSerializer::loadContentFromStorage(m_chatFilePath, storedImage.storedPath);
        if (base64Data.isEmpty()) {
            LOG_MESSAGE(QString("Warning: Failed to load image: %1").arg(storedImage.storedPath));
            continue;
        }

        PluginLLMCore::ImageAttachment apiImage;
        apiImage.data = base64Data;
        apiImage.mediaType = storedImage.mediaType;
        apiImage.isUrl = false;

        apiImages.append(apiImage);
    }

    return apiImages;
}

void ClientInterface::setChatFilePath(const QString &filePath)
{
    if (!m_chatFilePath.isEmpty() && m_chatFilePath != filePath) {
        const auto providerName = Settings::generalSettings().caProvider();
        auto *provider = PluginLLMCore::ProvidersManager::instance().getProviderByName(providerName);

        if (provider
            && provider->capabilities().testFlag(PluginLLMCore::ProviderCapability::Tools)
            && provider->toolsManager()) {
            if (auto *todoTool = qobject_cast<QodeAssist::Tools::TodoTool *>(
                    provider->toolsManager()->tool("todo_tool"))) {
                todoTool->clearSession(m_chatFilePath);
            }
        }
    }

    m_chatFilePath = filePath;
    m_chatModel->setChatFilePath(filePath);
}

QString ClientInterface::chatFilePath() const
{
    return m_chatFilePath;
}

} // namespace QodeAssist::Chat
