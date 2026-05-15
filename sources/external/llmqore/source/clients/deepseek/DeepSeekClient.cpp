// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/DeepSeekClient.hpp>

#include <QJsonArray>
#include <QJsonDocument>

#include "clients/openai/OpenAIMessage.hpp"
#include <LLMQore/HttpClient.hpp>
#include <LLMQore/Log.hpp>

namespace LLMQore {

DeepSeekClient::DeepSeekClient(QObject *parent)
    : DeepSeekClient({}, {}, {}, parent)
{}

DeepSeekClient::DeepSeekClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : OpenAIClient(url, apiKey, model, parent)
{}

RequestID DeepSeekClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    QJsonObject request = payload;
    request["stream"] = (mode == RequestMode::Streaming);

    const RequestID id = createRequest();
    const QString resolved = endpoint.isEmpty() ? QStringLiteral("/chat/completions") : endpoint;

    qCDebug(llmDeepSeekLog).noquote()
        << QString("Sending request %1 to %2%3").arg(id, m_url, resolved);

    sendRequest(id, QUrl(m_url + resolved), request, mode);
    return id;
}

QFuture<QList<QString>> DeepSeekClient::listModels()
{
    QUrl url(m_url + "/models");
    QNetworkRequest request = prepareNetworkRequest(url);

    return httpClient()
        ->send(request, QByteArrayView("GET"))
        .then(this, [](const HttpResponse &response) {
            QList<QString> models;
            if (!response.isSuccess())
                return models;

            const QJsonObject json = QJsonDocument::fromJson(response.body).object();
            if (json.contains("data")) {
                const QJsonArray modelArray = json["data"].toArray();
                for (const QJsonValue &value : modelArray) {
                    const QJsonObject modelObject = value.toObject();
                    if (modelObject.contains("id"))
                        models.append(modelObject["id"].toString());
                }
            }
            return models;
        })
        .onFailed(this, [](const std::exception &) { return QList<QString>{}; });
}

void DeepSeekClient::cleanupDerivedData(const RequestID &id)
{
    // Emit any pending thinking blocks before the message is destroyed.
    // This ensures the full accumulated reasoning content reaches the UI
    // even if the final stream chunk(s) carried no reasoning delta.
    notifyPendingThinkingBlocks(id);
    OpenAIClient::cleanupDerivedData(id);
}

void DeepSeekClient::processStreamChunk(const RequestID &id, const QJsonObject &chunk)
{
    QJsonArray choices = chunk["choices"].toArray();
    if (choices.isEmpty())
        return;

    QJsonObject choice = choices[0].toObject();
    QJsonObject delta = choice["delta"].toObject();
    QString finishReason = choice["finish_reason"].toString();

    OpenAIMessage *message = m_messages.value(id);
    if (!message) {
        message = new OpenAIMessage(this);
        m_messages[id] = message;
        qCDebug(llmDeepSeekLog).noquote()
            << QString("Created OpenAIMessage for request %1").arg(id);
    } else if (message->state() == MessageState::RequiresToolExecution) {
        // Save accumulated thinking before continuation clears all blocks.
        // Without this, all pre-tool reasoning content is lost and the UI
        // would only show post-tool thinking — a much smaller fragment.
        QString savedThinking;
        auto oldThinkingBlocks = message->getCurrentThinkingContent();
        for (auto *block : oldThinkingBlocks) {
            if (!block->thinking().trimmed().isEmpty())
                savedThinking += block->thinking();
        }

        message->startNewContinuation();
        qCDebug(llmDeepSeekLog).noquote()
            << QString("Starting continuation for request %1").arg(id);

        // Restore old thinking into the fresh continuation state so that
        // new reasoning deltas are appended to the complete accumulated content.
        if (!savedThinking.isEmpty()) {
            message->handleReasoningDelta(savedThinking);
        }
    }

    // Handle reasoning_content (DeepSeek-specific field)
    // DeepSeek sends incremental deltas in each streaming chunk, so we must
    // accumulate them via handleReasoningDelta() and then emit the FULL
    // accumulated content from the message object — not just the raw delta.
    if (delta.contains("reasoning_content") && !delta["reasoning_content"].isNull()) {
        QString reasoning = delta["reasoning_content"].toString();
        if (!reasoning.isEmpty()) {
            message->handleReasoningDelta(reasoning);
            auto thinkingBlocks = message->getCurrentThinkingContent();
            QString fullReasoning;
            for (auto *block : thinkingBlocks)
                fullReasoning += block->thinking();
            emit thinkingBlockReceived(id, fullReasoning, "");
        }
    }

    if (delta.contains("content") && !delta["content"].isNull()) {
        QString content = delta["content"].toString();
        message->handleContentDelta(content);
        addChunk(id, content);
    }

    if (delta.contains("tool_calls")) {
        // qDebug() << __FILE__ << __LINE__ << __FUNCTION__ << "receive tool call:\n" << chunk;
        QJsonArray toolCalls = delta["tool_calls"].toArray();
        for (const auto &toolCallValue : toolCalls) {
            QJsonObject toolCall = toolCallValue.toObject();
            int index = toolCall["index"].toInt();

            if (toolCall.contains("id")) {
                QString toolId = toolCall["id"].toString();
                QJsonObject function = toolCall["function"].toObject();
                QString name = function["name"].toString();
                message->handleToolCallStart(index, toolId, name);
            }

            if (toolCall.contains("function")) {
                QJsonObject function = toolCall["function"].toObject();
                if (function.contains("arguments"))
                    message->handleToolCallDelta(index, function["arguments"].toString());
            }
        }
    }

    if (!finishReason.isEmpty() && finishReason != "null") {
        message->completeAllPendingToolCalls();
        message->handleFinishReason(finishReason);
        executeToolsFromMessage(id);
    }
}

void DeepSeekClient::processBufferedResponse(const RequestID &id, const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        failRequest(id, QStringLiteral("Invalid JSON in buffered response"));
        return;
    }

    QJsonObject response = doc.object();

    if (response["error"].isObject()) {
        QJsonObject error = response["error"].toObject();
        failRequest(id, error["message"].toString());
        return;
    }

    QJsonArray choices = response["choices"].toArray();
    if (choices.isEmpty()) {
        failRequest(id, QStringLiteral("Empty choices in buffered response"));
        return;
    }

    QJsonObject choice = choices[0].toObject();
    QJsonObject messageObj = choice["message"].toObject();
    QString finishReason = choice["finish_reason"].toString();

    auto *message = new OpenAIMessage(this);
    m_messages[id] = message;

    // Handle reasoning_content (DeepSeek-specific field)
    if (messageObj.contains("reasoning_content") && !messageObj["reasoning_content"].isNull()) {
        QString reasoning = messageObj["reasoning_content"].toString();
        if (!reasoning.isEmpty()) {
            message->handleReasoningDelta(reasoning);
            // Non-streaming: the full reasoning is available in one shot
            emit thinkingBlockReceived(id, reasoning, "");
        }
    }

    QString content = messageObj["content"].toString();
    if (!content.isEmpty()) {
        message->handleContentDelta(content);
        addChunk(id, content);
    }

    if (messageObj.contains("tool_calls")) {
        QJsonArray toolCalls = messageObj["tool_calls"].toArray();
        for (int i = 0; i < toolCalls.size(); ++i) {
            QJsonObject toolCall = toolCalls[i].toObject();
            QString toolId = toolCall["id"].toString();
            QJsonObject function = toolCall["function"].toObject();
            QString name = function["name"].toString();
            QString arguments = function["arguments"].toString();

            message->handleToolCallStart(i, toolId, name);
            message->handleToolCallDelta(i, arguments);
            message->handleToolCallComplete(i);
        }
    }

    if (!finishReason.isEmpty()) {
        message->handleFinishReason(finishReason);
        executeToolsFromMessage(id);
    }
}

} // namespace LLMQore
