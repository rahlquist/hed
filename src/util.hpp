// hed - heredoc replacement / one-shot file tool / nano-style editor
// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace hed {

struct Text {
    std::vector<std::string> lines;
    bool trailingNewline = true;
    bool crlf = false;
};

Text splitText(const std::string& s);
std::string joinText(const Text& t);
std::vector<std::string> splitSimple(const std::string& s, char d);

bool readFile(const std::string& path, std::string& out, std::string& err);
bool readFd(int fd, std::string& out);
bool writeAll(int fd, const std::string& data);
bool pathExists(const std::string& p);
bool isDir(const std::string& p);
long fileMtime(const std::string& p);  // mtime in nanoseconds since epoch, -1 if missing
bool atomicWrite(const std::string& path, const std::string& data, std::string& err, int forceMode = -1);
bool appendToFile(const std::string& path, const std::string& data, std::string& err, int forceMode = -1);
bool makeBackup(const std::string& path, std::string& backupPath, std::string& err);
bool mkdirParents(const std::string& dir, std::string& err);

std::string dirName(const std::string& p);
std::string baseName(const std::string& p);
std::string toLower(std::string s);
bool hasUpper(const std::string& s);
std::string trimRight(const std::string& s);
std::string leadingWs(const std::string& s);
bool startsWith(const std::string& s, const std::string& p);
bool endsWith(const std::string& s, const std::string& p);

std::string interpretEscapes(const std::string& s);
std::string stripLeadingTabs(const std::string& s);
std::string dedent(const std::string& s);

// 1-based inclusive range. Forms: N  A:B  A:  :B  A:+N  A,B  -N: (last N lines)  B may be '$'
bool parseRange(const std::string& spec, size_t total, size_t& a, size_t& b, std::string& err);

size_t u8next(const std::string& s, size_t i);
size_t u8prev(const std::string& s, size_t i);
int u8width(const std::string& s, size_t i, size_t& len);
size_t displayWidth(const std::string& s);

std::string jsonEscape(const std::string& s);
std::string homeDir();
std::string configDir();
std::string themesDir();  // ~/.config/hed/themes (honours $XDG_CONFIG_HOME)
bool listDir(const std::string& dir, std::vector<std::string>& out);  // entry names, false if unreadable/missing

std::string unifiedDiff(const std::vector<std::string>& a, const std::vector<std::string>& b,
                        const std::string& nameA, const std::string& nameB, int context, bool color);

}  // namespace hed
