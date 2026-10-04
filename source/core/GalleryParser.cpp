#include "GalleryParser.h"

#include "HtmlUtil.h"
#include "Json.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <set>
#include <utility>

namespace ehviewer {
namespace {

bool Fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}

bool IsTokenCharacter(unsigned char c) {
    return std::isalnum(c) != 0;
}

bool ParsePositiveInteger(const std::string& value, std::int64_t* result) {
    if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return std::isdigit(c) != 0;
        })) return false;
    char* end = nullptr;
    const long long parsed = std::strtoll(value.c_str(), &end, 10);
    if (end == value.c_str() || *end != '\0' || parsed <= 0) return false;
    *result = parsed;
    return true;
}

std::string ClassElementTextAfter(const std::string& document, const std::string& class_name,
                                  std::size_t begin, std::size_t maximum_distance) {
    const std::size_t position = document.find("class=\"" + class_name + "\"", begin);
    if (position == std::string::npos || position - begin > maximum_distance) return {};
    const std::size_t content_begin = document.find('>', position);
    const std::size_t content_end = document.find("</", content_begin + 1);
    if (content_begin == std::string::npos || content_end == std::string::npos) return {};
    return html::PlainText(document.substr(content_begin + 1, content_end - content_begin - 1));
}

// Returns the raw value of attribute name inside one tag, or an empty string.
std::string Attribute(const std::string& tag, const std::string& name) {
    std::size_t cursor = 0;
    while ((cursor = tag.find(name + "=", cursor)) != std::string::npos) {
        const bool boundary = cursor == 0 || std::isspace(static_cast<unsigned char>(tag[cursor - 1]));
        const std::size_t quote_position = cursor + name.size() + 1;
        if (!boundary || quote_position >= tag.size()) {
            cursor = quote_position;
            continue;
        }
        const char quote = tag[quote_position];
        if (quote != '"' && quote != '\'') return {};
        const std::size_t end = tag.find(quote, quote_position + 1);
        if (end == std::string::npos) return {};
        return html::DecodeEntities(tag.substr(quote_position + 1, end - quote_position - 1));
    }
    return {};
}

// Returns the complete "<img ...>" tag beginning at or after begin.
std::string NextImageTag(const std::string& document, std::size_t begin, std::size_t limit,
                         std::size_t* found_at = nullptr) {
    const std::size_t position = document.find("<img", begin);
    if (position == std::string::npos || position >= limit) return {};
    const std::size_t end = document.find('>', position);
    if (end == std::string::npos) return {};
    if (found_at) *found_at = position;
    return document.substr(position, end - position + 1);
}

std::string ImageSource(const std::string& tag) {
    std::string source = Attribute(tag, "data-src");
    if (source.empty()) source = Attribute(tag, "src");
    return source;
}

bool LooksLikeThumbnail(const std::string& url) {
    if (url.compare(0, 4, "http") != 0) return false;
    // Site chrome lives below ehgt.org/g/ and /img/; thumbnails use /t/ or /w/.
    if (url.find("ehgt.org/g/") != std::string::npos || url.find("/img/") != std::string::npos)
        return false;
    return url.find("/t/") != std::string::npos || url.find("/w/") != std::string::npos ||
           url.find("ehgt.org/") != std::string::npos;
}

std::string FindListThumbnail(const std::string& document, std::int64_t gid,
                              std::size_t link_position) {
    // Compact/minimal layouts keep the thumbnail in a hover pane with id="it<gid>".
    const std::size_t pane = document.find("id=\"it" + std::to_string(gid) + "\"");
    if (pane != std::string::npos) {
        const std::string source = ImageSource(NextImageTag(document, pane, pane + 2048));
        if (LooksLikeThumbnail(source)) return source;
    }
    // Extended/thumbnail layouts put the <img> right after the gallery link.
    std::size_t cursor = link_position;
    const std::size_t limit = std::min(document.size(), link_position + 3072);
    for (;;) {
        std::size_t found = 0;
        const std::string tag = NextImageTag(document, cursor, limit, &found);
        if (tag.empty()) return {};
        const std::string source = ImageSource(tag);
        if (LooksLikeThumbnail(source)) return source;
        cursor = found + tag.size();
    }
}

std::string CssUrlAfter(const std::string& document, std::size_t begin, std::size_t distance) {
    const std::size_t open = document.find("url(", begin);
    if (open == std::string::npos || open - begin > distance) return {};
    const std::size_t close = document.find(')', open + 4);
    if (close == std::string::npos) return {};
    std::string url = document.substr(open + 4, close - open - 4);
    if (!url.empty() && (url.front() == '"' || url.front() == '\'')) url.erase(url.begin());
    if (!url.empty() && (url.back() == '"' || url.back() == '\'')) url.pop_back();
    return html::DecodeEntities(url);
}

int IntegerBefore(const std::string& document, std::size_t end) {
    std::size_t begin = end;
    while (begin > 0 && std::isdigit(static_cast<unsigned char>(document[begin - 1]))) --begin;
    if (begin == end || end - begin > 9) return 0;
    return std::atoi(document.substr(begin, end - begin).c_str());
}

std::string QuotedAfter(const std::string& document, const std::string& prefix, char quote) {
    const std::size_t position = document.find(prefix);
    if (position == std::string::npos) return {};
    const std::size_t begin = position + prefix.size();
    const std::size_t end = document.find(quote, begin);
    if (end == std::string::npos || end - begin > 4096) return {};
    return html::DecodeEntities(document.substr(begin, end - begin));
}

std::string Trim(const std::string& value) {
    std::size_t begin = 0;
    std::size_t end = value.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
    return value.substr(begin, end - begin);
}

}  // namespace

bool ParseGalleryList(const std::string& document, std::vector<GallerySummary>* galleries,
                      std::string* error) {
    if (galleries == nullptr) return Fail(error, "Gallery list output is null");
    galleries->clear();
    std::set<std::int64_t> seen;
    std::vector<std::size_t> links;
    std::size_t cursor = 0;
    while ((cursor = document.find("/g/", cursor)) != std::string::npos) {
        const std::size_t link_position = cursor;
        const std::size_t gid_begin = cursor + 3;
        const std::size_t gid_end = document.find('/', gid_begin);
        if (gid_end == std::string::npos) break;
        std::int64_t gid = 0;
        // Site assets such as ehgt.org/g/mr.gif also contain "/g/"; skip them.
        if (gid_end - gid_begin > 20 || !ParsePositiveInteger(document.substr(gid_begin, gid_end - gid_begin), &gid)) {
            cursor = gid_begin;
            continue;
        }
        std::size_t token_end = gid_end + 1;
        while (token_end < document.size() && IsTokenCharacter(document[token_end])) ++token_end;
        const std::string token = document.substr(gid_end + 1, token_end - gid_end - 1);
        if (token.empty() || token.size() > 64 || seen.find(gid) != seen.end()) {
            cursor = token_end;
            continue;
        }
        std::string title = ClassElementTextAfter(document, "glink", token_end, 4096);
        if (title.empty()) title = ClassElementTextAfter(document, "gl4t glname", token_end, 512);
        if (!title.empty()) {
            GallerySummary summary;
            summary.gid = gid;
            summary.token = token;
            summary.title = title;
            summary.path = "/g/" + std::to_string(gid) + "/" + token + "/";
            summary.thumb_url = FindListThumbnail(document, gid, link_position);
            galleries->push_back(std::move(summary));
            links.push_back(link_position);
            seen.insert(gid);
        }
        cursor = token_end;
    }
    if (galleries->empty()) return Fail(error, "No galleries found in response");

    // Page count and category live in the gallery's own block: the hover pane
    // (id="it<gid>") before the link in compact/minimal layouts, otherwise
    // between this gallery's first link and the next gallery's.
    for (std::size_t i = 0; i < galleries->size(); ++i) {
        GallerySummary& gallery = (*galleries)[i];
        const std::size_t next = i + 1 < links.size() ? links[i + 1] : document.size();
        std::size_t begin = links[i];
        std::size_t end = next;
        const std::size_t pane = document.find("id=\"it" + std::to_string(gallery.gid) + "\"");
        if (pane != std::string::npos && pane < links[i]) {
            begin = pane;
            end = links[i];
        }
        const std::string block = document.substr(begin, end - begin);
        const std::size_t pages = block.find(" pages</div>");
        if (pages != std::string::npos) gallery.pages = IntegerBefore(block, pages);
        for (const char* marker : {"class=\"cn ", "class=\"cs "}) {
            const std::size_t category = block.find(marker);
            if (category == std::string::npos) continue;
            const std::size_t open = block.find('>', category);
            const std::size_t close = block.find('<', open);
            if (open != std::string::npos && close != std::string::npos)
                gallery.category = html::PlainText(block.substr(open + 1, close - open - 1));
            break;
        }
        const std::size_t stars = block.find("class=\"ir");
        if (stars != std::string::npos) {
            const std::size_t style = block.find("style=\"", stars);
            const std::size_t tag_end = block.find('>', stars);
            if (style != std::string::npos && style < tag_end)
                gallery.rating = RatingFromStarSprite(block.substr(style + 7, tag_end - style - 7));
        }
        const std::size_t posted = document.find("id=\"posted_" + std::to_string(gallery.gid) + "\"");
        if (posted != std::string::npos) {
            const std::size_t open = document.find('>', posted);
            const std::size_t close = document.find('<', open);
            if (open != std::string::npos && close != std::string::npos)
                gallery.posted = html::PlainText(document.substr(open + 1, close - open - 1));
        }
    }
    return true;
}

bool ParseGalleryDetail(const std::string& document, std::int64_t gid,
                        const std::string& token, GalleryDetail* detail, std::string* error) {
    if (detail == nullptr) return Fail(error, "Gallery detail output is null");
    GalleryDetail parsed;
    parsed.gid = gid;
    parsed.token = token;
    parsed.title = html::ElementTextById(document, "gn");
    parsed.title_jpn = html::ElementTextById(document, "gj");
    parsed.category = html::ElementTextById(document, "gdc");
    if (parsed.title.empty()) return Fail(error, "Gallery title not found");

    const std::size_t cover = document.find("id=\"gd1\"");
    if (cover != std::string::npos) parsed.cover_url = CssUrlAfter(document, cover, 512);

    // <td class="gdt1">File Size:</td><td class="gdt2">1.17 GiB</td>
    const auto table_value = [&document](const char* label) -> std::string {
        const std::size_t position = document.find(std::string(">") + label + "</td>");
        if (position == std::string::npos) return {};
        const std::size_t cell = document.find("class=\"gdt2\"", position);
        if (cell == std::string::npos || cell - position > 256) return {};
        const std::size_t open = document.find('>', cell);
        const std::size_t close = document.find("</td>", open);
        if (open == std::string::npos || close == std::string::npos) return {};
        return Trim(html::PlainText(document.substr(open + 1, close - open - 1)));
    };
    parsed.posted = table_value("Posted:");
    parsed.language = table_value("Language:");
    parsed.file_size = table_value("File Size:");
    parsed.favorited = table_value("Favorited:");
    parsed.uploader = html::ElementTextById(document, "gdn");
    const std::string rating = html::ElementTextById(document, "rating_label");
    const std::string rating_count = html::ElementTextById(document, "rating_count");
    if (rating.compare(0, 9, "Average: ") == 0)
        parsed.rating = rating.substr(9) + (rating_count.empty() ? "" : " (" + rating_count + ")");

    std::size_t length = document.find("Length:");
    if (length == std::string::npos) length = 0;
    const std::size_t pages_position = document.find(" pages", length);
    if (pages_position != std::string::npos) parsed.pages = IntegerBefore(document, pages_position);

    const std::size_t table = document.find("class=\"ptt\"");
    if (table != std::string::npos) {
        const std::size_t table_end = document.find("</table>", table);
        std::size_t cursor = table;
        while ((cursor = document.find("?p=", cursor)) != std::string::npos &&
               (table_end == std::string::npos || cursor < table_end)) {
            cursor += 3;
            std::size_t end = cursor;
            while (end < document.size() && std::isdigit(static_cast<unsigned char>(document[end])))
                ++end;
            const int index = IntegerBefore(document, end);
            parsed.preview_page_count = std::max(parsed.preview_page_count, index + 1);
            cursor = end;
        }
    }

    ParsePreviewTokens(document, gid, &parsed.page_tokens);
    parsed.comments = ParseComments(document);

    // <td class="tc">parody:</td><td><div id="td_parody:flower_knight_girl" ...
    const std::size_t taglist = document.find("id=\"taglist\"");
    if (taglist != std::string::npos) {
        const std::size_t end = document.find("</table>", taglist);
        std::size_t cursor = taglist;
        while (cursor < end) {
            const std::size_t row = document.find("class=\"tc\">", cursor);
            const std::size_t first_tag = document.find("id=\"td_", cursor);
            if (first_tag == std::string::npos || first_tag >= end) break;
            TagGroup group;
            std::size_t tags_begin = first_tag;
            if (row != std::string::npos && row < first_tag) {
                const std::size_t open = row + 11;
                const std::size_t close = document.find('<', open);
                group.name_space = Trim(document.substr(open, close - open));
                if (!group.name_space.empty() && group.name_space.back() == ':') group.name_space.pop_back();
                tags_begin = close;
            }
            const std::size_t next_row = document.find("class=\"tc\">", tags_begin);
            const std::size_t group_end = std::min(next_row == std::string::npos ? end : next_row, end);
            std::size_t tag = tags_begin;
            while ((tag = document.find("id=\"td_", tag)) != std::string::npos && tag < group_end) {
                const std::size_t begin = tag + 7;
                const std::size_t quote = document.find('"', begin);
                std::string value = html::DecodeEntities(document.substr(begin, quote - begin));
                const std::size_t colon = value.find(':');
                if (colon != std::string::npos) value = value.substr(colon + 1);
                std::replace(value.begin(), value.end(), '_', ' ');
                if (!value.empty()) group.tags.push_back(value);
                tag = quote;
            }
            if (!group.tags.empty()) parsed.tags.push_back(std::move(group));
            cursor = group_end;
        }
    }

    // var apiuid = 4286824; var apikey = "..."; var api_url = "...";
    const std::size_t uid = document.find("var apiuid = ");
    if (uid != std::string::npos) parsed.api_uid = std::atoll(document.c_str() + uid + 13);
    parsed.api_key = QuotedAfter(document, "var apikey = \"", '"');
    parsed.api_url = QuotedAfter(document, "var api_url = \"", '"');
    // Favorited galleries show the folder as <div class="i" ... title="Favorites 3">
    // inside id="fav"; otherwise the link says "Add to Favorites".
    const std::size_t fav = document.find("id=\"fav\"");
    if (fav != std::string::npos) {
        const std::size_t close = document.find("</div>", fav);
        const std::size_t title = document.find("title=\"", fav);
        if (title != std::string::npos && (close == std::string::npos || title < close)) {
            const std::size_t end = document.find('"', title + 7);
            if (end != std::string::npos)
                parsed.favorite_name = html::DecodeEntities(document.substr(title + 7, end - title - 7));
        }
    }
    *detail = std::move(parsed);
    return true;
}

double RatingFromStarSprite(const std::string& style) {
    const std::size_t position = style.find("background-position:");
    if (position == std::string::npos) return -1.0;
    const char* cursor = style.c_str() + position + 20;
    char* end = nullptr;
    const long x = std::strtol(cursor, &end, 10);
    if (end == cursor) return -1.0;
    const char* y_begin = std::strstr(end, "px");
    if (y_begin == nullptr) return -1.0;
    const long y = std::strtol(y_begin + 2, &end, 10);
    double rating = 5.0 - static_cast<double>(std::labs(x)) / 16.0;
    if (y == -21) rating -= 0.5;
    return std::max(0.0, std::min(5.0, rating));
}

std::string TagSearchQuery(const std::string& name_space, const std::string& tag) {
    const std::string quoted = tag.find(' ') != std::string::npos ? "\"" + tag + "$\"" : tag + "$";
    return name_space.empty() ? quoted : name_space + ":" + quoted;
}

bool ParseFavoriteSlots(const std::string& document, FavoriteSlots* slots) {
    if (slots == nullptr) return false;
    FavoriteSlots parsed;
    int checked = -1;
    for (int index = 0; index < 10; ++index) {
        const std::string id = "id=\"fav" + std::to_string(index) + "\"";
        const std::size_t input = document.find(id);
        if (input == std::string::npos) return false;
        const std::size_t tag_begin = document.rfind('<', input);
        const std::size_t tag_end = document.find('>', input);
        if (tag_begin != std::string::npos && tag_end != std::string::npos &&
            document.substr(tag_begin, tag_end - tag_begin).find("checked") != std::string::npos) {
            checked = index;
        }
        // The folder name is the text of the div that clicks this radio.
        const std::string click = "getElementById('fav" + std::to_string(index) + "').click()\">";
        const std::size_t label = document.find(click, input);
        std::string name;
        if (label != std::string::npos) {
            const std::size_t begin = label + click.size();
            const std::size_t end = document.find('<', begin);
            if (end != std::string::npos) name = Trim(html::PlainText(document.substr(begin, end - begin)));
        }
        parsed.names.push_back(name.empty() ? "Favorites " + std::to_string(index) : name);
    }
    // Folder 0 is pre-checked for new favorites; only a "favdel" option means
    // the gallery is already in a folder.
    parsed.current = document.find("value=\"favdel\"") != std::string::npos ? checked : -1;
    *slots = std::move(parsed);
    return true;
}

bool ParseFavoriteFolders(const std::string& document, FavoriteFolders* folders) {
    if (folders == nullptr) return false;
    FavoriteFolders parsed;
    parsed.folders.resize(10);
    for (int index = 0; index < 10; ++index) parsed.folders[index].name = "Favorites " + std::to_string(index);
    int found = 0;
    // <div class="fp[ fps]" onclick="...favorites.php?favcat=N'"><div>COUNT</div>
    // <div class="i" ...></div><div>NAME</div></div>; "fps" marks the shown one
    // and the trailing "Show All Favorites" entry has no favcat.
    for (std::size_t position = document.find("<div class=\"fp"); position != std::string::npos;
         position = document.find("<div class=\"fp", position + 1)) {
        const std::size_t tag_end = document.find('>', position);
        if (tag_end == std::string::npos) break;
        const std::string tag = document.substr(position, tag_end - position);
        const bool shown = tag.find("fps") != std::string::npos;
        const std::size_t favcat = tag.find("favcat=");
        if (favcat == std::string::npos) {
            if (shown) parsed.current = -1;
            continue;
        }
        const int index = std::atoi(tag.c_str() + favcat + 7);
        if (index < 0 || index > 9) continue;
        if (shown) parsed.current = index;
        // Texts of the inner divs: count first, then the name.
        const std::size_t block_end = std::min(document.size(), document.find("<div class=\"fp", tag_end));
        std::vector<std::string> texts;
        for (std::size_t open = document.find("<div", tag_end); open < block_end;
             open = document.find("<div", open + 1)) {
            const std::size_t begin = document.find('>', open);
            const std::size_t end = begin == std::string::npos ? begin : document.find('<', begin);
            if (end == std::string::npos || end > block_end) break;
            const std::string text = Trim(html::PlainText(document.substr(begin + 1, end - begin - 1)));
            if (!text.empty()) texts.push_back(text);
        }
        if (!texts.empty()) parsed.folders[index].count = std::atoi(texts[0].c_str());
        if (texts.size() > 1) parsed.folders[index].name = texts[1];
        ++found;
    }
    if (found == 0) return false;
    *folders = std::move(parsed);
    return true;
}

bool ParseRatingResponse(const std::string& text, RatingResult* result, std::string* error) {
    json::Value root;
    if (!json::Parse(text, &root, error)) return false;
    const std::string api_error = root.GetString("error");
    if (!api_error.empty()) return Fail(error, api_error);
    const json::Value* average = root.Find("rating_avg");
    if (average == nullptr || average->type != json::Value::Type::Number)
        return Fail(error, "Rating response has no rating_avg");
    result->average = average->number;
    const json::Value* user = root.Find("rating_usr");
    result->user = user != nullptr && user->type == json::Value::Type::Number ? user->number : 0.0;
    result->count = static_cast<int>(root.GetInt("rating_cnt"));
    return true;
}

std::string BuildRateRequest(std::int64_t api_uid, const std::string& api_key, std::int64_t gid,
                             const std::string& token, int rating) {
    // api_key and token are hex strings from the site, so no escaping is needed;
    // anything else is rejected rather than injected into the JSON.
    const auto hex = [](const std::string& value) {
        return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) {
            return std::isxdigit(c) != 0;
        });
    };
    if (!hex(api_key) || !hex(token) || rating < 1 || rating > 10) return {};
    return "{\"method\":\"rategallery\",\"apiuid\":" + std::to_string(api_uid) + ",\"apikey\":\"" +
           api_key + "\",\"gid\":" + std::to_string(gid) + ",\"token\":\"" + token +
           "\",\"rating\":" + std::to_string(rating) + "}";
}

ListNavigation ParseListNavigation(const std::string& document) {
    ListNavigation navigation;
    const auto link = [&document](const char* id) -> std::string {
        // <a id="unext" href="https://exhentai.org/?next=4230495">; the
        // disabled form is a <span id="unext"> without href.
        const std::size_t position = document.find(std::string("id=\"") + id + "\"");
        if (position == std::string::npos) return {};
        const std::size_t tag_begin = document.rfind('<', position);
        const std::size_t tag_end = document.find('>', position);
        if (tag_begin == std::string::npos || tag_end == std::string::npos) return {};
        return Attribute(document.substr(tag_begin, tag_end - tag_begin + 1), "href");
    };
    navigation.next_url = link("unext");
    if (navigation.next_url.empty()) navigation.next_url = link("dnext");
    navigation.prev_url = link("uprev");
    if (navigation.prev_url.empty()) navigation.prev_url = link("dprev");
    return navigation;
}

std::vector<GalleryComment> ParseComments(const std::string& document) {
    std::vector<GalleryComment> comments;
    const auto element_text = [](const std::string& block, const std::string& marker) -> std::string {
        const std::size_t position = block.find(marker);
        if (position == std::string::npos) return {};
        const std::size_t open = block.find('>', position);
        if (open == std::string::npos) return {};
        // Comment bodies nest <a>/<span>; stop at the closing </div>.
        const std::size_t close = block.find("</div>", open);
        if (close == std::string::npos) return {};
        return block.substr(open + 1, close - open - 1);
    };
    std::size_t cursor = 0;
    while ((cursor = document.find("<div class=\"c1\">", cursor)) != std::string::npos) {
        const std::size_t next = document.find("<div class=\"c1\">", cursor + 16);
        const std::string block = document.substr(cursor, (next == std::string::npos ? document.size() : next) - cursor);
        cursor += 16;

        GalleryComment comment;
        const std::string header = html::PlainText(element_text(block, "class=\"c3\""));
        // "Posted on 04 November 2022, 06:42 by: user"
        const std::size_t by = header.find(" by:");
        if (by != std::string::npos) {
            comment.posted = Trim(header.substr(0, by));
            if (comment.posted.compare(0, 10, "Posted on ") == 0) comment.posted.erase(0, 10);
            comment.author = Trim(header.substr(by + 4));
        } else {
            comment.posted = header;
        }
        comment.uploader = block.find("Uploader Comment") != std::string::npos;
        const std::string score = html::PlainText(element_text(block, "class=\"c5"));
        if (score.compare(0, 6, "Score ") == 0) comment.score = Trim(score.substr(6));

        std::string body = element_text(block, "class=\"c6\"");
        // Keep line breaks: mark <br> before stripping tags.
        std::string marked;
        for (std::size_t i = 0; i < body.size();) {
            if (body.compare(i, 3, "<br") == 0 || body.compare(i, 3, "<BR") == 0) {
                const std::size_t end = body.find('>', i);
                if (end == std::string::npos) break;
                marked += "<x>\x01<x>";
                i = end + 1;
            } else {
                marked.push_back(body[i++]);
            }
        }
        std::string text = html::PlainText(marked);
        std::string cleaned;
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '\x01') {
                cleaned.push_back(text[i]);
                continue;
            }
            while (!cleaned.empty() && cleaned.back() == ' ') cleaned.pop_back();
            cleaned.push_back('\n');
            while (i + 1 < text.size() && text[i + 1] == ' ') ++i;
        }
        comment.text = Trim(cleaned);
        if (!comment.text.empty() || !comment.author.empty()) comments.push_back(std::move(comment));
    }
    return comments;
}

int ParsePreviewTokens(const std::string& document, std::int64_t gid,
                       std::map<int, std::string>* tokens) {
    if (tokens == nullptr) return 0;
    const std::string suffix = "/" + std::to_string(gid) + "-";
    int added = 0;
    std::size_t cursor = 0;
    while ((cursor = document.find("/s/", cursor)) != std::string::npos) {
        const std::size_t token_begin = cursor + 3;
        std::size_t token_end = token_begin;
        while (token_end < document.size() && IsTokenCharacter(document[token_end])) ++token_end;
        cursor = token_end;
        if (token_end == token_begin || token_end - token_begin > 32 ||
            document.compare(token_end, suffix.size(), suffix) != 0) {
            continue;
        }
        std::size_t number_end = token_end + suffix.size();
        while (number_end < document.size() &&
               std::isdigit(static_cast<unsigned char>(document[number_end]))) ++number_end;
        const int page = IntegerBefore(document, number_end);
        if (page <= 0) continue;
        if (tokens->emplace(page - 1, document.substr(token_begin, token_end - token_begin)).second)
            ++added;
        cursor = number_end;
    }
    return added;
}

bool ParseGalleryPage(const std::string& document, GalleryPage* page, std::string* error) {
    if (page == nullptr) return Fail(error, "Gallery page output is null");
    GalleryPage parsed;

    const std::size_t image = document.find("id=\"img\"");
    if (image != std::string::npos) {
        const std::size_t tag_begin = document.rfind('<', image);
        const std::size_t tag_end = document.find('>', image);
        if (tag_begin != std::string::npos && tag_end != std::string::npos)
            parsed.image_url = Trim(Attribute(document.substr(tag_begin, tag_end - tag_begin + 1), "src"));
    }
    if (parsed.image_url.empty()) return Fail(error, "Image URL not found on page");

    parsed.skip_hath_key = QuotedAfter(document, "return nl('", '\'');
    if (parsed.skip_hath_key.empty()) parsed.skip_hath_key = QuotedAfter(document, "nl('", '\'');
    parsed.show_key = QuotedAfter(document, "var showkey=\"", '"');

    const std::size_t full = document.find("fullimg");
    if (full != std::string::npos) {
        const std::size_t href = document.rfind("href=\"", full);
        if (href != std::string::npos && full - href < 512) {
            const std::size_t end = document.find('"', href + 6);
            if (end != std::string::npos && end > full)
                parsed.origin_image_url = html::DecodeEntities(document.substr(href + 6, end - href - 6));
        }
    }

    // "<div>10.jpg :: 1280 x 879 :: 184.4 KB</div>"
    const std::size_t info = document.find(" :: ");
    if (info != std::string::npos) {
        const std::size_t open = document.rfind('>', info);
        const std::size_t close = document.find('<', info);
        if (open != std::string::npos && close != std::string::npos && info - open < 512) {
            const std::string text = html::DecodeEntities(document.substr(open + 1, close - open - 1));
            const std::size_t first = text.find(" :: ");
            parsed.file_name = Trim(text.substr(0, first));
            const std::size_t dimensions = first + 4;
            const std::size_t x = text.find(" x ", dimensions);
            if (x != std::string::npos) {
                parsed.width = std::atoi(text.c_str() + dimensions);
                parsed.height = std::atoi(text.c_str() + x + 3);
            }
        }
    }

    *page = std::move(parsed);
    return true;
}

}  // namespace ehviewer
