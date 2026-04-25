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
#ifndef SETTINGOPTIONSPAGE_H
#define SETTINGOPTIONSPAGE_H

#include <coreplugin/dialogs/ioptionspage.h>

#define REGISTER_PAGE(CLASS) const bool r_##CLASS = registerPage(new CLASS());

namespace QodeAssist::Settings {
    class SettingOptionsPage : public Core::IOptionsPage
    {
        public:
        virtual void retranslate() = 0;
    };

    bool registerPage(SettingOptionsPage *page);
    void retranslate();
}

#endif // SETTINGOPTIONSPAGE_H
