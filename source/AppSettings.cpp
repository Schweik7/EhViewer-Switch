#include "App.h"
#include "AppShared.h"
#include "Version.h"
#include "core/I18n.h"

#include <algorithm>

#ifdef __SWITCH__
#include <switch.h>
#endif

using ehviewer::i18n::T;
using namespace app_shared;

namespace
{
enum SettingId
{
    kSettingLanguage = 1, kSettingSite, kSettingInputDebug, kSettingOrientation, kSettingZoom,
    kSettingDoublePage, kSettingDirection, kSettingPrefetch, kSettingKeepAwake, kSettingThumbnails,
    kSettingCategories, kSettingHistory, kSettingClearHistory, kSettingProxy, kSettingProxyUrl,
    kSettingFronting, kSettingHosts, kSettingDoh, kSettingVersion, kSettingListLayout, kSettingTestLogin,
    kSettingReloadCookies
};
}

void App::LoadSettings()
{
    std::string error;
    if (!ehviewer::Settings::Load(kSettingsFile, &m_settings, &error))
        m_status = T("读取设置失败: ") + error;
    if (!ehviewer::History::Load(kHistoryFile, &m_history, &error))
        m_status = T("读取浏览历史失败: ") + error;
    if (!ehviewer::Subscriptions::Load(kSubscriptionsFile, &m_subscriptions, &error))
        m_status = T("读取订阅失败: ") + error;
    ApplySettings();
}

void App::SaveSettings()
{
    std::string error;
    if (!m_settings.Save(kSettingsFile, &error))
        m_status = T("保存设置失败: ") + error;
    ApplySettings();
}

void App::ApplySettings()
{
    bool english = m_settings.language == ehviewer::Settings::Language::English;
#ifdef __SWITCH__
    if (m_settings.language == ehviewer::Settings::Language::Auto)
    {
        // Follow the console language: Chinese variants stay Chinese,
        // everything else gets English.
        u64 code = 0;
        SetLanguage language = SetLanguage_ZHCN;
        if (R_SUCCEEDED(setInitialize()))
        {
            if (R_SUCCEEDED(setGetSystemLanguage(&code)))
                setMakeLanguage(code, &language);
            setExit();
        }
        english = language != SetLanguage_ZHCN && language != SetLanguage_ZHTW &&
                  language != SetLanguage_ZHHANS && language != SetLanguage_ZHHANT;
    }
#endif
    ehviewer::i18n::SetEnglish(english);
    m_ui.SetShowInputDebug(m_settings.show_input_debug);
    m_http.ResetRoutePreference();
}

std::vector<ehviewer::SettingRow> App::BuildSettingRows() const
{
    const auto on_off = [](bool value) { return std::string(value ? T("开") : T("关")); };
    const auto tri = [](int value) {
        return std::string(value < 0 ? T("跟随 cookies.ini") : value ? T("开") : T("关"));
    };
    static const char* const kLanguages[] = {"跟随系统", "简体中文", "English"};
    static const char* const kOrientations[] = {"竖屏 · 十字键在下", "竖屏 · 十字键在上", "横屏"};
    int hidden = 0;
    for (const ehviewer::CategoryInfo& category : ehviewer::kCategories)
        hidden += (m_settings.excluded_categories & category.bit) ? 1 : 0;
    const std::string proxy_url = m_settings.proxy_url.empty() ? m_cookies.proxy : m_settings.proxy_url;

    std::vector<ehviewer::SettingRow> rows;
    const auto header = [&rows](const char* label) { rows.push_back({T(label), "", "", true, 0}); };
    const auto row = [&rows](int id, const std::string& label, const std::string& value,
                             const std::string& description = std::string()) {
        rows.push_back({label, value, description, false, id});
    };
    header("通用");
    row(kSettingLanguage, T("界面语言"), T(kLanguages[static_cast<int>(m_settings.language)]));
    row(kSettingSite, T("站点"),
        m_settings.site == ehviewer::Settings::Site::EHentai ? "E-Hentai" : T("自动（有 igneous 时用 ExHentai）"));
    row(kSettingInputDebug, T("显示按键调试信息"), on_off(m_settings.show_input_debug));
    header("阅读");
    row(kSettingOrientation, T("默认阅读方向"), T(kOrientations[m_settings.reader_orientation]));
    row(kSettingZoom, T("默认缩放"), m_settings.reader_fit_width ? T("适应宽度") : T("整页"));
    row(kSettingDoublePage, T("横屏双页"), on_off(m_settings.reader_double_page));
    row(kSettingDirection, T("双页翻页方向"), m_settings.reader_right_to_left ? T("从右到左") : T("从左到右"));
    row(kSettingPrefetch, T("预读页数"), std::to_string(m_settings.prefetch_pages));
    row(kSettingKeepAwake, T("阅读和下载时防止休眠"), on_off(m_settings.keep_awake));
    header("浏览");
    row(kSettingListLayout, T("列表样式"), m_settings.list_layout == 1 ? T("网格（大缩略图）") : T("列表"));
    row(kSettingThumbnails, T("显示缩略图"), on_off(m_settings.show_thumbnails));
    row(kSettingCategories, T("分类筛选"),
        hidden == 0 ? std::string(T("全部显示")) : std::to_string(hidden) + T(" 个已隐藏"), T("首页和搜索生效"));
    row(kSettingHistory, T("记录浏览历史"), on_off(m_settings.history_enabled));
    row(kSettingClearHistory, T("清空浏览历史"), std::to_string(m_history.Entries().size()) + T(" 条"),
        T("按 A 清空"));
    header("网络");
    row(kSettingProxy, T("使用代理"), on_off(m_settings.proxy_enabled), T("仅 e-hentai.org 和排行榜需要"));
    row(kSettingProxyUrl, T("代理地址"),
        proxy_url.empty() ? std::string(T("未设置")) : ehviewer::RedactProxyUrl(proxy_url), T("按 A 输入"));
    row(kSettingFronting, T("域前置直连"), tri(m_settings.domain_fronting), T("代理不可用时直连 exhentai"));
    row(kSettingHosts, T("内置 hosts"), tri(m_settings.builtin_hosts));
    row(kSettingDoh, T("DoH"), tri(m_settings.doh));
    header("账户");
    row(kSettingTestLogin, T("测试 Cookie 登录"), m_cookies.igneous.empty() ? "E-Hentai" : "ExHentai", T("按 A 测试"));
    row(kSettingReloadCookies, T("重新读取 cookies.ini"), m_cookies.ipb_member_id.empty() ? T("未配置") : T("已配置"),
        T("按 A 读取"));
    header("关于");
    row(kSettingVersion, T("版本"), EHV_SWITCH_VERSION, "EhViewer for Nintendo Switch");
    return rows;
}

void App::ChangeSetting(std::size_t index, int delta)
{
    const std::vector<ehviewer::SettingRow> rows = BuildSettingRows();
    if (index >= rows.size() || rows[index].header)
        return;
    const auto cycle = [delta](int value, int count) { return ((value + delta) % count + count) % count; };
    const auto tri = [delta](int value) {
        // -1 (cookies.ini) -> 1 (on) -> 0 (off) -> -1
        static const int order[] = {-1, 1, 0};
        int position = value < 0 ? 0 : value == 1 ? 1 : 2;
        position = ((position + delta) % 3 + 3) % 3;
        return order[position];
    };
    switch (rows[index].id)
    {
    case kSettingLanguage: m_settings.language = static_cast<ehviewer::Settings::Language>(
                cycle(static_cast<int>(m_settings.language), 3)); break;
    case kSettingSite: m_settings.site = m_settings.site == ehviewer::Settings::Site::Auto
                ? ehviewer::Settings::Site::EHentai : ehviewer::Settings::Site::Auto; break;
    case kSettingInputDebug: m_settings.show_input_debug = !m_settings.show_input_debug; break;
    case kSettingListLayout:
        m_settings.list_layout = m_settings.list_layout == 1 ? 0 : 1;
        m_list_scroll = m_list_scroll_target = 0.0;
        break;
    case kSettingTestLogin:
        if (delta > 0)
            TestLogin();
        return;
    case kSettingReloadCookies:
        if (delta > 0)
            ReloadCookieConfig();
        return;
    case kSettingOrientation: m_settings.reader_orientation = cycle(m_settings.reader_orientation, 3); break;
    case kSettingZoom: m_settings.reader_fit_width = !m_settings.reader_fit_width; break;
    case kSettingDoublePage: m_settings.reader_double_page = !m_settings.reader_double_page; break;
    case kSettingDirection: m_settings.reader_right_to_left = !m_settings.reader_right_to_left; break;
    case kSettingPrefetch:
        m_settings.prefetch_pages = std::max(1, std::min(8, m_settings.prefetch_pages + (delta < 0 ? -1 : 1)));
        break;
    case kSettingKeepAwake: m_settings.keep_awake = !m_settings.keep_awake; break;
    case kSettingThumbnails: m_settings.show_thumbnails = !m_settings.show_thumbnails; break;
    case kSettingCategories: OpenPicker(Picker::Categories); return;
    case kSettingHistory: m_settings.history_enabled = !m_settings.history_enabled; break;
    case kSettingClearHistory:
    {
        if (delta < 0)
            return;
        m_history.Clear();
        std::string error;
        m_history.Save(kHistoryFile, &error);
        m_status = T("浏览历史已清空");
        return;
    }
    case kSettingProxy:
        if (!m_settings.proxy_enabled && m_settings.proxy_url.empty() && m_cookies.proxy.empty())
        {
            // Nothing to use yet: ask for the address first.
            PromptProxyUrl();
            return;
        }
        m_settings.proxy_enabled = !m_settings.proxy_enabled;
        break;
    case kSettingProxyUrl:
        if (delta < 0)
            return;
        PromptProxyUrl();
        return;
    case kSettingFronting: m_settings.domain_fronting = tri(m_settings.domain_fronting); break;
    case kSettingHosts: m_settings.builtin_hosts = tri(m_settings.builtin_hosts); break;
    case kSettingDoh: m_settings.doh = tri(m_settings.doh); break;
    default: return;
    }
    SaveSettings();
}

void App::PromptProxyUrl()
{
#ifdef __SWITCH__
    SwkbdConfig keyboard;
    if (R_FAILED(swkbdCreate(&keyboard, 0)))
    {
        m_status = T("无法打开系统键盘");
        return;
    }
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetHeaderText(&keyboard, T("代理地址"));
    swkbdConfigSetGuideText(&keyboard, T("例如 http://192.168.1.10:7890，留空表示不用代理"));
    const std::string current = m_settings.proxy_url.empty() ? m_cookies.proxy : m_settings.proxy_url;
    swkbdConfigSetInitialText(&keyboard, current.c_str());
    char text[512] = {};
    const Result result = swkbdShow(&keyboard, text, sizeof(text));
    swkbdClose(&keyboard);
    if (R_FAILED(result))
        return;
    std::string value = text;
    if (!value.empty() && value.find("://") == std::string::npos)
        value = "http://" + value;  // "192.168.1.10:7890" is the common case
    if (value.empty())
    {
        m_settings.proxy_url.clear();
        m_settings.proxy_enabled = false;
    }
    else if (!ehviewer::IsValidProxyUrl(value))
    {
        m_status = T("代理地址无效，需要 http:// 或 socks5:// 开头");
        return;
    }
    else
    {
        m_settings.proxy_url = value;
        m_settings.proxy_enabled = true;
    }
    SaveSettings();
#endif
}

void App::SaveSubscriptions()
{
    std::string error;
    if (!m_subscriptions.Save(kSubscriptionsFile, &error))
        m_status = T("保存订阅失败: ") + error;
}

void App::SaveCurrentSearchAsSubscription()
{
    if (m_search_keyword.empty())
        return;
    if (m_subscriptions.Add(m_search_keyword))
    {
        SaveSubscriptions();
        m_status = T("已加入我的订阅：") + m_search_keyword;
    }
    else
    {
        m_status = T("这个搜索已在我的订阅中");
    }
}

void App::PromptNewSubscription()
{
#ifdef __SWITCH__
    SwkbdConfig keyboard;
    if (R_FAILED(swkbdCreate(&keyboard, 0)))
    {
        m_status = T("无法打开系统键盘");
        return;
    }
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetHeaderText(&keyboard, T("新建订阅"));
    swkbdConfigSetGuideText(&keyboard, T("关键词或标签，例如 language:chinese"));
    swkbdConfigSetInitialText(&keyboard, m_search_keyword.c_str());
    char text[512] = {};
    const Result result = swkbdShow(&keyboard, text, sizeof(text));
    swkbdClose(&keyboard);
    if (R_FAILED(result) || text[0] == '\0')
        return;
    if (m_subscriptions.Add(text))
    {
        SaveSubscriptions();
        m_subscription_selected = m_subscriptions.Entries().size() - 1;
        m_status = T("已加入我的订阅：") + std::string(text);
    }
    else
    {
        m_status = T("这个搜索已在我的订阅中");
    }
#endif
}

void App::OpenHistory()
{
    m_list_source = ListSource::History;
    LoadGalleryList();
}

void App::RecordHistory(const ehviewer::GalleryDetail& detail)
{
    if (!m_settings.history_enabled)
        return;
    ehviewer::GallerySummary entry;
    entry.gid = detail.gid;
    entry.token = detail.token;
    entry.title = detail.title;
    entry.thumb_url = detail.cover_url;
    entry.category = detail.category;
    entry.posted = detail.posted;
    entry.pages = detail.pages;
    // The list shows the star rating; reuse it if the gallery came from there.
    for (const ehviewer::GallerySummary& listed : m_galleries)
        if (listed.gid == detail.gid)
            entry.rating = listed.rating;
    m_history.Add(entry);
    std::string error;
    m_history.Save(kHistoryFile, &error);
}
