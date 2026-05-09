// Copyright (C) 2024-2026 Petr Mironychev
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ContentFile.hpp"
#include <QList>
#include <QRegularExpression>
#include <QString>

namespace QodeAssist::Context {

/**
 * @brief Tokenizer类型枚举，用于指定不同的token计算策略
 */
enum class TokenizerType {
    Auto,       /**< 自动检测文本类型并选择最佳策略 */
    GPT4,       /**< GPT-4 / GPT-3.5 使用 cl100k_base tokenizer */
    Claude,     /**< Claude 系列模型 tokenizer */
    Chinese,    /**< 中文优化模型 (如 ChatGLM、Qwen 等) */
    CodeLlama   /**< 代码专用模型 (CodeLlama、StarCoder 等) */
};

class TokenUtils
{
public:
    /**
     * @brief 估算文本中的token数量
     * @param text 输入文本
     * @param tokenizerType Tokenizer类型，默认为Auto
     * @return 估算的token数量
     */
    static int estimateTokens(const QString &text,
                              TokenizerType tokenizerType);

    /**
     * @brief 向后兼容的简化版本（默认使用Auto策略）
     */
    static int estimateTokens(const QString &text);

    static int estimateFileTokens(const Context::ContentFile &file,
                                  TokenizerType tokenizerType = TokenizerType::Auto);
    static int estimateFilesTokens(const QList<Context::ContentFile> &files,
                                   TokenizerType tokenizerType = TokenizerType::Auto);

private:
    // 各模型专用token估算方法
    static int estimateTokensCl100k(const QString &text);
    static int estimateTokensClaude(const QString &text);
    static int estimateTokensChinese(const QString &text);
    static int estimateTokensCode(const QString &text);

    // 辅助检测方法
    static double detectChineseRatio(const QString &text);
    static bool isCodeLike(const QString &text);
    static bool isCommonCodeKeyword(const QString &word);
};

} // namespace QodeAssist::Context
