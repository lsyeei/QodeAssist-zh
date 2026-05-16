// Copyright (C) 2024-2026 Petr Mironychev
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AlibabaProvider.hpp"

#include <LLMQore/ToolsManager.hpp>
#include "logger/Logger.hpp"
#include "settings/ChatAssistantSettings.hpp"
#include "settings/CodeCompletionSettings.hpp"
#include "settings/QuickRefactorSettings.hpp"
#include "settings/GeneralSettings.hpp"
#include "settings/ProviderSettings.hpp"
#include "tools/ToolsRegistration.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace QodeAssist::Providers {

AlibabaProvider::AlibabaProvider(QObject *parent)
    : PluginLLMCore::Provider(parent)
    , m_client(new ::LLMQore::AlibabaClient(QString(), QString(), QString(), this))
{
    Tools::registerQodeAssistTools(m_client->tools());
}

QString AlibabaProvider::name() const
{
    return "Alibaba";
}

QString AlibabaProvider::apiKey() const
{
    return Settings::providerSettings().alibabaApiKey();
}

QString AlibabaProvider::url() const
{
    return "https://dashscope.aliyuncs.com/compatible-mode/v1";
}

void AlibabaProvider::prepareRequest(
    QJsonObject &request,
    PluginLLMCore::PromptTemplate *prompt,
    PluginLLMCore::ContextData context,
    PluginLLMCore::RequestType type,
    bool isToolsEnabled,
    bool isThinkingEnabled)
{
    if (!prompt->isSupportProvider(providerID())) {
        LOG_MESSAGE(QString("Template %1 doesn't support %2 provider").arg(name(), prompt->name()));
    }

    prompt->prepareRequest(request, context);

    auto applyModelParams = [&request](const auto &settings) {
        request["max_tokens"] = settings.maxTokens();
        request["temperature"] = settings.temperature();

        if (settings.useTopP())
            request["top_p"] = settings.topP();
        if (settings.useTopK())
            request["top_k"] = settings.topK();
        if (settings.useFrequencyPenalty())
            request["frequency_penalty"] = settings.frequencyPenalty();
        if (settings.usePresencePenalty())
            request["presence_penalty"] = settings.presencePenalty();
    };

    if (type == PluginLLMCore::RequestType::CodeCompletion) {
        applyModelParams(Settings::codeCompletionSettings());
    } else if (type == PluginLLMCore::RequestType::QuickRefactoring) {
        applyModelParams(Settings::quickRefactorSettings());
    } else {
        applyModelParams(Settings::chatAssistantSettings());
    }

    if (isToolsEnabled) {
        auto toolsDefinitions = m_client->tools()->getToolsDefinitions();
        if (!toolsDefinitions.isEmpty()) {
            request["tools"] = toolsDefinitions;
            LOG_MESSAGE(QString("Added %1 tools to Alibaba request").arg(toolsDefinitions.size()));
        }
    }
}

QFuture<QList<QString>> AlibabaProvider::getInstalledModels(const QString &baseUrl)
{
    m_client->setUrl(baseUrl);
    m_client->setApiKey(apiKey());
    return m_client->listModels();
}

PluginLLMCore::ProviderID AlibabaProvider::providerID() const
{
    return PluginLLMCore::ProviderID::Alibaba;
}

PluginLLMCore::ProviderCapabilities AlibabaProvider::capabilities() const
{
    return PluginLLMCore::ProviderCapability::Tools | PluginLLMCore::ProviderCapability::Thinking
           | PluginLLMCore::ProviderCapability::Image
           | PluginLLMCore::ProviderCapability::ModelListing;
}

::LLMQore::BaseClient *AlibabaProvider::client() const
{
    return m_client;
}

} // namespace QodeAssist::Providers