#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace ehviewer {

struct GallerySummary {
    std::int64_t gid = 0;
    std::string token;
    std::string title;
    std::string path;
    std::string thumb_url;
    std::string category;
    std::string posted;
    int pages = 0;
    // Average rating 0..5 in half stars, -1 when the list does not show it.
    double rating = -1.0;
};

// Converts the star sprite position "background-position:-16px -21px" of a
// list entry into a rating (5 stars minus 16px per star, -21px = half star).
double RatingFromStarSprite(const std::string& style);

struct GalleryComment {
    std::string author;
    std::string posted;  // e.g. "04 November 2022, 06:42"
    std::string score;   // e.g. "+48"; empty for the uploader comment
    std::string text;    // plain text, lines separated by '\n'
    bool uploader = false;
};

struct TagGroup {
    std::string name_space;         // e.g. "parody"; empty for untagged
    std::vector<std::string> tags;  // e.g. "flower knight girl"
};

// Search query for one tag, exact match: parody:"flower knight girl$".
std::string TagSearchQuery(const std::string& name_space, const std::string& tag);

// One preview thumbnail on a detail page. Normal previews are cut from a
// sprite sheet: offset_x/width/height select the part of image_url; large
// previews use the whole image (width == 0).
struct GalleryPreview {
    int page_index = 0;
    std::string image_url;
    int offset_x = 0;
    int width = 0;
    int height = 0;
};

struct GalleryDetail {
    std::int64_t gid = 0;
    std::string token;
    std::string title;
    std::string title_jpn;
    std::string category;
    std::string cover_url;
    std::string uploader;
    std::string posted;
    std::string language;
    std::string file_size;
    std::string favorited;
    std::string rating;
    int pages = 0;
    // Number of "?p=N" preview pages on the detail page (at least 1).
    int preview_page_count = 1;
    // Zero-based gallery page index -> image page token from /s/<token>/<gid>-<n>.
    std::map<int, std::string> page_tokens;
    std::vector<GalleryComment> comments;
    std::vector<TagGroup> tags;
    // From the page script; needed for api.php (rating).
    std::int64_t api_uid = 0;
    std::string api_key;
    std::string api_url;
    // Favorite folder name, empty when the gallery is not a favorite.
    std::string favorite_name;
    // Previews loaded so far (the detail page holds the first preview page).
    std::vector<GalleryPreview> previews;
};

// The add-to-favorites popup (gallerypopups.php?act=addfav).
struct FavoriteSlots {
    std::vector<std::string> names;  // folder 0..9
    int current = -1;                // folder holding this gallery, -1 if none
};

// Folder bar at the top of favorites.php.
struct FavoriteFolder {
    std::string name;
    int count = 0;
};
struct FavoriteFolders {
    std::vector<FavoriteFolder> folders;  // folder 0..9
    int current = -1;                     // shown folder, -1 for "all"
};

struct RatingResult {
    double average = 0.0;
    double user = 0.0;
    int count = 0;
};

// Cursor links of a gallery list page ("?next=<gid>" / "?prev=<gid>").
struct ListNavigation {
    std::string next_url;
    std::string prev_url;
};

struct GalleryPage {
    std::string image_url;
    std::string skip_hath_key;
    std::string show_key;
    std::string origin_image_url;
    // File name reported by the site, e.g. "10.jpg" from "10.jpg :: 1280 x 879".
    std::string file_name;
    int width = 0;
    int height = 0;
};

bool ParseGalleryList(const std::string& document, std::vector<GallerySummary>* galleries,
                      std::string* error = nullptr);
bool ParseGalleryDetail(const std::string& document, std::int64_t gid,
                        const std::string& token, GalleryDetail* detail,
                        std::string* error = nullptr);
// Collects page tokens from any detail preview page (?p=N) into tokens.
// Returns the number of new tokens.
int ParsePreviewTokens(const std::string& document, std::int64_t gid,
                       std::map<int, std::string>* tokens);
bool ParseGalleryPage(const std::string& document, GalleryPage* page,
                      std::string* error = nullptr);
ListNavigation ParseListNavigation(const std::string& document);
bool ParseFavoriteSlots(const std::string& document, FavoriteSlots* slots);
// Preview thumbnails of a detail page (any ?p=N), in page order.
std::vector<GalleryPreview> ParsePreviews(const std::string& document, std::int64_t gid);
// Returns false when the page has no folder bar (not a favorites page).
bool ParseFavoriteFolders(const std::string& document, FavoriteFolders* folders);
bool ParseRatingResponse(const std::string& json, RatingResult* result, std::string* error = nullptr);
// {"method":"rategallery",...}; rating is 1..10 (half stars).
std::string BuildRateRequest(std::int64_t api_uid, const std::string& api_key, std::int64_t gid,
                             const std::string& token, int rating);
std::vector<GalleryComment> ParseComments(const std::string& document);

}  // namespace ehviewer
