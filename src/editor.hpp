// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
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
    std::string theme = "catppuccin";  // syntax palette: built-in or a custom theme name
    size_t logSize = 100;              // status messages kept in memory / shown by 'hed log'; 0 = unlimited
};

// ---------------------------------------------------------------------------
// Editor configuration.
//
// hed reads its editor settings from an INI-style file:
//
//     [editor]
//     tabsize = 4
//     indent = auto
//     spaces = 4
//     spell = true
//     numbers = true
//     theme = catppuccin
//     log_size = 100
//
// The file lives at $XDG_CONFIG_HOME/hed/config (usually ~/.config/hed/config).
// Precedence, highest first:
//   1. command-line flags        (e.g. `hed FILE --tabsize 8`)
//   2. environment variables     (HED_TABSIZE, HED_INDENT, HED_SPACES,
//                                 HED_SPELL, HED_NUMBERS, HED_THEME, HED_LOG_SIZE)
//   3. the config file           ([editor] section above)
//   4. built-in defaults
// ---------------------------------------------------------------------------
struct Config {
    int tabsize = 4;                 // display width of a tab character
    int indent = 0;                  // 0 = auto-detect, 1 = tabs, 2 = spaces
    int spaces = 4;                  // indent width when indent = 2 (spaces)
    bool spell = true;               // start with misspelling underlining on
    bool numbers = true;             // show the line-number gutter
    std::string theme = "catppuccin";  // syntax palette: built-in or a custom theme name
    size_t logSize = 100;            // status messages kept in memory; 0 = unlimited
};

std::string configFilePath();  // ~/.config/hed/config (honours $XDG_CONFIG_HOME)
Config loadConfig();           // defaults, then the config file, then HED_* env vars
bool saveConfig(const Config& c, std::string& err);  // writes a well-commented file

int runEditor(const EditorOptions& o);

}  // namespace hed
