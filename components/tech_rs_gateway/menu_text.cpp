// SPDX-License-Identifier: MIT
#include "menu_text.h"

std::vector<std::string> MenuText::split(const std::string& text, char delimiter) {
    std::vector<std::string> parts;
    size_t start = 0;

    for (;;) {
        const auto end = text.find(delimiter, start);
        parts.push_back(text.substr(start, end == std::string::npos ? end : end - start));

        if (end == std::string::npos) {
            break;
        }

        start = end + 1;
    }

    return parts;
}
