#include "core/CookieConfig.h"
#include "core/FileUtil.h"
#include "core/GalleryManifest.h"
#include "core/GalleryParser.h"
#include "core/HtmlUtil.h"
#include "core/I18n.h"
#include "core/Json.h"
#include "core/History.h"
#include "core/Library.h"
#include "core/ListLayout.h"
#include "core/ReleaseInfo.h"
#include "core/Settings.h"
#include "core/Subscriptions.h"
#include "core/SpiderInfo.h"
#include "core/StorageLayout.h"
#include "net/NetworkPlan.h"
#include "reader/ReaderCore.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void Check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::cerr << "FAIL line " << line << ": " << expression << '\n';
    ++failures;
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

void TestCookies() {
    ehviewer::CookieConfig config;
    std::string error;
    CHECK(ehviewer::CookieConfig::Parse(
        "ipb_member_id=123456\n"
        "ipb_pass_hash=0123456789abcdefghijklmnopqrstuv\n",
        &config, &error));
    CHECK(config.proxy.empty());
    CHECK(config.builtin_hosts);
    CHECK(config.doh);

    CHECK(ehviewer::CookieConfig::Parse(
        "# copied from browser\n"
        "Cookie: ipb_member_id=123456; ipb_pass_hash=0123456789abcdefghijklmnopqrstuv; "
        "igneous=mystery\n"
        "proxy=socks://account:secret@192.0.2.1:1080\n"
        "builtin_hosts=yes\n"
        "doh=on\n"
        "domain_fronting=off\n",
        &config, &error));
    CHECK(!config.domain_fronting);
    CHECK(config.ipb_member_id == "123456");
    CHECK(config.proxy == "socks5h://account:secret@192.0.2.1:1080");
    CHECK(config.builtin_hosts);
    CHECK(config.doh);
    CHECK(config.BuildCookieHeader() ==
          "ipb_member_id=123456; ipb_pass_hash=0123456789abcdefghijklmnopqrstuv; "
          "igneous=mystery; nw=1");
    CHECK(config.RedactedSummary().find("123456") == std::string::npos);
    CHECK(config.RedactedSummary().find("mystery") == std::string::npos);
    CHECK(config.RedactedSummary().find("account") == std::string::npos);
    CHECK(config.RedactedSummary().find("secret") == std::string::npos);
    CHECK(config.RedactedSummary().find("192.0.2.1") == std::string::npos);

    CHECK(!ehviewer::CookieConfig::Parse(
        "ipb_member_id=123\nipb_pass_hash=too-short\n", &config, &error));
    CHECK(!ehviewer::CookieConfig::Parse(
        "ipb_member_id=123\n"
        "ipb_pass_hash=0123456789abcdefghijklmnopqrstuv\n"
        "doh=maybe\n", &config, &error));
    CHECK(error.find("maybe") == std::string::npos);
    CHECK(!ehviewer::CookieConfig::Parse(
        "ipb_member_id=123\n"
        "ipb_pass_hash=0123456789abcdefghijklmnopqrstuv\n"
        "proxy=ftp://account:secret@example.test:21\n", &config, &error));
    CHECK(error.find("account") == std::string::npos);
    CHECK(error.find("secret") == std::string::npos);
}

void TestStorageLayout() {
    ehviewer::StorageLayout layout("sdmc:/manga/EhViewerSwitch/");
    CHECK(layout.GalleryDirectory(42) == "sdmc:/manga/EhViewerSwitch/galleries/g_42");
    CHECK(layout.IncomingDirectory(42) == "sdmc:/manga/EhViewerSwitch/.incoming/g_42");
    CHECK(ehviewer::StorageLayout::PageFileName(0, ".JPEG") == "p_00000001.jpg");
    CHECK(ehviewer::StorageLayout::PageFileName(11, "unknown") == "p_00000012.jpg");
    CHECK(ehviewer::StorageLayout::IsSafePhysicalComponent("p_00000001.webp"));
    CHECK(!ehviewer::StorageLayout::IsSafePhysicalComponent("../cover.jpg"));
    CHECK(!ehviewer::StorageLayout::IsSafePhysicalComponent("中文.jpg"));
}

void TestSpiderInfo() {
    const std::string v2 =
        "VERSION2\n00000002\n987654\ngallery-token\n1\n3\n20\n4\n"
        "0 token-zero\n2 token-two\n";
    ehviewer::SpiderInfo info;
    std::string error;
    CHECK(ehviewer::SpiderInfo::Parse(v2, &info, &error));
    CHECK(info.start_page == 2);
    CHECK(info.gid == 987654);
    CHECK(info.preview_per_page == 20);
    CHECK(info.page_tokens.at(2) == "token-two");

    ehviewer::SpiderInfo round_trip;
    CHECK(ehviewer::SpiderInfo::Parse(info.Serialize(), &round_trip, &error));
    CHECK(round_trip.page_tokens == info.page_tokens);

    const std::string v1 =
        "00000001\n123\nlegacy-token\n1\n2\nlegacy-unused\n3\n1 ptoken\n";
    CHECK(ehviewer::SpiderInfo::Parse(v1, &info, &error));
    CHECK(info.pages == 3);
    CHECK(info.preview_per_page == 0);
}

void TestManifest() {
    ehviewer::GalleryManifest manifest;
    manifest.gid = 321;
    manifest.token = "token";
    manifest.title = "测试漫画";
    manifest.original_directory_name = "[作者] 测试漫画";
    manifest.total_pages = 2;
    manifest.files.push_back({"p_00000001.jpg", "001 第一页.jpg", "page", 0, 123, "abc"});
    manifest.files.push_back({"cover.jpg", "封面.jpg", "cover", -1, 45, "def"});
    std::string error;
    CHECK(manifest.Validate(&error));
    const std::string json = manifest.SerializeJson();
    CHECK(json.find("p_00000001.jpg") != std::string::npos);
    CHECK(json.find("001 第一页.jpg") != std::string::npos);

    manifest.files[0].original_name = "../escape.jpg";
    CHECK(!manifest.Validate(&error));
}

void TestGalleryParsers() {
    std::string html;
    std::string error;
    CHECK(ehviewer::file::ReadAll(
        "../app/src/test/resources/com/hippo/ehviewer/client/parser/GalleryListParserNew3.html",
        &html, &error));
    std::vector<ehviewer::GallerySummary> galleries;
    CHECK(ehviewer::ParseGalleryList(html, &galleries, &error));
    CHECK(galleries.size() >= 20);
    CHECK(galleries.front().gid == 2426224);
    CHECK(galleries.front().token == "85f3da11e1");
    CHECK(galleries.front().title.find("Onegai") != std::string::npos);

    CHECK(galleries.front().thumb_url ==
          "https://exhentai.org/t/a3/71/a371c52f0fbcc7d0156f4f62f0abc16e22d433a9-4185307-2450-3500-jpg_250.jpg");
    CHECK(galleries.front().pages == 17);
    CHECK(galleries.front().category == "Manga");
    CHECK(galleries.front().posted == "2023-01-06 16:58");
    CHECK(galleries.front().rating == 4.5);  // background-position:0px -21px
    CHECK(ehviewer::RatingFromStarSprite("background-position:-16px -1px;opacity:1") == 4.0);
    CHECK(ehviewer::RatingFromStarSprite("background-position:-48px -21px") == 1.5);
    CHECK(ehviewer::RatingFromStarSprite("color:red") == -1.0);
    std::size_t with_thumbs = 0;
    for (const auto& gallery : galleries) with_thumbs += gallery.thumb_url.empty() ? 0 : 1;
    CHECK(with_thumbs == galleries.size());

    const char* layouts[] = {"ECompat", "EExtended", "EMinimal", "EThumbnail", "ExThumbnail"};
    for (const char* layout : layouts) {
        CHECK(ehviewer::file::ReadAll(
            std::string("../app/src/test/resources/com/hippo/ehviewer/client/parser/"
                        "GalleryListParserTest") + layout + ".html",
            &html, &error));
        CHECK(ehviewer::ParseGalleryList(html, &galleries, &error));
        CHECK(galleries.size() >= 20);
        CHECK(!galleries.empty() && !galleries.front().thumb_url.empty());
        CHECK(!galleries.empty() && galleries.front().thumb_url.find("ehgt.org/g/") == std::string::npos);
        CHECK(!galleries.empty() && galleries.front().pages > 0);
        CHECK(!galleries.empty() && galleries.front().category == "Non-H");
    }

    CHECK(ehviewer::file::ReadAll(
        "../app/src/test/resources/com/hippo/ehviewer/client/parser/GalleryDetail.html",
        &html, &error));
    ehviewer::GalleryDetail detail;
    CHECK(ehviewer::ParseGalleryDetail(html, 3101249, "7fb0ea5a0e", &detail, &error));
    CHECK(!detail.title.empty());
    CHECK(detail.pages == 838);
    CHECK(detail.cover_url.find("-jpg_250.jpg") != std::string::npos);
    CHECK(detail.file_size == "172.2 MiB");
    CHECK(detail.language == "Japanese");
    CHECK(detail.posted == "2024-10-25 08:20");
    CHECK(detail.favorited == "2225 times");
    CHECK(detail.uploader == "longshenadu");
    CHECK(detail.rating == "4.46 (189)");
    CHECK(detail.tags.size() >= 3);
    if (!detail.tags.empty()) {
        CHECK(detail.tags[0].name_space == "parody");
        CHECK(!detail.tags[0].tags.empty() && detail.tags[0].tags[0] == "flower knight girl");
    }
    CHECK(ehviewer::TagSearchQuery("parody", "flower knight girl") == "parody:\"flower knight girl$\"");
    CHECK(ehviewer::TagSearchQuery("female", "milf") == "female:milf$");
    CHECK(detail.api_uid == 4596468);
    CHECK(detail.api_key.size() > 8);
    CHECK(detail.api_url == "https://s.exhentai.org/api.php");
    CHECK(detail.favorite_name.empty());
    ehviewer::GalleryDetail favorited;
    CHECK(ehviewer::ParseGalleryDetail(
        "<h1 id=\"gn\">T</h1><div id=\"fav\"><div class=\"i\" style=\"\" title=\"Favorites 3\"></div></div>",
        1, "t", &favorited, &error));
    CHECK(favorited.favorite_name == "Favorites 3");
    CHECK(detail.comments.size() >= 3);
    if (detail.comments.size() >= 3) {
        CHECK(detail.comments[0].uploader);
        CHECK(detail.comments[0].author == "longshenadu");
        CHECK(detail.comments[0].posted == "25 October 2024, 08:20");
        CHECK(detail.comments[0].text.find("\n") != std::string::npos);
        CHECK(detail.comments[0].text.find("<br") == std::string::npos);
        CHECK(detail.comments[0].text.find("It's basically") != std::string::npos);
        CHECK(!detail.comments[2].uploader);
        CHECK(!detail.comments[2].score.empty());
    }
    CHECK(detail.preview_page_count > 1);
    CHECK(detail.page_tokens.size() >= 20);
    CHECK(detail.page_tokens.count(0) == 1 && detail.page_tokens.at(0) == "c78aece64f");
    CHECK(detail.page_tokens.count(1) == 1 && detail.page_tokens.at(1) == "0c699dde4a");

    CHECK(ehviewer::file::ReadAll(
        "../app/src/test/resources/com/hippo/ehviewer/client/parser/GalleryPageParserTest.html",
        &html, &error));
    ehviewer::GalleryPage page;
    CHECK(ehviewer::ParseGalleryPage(html, &page, &error));
    CHECK(page.image_url ==
          "http://108.6.41.160:2688/h/5c63e9a5810d8d9c873d9e0dfaadc4a0d70a13bf-188862-1280-879-jpg/"
          "keystamp=1550291700-145ecbbb10;fileindex=67290651;xres=1280/10.jpg");
    CHECK(page.skip_hath_key == "26664-430636");
    CHECK(page.show_key == "ghz0e5m98a4");
    CHECK(page.origin_image_url ==
          "https://e-hentai.org/fullimg.php?gid=1363978&page=10&key=qt2hwrx98a4");
    CHECK(page.file_name == "10.jpg");
    CHECK(page.width == 1280);
    CHECK(page.height == 879);
    CHECK(!ehviewer::ParseGalleryPage("<html>509</html>", &page, &error));

    const ehviewer::ListNavigation navigation = ehviewer::ParseListNavigation(
        "<div class=\"searchnav\"><div><a id=\"ufirst\">&lt;&lt; First</a></div>"
        "<div><span id=\"uprev\">&lt; Prev</span></div>"
        "<div><a id=\"unext\" href=\"https://exhentai.org/?f_search=a&amp;next=4230495\">Next &gt;</a></div>");
    CHECK(navigation.next_url == "https://exhentai.org/?f_search=a&next=4230495");
    CHECK(navigation.prev_url.empty());
    CHECK(ehviewer::html::UrlEncode("language:chinese 中$") == "language%3Achinese+%E4%B8%AD%24");

    for (const char* name : {"TopListGallary.html", "FavoritesListParser.html"}) {
        CHECK(ehviewer::file::ReadAll(
            std::string("../app/src/test/resources/com/hippo/ehviewer/client/parser/") + name,
            &html, &error));
        CHECK(ehviewer::ParseGalleryList(html, &galleries, &error));
        CHECK(galleries.size() == 50);
    }

    // Favorites popup: names come from the labels; "favdel" marks a favorite.
    std::string popup;
    for (int index = 0; index < 10; ++index) {
        const std::string n = std::to_string(index);
        popup += "<input type=\"radio\" name=\"favcat\" value=\"" + n + "\" id=\"fav" + n + "\"" +
                 (index == 3 ? " checked=\"checked\"" : "") + " />"
                 "<div onclick=\"document.getElementById('fav" + n + "').click()\">" +
                 (index == 1 ? "本子 &amp; 漫画" : "Favorites " + n) + "</div>";
    }
    ehviewer::FavoriteSlots slots;
    CHECK(ehviewer::ParseFavoriteSlots(popup, &slots));
    CHECK(slots.names.size() == 10 && slots.names[1] == "本子 & 漫画" && slots.names[9] == "Favorites 9");
    CHECK(slots.current == -1);
    CHECK(ehviewer::ParseFavoriteSlots(popup + "<input type=\"radio\" value=\"favdel\" />", &slots));
    CHECK(slots.current == 3);
    CHECK(!ehviewer::ParseFavoriteSlots("<html></html>", &slots));

    // Preview sprites of a detail page (structure copied from the live page).
    std::string grid = "<div id=\"gdt\" class=\"gt200\">";
    for (int index = 0; index < 3; ++index) {
        grid += "<a href=\"https://exhentai.org/s/ab12cd34ef/777-" + std::to_string(index + 1) +
                "\"><div title=\"Page\" style=\"width:176px;height:300px;background:transparent "
                "url(https://abc.hath.network/c2/xyz/777-0.webp) -" + std::to_string(index * 200) +
                "px 0 no-repeat\"></div></a>";
    }
    grid += "<a href=\"https://exhentai.org/s/ffff000011/777-4\"><img src=\"https://ehgt.org/t/big.jpg\" /></a></div>";
    const std::vector<ehviewer::GalleryPreview> previews = ehviewer::ParsePreviews(grid, 777);
    CHECK(previews.size() == 4);
    CHECK(previews[0].page_index == 0 && previews[0].offset_x == 0 && previews[0].width == 176 &&
          previews[0].height == 300 && previews[0].image_url == "https://abc.hath.network/c2/xyz/777-0.webp");
    CHECK(previews[2].page_index == 2 && previews[2].offset_x == 400);
    CHECK(previews[3].page_index == 3 && previews[3].width == 0 && previews[3].image_url == "https://ehgt.org/t/big.jpg");
    CHECK(ehviewer::ParsePreviews("<html></html>", 777).empty());

    // Folder bar of favorites.php (structure copied from the live page).
    std::string bar = "<div class=\"ido\">";
    for (int index = 0; index < 10; ++index) {
        const std::string n = std::to_string(index);
        bar += std::string("<div class=\"fp") + (index == 2 ? " fps" : "") +
               "\" onclick=\"document.location='https://exhentai.org/favorites.php?favcat=" + n + "'\">\n"
               "\t<div style=\"font-weight:bold\">" + std::to_string(index * 10 + 1) + "</div>\n"
               "\t<div class=\"i\" title=\"Favorites " + n + "\"></div>\n"
               "\t<div style=\"float:left\">" + (index == 1 ? "本子 &amp; 漫画" : "Favorites " + n) +
               "</div>\n</div>\n";
    }
    ehviewer::FavoriteFolders folders;
    CHECK(ehviewer::ParseFavoriteFolders(
        bar + "<div class=\"fp\" onclick=\"document.location='https://exhentai.org/favorites.php'\">"
              "Show All Favorites</div></div>", &folders));
    CHECK(folders.folders.size() == 10 && folders.current == 2);
    CHECK(folders.folders[1].name == "本子 & 漫画" && folders.folders[1].count == 11);
    CHECK(folders.folders[9].name == "Favorites 9" && folders.folders[9].count == 91);
    for (std::size_t at = bar.find(" fps"); at != std::string::npos; at = bar.find(" fps"))
        bar.erase(at, 4);
    CHECK(ehviewer::ParseFavoriteFolders(
        bar + "<div class=\"fp fps\" onclick=\"document.location='https://exhentai.org/favorites.php'\">"
              "Show All Favorites</div>", &folders));
    CHECK(folders.current == -1 && folders.folders[0].count == 1);
    CHECK(!ehviewer::ParseFavoriteFolders("<html></html>", &folders));

    ehviewer::RatingResult rating;
    CHECK(ehviewer::ParseRatingResponse(
        "{\"rating_avg\":4.12,\"rating_usr\":4.5,\"rating_cnt\":123,\"rating_width\":\"60\"}", &rating, &error));
    CHECK(rating.average == 4.12 && rating.user == 4.5 && rating.count == 123);
    CHECK(!ehviewer::ParseRatingResponse("{\"error\":\"Key mismatch\"}", &rating, &error));
    CHECK(error == "Key mismatch");
    CHECK(ehviewer::BuildRateRequest(7, "ab12", 99, "cd34", 9) ==
          "{\"method\":\"rategallery\",\"apiuid\":7,\"apikey\":\"ab12\",\"gid\":99,"
          "\"token\":\"cd34\",\"rating\":9}");
    CHECK(ehviewer::BuildRateRequest(7, "ab\"12", 99, "cd34", 9).empty());
    CHECK(ehviewer::BuildRateRequest(7, "ab12", 99, "cd34", 11).empty());
}

void TestNetworkPlan() {
    using ehviewer::RouteKind;
    ehviewer::HttpRequestOptions options;
    options.proxy = "http://192.0.2.1:7890";
    options.builtin_hosts = true;
    options.doh = true;
    options.domain_fronting = true;

    // ExHentai: proxy, two fronting addresses, then the classic routes.
    auto plan = ehviewer::BuildAttemptPlan(options, "https://exhentai.org/g/1/a/");
    CHECK(plan.size() == 6);
    CHECK(plan.size() == 6 && plan[1].kind == RouteKind::Fronted &&
          plan[1].options.front_address == "178.175.128.252" && plan[1].options.proxy.empty());
    CHECK(plan.size() == 6 && plan[2].kind == RouteKind::Fronted &&
          plan[2].options.front_address == "178.175.129.252");
    CHECK(plan.size() == 6 && plan[1].options.connect_timeout_seconds <= 6);
    ehviewer::PreferRoute(&plan, RouteKind::Fronted);
    CHECK(plan[0].kind == RouteKind::Fronted && plan[1].kind == RouteKind::Fronted);
    CHECK(plan[0].options.front_address == "178.175.128.252");
    CHECK(plan[2].kind == RouteKind::Proxy);
    options.domain_fronting = false;
    CHECK(ehviewer::BuildAttemptPlan(options, "https://exhentai.org/").size() == 4);
    options.domain_fronting = true;
    // Cloudflare-hosted e-hentai.org and plain http have no fronting route.
    CHECK(ehviewer::BuildAttemptPlan(options, "https://e-hentai.org/").size() == 4);
    CHECK(ehviewer::BuildAttemptPlan(options, "http://ehgt.org/x").size() == 4);
    CHECK(ehviewer::BuildAttemptPlan(options, "https://ehgt.org/w/1.webp").size() == 6);

    CHECK(ehviewer::UrlHost("https://S.ExHentai.org:443/t/x?y") == "s.exhentai.org");
    CHECK(ehviewer::UrlHost("not a url").empty());
    CHECK(ehviewer::FrontedUrl("https://exhentai.org/s/ab/1-2?nl=3", "1.2.3.4") ==
          "https://1.2.3.4/s/ab/1-2?nl=3");
    CHECK(ehviewer::FrontedUrl("https://exhentai.org", "1.2.3.4") == "https://1.2.3.4/");
    CHECK(ehviewer::FrontingAddresses("e-hentai.org").empty());
    CHECK(ehviewer::FrontingAddresses("evil-exhentai.org").empty());

    plan = ehviewer::BuildAttemptPlan(options, "https://e-hentai.org/");
    CHECK(plan.size() == 4);
    CHECK(plan[0].kind == RouteKind::Proxy && plan[0].options.proxy == options.proxy);
    CHECK(!plan[0].options.builtin_hosts && !plan[0].options.doh);
    CHECK(plan[1].kind == RouteKind::BuiltinHosts && plan[1].options.proxy.empty());
    CHECK(plan[1].options.builtin_hosts && !plan[1].options.doh);
    CHECK(plan[2].kind == RouteKind::Doh && plan[2].options.doh && !plan[2].options.builtin_hosts);
    CHECK(plan[3].kind == RouteKind::SystemDns);
    CHECK(plan[3].options.proxy.empty() && !plan[3].options.builtin_hosts && !plan[3].options.doh);

    ehviewer::PreferRoute(&plan, RouteKind::Doh);
    CHECK(plan[0].kind == RouteKind::Doh);
    CHECK(plan[1].kind == RouteKind::Proxy);
    CHECK(plan[2].kind == RouteKind::BuiltinHosts);

    options.proxy.clear();
    options.builtin_hosts = false;
    options.doh = false;
    plan = ehviewer::BuildAttemptPlan(options, "https://e-hentai.org/");
    CHECK(plan.size() == 1 && plan[0].kind == RouteKind::SystemDns);
    ehviewer::PreferRoute(&plan, RouteKind::Proxy);
    CHECK(plan.size() == 1);

    CHECK(ehviewer::IsSiteHost("https://exhentai.org/g/1/a/"));
    CHECK(ehviewer::IsSiteHost("https://s.exhentai.org/t/aa/bb/x.jpg"));
    CHECK(ehviewer::IsSiteHost("https://ul.EHGT.org:443/x.jpg"));
    CHECK(ehviewer::IsSiteHost("https://e-hentai.org"));
    CHECK(!ehviewer::IsSiteHost("http://e-hentai.org/"));
    CHECK(!ehviewer::IsSiteHost("http://108.6.41.160:2688/h/abc/10.jpg"));
    CHECK(!ehviewer::IsSiteHost("https://evil-e-hentai.org/"));
    CHECK(!ehviewer::IsSiteHost("https://e-hentai.org.evil.test/"));
    CHECK(!ehviewer::IsSiteHost("https://e-hentai.org@evil.test/"));
}

void TestJsonAndManifestRoundTrip() {
    ehviewer::json::Value value;
    std::string error;
    CHECK(ehviewer::json::Parse(
        "{\"a\": \"x\\u4e2d\\/\\\"\", \"n\": -12, \"list\": [1, true, null, {\"k\": \"v\"}]}",
        &value, &error));
    CHECK(value.GetString("a") == "x中/\"");
    CHECK(value.GetInt("n") == -12);
    CHECK(value.Find("list") != nullptr && value.Find("list")->array.size() == 4);
    CHECK(!ehviewer::json::Parse("{\"a\": }", &value, &error));
    CHECK(!ehviewer::json::Parse("[1, 2", &value, &error));
    CHECK(!ehviewer::json::Parse("{} trailing", &value, &error));

    ehviewer::GalleryManifest manifest;
    manifest.gid = 99;
    manifest.token = "abc";
    manifest.title = "标题 \"quoted\"\n";
    manifest.original_directory_name = "99-标题";
    manifest.total_pages = 2;
    manifest.current_page = 1;
    manifest.files.push_back({"p_00000001.jpg", "99-标题/00000001.jpg", "page", 0, 10, ""});
    manifest.files.push_back({"p_00000002.png", "99-标题/00000002.png", "page", 1, 20, ""});
    ehviewer::GalleryManifest parsed;
    CHECK(ehviewer::GalleryManifest::Parse(manifest.SerializeJson(), &parsed, &error));
    CHECK(parsed.gid == 99);
    CHECK(parsed.title == manifest.title);
    CHECK(parsed.current_page == 1);
    CHECK(parsed.files.size() == 2);
    CHECK(parsed.files[1].physical_name == "p_00000002.png");
    CHECK(parsed.files[1].page_index == 1);
    CHECK(parsed.files[1].size == 20);
    CHECK(!ehviewer::GalleryManifest::Parse(
        "{\"formatVersion\":1,\"gid\":1,\"token\":\"t\",\"files\":"
        "[{\"physicalName\":\"../x\",\"originalName\":\"a.jpg\",\"pageIndex\":0}]}",
        &parsed, &error));
}

void TestStorageNames() {
    using ehviewer::StorageLayout;
    CHECK(StorageLayout::PageIndexFromFileName("p_00000012.jpg") == 11);
    CHECK(StorageLayout::PageIndexFromFileName("p_00000001.webp") == 0);
    CHECK(StorageLayout::PageIndexFromFileName("p_00000000.jpg") == -1);
    CHECK(StorageLayout::PageIndexFromFileName("p_00000001.jpg.new") == -1);
    CHECK(StorageLayout::PageIndexFromFileName("cover.jpg") == -1);
    CHECK(StorageLayout::OriginalDirectoryName(7, "a/b:c?  ") == "7-a b c");
    CHECK(StorageLayout::OriginalDirectoryName(7, "   ") == "7");
    CHECK(StorageLayout::OriginalPageName(0, "png") == "00000001.png");
    CHECK(StorageLayout::DetectImageExtension(std::string("\xff\xd8\xff\xe0", 4)) == "jpg");
    CHECK(StorageLayout::DetectImageExtension(std::string("\x89PNG\r\n\x1a\n", 8)) == "png");
    CHECK(StorageLayout::DetectImageExtension("GIF89a....") == "gif");
    CHECK(StorageLayout::DetectImageExtension(std::string("RIFF\x10\x00\x00\x00WEBPVP8 ", 16)) == "webp");
    CHECK(StorageLayout::DetectImageExtension("<html>") == "");
}

void TestDirectoryOperations() {
    const std::string root = "build-host/test-data/tree";
    std::string error;
    CHECK(ehviewer::file::RemoveTree(root, &error));
    CHECK(ehviewer::file::WriteAllAtomic(root + "/b.txt", "b", &error));
    CHECK(ehviewer::file::WriteAllAtomic(root + "/a/inner.txt", "inner", &error));
    std::vector<ehviewer::file::DirectoryEntry> entries;
    CHECK(ehviewer::file::ListDirectory(root, &entries, &error));
    CHECK(entries.size() == 2);
    CHECK(entries.size() == 2 && entries[0].name == "a" && entries[0].directory);
    CHECK(entries.size() == 2 && entries[1].name == "b.txt" && !entries[1].directory);
    CHECK(ehviewer::file::FileSize(root + "/a/inner.txt") == 5);
    CHECK(ehviewer::file::Rename(root + "/a", root + "/renamed", &error));
    CHECK(ehviewer::file::Exists(root + "/renamed/inner.txt"));
    CHECK(ehviewer::file::RemoveTree(root, &error));
    CHECK(!ehviewer::file::Exists(root));
}

void TestSettingsAndHistory() {
    ehviewer::Settings settings;
    std::string error;
    CHECK(ehviewer::Settings::Parse("", &settings, &error));
    CHECK(settings.reader_orientation == 0 && settings.prefetch_pages == 3 && settings.domain_fronting == -1);
    settings.language = ehviewer::Settings::Language::English;
    settings.reader_orientation = 2;
    settings.prefetch_pages = 6;
    settings.excluded_categories = 512 | 1;
    settings.domain_fronting = 0;
    settings.show_input_debug = true;
    ehviewer::Settings parsed;
    CHECK(ehviewer::Settings::Parse(settings.Serialize(), &parsed, &error));
    CHECK(parsed.language == ehviewer::Settings::Language::English);
    CHECK(parsed.reader_orientation == 2 && parsed.prefetch_pages == 6);
    CHECK(parsed.excluded_categories == 513 && parsed.domain_fronting == 0 && parsed.show_input_debug);
    // Invalid values keep defaults; unknown keys are ignored.
    CHECK(ehviewer::Settings::Parse("prefetch_pages=99\nfuture_key=1\nreader_orientation=x\n", &parsed, &error));
    CHECK(parsed.prefetch_pages == 3 && parsed.reader_orientation == 0);
    CHECK(!parsed.proxy_enabled && parsed.proxy_url.empty());
    settings.proxy_enabled = true;
    settings.proxy_url = "socks5h://u:p@192.0.2.1:1080";
    CHECK(ehviewer::Settings::Parse(settings.Serialize(), &parsed, &error));
    CHECK(parsed.proxy_enabled && parsed.proxy_url == settings.proxy_url);
    CHECK(ehviewer::Settings::Parse("proxy_url=ftp://x\nproxy_enabled=1\n", &parsed, &error));
    CHECK(parsed.proxy_url.empty() && parsed.proxy_enabled);
    CHECK(ehviewer::IsValidProxyUrl("http://192.168.1.10:7890"));
    CHECK(!ehviewer::IsValidProxyUrl("http://a b:1"));
    CHECK(!ehviewer::IsValidProxyUrl("http://"));
    CHECK(ehviewer::RedactProxyUrl("socks5h://u:p@192.0.2.1:1080") == "socks5h://***@192.0.2.1:1080");
    CHECK(ehviewer::RedactProxyUrl("http://192.0.2.1:7890") == "http://192.0.2.1:7890");
    int mask = 0;
    for (const auto& category : ehviewer::kCategories) mask |= category.bit;
    CHECK(mask == 1023);

    ehviewer::History history;
    for (int i = 1; i <= 205; ++i) {
        ehviewer::GallerySummary entry;
        entry.gid = i;
        entry.token = "abc" + std::to_string(i);
        entry.title = "标题 \"" + std::to_string(i) + "\"";
        entry.rating = i % 2 == 0 ? 4.5 : -1.0;
        history.Add(entry);
    }
    CHECK(history.Entries().size() == ehviewer::History::kLimit);
    CHECK(history.Entries().front().gid == 205);
    ehviewer::GallerySummary again;
    again.gid = 100;
    again.token = "abc100";
    history.Add(again);
    CHECK(history.Entries().front().gid == 100 && history.Entries().size() == ehviewer::History::kLimit);
    ehviewer::GallerySummary bad;
    bad.gid = 7;
    bad.token = "../x";
    history.Add(bad);
    CHECK(history.Entries().front().gid == 100);
    ehviewer::History loaded;
    CHECK(ehviewer::History::Parse(history.Serialize(), &loaded, &error));
    CHECK(loaded.Entries().size() == history.Entries().size());
    CHECK(loaded.Entries()[1].title == history.Entries()[1].title);
    CHECK(loaded.Entries()[0].path == "/g/100/abc100/");
    CHECK(loaded.Entries()[1].rating == history.Entries()[1].rating);
    CHECK(loaded.Entries()[2].rating == history.Entries()[2].rating);
    loaded.Remove(100);
    CHECK(loaded.Entries().front().gid == 205);

    ehviewer::Subscriptions subscriptions;
    CHECK(subscriptions.Add("  parody:\"blue archive$\"  "));
    CHECK(subscriptions.Add("language:chinese", "中文"));
    CHECK(!subscriptions.Add("parody:\"blue archive$\""));
    CHECK(!subscriptions.Add("   "));
    CHECK(subscriptions.Entries().size() == 2);
    CHECK(subscriptions.Entries()[0].name == "parody:\"blue archive$\"");
    ehviewer::Subscriptions reloaded;
    CHECK(ehviewer::Subscriptions::Parse(subscriptions.Serialize(), &reloaded, &error));
    CHECK(reloaded.Entries().size() == 2 && reloaded.Entries()[1].name == "中文");
    CHECK(reloaded.Entries()[0].query == "parody:\"blue archive$\"");
    CHECK(reloaded.Remove(0) && !reloaded.Remove(5) && reloaded.Entries().size() == 1);
    CHECK(reloaded.Contains("language:chinese"));
}

// Every Chinese literal shown by the UI must have an English entry, except
// a few internal keywords used only to colour the status bar.
void TestTranslations() {
    namespace i18n = ehviewer::i18n;
    std::string offending;
    CHECK(i18n::TableLooksTranslated(&offending));
    if (!offending.empty()) std::cerr << "Untranslated value for: " << offending << '\n';
    CHECK(std::string(i18n::T("设置")) == "设置");
    i18n::SetEnglish(true);
    CHECK(std::string(i18n::T("设置")) == "Settings");
    CHECK(i18n::T(std::string("未知 key")) == "未知 key");
    i18n::SetEnglish(false);

    const char* const internal[] = {"失败", "错误", "异常", "超时", "失效", "正在", "请求进行",
                                    "取消网络", "成功", "已加载"};
    for (const char* source : {"source/App.cpp", "source/AppInput.cpp", "source/AppLibrary.cpp",
                               "source/AppList.cpp", "source/AppNetwork.cpp", "source/AppSettings.cpp",
                               "source/AppShared.h", "source/ui/Ui.cpp", "source/ui/UiGallery.cpp",
                               "source/ui/UiLocal.cpp", "source/ui/UiShared.h", "source/ui/ReaderView.cpp", "source/ui/ReaderOverlay.cpp",
                               "source/download/Downloader.cpp", "source/net/NetworkPlan.cpp", "source/net/Updater.cpp"}) {
        std::string text;
        std::string error;
        CHECK(ehviewer::file::ReadAll(source, &text, &error));
        std::size_t cursor = 0;
        while ((cursor = text.find('"', cursor)) != std::string::npos) {
            // Skip character literals like '"'.
            if (cursor > 0 && text[cursor - 1] == '\'') { ++cursor; continue; }
            std::size_t end = cursor + 1;
            while (end < text.size() && text[end] != '"' && text[end] != '\n') {
                if (text[end] == '\\') ++end;
                ++end;
            }
            if (end >= text.size() || text[end] != '"') { cursor = end; continue; }
            std::string literal = text.substr(cursor + 1, end - cursor - 1);
            cursor = end + 1;
            const bool han = std::any_of(literal.begin(), literal.end(), [](char c) {
                const unsigned char byte = static_cast<unsigned char>(c);
                return byte >= 0xe4 && byte <= 0xe9;
            });
            if (!han) continue;
            // Undo the two escapes used in UI strings.
            std::string unescaped;
            for (std::size_t i = 0; i < literal.size(); ++i) {
                if (literal[i] == '\\' && i + 1 < literal.size()) {
                    unescaped.push_back(literal[i + 1] == 'n' ? '\n' : literal[i + 1]);
                    ++i;
                } else {
                    unescaped.push_back(literal[i]);
                }
            }
            bool allowed = i18n::HasTranslation(unescaped);
            for (const char* keyword : internal) allowed = allowed || unescaped == keyword;
            if (!allowed) {
                std::cerr << "Missing English for " << source << ": \"" << unescaped << "\"\n";
                CHECK(false);
            }
        }
    }
}

void TestListLayout() {
    const ehviewer::ListGeometry rows = ehviewer::GalleryListGeometry(0);
    CHECK(rows.columns == 1 && rows.Item(2).y == 2 * (84 + 6));
    CHECK(rows.ContentHeight(25) == 25 * 90 - 6);
    CHECK(rows.MaxScroll(3) == 0.0);
    CHECK(rows.ScrollToShow(0, 500.0, 25) == 0.0);
    // Item 10 ends at 984; it must end inside the 538px viewport.
    const double scroll = rows.ScrollToShow(10, 0.0, 25);
    CHECK(scroll >= 984 - 538 && scroll <= 10 * 90);
    CHECK(rows.Move(0, 25, 0, -1) == 0 && rows.Move(24, 25, 0, 1) == 24 && rows.Move(3, 25, 0, 1) == 4);

    const ehviewer::ListGeometry grid = ehviewer::GalleryListGeometry(1);
    CHECK(grid.columns == 5 && grid.item_width == 192);
    CHECK(grid.Item(6).x == grid.origin_x + 192 + 14 && grid.Item(6).y == 346 + 14);
    CHECK(grid.Move(2, 25, 0, 1) == 7 && grid.Move(7, 25, 1, 0) == 8 && grid.Move(7, 25, 0, -1) == 2);
    CHECK(grid.Move(18, 22, 0, 1) == 21);  // partial last row
    CHECK(grid.Move(21, 22, 0, 1) == 21);
    CHECK(grid.FirstVisible(400.0, 25) == 5);
    CHECK(grid.MaxScroll(25) == 5 * 360 - 14 - 538);
}

void TestLibrary() {
    const std::string root = "build-host/test-data/library";
    std::string error;
    CHECK(ehviewer::file::RemoveTree(root, &error));
    const ehviewer::StorageLayout layout(root);

    ehviewer::GalleryManifest manifest;
    manifest.gid = 42;
    manifest.token = "tok";
    manifest.title = "书";
    manifest.total_pages = 2;
    manifest.original_directory_name = "42-书";
    // Deliberately out of order; reading order comes from pageIndex.
    manifest.files.push_back({"p_00000002.png", "42-书/00000002.png", "page", 1, 1, ""});
    manifest.files.push_back({"p_00000001.jpg", "42-书/00000001.jpg", "page", 0, 1, ""});
    manifest.files.push_back({"cover.jpg", "42-书/cover.jpg", "cover", -1, 1, ""});
    CHECK(ehviewer::file::WriteAllAtomic(layout.ManifestPath(42), manifest.SerializeJson(), &error));
    // Invalid or mismatched entries are ignored.
    CHECK(ehviewer::file::WriteAllAtomic(layout.ManifestPath(7), "{broken", &error));
    manifest.gid = 8;
    CHECK(ehviewer::file::WriteAllAtomic(layout.ManifestPath(9), manifest.SerializeJson(), &error));

    std::vector<ehviewer::LibraryEntry> entries;
    CHECK(ehviewer::ScanLibrary(layout, &entries, &error));
    CHECK(entries.size() == 1);
    if (entries.size() == 1) {
        CHECK(entries[0].gid == 42);
        CHECK(entries[0].cover_path == layout.GalleryDirectory(42) + "/cover.jpg");
        const auto pages = ehviewer::PagePaths(entries[0]);
        CHECK(pages.size() == 2);
        CHECK(pages.size() == 2 && pages[0] == layout.GalleryDirectory(42) + "/p_00000001.jpg");
        CHECK(ehviewer::SaveReadingProgress(&entries[0], 5, &error));
        CHECK(entries[0].manifest.current_page == 1);
        CHECK(ehviewer::ScanLibrary(layout, &entries, &error));
        CHECK(entries.size() == 1 && entries[0].manifest.current_page == 1);
    }

    // A gallery still downloading: manifest without pages plus one page file.
    ehviewer::GalleryManifest partial;
    partial.gid = 50;
    partial.token = "t";
    partial.title = "downloading";
    partial.total_pages = 3;
    const std::string incoming = layout.IncomingDirectory(50);
    CHECK(ehviewer::file::WriteAllAtomic(incoming + "/manifest.json", partial.SerializeJson(), &error));
    CHECK(ehviewer::file::WriteAllAtomic(incoming + "/p_00000002.webp", "x", &error));
    CHECK(ehviewer::ScanLibrary(layout, &entries, &error));
    CHECK(entries.size() == 2);
    if (entries.size() == 2) {
        CHECK(entries[0].gid == 50 && !entries[0].complete && entries[0].pages_present == 1);
        CHECK(entries[1].gid == 42 && entries[1].complete);
    }
    const auto directories = ehviewer::PageDirectories(layout, 50);
    CHECK(ehviewer::FindPageFile(directories, 1) == incoming + "/p_00000002.webp");
    CHECK(ehviewer::FindPageFile(directories, 0).empty());
    CHECK(ehviewer::SaveReadingProgress(layout, 50, 2, &error));
    CHECK(ehviewer::ScanLibrary(layout, &entries, &error));
    CHECK(!entries.empty() && entries[0].manifest.current_page == 2);
    CHECK(!ehviewer::SaveReadingProgress(layout, 777, 0, &error));
    CHECK(ehviewer::file::RemoveTree(root, &error));
}

void TestAtomicFile() {
    const std::string directory = "build-host/test-data/nested";
    const std::string path = directory + "/value.txt";
    std::string error;
    CHECK(ehviewer::file::CreateDirectoryRecursive(directory, &error));
    CHECK(ehviewer::file::WriteAllAtomic(path, "first", &error));
    CHECK(ehviewer::file::WriteAllAtomic(path, "second", &error));
    std::string data;
    CHECK(ehviewer::file::ReadAll(path, &data, &error));
    CHECK(data == "second");
    CHECK(ehviewer::file::Remove(path));
}

void TestReleaseInfo() {
    ehviewer::ReleaseInfo info;
    std::string error;
    const std::string json =
        "{\"tag_name\":\"v0.5.3\",\"body\":\"notes\",\"assets\":["
        "{\"name\":\"EhViewerSwitch-0.5.3.zip\",\"size\":10,\"browser_download_url\":\"https://x/zip\"},"
        "{\"name\":\"EhViewerSwitch.nro\",\"size\":10450260,\"browser_download_url\":\"https://x/nro\"}]}";
    CHECK(ehviewer::ParseReleaseInfo(json, ehviewer::kUpdateAssetName, &info, &error));
    CHECK(info.tag == "v0.5.3" && info.notes == "notes");
    CHECK(info.asset_url == "https://x/nro" && info.asset_size == 10450260ULL);
    // A release without the NRO is valid but has no asset.
    CHECK(ehviewer::ParseReleaseInfo("{\"tag_name\":\"v1.0.0\",\"assets\":[]}", "a.nro", &info, &error));
    CHECK(info.asset_url.empty());
    CHECK(!ehviewer::ParseReleaseInfo("{\"message\":\"API rate limit exceeded\"}", "a.nro", &info, &error));
    CHECK(error == "API rate limit exceeded");
    CHECK(!ehviewer::ParseReleaseInfo("<html>", "a.nro", &info, &error));

    CHECK(ehviewer::IsNewerVersion("v0.5.3", "0.5.2"));
    CHECK(ehviewer::IsNewerVersion("0.5.10", "0.5.9"));
    CHECK(ehviewer::IsNewerVersion("1.0", "0.9.9"));
    CHECK(!ehviewer::IsNewerVersion("v0.5.2", "0.5.2"));
    CHECK(!ehviewer::IsNewerVersion("0.5.1", "0.5.2"));
    CHECK(!ehviewer::IsNewerVersion("v0.5.2-beta", "0.5.2"));
    CHECK(!ehviewer::IsNewerVersion("garbage", "0.5.2"));
    CHECK(ehviewer::IsNewerVersion("0.5.3", "dev") );

    std::string nro(0x20, '\0');
    nro.replace(0x10, 4, "NRO0");
    CHECK(ehviewer::LooksLikeNro(nro));
    CHECK(!ehviewer::LooksLikeNro("<!DOCTYPE html><html>error</html>"));
}
void TestPortraitReaderCore() {
    using namespace ehviewer::reader;
    ReaderState state;
    CHECK(state.orientation == Orientation::PortraitCounterClockwise);
    CHECK(LogicalCanvas(state.orientation).width == 720.0);
    CHECK(EffectivePageMode(state) == PageMode::Single);
    // Default: console turned counter-clockwise, left D-pad at the bottom.
    // Pressing the D-pad's physical "left" (now pointing down) scrolls down.
    CHECK(PhysicalToLogical(Direction::Left, state.orientation) == Direction::Down);
    CHECK(PhysicalToLogical(Direction::Up, state.orientation) == Direction::Left);
    const Point physical = LogicalToPhysical({0.0, 0.0}, state.orientation);
    CHECK(physical.x == 1280.0);
    CHECK(physical.y == 0.0);
    const Point bottom_left = LogicalToPhysical({0.0, 1280.0}, state.orientation);
    CHECK(bottom_left.x == 0.0 && bottom_left.y == 0.0);
    const Point round_trip = PhysicalToLogical(LogicalToPhysical({100.0, 200.0}, state.orientation),
                                               state.orientation);
    CHECK(std::fabs(round_trip.x - 100.0) < 1e-9 && std::fabs(round_trip.y - 200.0) < 1e-9);
    // Tap zones (reader guide): physical touches land on the rotated canvas.
    const Size portrait = LogicalCanvas(state.orientation);
    CHECK(ClassifyTap({100.0, 640.0}, portrait, false) == TapZone::Previous);
    CHECK(ClassifyTap({650.0, 640.0}, portrait, false) == TapZone::Next);
    CHECK(ClassifyTap({360.0, 300.0}, portrait, false) == TapZone::Menu);
    CHECK(ClassifyTap({360.0, 1000.0}, portrait, false) == TapZone::Progress);
    CHECK(ClassifyTap({100.0, 640.0}, portrait, true) == TapZone::Next);
    // With the console turned counter-clockwise the panel's top edge is on the
    // reader's left: touching it (x=640, y=10) goes to the previous page.
    CHECK(ClassifyTap(PhysicalToLogical({640.0, 10.0}, state.orientation), portrait, false) == TapZone::Previous);
    CHECK(ClassifyTap(PhysicalToLogical({640.0, 710.0}, state.orientation), portrait, false) == TapZone::Next);
    const Point cw = LogicalToPhysical({0.0, 0.0}, Orientation::PortraitClockwise);
    CHECK(cw.x == 0.0 && cw.y == 720.0);
    const PageTransform transform = CalculatePageTransform(
        {1200.0, 1800.0}, {0.0, 0.0, 720.0, 1280.0}, ScaleMode::FitWidth);
    CHECK(transform.valid);
    CHECK(transform.scale == 0.6);
    CHECK(EstimateDecodedBytes(1200, 1800) == 8640000ULL);
}

}  // namespace

int main() {
    TestCookies();
    TestStorageLayout();
    TestSpiderInfo();
    TestManifest();
    TestGalleryParsers();
    TestAtomicFile();
    TestPortraitReaderCore();
    TestNetworkPlan();
    TestJsonAndManifestRoundTrip();
    TestStorageNames();
    TestDirectoryOperations();
    TestLibrary();
    TestSettingsAndHistory();
    TestTranslations();
    TestReleaseInfo();
    TestListLayout();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "All EhViewer Switch host tests passed.\n";
    return 0;
}
