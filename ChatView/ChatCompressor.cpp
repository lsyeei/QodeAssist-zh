// Copyright (C) 2024-2026 Petr Mironychev
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ChatCompressor.hpp"

#include <LLMQore/BaseClient.hpp>
#include "ChatModel.hpp"
#include "GeneralSettings.hpp"
#include "PromptTemplateManager.hpp"
#include "ProvidersManager.hpp"
#include "logger/Logger.hpp"

#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>

namespace QodeAssist::Chat {

ChatCompressor::ChatCompressor(QObject *parent)
    : QObject(parent)
{}

void ChatCompressor::startCompression(const QString &chatFilePath, ChatModel *chatModel)
{
    if (m_isCompressing) {
        emit compressionFailed(tr("Compression already in progress"));
        return;
    }

    if (chatFilePath.isEmpty()) {
        emit compressionFailed(tr("No chat file to compress"));
        return;
    }

    if (!chatModel || chatModel->rowCount() == 0) {
        emit compressionFailed(tr("Chat is empty, nothing to compress"));
        return;
    }

    auto providerName = Settings::generalSettings().caProvider();
    m_provider = PluginLLMCore::ProvidersManager::instance().getProviderByName(providerName);

    if (!m_provider) {
        emit compressionFailed(tr("No provider available"));
        return;
    }

    auto templateName = Settings::generalSettings().caTemplate();
    auto promptTemplate = PluginLLMCore::PromptTemplateManager::instance().getChatTemplateByName(
        templateName);

    if (!promptTemplate) {
        emit compressionFailed(tr("No template available"));
        return;
    }

    m_isCompressing = true;
    m_chatModel = chatModel;
    m_originalChatPath = chatFilePath;
    m_accumulatedSummary.clear();

    emit compressionStarted();

    connectProviderSignals();

    QJsonObject payload{
        {"model", Settings::generalSettings().caModel()}, {"stream", true}};

    buildRequestPayload(payload, promptTemplate);

    const QString customEndpoint = Settings::generalSettings().caCustomEndpoint();
    const QString endpoint = !customEndpoint.isEmpty() ? customEndpoint
                                                       : promptTemplate->endpoint();
    m_currentRequestId = m_provider->sendRequest(
        QUrl(Settings::generalSettings().caUrl()), payload, endpoint);
    LOG_MESSAGE(QString("Starting compression request: %1").arg(m_currentRequestId));
}

QString ChatCompressor::compressHistorySync(ChatModel *chatModel)
{
    if (m_isCompressing) {
        LOG_MESSAGE("Compression already in progress");
        return {};
    }

    if (!chatModel || chatModel->rowCount() == 0) {
        LOG_MESSAGE("Chat is empty, nothing to compress");
        return {};
    }

    auto providerName = Settings::generalSettings().caProvider();
    m_provider = PluginLLMCore::ProvidersManager::instance().getProviderByName(providerName);

    if (!m_provider) {
        LOG_MESSAGE("No provider available for compression");
        return {};
    }

    auto templateName = Settings::generalSettings().caTemplate();
    auto promptTemplate = PluginLLMCore::PromptTemplateManager::instance().getChatTemplateByName(
        templateName);

    if (!promptTemplate) {
        LOG_MESSAGE("No template available for compression");
        return {};
    }

    m_isCompressing = true;
    m_chatModel = chatModel;
    m_accumulatedSummary.clear();

    QJsonObject payload{
        {"model", Settings::generalSettings().caModel()}, {"stream", true}};

    buildRequestPayload(payload, promptTemplate);

    const QString customEndpoint = Settings::generalSettings().caCustomEndpoint();
    const QString endpoint = !customEndpoint.isEmpty() ? customEndpoint
                                                       : promptTemplate->endpoint();
    // qDebug() << __FUNCTION__ << "压缩内容：" << payload;
    // 同步等待压缩完成
    bool success = false;
    QEventLoop loop;

    auto *client = m_provider->client();

    QMetaObject::Connection chunkConn = connect(
        client,
        &::LLMQore::BaseClient::chunkReceived,
        this,
        [this](const QString &requestId, const QString &partialText) {
            if (requestId == m_currentRequestId)
                m_accumulatedSummary += partialText;
        });

    QMetaObject::Connection doneConn = connect(
        client,
        &::LLMQore::BaseClient::requestCompleted,
        this,
        [&](const QString &requestId, const QString &) {
            if (requestId == m_currentRequestId) {
                success = true;
                loop.quit();
            }
        });

    QMetaObject::Connection failConn = connect(
        client,
        &::LLMQore::BaseClient::requestFailed,
        this,
        [&](const QString &requestId, const QString &) {
            if (requestId == m_currentRequestId)
                loop.quit();
        });

    m_currentRequestId = m_provider->sendRequest(
        QUrl(Settings::generalSettings().caUrl()), payload, endpoint);

    if (m_currentRequestId.isEmpty()) {
        disconnect(chunkConn);
        disconnect(doneConn);
        disconnect(failConn);
        cleanupState();
        LOG_MESSAGE("Failed to send compression request");
        return {};
    }

    LOG_MESSAGE(QString("Sync compression started, requestId: %1").arg(m_currentRequestId));

    // 阻塞等待压缩完成（事件循环会处理网络回调）
    loop.exec();

    disconnect(chunkConn);
    disconnect(doneConn);
    disconnect(failConn);

    QString summary = m_accumulatedSummary;
    int splitIdx = m_splitIndex;
    cleanupState();

    if (!success || summary.isEmpty()) {
        LOG_MESSAGE("Sync compression failed or returned empty summary");
        return {};
    }

    // 设置回splitIndex供调用方使用
    m_splitIndex = splitIdx;

    LOG_MESSAGE(QString("Sync compression completed, summary length: %1").arg(summary.length()));
    return summary;
}

bool ChatCompressor::isCompressing() const
{
    return m_isCompressing;
}

void ChatCompressor::cancelCompression()
{
    if (!m_isCompressing)
        return;

    LOG_MESSAGE("Cancelling compression request");

    if (m_provider && !m_currentRequestId.isEmpty())
        m_provider->cancelRequest(m_currentRequestId);

    cleanupState();
    emit compressionFailed(tr("Compression cancelled"));
}

void ChatCompressor::onPartialResponseReceived(const QString &requestId, const QString &partialText)
{
    if (!m_isCompressing || requestId != m_currentRequestId)
        return;

    m_accumulatedSummary += partialText;
}

void ChatCompressor::onFullResponseReceived(const QString &requestId, const QString &fullText)
{
    Q_UNUSED(fullText)

    if (!m_isCompressing || requestId != m_currentRequestId)
        return;

    LOG_MESSAGE(
        QString("Received summary, length: %1 characters").arg(m_accumulatedSummary.length()));

    QString compressedPath = createCompressedChatPath(m_originalChatPath);
    if (!createCompressedChatFile(m_originalChatPath, compressedPath, m_accumulatedSummary)) {
        handleCompressionError(tr("Failed to save compressed chat"));
        return;
    }

    LOG_MESSAGE(QString("Compression completed: %1").arg(compressedPath));
    cleanupState();
    emit compressionCompleted(compressedPath);
}

void ChatCompressor::onRequestFailed(const QString &requestId, const QString &error)
{
    if (!m_isCompressing || requestId != m_currentRequestId)
        return;

    LOG_MESSAGE(QString("Compression request failed: %1").arg(error));
    handleCompressionError(tr("Compression failed: %1").arg(error));
}

void ChatCompressor::handleCompressionError(const QString &error)
{
    cleanupState();
    emit compressionFailed(error);
}

QString ChatCompressor::createCompressedChatPath(const QString &originalPath) const
{
    QFileInfo fileInfo(originalPath);
    QString hash = QString::number(QDateTime::currentMSecsSinceEpoch() % 100000, 16);
    return QString("%1/%2_%3.%4")
        .arg(fileInfo.absolutePath(), fileInfo.completeBaseName(), hash, fileInfo.suffix());
}

QString ChatCompressor::buildCompressionPrompt() const
{
    return QStringLiteral(
        "Please create a comprehensive summary of our entire conversation above. "
        "The summary should:\n"
        "1. Preserve all important context, decisions, and key information\n"
        "2. Maintain technical details, code snippets, file references, and specific examples\n"
        "3. Keep the chronological flow of the discussion\n"
        "4. Be significantly shorter than the original (aim for 30-40% of original length)\n"
        "5. Be written in clear, structured format\n"
        "6. Use markdown formatting for better readability\n\n"
        "Create the summary now:");
}

void ChatCompressor::buildRequestPayload(
    QJsonObject &payload, PluginLLMCore::PromptTemplate *promptTemplate)
{
    PluginLLMCore::ContextData context;

    context.systemPrompt = QStringLiteral(
        "You are a helpful assistant that creates concise summaries of conversations. "
        "Your summaries preserve key information, technical details, and the flow of discussion.");

    auto history = m_chatModel->getChatHistory();

    // Find split point: keep the last 2 user messages (and their corresponding responses)
    m_splitIndex = 0;
    int userCount = 0;
    for (int i = history.size() - 1; i >= 0; --i) {
        if (history[i].role == ChatModel::ChatRole::User) {
            userCount++;
            if (userCount == 2) {
                m_splitIndex = i;
                break;
            }
        }
    }

    // Only send messages before the split point to LLM for compression
    QVector<PluginLLMCore::Message> messages;
    for (int i = 0; i < m_splitIndex; ++i) {
        const auto &msg = history[i];
        if (msg.role == ChatModel::ChatRole::Tool
            || msg.role == ChatModel::ChatRole::FileEdit
            || msg.role == ChatModel::ChatRole::Thinking)
            continue;

        PluginLLMCore::Message apiMessage;
        apiMessage.role = (msg.role == ChatModel::ChatRole::User) ? "user" : "assistant";
        apiMessage.content = msg.content;
        messages.append(apiMessage);
    }

    PluginLLMCore::Message compressionRequest;
    compressionRequest.role = "user";
    compressionRequest.content = buildCompressionPrompt();
    messages.append(compressionRequest);

    context.history = messages;

    m_provider->prepareRequest(
        payload, promptTemplate, context, PluginLLMCore::RequestType::Chat, false, false);
}

bool ChatCompressor::createCompressedChatFile(
    const QString &sourcePath, const QString &destPath, const QString &summary)
{
    QFile sourceFile(sourcePath);
    if (!sourceFile.open(QIODevice::ReadOnly)) {
        LOG_MESSAGE(QString("Failed to open source chat file: %1").arg(sourcePath));
        return false;
    }

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(sourceFile.readAll(), &parseError);
    sourceFile.close();

    if (doc.isNull() || !doc.isObject()) {
        LOG_MESSAGE(QString("Invalid JSON in chat file: %1 (Error: %2)")
                        .arg(sourcePath, parseError.errorString()));
        return false;
    }

    QJsonObject root = doc.object();

    QJsonObject summaryMessage;
    summaryMessage["role"] = "assistant";
    summaryMessage["content"] = QString("# Chat Summary\n\n%1").arg(summary);
    summaryMessage["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    summaryMessage["isRedacted"] = false;
    summaryMessage["attachments"] = QJsonArray();
    summaryMessage["images"] = QJsonArray();

    QJsonArray newMessages;
    newMessages.append(summaryMessage);

    // Keep recent messages (last 2 rounds) from the original chat
    if (m_splitIndex > 0) {
        QJsonArray originalMessages = root["messages"].toArray();
        for (int i = m_splitIndex; i < originalMessages.size(); ++i) {
            newMessages.append(originalMessages[i].toObject());
        }
    }

    root["messages"] = newMessages;

    if (QFile::exists(destPath))
        QFile::remove(destPath);

    QFile destFile(destPath);
    if (!destFile.open(QIODevice::WriteOnly)) {
        LOG_MESSAGE(QString("Failed to create compressed chat file: %1").arg(destPath));
        return false;
    }

    destFile.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

void ChatCompressor::connectProviderSignals()
{
    auto *c = m_provider->client();

    m_connections.append(connect(
        c,
        &::LLMQore::BaseClient::chunkReceived,
        this,
        &ChatCompressor::onPartialResponseReceived,
        Qt::UniqueConnection));

    m_connections.append(connect(
        c,
        &::LLMQore::BaseClient::requestCompleted,
        this,
        &ChatCompressor::onFullResponseReceived,
        Qt::UniqueConnection));

    m_connections.append(connect(
        c,
        &::LLMQore::BaseClient::requestFailed,
        this,
        &ChatCompressor::onRequestFailed,
        Qt::UniqueConnection));
}

void ChatCompressor::disconnectAllSignals()
{
    for (const auto &connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
}

void ChatCompressor::cleanupState()
{
    disconnectAllSignals();

    m_isCompressing = false;
    m_currentRequestId.clear();
    m_originalChatPath.clear();
    m_accumulatedSummary.clear();
    m_chatModel = nullptr;
    m_provider = nullptr;
    m_splitIndex = 0;
}

} // namespace QodeAssist::Chat
