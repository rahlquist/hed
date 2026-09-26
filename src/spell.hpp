// SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "highlight.hpp"

namespace hed {

struct Misspelling { size_t start, len; std::string word; };

class Speller {
  public:
    static Speller& instance();
    bool ensureLoaded();
    bool ok() const { return !dict_.empty(); }
    const std::string& error() const { return error_; }
    bool known(const std::string& word) const;
    std::vector<std::string> suggest(const std::string& word, size_t n) const;
    bool addUserWord(const std::string& w, std::string& err);
    void ignore(const std::string& w);
    size_t size() const { return dict_.size(); }

  private:
    bool inDict(const std::string& lw) const;
    bool affixKnown(const std::string& lw) const;
    void loadList(const char* data, size_t n, uint64_t defFreq);
    std::unordered_map<std::string, uint64_t> dict_;
    std::unordered_set<std::string> ignored_;
    bool tried_ = false;
    std::string error_;
};

std::vector<Misspelling> findMisspellings(const Speller& sp, LangId lang, const std::string& line,
                                          const std::vector<uint8_t>& hl, bool allText);
std::string userDictPath();

}  // namespace hed
