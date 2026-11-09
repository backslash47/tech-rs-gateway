// SPDX-License-Identifier: MIT
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

/// Helpers for delimiter-based controller text records (not a quoted-CSV parser).
struct MenuText {
    /** @brief Splits text without discarding empty fields, including the trailing field.
     * @param text Input record; an empty string produces one empty field.
     * @param delimiter Single separator character; no escaping is interpreted.
     * @return Owned field strings in input order.
     */
    static std::vector<std::string> split(const std::string& text, char delimiter);
};
