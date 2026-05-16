
// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#include <LLMQore/TencentClient.hpp>

#include <QJsonArray>
#include <QJsonDocument>

#include "clients/openai/OpenAIMessage.hpp"
#include <LLMQore/HttpClient.hpp>
#include <LLMQore/Log.hpp>

namespace LLMQore {

TencentClient::TencentClient(QObject *parent)
    : TencentClient({}, {}, {}, parent)
{}

TencentClient::TencentClient(
    const QString &url, const QString &apiKey, const QString &model, QObject *parent)
    : OpenAIClient(url, apiKey, model, parent)
{}

RequestID TencentClient::sendMessage(
    const QJsonObject &payload, const QString &endpoint, RequestMode mode)
{
    QJsonObject request = payload;
    request["stream"] = (mode == RequestMode::Streaming);

    const RequestID id = createRequest();
    const QString resolved = endpoint.isEmpty() ? QStringLiteral("/chat/completions") : endpoint;

    qCDebug(llmTencentLog).noquote()
        << QString("Sending request %1 to %2%3").arg(id, m_url, resolved);

    sendRequest(id, QUrl(m_url + resolved), request, mode);
    return id;
}

QFuture<QList<QString>> TencentClient::listModels()
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

void TencentClient::cleanupDerivedData(const RequestID &id)
{
    // Emit any pending thinking blocks before the message is destroyed.
    notifyPendingThinkingBlocks(id);
    OpenAIClient::cleanupDerivedData(id);
}

void TencentClient::processStreamChunk(const RequestID &id, const QJsonObject &chunk)
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
        qCDebug(llmTencentLog).noquote()
            << QString("Created OpenAIMessage for request %1").arg(id);
    } else if (message->state() == MessageState::RequiresToolExecution) {
        message->startNewContinuation();
        qCDebug(llmTencentLog).noquote()
            << QString("Starting continuation for request %1").arg(id);
    }

    // Handle reasoning_content (Tencent-specific field, if any)
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

void TencentClient::processBufferedResponse(const RequestID &id, const QByteArray &data)
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

    // Handle reasoning_content (Tencent-specific field, if any)
    if (messageObj.contains("reasoning_content") && !messageObj["reasoning_content"].isNull()) {
        QString reasoning = messageObj["reasoning_content"].toString();
        if (!reasoning.isEmpty()) {
            message->handleReasoningDelta(reasoning);
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
