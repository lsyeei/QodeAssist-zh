// Copyright (C) 2026 Petr Mironychev
// SPDX-License-Identifier: MIT

#pragma once

#include <LLMQore/OpenAIClient.hpp>

namespace LLMQore {

class LLMQORE_EXPORT GLMClient : public OpenAIClient
{
    Q_OBJECT
public:
    explicit GLMClient(QObject *parent = nullptr);
    explicit GLMClient(
        const QString &url, const QString &apiKey, const QString &model, QObject *parent = nullptr);

    RequestID sendMessage(
        const QJsonObject &payload,
        const QString &endpoint = {},
        RequestMode mode = RequestMode::Streaming) override;

    QFuture<QList<QString>> listModels() override;

protected:
    void processStreamChunk(const RequestID &id, const QJsonObject &chunk) override;
    void processBufferedResponse(const RequestID &id, const QByteArray &data) override;
    void cleanupDerivedData(const RequestID &id) override;
};

} // namespace LLMQore
