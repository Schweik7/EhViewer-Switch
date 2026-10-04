#pragma once

#include <cstddef>
#include <string>

namespace ehviewer::i18n {

// UI strings are written in Simplified Chinese in the source and looked up
// here when English is active. Format strings are translated as a whole, so
// keep the printf conversions in the same order in both languages.
void SetEnglish(bool english);
bool IsEnglish();
const char* T(const char* chinese);
std::string T(const std::string& chinese);

// Number of entries in the translation table (for tests).
std::size_t TableSize();
bool HasTranslation(const std::string& chinese);
// Returns true if every table key is non-empty and every value is ASCII
// printable plus a few symbols, i.e. no untranslated Chinese leaked in.
bool TableLooksTranslated(std::string* offending_key = nullptr);

}  // namespace ehviewer::i18n
