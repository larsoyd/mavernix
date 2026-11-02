#pragma once

#include <string>
#include <vector>

// swell marks a mnemonic with & where dbusmenu uses _
inline std::string menuLabel(const std::string& text)
{
    std::string label;
    for (size_t at = 0; at < text.size() && text[at] != '\t'; at++)
    {
        if (text[at] == '&' && at + 1 < text.size() && text[at + 1] == '&')
            label += text[++at];
        else if (text[at] == '&')
            label += '_';
        else if (text[at] == '_')
            label += "__";
        else
            label += text[at];
    }
    return label;
}

inline std::string shortcutKey(const std::string& key)
{
    if (key == "Ctrl")
        return "Control";
    if (key == "Win")
        return "Super";
    return key;
}

inline std::vector<std::string> menuShortcut(const std::string& text)
{
    const size_t tab = text.find('\t');
    if (tab == std::string::npos || tab + 1 == text.size())
        return {};
    const std::string shortcut = text.substr(tab + 1);
    std::vector<std::string> keys;
    size_t start = 0;
    // the last key can be a plus sign itself
    for (size_t plus = shortcut.find('+'); plus != std::string::npos && plus + 1 < shortcut.size() && plus > start;
         plus = shortcut.find('+', start))
    {
        keys.push_back(shortcutKey(shortcut.substr(start, plus - start)));
        start = plus + 1;
    }
    keys.push_back(shortcutKey(shortcut.substr(start)));
    return keys;
}
