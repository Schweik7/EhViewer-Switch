#include "App.h"
#include "AppShared.h"
#include "core/FileUtil.h"
#include "core/I18n.h"
#include "core/StorageLayout.h"

#include <exception>

#ifdef __SWITCH__
#include <switch.h>
#include <SDL2/SDL.h>
#endif

using ehviewer::i18n::T;
using namespace app_shared;

namespace
{
#ifdef __SWITCH__
const char* ButtonName(u64 buttons)
{
    if (buttons & HidNpadButton_A) return "A";
    if (buttons & HidNpadButton_B) return "B";
    if (buttons & HidNpadButton_X) return "X";
    if (buttons & HidNpadButton_Y) return "Y";
    if (buttons & HidNpadButton_Up) return "Up";
    if (buttons & HidNpadButton_Down) return "Down";
    if (buttons & HidNpadButton_Left) return "Left";
    if (buttons & HidNpadButton_Right) return "Right";
    if (buttons & HidNpadButton_Plus) return "Plus";
    if (buttons & HidNpadButton_Minus) return "Minus";
    if (buttons & HidNpadButton_L) return "L";
    if (buttons & HidNpadButton_R) return "R";
    if (buttons & HidNpadButton_ZL) return "ZL";
    if (buttons & HidNpadButton_ZR) return "ZR";
    if (buttons & HidNpadButton_StickL) return "StickL";
    if (buttons & HidNpadButton_StickR) return "StickR";
    return "Other";
}
#endif
}

App::App()
    : m_downloader(&m_http, kCaFile, kLibraryRoot),
      m_thumbnails(&m_http, kCaFile),
      m_reader(&m_ui),
      m_updater(&m_http, kCaFile)
{
}

bool App::Init()
{
    std::string ui_error;
    if (!m_ui.Initialize(&ui_error))
    {
        m_status = ui_error;
        return false;
    }
    std::string storage_error;
    const ehviewer::StorageLayout layout(kLibraryRoot);
    ehviewer::file::CreateDirectoryRecursive(kConfigDir, &storage_error);
    ehviewer::file::CreateDirectoryRecursive(layout.GalleriesRoot(), &storage_error);
    ehviewer::file::CreateDirectoryRecursive(layout.IncomingRoot(), &storage_error);
    LoadSettings();
    ReloadCookieConfig();
    // A version downloaded last time is swapped in before anything else.
    m_update_notice = m_updater.ApplyPendingUpdate();

#ifdef __SWITCH__
    // Page requests, thumbnails and downloads run on separate worker threads;
    // each blocking BSD call holds one service session, so ask for more than
    // the default three and fall back to the defaults if the system refuses.
    SocketInitConfig socket_config = *socketGetDefaultInitConfig();
    socket_config.num_bsd_sessions = 6;
    Result socket_result = socketInitialize(&socket_config);
    if (R_FAILED(socket_result))
        socket_result = socketInitializeDefault();
    m_socket_ready = R_SUCCEEDED(socket_result);
    // Battery level for the reader's status bar.
    m_psm_ready = R_SUCCEEDED(psmInitialize());
#else
    m_socket_ready = true;
#endif
    std::string network_error;
    if (m_socket_ready)
        m_curl_ready = m_http.Initialize(&network_error);
    if (m_curl_ready)
    {
        m_downloader.Start();
        m_thumbnails.Start();
    }

    if (!m_socket_ready)
        m_status = "Network initialization failed";
    else if (!m_curl_ready)
        m_status = "curl initialization failed: " + network_error;
    else if (!storage_error.empty())
        m_status = storage_error;
    if (!m_update_notice.empty())
        m_status = m_update_notice;
    return true;
}

void App::Run()
{
#ifdef __SWITCH__
    PadState pad;
    // Read all eight player slots as well as handheld input. nxlink/hbmenu can
    // leave the active controller assigned outside player 1.
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeAny(&pad);
    hidInitializeTouchScreen();

    // The app opens on the download library, like EhViewer's offline start.
    RefreshLibrary();
    if (m_settings.auto_check_update && m_curl_ready && ehviewer::file::Exists(kCaFile))
        StartUpdateCheck(true);
    Draw();
    unsigned last_animation = 0;
    while (appletMainLoop())
    {
        PollNetworkTask();
        if (!m_ui.PumpEvents())
            break;
        padUpdate(&pad);
        u64 down = padGetButtonsDown(&pad);
        const u64 held = padGetButtons(&pad);
        bool redraw = false;
        // Touch gestures become the same virtual button presses as the pad.
        const bool touched = m_touch_down;
        HandleTouch(&down);
        if (touched || m_touch_down)
            redraw = true;
        if (down != 0)
        {
            ++m_input_events;
            m_last_input = ButtonName(down);
            redraw = true;
        }
        if (down & HidNpadButton_Plus)
        {
            if (m_screen == Screen::Reader)
                CloseReader();
            CancelNetworkTask();
            break;
        }
        if (m_pending_nav >= 0 && m_picker == Picker::None && m_screen != Screen::Reader)
        {
            Navigate(m_pending_nav);
            down = 0;
        }
        m_pending_nav = -1;
        // ZL/ZR step through the drawer on the top-level screens.
        const int sidebar = CurrentSidebarItem();
        if (m_picker == Picker::None && (down & (HidNpadButton_ZL | HidNpadButton_ZR)) &&
            (sidebar != ehviewer::kSidebarNone ||
             (m_screen == Screen::GalleryList && m_list_source == ListSource::Search)))
        {
            const int base = sidebar == ehviewer::kSidebarNone ? ehviewer::kSidebarHome : sidebar;
            const int delta = (down & HidNpadButton_ZR) ? 1 : -1;
            Navigate((base + delta + ehviewer::kSidebarCount) % ehviewer::kSidebarCount);
            down = 0;
        }

        if (m_screen == Screen::Reader)
        {
            // B, − (menu), the guide and the page menu are handled by the reader.
            const bool changed = m_reader.HandleInput(down, held);
            redraw = m_reader.Update() || changed;
            // Read-while-downloading: fetch from the page being read.
            m_downloader.SetFocus(m_reading_gid, m_reader.CurrentPage());
            if (HandleReaderAction())
                redraw = true;
        }
        else if (m_picker != Picker::None)
        {
            HandlePickerInput(down);
        }
        else if (m_task_type != TaskType::None && (down & HidNpadButton_B))
        {
            CancelNetworkTask();
            m_status = T("正在取消网络请求...");
        }
        else
            HandleScreenInput(down);
        UpdateKeepAwake();
        if (UpdateDownloadSpeed())
            redraw = true;
        if (m_screen == Screen::GalleryList && m_picker == Picker::None)
        {
            if (AnimateListScroll())
                redraw = true;
            MaybeLoadMore();
        }
        if (PumpUpdater() && m_screen != Screen::Reader)
            redraw = true;
        if (PumpThumbnails() && m_screen != Screen::Reader)
            redraw = true;
        if (PumpDownloadProgress() && m_screen != Screen::Reader)
            redraw = true;
        if (m_route_changed.exchange(false) && m_task_type != TaskType::None)
        {
            m_status = m_task_status + T("（线路：") + T(TakeRouteLabel()) + T("）");
            redraw = true;
        }
        if (m_screen == Screen::Reader && m_reader.Update())
            redraw = true;
        // Keep the busy indicator moving while a request runs.
        const unsigned now = SDL_GetTicks();
        if (m_task_type != TaskType::None && m_screen != Screen::Reader && now - last_animation > 110)
        {
            last_animation = now;
            redraw = true;
        }
        if (redraw)
            Draw();
        else
            svcSleepThread(16'666'667);
    }
#endif
}

void App::Uninit()
{
    CancelNetworkTask();
    if (m_reader.IsOpen())
        CloseReader();
    m_thumbnails.Stop();
    m_downloader.Stop();
    m_updater.Stop();
    // Superseded requests are already cancelled; wait for them to unwind.
    if (m_network_task.valid())
        m_retired_tasks.push_back(std::move(m_network_task));
    for (std::future<TaskResult>& task : m_retired_tasks)
    {
        try { task.get(); }
        catch (...) {}
    }
    m_retired_tasks.clear();
#ifdef __SWITCH__
    if (m_psm_ready)
        psmExit();
    if (m_curl_ready)
        m_http.Shutdown();
    if (m_socket_ready)
        socketExit();
#else
    m_http.Shutdown();
#endif
    m_curl_ready = false;
    m_socket_ready = false;
    m_ui.Shutdown();
}

void App::Draw()
{
    switch (m_screen)
    {
    case Screen::GalleryList:
    {
        std::string title = std::string(T(SourceName(m_list_source)));
        if (m_list_source == ListSource::Search)
            title = std::string(T("搜索：")) + m_search_keyword;
        if (m_list_source == ListSource::Toplist)
            title = std::string(T("排行榜")) + " · " + T(kToplistNames[m_toplist_period]) + T("（X 切换）");
        if (m_list_source == ListSource::Favorites)
            title = std::string(T("收藏")) + " · " + FavoriteFolderName(m_favorite_folder) + T("（X 切换）");
        std::string subtitle = std::to_string(m_galleries.size()) + T(" 个");
        if (m_list_source == ListSource::History)
            subtitle += std::string("   ") + T("A 详情 · Y 下载");
        else if (m_list_source == ListSource::Search)
            subtitle += std::string("   ") + T("X 加入我的订阅 · − 重新搜索 · Y 下载");
        else
            subtitle += std::string("   ") + T("ZL/ZR 切换菜单 · − 搜索 · Y 下载");
        if (!m_thumb_info.empty())
            subtitle += " · " + m_thumb_info;
        std::string footer;
        if (m_loading_more)
            footer = T("正在加载更多…");
        else if (m_galleries.empty() && m_task_type == TaskType::GalleryList)
            footer = T("正在加载…");
        else if (!m_galleries.empty() && m_list_nav.next_url.empty() && m_list_source != ListSource::History)
            footer = T("没有更多了");
        m_ui.DrawGalleryList(m_galleries, m_selected, title, subtitle, CurrentSidebarItem(),
                             m_settings.list_layout, m_list_scroll, footer, m_status, m_last_input,
                             m_input_events);
        break;
    }
    case Screen::Subscriptions:
        m_ui.DrawSubscriptions(m_subscriptions.Entries(), m_subscription_selected, m_status, m_last_input,
                               m_input_events);
        break;
    case Screen::Tags:
        m_ui.DrawTags(m_detail, m_tag_selected, m_status, m_last_input, m_input_events);
        break;
    case Screen::Settings:
        m_ui.DrawSettings(BuildSettingRows(), m_settings_selected, m_status, m_last_input, m_input_events);
        break;
    case Screen::GalleryDetail:
        m_ui.DrawGalleryDetail(m_detail, LocalState(m_detail.gid), DownloadSnapshot(), m_status,
                               m_last_input, m_input_events);
        break;
    case Screen::Previews:
        m_ui.DrawPreviews(m_detail, m_preview_selected, m_preview_first_row,
                          m_task_type == TaskType::Previews ? T("正在载入更多预览") : "", m_status, m_last_input,
                          m_input_events);
        break;
    case Screen::Comments:
        m_ui.DrawComments(m_detail, &m_comment_scroll, m_status, m_last_input, m_input_events);
        break;
    case Screen::Library:
        m_ui.DrawLibrary(m_library, m_library_selected, DownloadSnapshot(), m_status,
                         m_last_input, m_input_events);
        break;
    case Screen::Reader:
        m_reader.Draw();
        break;
    }
}
