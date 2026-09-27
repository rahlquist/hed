// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace hed {

enum Hl : uint8_t {
    H_NORMAL, H_COMMENT, H_STRING, H_NUMBER, H_KEYWORD, H_TYPE, H_BUILTIN, H_FUNCTION,
    H_VARIABLE, H_CONSTANT, H_TAG, H_ATTR, H_PREPROC, H_ESCAPE, H_PROPERTY, H_OPERATOR
};

enum LangId { L_TEXT, L_PYTHON, L_BASH, L_FISH, L_JS, L_TS, L_SQL, L_HTML, L_CSS, L_JSON, L_MARKDOWN, L_COUNT };

// Highlighter state carried from the end of one line to the start of the next.
struct HlState {
    int mode = 0, sub = 0, depth = 0;
    std::string term;
    bool operator==(const HlState& o) const { return mode == o.mode && sub == o.sub && depth == o.depth && term == o.term; }
    bool operator!=(const HlState& o) const { return !(*this == o); }
};

struct CommentStyle { std::string line, open, close; };

// A syntax colour palette: one full SGR escape sequence (e.g. "\x1b[38;5;114m")
// per highlight token. The built-in palettes are compiled in; custom palettes
// are loaded from ~/.config/hed/themes/<name>.theme files.
struct Palette {
    std::string comment, string, number, keyword, type, builtin, func,
        variable, constant, tag, attr, preproc, escape, property, op;
};

const char* langName(LangId id);
bool langFromName(const std::string& name, LangId& out);
LangId detectLang(const std::string& path, const std::string& content);
std::string langTable();
void highlightLine(LangId id, const std::string& line, HlState& st, std::vector<uint8_t>& hl);
void setTheme(const std::string& name);  // built-in (catppuccin|dark|light) or a custom theme; custom file
                                         // overrides a built-in of the same name; missing -> catppuccin + stderr warning
// Custom themes: INI-style files at ~/.config/hed/themes/<name>.theme with a
// [theme] section mapping highlight keys to SGR parameters ("38;5;114").
// Missing keys fall back to the built-in catppuccin value for that key.
bool loadCustomTheme(const std::string& name, Palette& out);  // false if the file is missing or malformed
bool themeExists(const std::string& name);                    // built-in, or an existing custom theme file
std::string themeTemplate(const std::string& name);           // INI template from that theme's palette
std::vector<std::pair<std::string, bool>> availableThemes();  // (name, backed-by-custom-file), built-ins first
const char* hlSgr(uint8_t h);
bool spellRegion(LangId id, uint8_t h);
CommentStyle commentStyle(LangId id);
bool isBraceLang(LangId id);
int defaultIndent(LangId id);
std::string renderAnsi(const std::string& s, const std::vector<uint8_t>& hl,
                       const std::vector<std::pair<size_t, size_t>>* underline = nullptr);

}  // namespace hed
