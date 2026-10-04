#pragma once

#include <string>

namespace ehviewer::html {

std::string DecodeEntities(const std::string& value);
std::string PlainText(const std::string& html);
std::string ElementTextById(const std::string& document, const std::string& id);
// Percent-encodes a query value (UTF-8 bytes, spaces as "+").
std::string UrlEncode(const std::string& value);

}  // namespace ehviewer::html
