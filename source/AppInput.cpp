#include "App.h"
#include "AppShared.h"
#include "core/GalleryParser.h"
#include "core/I18n.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#ifdef __SWITCH__
#include <switch.h>
#include <SDL2/SDL.h>
#endif

using ehviewer::i18n::T;
using namespace app_shared;

namespace
{
void MoveSelection(std::size_t* selected, std::size_t count, bool up, bool down)
{
    if (up && *selected > 0)
        --*selected;
    if (down && *selected + 1 < count)
        ++*selected;
}
}

void App::PromptSearch()
{
#ifdef __SWITCH__
    SwkbdConfig keyboard;
    if (R_FAILED(swkbdCreate(&keyboard, 0)))
    {
        m_status = T("无法打开系统键盘");
        return;
    }
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetHeaderText(&keyboard, T("搜索图库"));
    swkbdConfigSetGuideText(&keyboard, T("关键词或标签，例如 language:chinese"));
    swkbdConfigSetInitialText(&keyboard, m_search_keyword.c_str());
    char text[512] = {};
    const Result result = swkbdShow(&keyboard, text, sizeof(text));
    swkbdClose(&keyboard);
    if (R_FAILED(result) || text[0] == '\0')
        return;
    SearchFor(text);
#endif
}

void App::OpenPicker(Picker picker)
{
    m_overlay = ehviewer::OverlayMenu();
    if (picker == Picker::Favorite)
    {
        m_overlay.title = T("加入收藏夹");
        for (std::size_t index = 0; index < m_favorite_slots.names.size(); ++index)
        {
            std::string item = m_favorite_slots.names[index];
            if (static_cast<int>(index) == m_favorite_slots.current)
                item += T("（当前）");
            m_overlay.items.push_back(item);
        }
        if (m_favorite_slots.current >= 0)
            m_overlay.items.push_back(T("从收藏中移除"));
        m_overlay.selected = m_favorite_slots.current >= 0 ? static_cast<std::size_t>(m_favorite_slots.current) : 0;
    }
    else if (picker == Picker::FavoriteFolder)
    {
        m_overlay.title = T("切换收藏夹");
        int total = 0;
        for (const ehviewer::FavoriteFolder& folder : m_favorite_folders.folders)
            total += folder.count;
        m_overlay.items.push_back(std::string(T("全部")) + (total > 0 ? "  (" + std::to_string(total) + ")" : ""));
        for (std::size_t index = 0; index < 10; ++index)
        {
            const int count = index < m_favorite_folders.folders.size() ? m_favorite_folders.folders[index].count : 0;
            m_overlay.items.push_back(FavoriteFolderName(static_cast<int>(index)) + "  (" + std::to_string(count) + ")");
        }
        m_overlay.selected = static_cast<std::size_t>(m_favorite_folder + 1);
        m_overlay.hint = T("A 打开 · B 取消");
    }
    else if (picker == Picker::Categories)
    {
        m_overlay.title = T("显示的分类");
        for (const ehviewer::CategoryInfo& category : ehviewer::kCategories)
            m_overlay.items.push_back(std::string((m_settings.excluded_categories & category.bit) ? "☐  " : "☑  ") +
                                      category.name);
        m_overlay.hint = T("A 切换显示 · B 完成");
    }
    else if (picker == Picker::ConfirmDeleteSubscription)
    {
        m_overlay.title = T("删除订阅「") + m_subscriptions.Entries()[m_subscription_selected].name + T("」？");
        m_overlay.items = {T("删除"), T("取消")};
        m_overlay.selected = 1;
    }
    else if (picker == Picker::ConfirmDelete)
    {
        const ehviewer::LibraryEntry& entry = m_library[m_library_selected];
        m_overlay.title = T("删除「") + entry.manifest.title + T("」？");
        m_overlay.items = {T("删除本地文件"), T("取消")};
        m_overlay.selected = 1;
        m_overlay.hint = T("删除后无法恢复");
    }
    else
    {
        m_overlay.title = T("给这个图库评分");
        for (int rating = 10; rating >= 1; --rating)
        {
            std::string stars;
            for (int star = 1; star <= 5; ++star)
                stars += rating >= star * 2 ? "★" : rating == star * 2 - 1 ? "☆" : "·";
            char label[32];
            std::snprintf(label, sizeof(label), "  %.1f", rating / 2.0);
            m_overlay.items.push_back(stars + label);
        }
        m_overlay.selected = 2;  // 4.0 stars
        m_overlay.hint = T("上下选择（☆ 为半星） · A 提交 · B 取消");
    }
    m_picker = picker;
    m_ui.SetOverlay(&m_overlay);
}

bool App::HandlePickerInput(std::uint64_t down)
{
#ifdef __SWITCH__
    if (down & HidNpadButton_AnyUp && m_overlay.selected > 0)
        --m_overlay.selected;
    if (down & HidNpadButton_AnyDown && m_overlay.selected + 1 < m_overlay.items.size())
        ++m_overlay.selected;
    const Picker picker = m_picker;
    const std::size_t selected = m_overlay.selected;
    if (picker == Picker::Categories)
    {
        // Multi-select: A toggles and keeps the list open; B saves and closes.
        if ((down & HidNpadButton_A) && selected < 10)
        {
            m_settings.excluded_categories ^= ehviewer::kCategories[selected].bit;
            OpenPicker(Picker::Categories);
            m_overlay.selected = selected;
        }
        if (down & HidNpadButton_B)
        {
            m_picker = Picker::None;
            m_ui.SetOverlay(nullptr);
            SaveSettings();
        }
        return down != 0;
    }
    if (down & (HidNpadButton_A | HidNpadButton_B))
    {
        m_picker = Picker::None;
        m_ui.SetOverlay(nullptr);
    }
    if (down & HidNpadButton_A)
    {
        if (picker == Picker::Favorite)
            SetFavorite(selected < m_favorite_slots.names.size() ? static_cast<int>(selected) : -1);
        else if (picker == Picker::FavoriteFolder)
        {
            m_favorite_folder = static_cast<int>(selected) - 1;
            OpenListSource(ListSource::Favorites);
        }
        else if (picker == Picker::ConfirmDelete)
        {
            if (selected == 0)
                DeleteSelectedLibraryEntry();
        }
        else if (picker == Picker::ConfirmDeleteSubscription)
        {
            if (selected == 0 && m_subscriptions.Remove(m_subscription_selected))
            {
                SaveSubscriptions();
                if (m_subscription_selected > 0 && m_subscription_selected >= m_subscriptions.Entries().size())
                    --m_subscription_selected;
                m_status = T("已删除");
            }
        }
        else
            RateGallery(10 - static_cast<int>(selected));
    }
    return down != 0;
#else
    (void)down;
    return false;
#endif
}

void App::PromptReaderJump()
{
#ifdef __SWITCH__
    SwkbdConfig keyboard;
    if (R_FAILED(swkbdCreate(&keyboard, 0)))
        return;
    swkbdConfigMakePresetDefault(&keyboard);
    swkbdConfigSetType(&keyboard, SwkbdType_NumPad);
    swkbdConfigSetStringLenMax(&keyboard, 5);
    const std::string guide = std::string(ehviewer::i18n::T("跳到第几页")) + " (1-" +
                              std::to_string(m_reader.PageCount()) + ")";
    swkbdConfigSetGuideText(&keyboard, guide.c_str());
    char text[16] = {};
    const Result result = swkbdShow(&keyboard, text, sizeof(text));
    swkbdClose(&keyboard);
    if (R_SUCCEEDED(result) && text[0] != '\0')
        m_reader.JumpTo(std::atoi(text) - 1);
    m_reader.Draw();
#endif
}

void App::SearchFor(const std::string& query)
{
    if (query.empty())
        return;
    m_search_keyword = query;
    OpenListSource(ListSource::Search);
}

int App::CurrentSidebarItem() const
{
    switch (m_screen)
    {
    case Screen::GalleryList:
        switch (m_list_source)
        {
        case ListSource::Latest: return ehviewer::kSidebarHome;
        case ListSource::Watched: return ehviewer::kSidebarWatched;
        case ListSource::Popular: return ehviewer::kSidebarPopular;
        case ListSource::Toplist: return ehviewer::kSidebarToplist;
        case ListSource::Favorites: return ehviewer::kSidebarFavorites;
        case ListSource::History: return ehviewer::kSidebarHistory;
        default: return ehviewer::kSidebarNone;
        }
    case Screen::Subscriptions: return ehviewer::kSidebarSubscriptions;
    case Screen::Library: return ehviewer::kSidebarDownloads;
    case Screen::Settings: return ehviewer::kSidebarSettings;
    default: return ehviewer::kSidebarNone;
    }
}

void App::Navigate(int sidebar_item)
{
    if (m_screen == Screen::Reader)
        CloseReader();
    // Leaving a page drops the page request still running for it, so quick
    // ZL/ZR presses never wait for (or land on) an old page.
    if (m_task_type == TaskType::GalleryList || m_task_type == TaskType::GalleryDetail)
        RetireNetworkTask();
    const auto show_source = [this](ListSource source) {
        // Reuse the loaded list when coming back to the same source.
        if (m_list_source == source && !m_galleries.empty() && m_screen != Screen::GalleryList)
        {
            m_screen = Screen::GalleryList;
            RequestListThumbnails();
            return;
        }
        OpenListSource(source);
    };
    switch (sidebar_item)
    {
    case ehviewer::kSidebarHome: show_source(ListSource::Latest); break;
    case ehviewer::kSidebarWatched: show_source(ListSource::Watched); break;
    case ehviewer::kSidebarPopular: show_source(ListSource::Popular); break;
    case ehviewer::kSidebarToplist: show_source(ListSource::Toplist); break;
    case ehviewer::kSidebarFavorites: show_source(ListSource::Favorites); break;
    case ehviewer::kSidebarSubscriptions: m_screen = Screen::Subscriptions; break;
    case ehviewer::kSidebarDownloads:
        RefreshLibrary();
        m_screen = Screen::Library;
        break;
    case ehviewer::kSidebarHistory: OpenHistory(); break;
    default:
        m_screen = Screen::Settings;
        if (m_settings_selected == 0)
            m_settings_selected = 1;
        break;
    }
}

void App::HandleTouch(std::uint64_t* down)
{
#ifdef __SWITCH__
    HidTouchScreenState state{};
    const bool touching = hidGetTouchScreenStates(&state, 1) > 0 && state.count > 0;
    if (touching)
    {
        const int x = static_cast<int>(state.touches[0].x);
        const int y = static_cast<int>(state.touches[0].y);
        if (!m_touch_down)
        {
            m_touch_down = true;
            m_touch_dragging = false;
            m_touch_start_x = m_touch_last_x = x;
            m_touch_start_y = m_touch_last_y = y;
            m_touch_drag_accumulator = 0;
            m_touch_start_tick = SDL_GetTicks();
            m_touch_long_fired = false;
            m_list_velocity = 0.0;  // a new touch stops a fling
            return;
        }
        const int dx = x - m_touch_last_x;
        const int dy = y - m_touch_last_y;
        if (std::abs(x - m_touch_start_x) > kTapSlop || std::abs(y - m_touch_start_y) > kTapSlop)
            m_touch_dragging = true;
        m_touch_last_x = x;
        m_touch_last_y = y;
        // Holding still opens the reader's page menu, as on Android.
        if (m_screen == Screen::Reader && !m_touch_dragging && !m_touch_long_fired &&
            SDL_GetTicks() - m_touch_start_tick >= kLongPressMilliseconds)
        {
            m_touch_long_fired = true;
            m_reader.LongPress(m_touch_start_x, m_touch_start_y);
            return;
        }
        if (!m_touch_dragging || (dx == 0 && dy == 0) || m_touch_long_fired)
            return;
        if (m_screen == Screen::Reader)
        {
            m_reader.Drag(dx, dy, x, y);
            return;
        }
        if (m_screen == Screen::GalleryList)
        {
            // The gallery list follows the finger and keeps the speed as a fling.
            m_list_scroll -= dy;
            m_list_scroll_target = m_list_scroll;
            m_list_velocity = -dy;
            m_list_user_scrolled = true;
            return;
        }
        // Other lists scroll by one row per ~70px of vertical drag (finger up = down).
        m_touch_drag_accumulator += dy;
        const int row_height = m_screen == Screen::Comments ? ehviewer::Ui::kCommentLineHeight : 70;
        while (std::abs(m_touch_drag_accumulator) >= row_height)
        {
            const bool up = m_touch_drag_accumulator > 0;
            m_touch_drag_accumulator += up ? -row_height : row_height;
            if (m_screen == Screen::Comments)
                m_comment_scroll = std::max(0, m_comment_scroll + (up ? -1 : 1));
            else
                *down |= up ? HidNpadButton_Up : HidNpadButton_Down;
        }
        return;
    }
    if (!m_touch_down)
        return;
    // Finger lifted.
    m_touch_down = false;
    if (m_touch_dragging || m_touch_long_fired)
        return;
    const int x = m_touch_start_x;
    const int y = m_touch_start_y;
    if (m_screen == Screen::Reader && m_picker == Picker::None)
    {
        m_reader.Tap(x, y);
        return;
    }
    const std::vector<ehviewer::HitRegion>& hits = m_ui.HitRegions();
    for (auto it = hits.rbegin(); it != hits.rend(); ++it)
    {
        if (x < it->x || y < it->y || x >= it->x + it->width || y >= it->y + it->height)
            continue;
        if (it->buttons & ehviewer::kTouchNav)
        {
            m_pending_nav = it->index;
            return;
        }
        if (it->buttons & ehviewer::kTouchTag)
        {
            std::size_t flat = 0;
            for (const ehviewer::TagGroup& group : m_detail.tags)
                for (const std::string& tag : group.tags)
                    if (flat++ == static_cast<std::size_t>(it->index))
                        SearchFor(ehviewer::TagSearchQuery(group.name_space, tag));
            return;
        }
        if (it->buttons & ehviewer::kTouchUploader)
        {
            *down |= HidNpadButton_Minus;
            return;
        }
        if (it->index >= 0)
        {
            const std::size_t index = static_cast<std::size_t>(it->index);
            if (m_picker != Picker::None)
                m_overlay.selected = index;
            else if (m_screen == Screen::GalleryList)
                m_selected = index;
            else if (m_screen == Screen::Library)
                m_library_selected = index;
            else if (m_screen == Screen::Settings)
                m_settings_selected = index;
            else if (m_screen == Screen::Tags)
                m_tag_selected = index;
            else if (m_screen == Screen::Subscriptions)
                m_subscription_selected = index;
            else if (m_screen == Screen::Previews)
                m_preview_selected = index;
        }
        *down |= it->buttons;
        return;
    }
#else
    (void)down;
#endif
}

void App::HandleScreenInput(std::uint64_t down)
{
    switch (m_screen)
    {
    case Screen::GalleryList: HandleListInput(down); break;
    case Screen::Subscriptions: HandleSubscriptionsInput(down); break;
    case Screen::GalleryDetail: HandleDetailInput(down); break;
    case Screen::Comments: HandleCommentsInput(down); break;
    case Screen::Tags: HandleTagsInput(down); break;
    case Screen::Settings: HandleSettingsInput(down); break;
    case Screen::Library: HandleLibraryInput(down); break;
    case Screen::Previews: HandlePreviewsInput(down); break;
    default: break;
    }
}

void App::HandleListInput(std::uint64_t down)
{
#ifdef __SWITCH__
    const ehviewer::ListGeometry geometry = Geometry();
    const bool moving = down & (HidNpadButton_AnyUp | HidNpadButton_AnyDown |
                                HidNpadButton_AnyLeft | HidNpadButton_AnyRight);
    if (moving && m_list_user_scrolled)
    {
        // After touch scrolling, the D-pad continues from what is on screen.
        const ehviewer::ItemRect item = geometry.Item(m_selected);
        if (item.y + item.height < m_list_scroll || item.y > m_list_scroll + geometry.viewport_height)
            m_selected = geometry.FirstVisible(m_list_scroll, m_galleries.size());
        m_list_user_scrolled = false;
    }
    if (moving)
    {
        const int dx = (down & HidNpadButton_AnyLeft) ? -1 : (down & HidNpadButton_AnyRight) ? 1 : 0;
        const int dy = (down & HidNpadButton_AnyUp) ? -1 : (down & HidNpadButton_AnyDown) ? 1 : 0;
        m_selected = geometry.Move(m_selected, m_galleries.size(), geometry.columns > 1 ? dx : 0, dy);
        ScrollListToSelection();
    }
    if ((down & HidNpadButton_X) && m_list_source == ListSource::Search)
        SaveCurrentSearchAsSubscription();
    if (down & HidNpadButton_A)
        LoadSelectedGallery();
    if ((down & HidNpadButton_Y) && m_selected < m_galleries.size())
        EnqueueDownload(m_galleries[m_selected], nullptr);
    if ((down & HidNpadButton_X) && m_list_source == ListSource::Toplist)
    {
        // X cycles the toplist period instead of refreshing.
        m_toplist_period = (m_toplist_period + 1) % 4;
        OpenListSource(ListSource::Toplist);
    }
    else if ((down & HidNpadButton_X) && m_list_source == ListSource::Favorites)
        OpenPicker(Picker::FavoriteFolder);
    else if ((down & HidNpadButton_X) && m_list_source != ListSource::Search)
        LoadGalleryList(m_list_url, m_list_page_number);
    if ((down & HidNpadButton_R) && !m_list_nav.next_url.empty())
        LoadGalleryList(m_list_nav.next_url, m_list_page_number + 1);
    if ((down & HidNpadButton_L) && !m_list_nav.prev_url.empty())
        LoadGalleryList(m_list_nav.prev_url, std::max(1, m_list_page_number - 1));
    if (down & HidNpadButton_Minus)
        PromptSearch();
    if (down & HidNpadButton_B)
        Navigate(ehviewer::kSidebarDownloads);
    if (m_screen == Screen::GalleryList)
        MaybeLoadMore();
#else
    (void)down;
#endif
}

void App::HandleSubscriptionsInput(std::uint64_t down)
{
#ifdef __SWITCH__
    MoveSelection(&m_subscription_selected, m_subscriptions.Entries().size(),
                  down & HidNpadButton_AnyUp, down & HidNpadButton_AnyDown);
    if ((down & HidNpadButton_A) && m_subscription_selected < m_subscriptions.Entries().size())
        SearchFor(m_subscriptions.Entries()[m_subscription_selected].query);
    if (down & HidNpadButton_X)
        PromptNewSubscription();
    if ((down & HidNpadButton_Y) && m_subscription_selected < m_subscriptions.Entries().size())
        OpenPicker(Picker::ConfirmDeleteSubscription);
    if (down & HidNpadButton_B)
        Navigate(ehviewer::kSidebarDownloads);
#else
    (void)down;
#endif
}

void App::HandleDetailInput(std::uint64_t down)
{
#ifdef __SWITCH__
    const ehviewer::GallerySummary summary = DetailSummary();
    if (down & HidNpadButton_ZR)
        OpenPreviews();
    if ((down & HidNpadButton_Y) && LocalState(m_detail.gid) != 2)
        EnqueueDownload(summary, &m_detail);
    if (down & HidNpadButton_A)
    {
        // Read right away: start (or resume) the download if the gallery
        // is not complete; pages appear in the reader as they arrive.
        if (LocalState(m_detail.gid) != 2)
            EnqueueDownload(summary, &m_detail, true);
        OpenReader(m_detail.gid, m_detail.title, m_detail.pages, Screen::GalleryDetail);
    }
    if (down & HidNpadButton_X)
    {
        m_comment_scroll = 0;
        m_screen = Screen::Comments;
    }
    if ((down & HidNpadButton_ZL) && !m_detail.tags.empty())
    {
        m_tag_selected = 0;
        m_screen = Screen::Tags;
    }
    if ((down & HidNpadButton_Minus) && !m_detail.uploader.empty())
    {
        // Same query the site uses for "uploader" links.
        const std::string& uploader = m_detail.uploader;
        SearchFor(uploader.find(' ') != std::string::npos ? "uploader:\"" + uploader + "\""
                                                          : "uploader:" + uploader);
    }
    if (down & HidNpadButton_R)
        LoadFavoriteSlots();
    if (down & HidNpadButton_L)
    {
        if (m_detail.api_uid <= 0 || m_detail.api_key.empty())
            m_status = T("这个页面没有评分所需的 API 参数，请刷新详情后再试");
        else
            OpenPicker(Picker::Rating);
    }
    if (down & HidNpadButton_B)
    {
        m_screen = Screen::GalleryList;
        RequestListThumbnails();
    }
#else
    (void)down;
#endif
}

void App::HandleCommentsInput(std::uint64_t down)
{
#ifdef __SWITCH__
    if (down & (HidNpadButton_AnyDown | HidNpadButton_R))
        m_comment_scroll += (down & HidNpadButton_R) ? 12 : 3;
    if (down & (HidNpadButton_AnyUp | HidNpadButton_L))
        m_comment_scroll = std::max(0, m_comment_scroll - ((down & HidNpadButton_L) ? 12 : 3));
    if (down & HidNpadButton_B)
        m_screen = Screen::GalleryDetail;
#else
    (void)down;
#endif
}

void App::HandleTagsInput(std::uint64_t down)
{
#ifdef __SWITCH__
    std::size_t count = 0;
    for (const ehviewer::TagGroup& group : m_detail.tags)
        count += group.tags.size();
    MoveSelection(&m_tag_selected, count, down & (HidNpadButton_AnyUp | HidNpadButton_AnyLeft),
                  down & (HidNpadButton_AnyDown | HidNpadButton_AnyRight));
    if ((down & HidNpadButton_A) && m_tag_selected < count)
    {
        std::size_t flat = 0;
        std::string query;
        for (const ehviewer::TagGroup& group : m_detail.tags)
            for (const std::string& tag : group.tags)
                if (flat++ == m_tag_selected)
                    query = ehviewer::TagSearchQuery(group.name_space, tag);
        SearchFor(query);
    }
    if (down & HidNpadButton_B)
        m_screen = Screen::GalleryDetail;
#else
    (void)down;
#endif
}

void App::HandleSettingsInput(std::uint64_t down)
{
#ifdef __SWITCH__
    const std::vector<ehviewer::SettingRow> rows = BuildSettingRows();
    // Skip section headers when moving.
    const auto move = [&](int delta) {
        std::size_t next = m_settings_selected;
        do
        {
            if ((delta < 0 && next == 0) || (delta > 0 && next + 1 >= rows.size()))
                return;
            next = static_cast<std::size_t>(static_cast<int>(next) + delta);
        } while (rows[next].header);
        m_settings_selected = next;
    };
    if (down & HidNpadButton_AnyUp)
        move(-1);
    if (down & HidNpadButton_AnyDown)
        move(1);
    if (down & (HidNpadButton_A | HidNpadButton_AnyRight))
        ChangeSetting(m_settings_selected, 1);
    if (down & HidNpadButton_AnyLeft)
        ChangeSetting(m_settings_selected, -1);
    if (down & HidNpadButton_B)
        Navigate(ehviewer::kSidebarDownloads);
#else
    (void)down;
#endif
}

void App::HandleLibraryInput(std::uint64_t down)
{
#ifdef __SWITCH__
    if ((down & HidNpadButton_X) && m_library_selected < m_library.size())
        OpenPicker(Picker::ConfirmDelete);
    const std::size_t before = m_library_selected;
    MoveSelection(&m_library_selected, m_library.size(),
                  down & HidNpadButton_AnyUp, down & HidNpadButton_AnyDown);
    if (before != m_library_selected)
        LoadLibraryCovers();
    if ((down & HidNpadButton_A) && m_library_selected < m_library.size())
    {
        const ehviewer::LibraryEntry& entry = m_library[m_library_selected];
        OpenReader(entry.gid, entry.manifest.title, entry.manifest.total_pages, Screen::Library,
                   entry.manifest.current_page);
    }
    if (down & HidNpadButton_Minus)
    {
        m_downloader.CancelAll();
        m_status = T("已暂停全部下载");
    }
    if ((down & HidNpadButton_Y) && m_library_selected < m_library.size() &&
        !m_library[m_library_selected].complete)
    {
        // Start or pause the selected task.
        const ehviewer::LibraryEntry& entry = m_library[m_library_selected];
        if (m_downloader.Pause(entry.gid))
            m_status = T("已暂停：") + entry.manifest.title;
        else
            ResumeLibraryDownload(entry);
    }
    if (down & HidNpadButton_R)
    {
        int started = 0;
        for (const ehviewer::LibraryEntry& entry : m_library)
            if (!entry.complete && ResumeLibraryDownload(entry, true))
                ++started;
        m_status = started > 0 ? T("已开始 ") + std::to_string(started) + T(" 个下载任务") : T("没有需要继续的下载");
    }
#else
    (void)down;
#endif
}

ehviewer::GallerySummary App::DetailSummary() const
{
    ehviewer::GallerySummary summary;
    summary.gid = m_detail.gid;
    summary.token = m_detail.token;
    summary.title = m_detail.title;
    summary.path = "/g/" + std::to_string(m_detail.gid) + "/" + m_detail.token + "/";
    return summary;
}

void App::OpenPreviews()
{
    m_preview_selected = 0;
    m_preview_first_row = 0;
    m_screen = Screen::Previews;
    RequestPreviewThumbnails();
    if (m_detail.previews.empty())
        LoadMorePreviews();
}

void App::RequestPreviewThumbnails()
{
    // Sprite sheets of the rows on screen plus one row either side.
    constexpr std::size_t columns = ehviewer::Ui::kPreviewColumns;
    const std::size_t first = (m_preview_first_row > 0 ? m_preview_first_row - 1 : 0) * columns;
    const std::size_t last = std::min(m_detail.previews.size(),
                                      (m_preview_first_row + ehviewer::Ui::kPreviewRows + 1) * columns);
    std::vector<ehviewer::ThumbnailRequest> requests;
    for (std::size_t index = first; index < last; ++index)
    {
        const std::string& url = m_detail.previews[index].image_url;
        const std::int64_t key = ehviewer::PreviewImageKey(url);
        const bool queued = std::any_of(requests.begin(), requests.end(),
            [key](const ehviewer::ThumbnailRequest& request) { return request.key == key; });
        if (!queued && !m_ui.HasThumbnail(key))
            requests.push_back({key, url});
    }
    if (!requests.empty())
        m_thumbnails.Replace(std::move(requests), m_cookies.BuildCookieHeader(), NetworkOptions());
}

void App::HandlePreviewsInput(std::uint64_t down)
{
#ifdef __SWITCH__
    constexpr std::size_t columns = ehviewer::Ui::kPreviewColumns;
    constexpr std::size_t rows = ehviewer::Ui::kPreviewRows;
    const std::size_t count = m_detail.previews.size();
    const std::size_t before = m_preview_first_row;
    if ((down & HidNpadButton_AnyLeft) && m_preview_selected > 0)
        --m_preview_selected;
    if ((down & HidNpadButton_AnyRight) && m_preview_selected + 1 < count)
        ++m_preview_selected;
    if ((down & HidNpadButton_AnyUp) && m_preview_selected >= columns)
        m_preview_selected -= columns;
    if ((down & HidNpadButton_AnyDown) && count > 0)
        m_preview_selected = std::min(count - 1, m_preview_selected + columns);
    // Keep the selection on screen.
    const std::size_t row = m_preview_selected / columns;
    if (row < m_preview_first_row)
        m_preview_first_row = row;
    if (row >= m_preview_first_row + rows)
        m_preview_first_row = row - rows + 1;
    if (m_preview_first_row != before)
        RequestPreviewThumbnails();
    // Near the end: fetch the next preview page.
    // Only on input, so a failed request is not retried every frame.
    if (down != 0 && count > 0 && (m_preview_first_row + rows + 1) * columns >= count)
        LoadMorePreviews();
    if ((down & HidNpadButton_A) && m_preview_selected < count)
    {
        // Read from this page; pages come in as they download.
        if (LocalState(m_detail.gid) != 2)
            EnqueueDownload(DetailSummary(), &m_detail, true);
        OpenReader(m_detail.gid, m_detail.title, m_detail.pages, Screen::Previews,
                   m_detail.previews[m_preview_selected].page_index);
    }
    if (down & HidNpadButton_B)
        m_screen = Screen::GalleryDetail;
#else
    (void)down;
#endif
}