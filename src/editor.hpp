// SPDX-License-Identifier: MIT
#pragma once
#include <string>

#include "highlight.hpp"

namespace hed {

struct EditorOptions {
    std::string file;
    bool langSet = false;
    LangId lang = L_TEXT;
    long line = 0, col = 0;
    int tabsize = 4;
    int indentMode = 0;   // 0 auto-detect, 1 tabs, 2 spaces
    int indentWidth = 0;  // 0 auto
    bool spell = true;
    bool numbers = true;
    bool pipeMode = false;  // buffer came from stdin and/or goes to stdout
    std::string initial;
    bool readOnly = false;
};

int runEditor(const EditorOptions& o);

}  // namespace hed
