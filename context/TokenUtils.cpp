// Copyright (C) 2024-2026 Petr Mironychev
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TokenUtils.hpp"

#include <QSet>

namespace QodeAssist::Context {

int TokenUtils::estimateTokens(const QString &text, TokenizerType tokenizerType)
{
    if (text.isEmpty()) {
        return 0;
    }

    // Auto模式下进行启发式检测
    if (tokenizerType == TokenizerType::Auto) {
        double chineseRatio = detectChineseRatio(text);
        if (chineseRatio > 0.3) {
            tokenizerType = TokenizerType::Chinese;
        } else if (isCodeLike(text)) {
            tokenizerType = TokenizerType::CodeLlama;
        } else {
            tokenizerType = TokenizerType::GPT4; // 默认使用GPT4策略
        }
    }

    switch (tokenizerType) {
    case TokenizerType::GPT4:
        return estimateTokensCl100k(text);
    case TokenizerType::Claude:
        return estimateTokensClaude(text);
    case TokenizerType::Chinese:
        return estimateTokensChinese(text);
    case TokenizerType::CodeLlama:
        return estimateTokensCode(text);
    default:
        return estimateTokensCl100k(text);
    }
}

int TokenUtils::estimateTokens(const QString &text)
{
    return estimateTokens(text, TokenizerType::Auto);
}

int TokenUtils::estimateFileTokens(const Context::ContentFile &file, TokenizerType tokenizerType)
{
    int total = 0;

    total += estimateTokens(file.filename, tokenizerType);
    total += estimateTokens(file.content, tokenizerType);
    total += 5; // 文件格式开销

    return total;
}

int TokenUtils::estimateFilesTokens(const QList<Context::ContentFile> &files,
                                    TokenizerType tokenizerType)
{
    int total = 0;
    for (const auto &file : files) {
        total += estimateFileTokens(file, tokenizerType);
    }
    return total;
}

/**
 * @brief 模拟 cl100k_base tokenizer (GPT-4/GPT-3.5使用)
 *
 * cl100k_base基于BPE算法，特点：
 * - 英文单词平均1.3个token
 * - 数字通常每个数字一个token
 * - CJK字符平均1.5-2个token
 * - 空格和标点符号通常单独token
 */
int TokenUtils::estimateTokensCl100k(const QString &text)
{
    int tokenCount = 0;
    bool inWord = false;
    bool inNumber = false;
    bool inCjk = false;

    for (const QChar &c : text) {
        ushort unicode = c.unicode();

        // CJK统一表意文字范围
        bool isCjk = (unicode >= 0x4E00 && unicode <= 0x9FFF)
                     || (unicode >= 0x3400 && unicode <= 0x4DBF)
                     || (unicode >= 0x3040 && unicode <= 0x309F)   // 平假名
                     || (unicode >= 0x30A0 && unicode <= 0x30FF)   // 片假名
                     || (unicode >= 0xAC00 && unicode <= 0xD7AF);  // 韩文

        // 数字
        bool isDigit = c.isDigit();

        // 字母
        bool isLetter = c.isLetter() && !isCjk;

        // 空白字符
        bool isSpace = c.isSpace();

        // 标点符号
        bool isPunct = c.isPunct() || c.isSymbol();

        if (isCjk) {
            if (!inCjk) {
                tokenCount++;
                inCjk = true;
            } else {
                // CJK字符平均1.5 token，累积计数
                static int cjkCounter = 0;
                cjkCounter++;
                if (cjkCounter % 2 == 0) {
                    tokenCount++;
                }
            }
            inWord = false;
            inNumber = false;
        } else if (isDigit) {
            if (!inNumber) {
                tokenCount++;
                inNumber = true;
            }
            inWord = false;
            inCjk = false;
        } else if (isLetter) {
            if (!inWord) {
                tokenCount++;
                inWord = true;
            } else {
                // 英文单词平均4字符一个token
                static int wordLen = 0;
                wordLen++;
                if (wordLen % 4 == 0) {
                    tokenCount++;
                }
            }
            inNumber = false;
            inCjk = false;
        } else if (isSpace) {
            // 空格通常作为独立token或附着token
            tokenCount++;
            inWord = false;
            inNumber = false;
            inCjk = false;
        } else if (isPunct) {
            // 标点通常单独token，但成对出现的可能合并
            tokenCount++;
            inWord = false;
            inNumber = false;
            inCjk = false;
        } else {
            tokenCount++;
            inWord = false;
            inNumber = false;
            inCjk = false;
        }
    }

    // 最小保证至少1个token
    return qMax(tokenCount, 1);
}

/**
 * @brief 模拟 Claude tokenizer
 *
 * Claude tokenizer特点与cl100k_base类似但略有不同：
 * - CJK字符处理略有差异
 * - 对长单词的切分略有不同
 */
int TokenUtils::estimateTokensClaude(const QString &text)
{
    // Claude与GPT-4非常接近，微调系数
    int baseEstimate = estimateTokensCl100k(text);

    // Claude对CJK字符处理略高效，对代码也略高效
    double chineseRatio = detectChineseRatio(text);
    double adjustment = 1.0;

    if (chineseRatio > 0.5) {
        adjustment = 0.95; // CJK文本略优化
    } else if (isCodeLike(text)) {
        adjustment = 0.92; // 代码文本优化更明显
    }

    return qMax(static_cast<int>(baseEstimate * adjustment), 1);
}

/**
 * @brief 中文优化模型的token估算
 *
 * 中文模型(如ChatGLM、Qwen)特点：
 * - CJK字符编码效率更高
 * - 中文通常1个字符约0.8-1个token
 * - 对中文词组的切分更智能
 */
int TokenUtils::estimateTokensChinese(const QString &text)
{
    int tokenCount = 0;

    for (const QChar &c : text) {
        ushort unicode = c.unicode();

        // CJK字符
        bool isCjk = (unicode >= 0x4E00 && unicode <= 0x9FFF)
                     || (unicode >= 0x3400 && unicode <= 0x4DBF)
                     || (unicode >= 0x3040 && unicode <= 0x309F)
                     || (unicode >= 0x30A0 && unicode <= 0x30FF)
                     || (unicode >= 0xAC00 && unicode <= 0xD7AF);

        if (isCjk) {
            // 中文字符约0.85 token/字
            tokenCount += 1;
        } else if (c.isLetter()) {
            // 英文单词平均4字符一个token
            tokenCount += 1;
        } else if (c.isDigit()) {
            tokenCount += 1;
        } else if (!c.isSpace()) {
            // 标点符号
            tokenCount += 1;
        }
    }

    // 调整系数：中文模型对英文词的切分更细
    QStringList words = text.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    for (const QString &word : words) {
        if (word.length() > 4) {
            // 长英文词在中文模型中可能被切分更细
            tokenCount += word.length() / 5;
        }
    }

    return qMax(tokenCount, 1);
}

/**
 * @brief 代码专用模型的token估算
 *
 * 代码模型(如CodeLlama、StarCoder)特点：
 * - 代码关键字通常单个token
 * - 标识符按subword切分
 * - 缩进和空白字符优化处理
 * - 对常见代码模式有专门优化
 */
int TokenUtils::estimateTokensCode(const QString &text)
{
    int tokenCount = 0;
    QStringList lines = text.split('\n', Qt::SkipEmptyParts);

    for (const QString &line : lines) {
        QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            tokenCount += 1; // 空行
            continue;
        }

        // 计算缩进token (代码模型通常将缩进作为独立token)
        int indentLevel = 0;
        for (const QChar &c : line) {
            if (c == ' ') {
                indentLevel++;
            } else if (c == '\t') {
                indentLevel += 4;
            } else {
                break;
            }
        }
        tokenCount += (indentLevel / 4) + (indentLevel % 4 > 0 ? 1 : 0);

        // 分割代码元素
        QStringList tokens = trimmed.split(QRegularExpression("([\\s\\(\\)\\[\\]\\{\\}\\.\\,\\;\\:\\+\\-\\*\\/\\=\\<\\>\\!\\&\\|])"),
                                           Qt::SkipEmptyParts);

        for (const QString &token : tokens) {
            QString cleanToken = token.trimmed();
            if (cleanToken.isEmpty()) {
                continue;
            }

            // 关键字通常单个token
            if (isCommonCodeKeyword(cleanToken)) {
                tokenCount += 1;
            }
            // 数字常量
            else if (cleanToken.at(0).isDigit()) {
                tokenCount += cleanToken.length() / 3 + 1;
            }
            // 字符串(引号包裹)
            else if ((cleanToken.startsWith('"') && cleanToken.endsWith('"'))
                     || (cleanToken.startsWith('\'') && cleanToken.endsWith('\''))) {
                tokenCount += cleanToken.length() / 4 + 2; // 引号各一个token
            }
            // 标识符 - 使用subword切分模拟
            else {
                // 下划线分隔的部分
                QStringList parts = cleanToken.split('_', Qt::SkipEmptyParts);
                for (const QString &part : parts) {
                    if (part.length() <= 2) {
                        tokenCount += 1;
                    } else {
                        // 驼峰命名检测
                        int subTokens = 1;
                        for (int i = 1; i < part.length(); ++i) {
                            if (part[i].isUpper() && part[i - 1].isLower()) {
                                subTokens++;
                            }
                        }
                        // 每个subword大约2-4字符一个token
                        tokenCount += qMax(1, part.length() / 3);
                    }
                }
            }
        }

        // 行尾token
        tokenCount += 1;
    }

    return qMax(tokenCount, 1);
}

/**
 * @brief 检测文本中CJK字符的比例
 */
double TokenUtils::detectChineseRatio(const QString &text)
{
    if (text.isEmpty()) {
        return 0.0;
    }

    int cjkCount = 0;
    int totalChars = 0;

    for (const QChar &c : text) {
        if (c.isSpace()) {
            continue;
        }

        totalChars++;
        ushort unicode = c.unicode();
        if ((unicode >= 0x4E00 && unicode <= 0x9FFF)
            || (unicode >= 0x3400 && unicode <= 0x4DBF)
            || (unicode >= 0x3040 && unicode <= 0x309F)
            || (unicode >= 0x30A0 && unicode <= 0x30FF)
            || (unicode >= 0xAC00 && unicode <= 0xD7AF)) {
            cjkCount++;
        }
    }

    return totalChars > 0 ? static_cast<double>(cjkCount) / totalChars : 0.0;
}

/**
 * @brief 启发式检测文本是否像代码
 *
 * 基于代码特征的评分系统：
 * - 常见代码符号 (括号、分号等)
 * - 缩进模式
 * - 常见关键字
 */
bool TokenUtils::isCodeLike(const QString &text)
{
    if (text.isEmpty()) {
        return false;
    }

    int score = 0;
    int codeIndicators = 0;

    // 代码特征符号权重
    struct Indicator {
        QString pattern;
        int weight;
    };

    static const Indicator indicators[] = {
        {";", 2},        // 分号
        {"{", 2},       // 花括号
        {"}", 2},
        {"(", 1},       // 圆括号
        {")", 1},
        {"[", 1},       // 方括号
        {"]", 1},
        {"//", 3},      // 行注释
        {"/*", 3},      // 块注释开始
        {"*/", 3},      // 块注释结束
        {"#include", 5}, // C/C++ include
        {"#define", 5},  // C/C++ define
        {"def ", 4},    // Python函数定义
        {"function", 3}, // JS函数定义
        {"class ", 3},  // 类定义
        {"const ", 2},  // const关键字
        {"let ", 2},    // JS let
        {"var ", 2},    // JS var
        {"int ", 2},    // C/C++ int
        {"void ", 2},   // C/C++ void
        {"return", 2},  // return语句
        {"if (", 3},    // if语句
        {"for (", 3},   // for循环
        {"while (", 3}, // while循环
    };

    for (const auto &ind : indicators) {
        if (text.contains(ind.pattern)) {
            score += ind.weight;
            codeIndicators++;
        }
    }

    // 检测缩进模式
    QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    int indentedLines = 0;
    for (const QString &line : lines) {
        if (!line.isEmpty() && (line[0] == ' ' || line[0] == '\t')) {
            indentedLines++;
        }
    }
    if (lines.size() > 0 && static_cast<double>(indentedLines) / lines.size() > 0.3) {
        score += 3;
    }

    // 检测赋值操作符频率
    int assignmentCount = text.count("= ");
    if (assignmentCount > 3) {
        score += 2;
    }

    // 阈值判断：根据文本长度动态调整
    int lengthThreshold = qMax(10, text.length() / 50);
    return score >= lengthThreshold || codeIndicators >= 3;
}

/**
 * @brief 检查单词是否为常见代码关键字
 */
bool TokenUtils::isCommonCodeKeyword(const QString &word)
{
    static const QSet<QString> keywords = {
        // C/C++/Java/C#
        "if",
        "else",
        "for",
        "while",
        "do",
        "switch",
        "case",
        "break",
        "continue",
        "return",
        "void",
        "int",
        "char",
        "float",
        "double",
        "bool",
        "true",
        "false",
        "null",
        "nullptr",
        "class",
        "struct",
        "public",
        "private",
        "protected",
        "virtual",
        "static",
        "const",
        "auto",
        "using",
        "namespace",
        "template",
        "typename",
        // Python
        "def",
        "elif",
        "pass",
        "None",
        "True",
        "False",
        "lambda",
        "yield",
        "with",
        "as",
        "import",
        "from",
        "try",
        "except",
        "finally",
        "raise",
        "assert",
        // JavaScript
        "function",
        "var",
        "let",
        "const",
        "undefined",
        "new",
        "this",
        "typeof",
        "instanceof",
        // Go
        "func",
        "package",
        "defer",
        "go",
        "chan",
        "map",
        "range",
        // Rust
        "fn",
        "mut",
        "impl",
        "trait",
        "match",
        "self",
        "Self",
        "Option",
        "Result",
        "Some",
        "None",
        "Ok",
        "Err",
        // 通用
        "export",
        "default",
        "async",
        "await",
    };

    return keywords.contains(word.toLower());
}

} // namespace QodeAssist::Context
