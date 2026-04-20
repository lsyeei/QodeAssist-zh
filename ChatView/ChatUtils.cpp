/*
 * @Author: lishiying lsyeei@163.com
 * @Date: 2026-04-08 15:49:32
 * @LastEditors: lishiying lsyeei@163.com
 * @LastEditTime: 2026-04-18 09:46:31
 * @FilePath: \QodeAssist-zh\ChatView\ChatUtils.cpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
/* 
 * Copyright (C) 2024-2025 Petr Mironychev
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

#include "ChatUtils.h"

#include <Logger.hpp>
#include <QClipboard>
#include <QGuiApplication>

namespace QodeAssist::Chat {

void ChatUtils::copyToClipboard(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

QString ChatUtils::getSafeMarkdownText(const QString &text) const
{
    if (text.isEmpty()) {
        return text;
    }
    
    bool needsSanitization = false;
    for (const QChar &ch : text) {
        if (ch.isNull() || (!ch.isPrint() && ch != '\n' && ch != '\t' && ch != '\r' && ch != ' ')) {
            needsSanitization = true;
            break;
        }
    }

    if (!needsSanitization) {
        return text;
    }

    QString safeText;
    safeText.reserve(text.size());

    for (QChar ch : text) {
        if (ch.isNull()) {
            safeText.append(' ');
        } else if (ch == '\n' || ch == '\t' || ch == '\r' || ch == ' ') {
            safeText.append(ch);
        } else if (ch.isPrint()) {
            safeText.append(ch);
        } else {
            safeText.append(QChar(0xFFFD));
        }
    }
    return safeText;
}

} // namespace QodeAssist::Chat
