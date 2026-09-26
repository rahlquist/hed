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

const char* langName(LangId id);
bool langFromName(const std::string& name, LangId& out);
LangId detectLang(const std::string& path, const std::string& content);
std::string langTable();
void highlightLine(LangId id, const std::string& line, HlState& st, std::vector<uint8_t>& hl);
const char* hlSgr(uint8_t h);
bool spellRegion(LangId id, uint8_t h);
CommentStyle commentStyle(LangId id);
bool isBraceLang(LangId id);
int defaultIndent(LangId id);
std::string renderAnsi(const std::string& s, const std::vector<uint8_t>& hl,
                       const std::vector<std::pair<size_t, size_t>>* underline = nullptr);

}  // namespace hed
