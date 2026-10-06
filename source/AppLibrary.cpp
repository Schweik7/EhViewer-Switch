#include "App.h"
#include "AppShared.h"
#include "core/FileUtil.h"
#include "core/I18n.h"
#include "core/StorageLayout.h"

#include <algorithm>
#include <cstdio>

#ifdef __SWITCH__
#include <switch.h>
#include <SDL2/SDL.h>
#endif

using ehviewer::i18n::T;
using namespace app_shared;

void App::EnqueueDownload(const ehviewer::GallerySummary& gallery,
                          const ehviewer::GalleryDetail* detail, bool quiet)
{
    if (!CheckNetworkReady())
        return;
    if (LocalState(gallery.gid) == 2)
    {
        if (!quiet)
            m_status = T("图库已在本地书库中");
        return;
    }
    ehviewer::DownloadJob job;
    job.gallery = gallery;
    if (detail != nullptr)
        job.detail = *detail;
    job.site_base = SiteBase();
    job.cookie_header = m_cookies.BuildCookieHeader();
    job.options = NetworkOptions();
    const bool added = m_downloader.Enqueue(std::move(job));
    if (!quiet)
        m_status = added ? T("已加入下载队列: ") + gallery.title : T("该图库已在下载队列中");
}

bool App::ResumeLibraryDownload(const ehviewer::LibraryEntry& entry, bool quiet)
{
    if (!CheckNetworkReady())
        return false;
    const ehviewer::GalleryManifest& manifest = entry.manifest;
    ehviewer::DownloadJob job;
    job.gallery.gid = manifest.gid;
    job.gallery.token = manifest.token;
    job.gallery.title = manifest.title;
    job.gallery.path = "/g/" + std::to_string(manifest.gid) + "/" + manifest.token + "/";
    job.site_base = SiteBase();
    job.cookie_header = m_cookies.BuildCookieHeader();
    job.options = NetworkOptions();
    const bool added = m_downloader.Enqueue(std::move(job));
    if (!quiet && added)
        m_status = T("已继续下载：") + manifest.title;
    return added;
}

void App::RequestListThumbnails()
{
    if (!m_settings.show_thumbnails)
        return;
    std::vector<ehviewer::ThumbnailRequest> requests;
    // Start near the selection so visible rows fill in first.
    const std::size_t first = m_selected > 2 ? m_selected - 2 : 0;
    for (std::size_t offset = 0; offset < m_galleries.size(); ++offset)
    {
        const ehviewer::GallerySummary& gallery = m_galleries[(first + offset) % m_galleries.size()];
        if (!gallery.thumb_url.empty() && !m_ui.HasThumbnail(gallery.gid))
            requests.push_back({gallery.gid, gallery.thumb_url});
    }
    if (!requests.empty())
    {
        m_thumb_decoded = 0;
        m_thumb_decode_failed = 0;
        m_thumb_decode_error.clear();
        m_thumbnails.Replace(std::move(requests), m_cookies.BuildCookieHeader(), NetworkOptions());
    }
}

bool App::PumpThumbnails()
{
    bool changed = false;
    ehviewer::ThumbnailResult result;
    // Decode at most a few per frame to keep input latency low.
    for (int count = 0; count < 3 && m_thumbnails.Pop(&result); ++count)
    {
        std::string error;
        if (m_ui.SetThumbnail(result.key, result.bytes, &error))
        {
            ++m_thumb_decoded;
            changed = true;
        }
        else
        {
            ++m_thumb_decode_failed;
            m_thumb_decode_error = error.empty() ? "unknown" : error;
        }
    }

    // Only surface thumbnail problems; normal progress needs no text.
    const ehviewer::ThumbnailStats stats = m_thumbnails.Stats();
    std::string info;
    if (stats.failed > 0 || m_thumb_decode_failed > 0)
    {
        info = T("缩略图失败 ") + std::to_string(stats.failed + m_thumb_decode_failed) + T(" 张");
        if (!m_thumb_decode_error.empty())
            info += T(" · 解码: ") + m_thumb_decode_error;
        else if (!stats.last_error.empty())
            info += " · " + stats.last_error;
    }
    if (info != m_thumb_info)
    {
        m_thumb_info = std::move(info);
        changed = true;
    }
    return changed;
}

bool App::PumpDownloadProgress()
{
    const unsigned version = m_downloader.Version();
    if (version == m_download_version)
        return false;
    m_download_version = version;
    const ehviewer::DownloadProgress progress = m_downloader.Snapshot();
    if (progress.active)
    {
        char badge[32];
        std::snprintf(badge, sizeof(badge), "%d/%d", progress.done, progress.total);
        m_ui.SetDownloadBadge(progress.total > 0 ? badge : "…");
    }
    else
    {
        m_ui.SetDownloadBadge(progress.queued > 0 ? std::to_string(progress.queued) : "");
    }
    if (progress.completed_count != m_completed_seen)
    {
        m_completed_seen = progress.completed_count;
        if (m_screen == Screen::Library)
            RefreshLibrary();
    }
    // Report a finished job once, when the worker goes idle.
    if (m_download_active && !progress.active && m_screen != Screen::Library &&
        !progress.last_message.empty())
        m_status = progress.last_message;
    m_download_active = progress.active;
    // A page re-download restarted the running job of its gallery.
    if (m_resume_after_cancel_gid != 0 && !(progress.active && progress.gid == m_resume_after_cancel_gid))
    {
        std::vector<ehviewer::LibraryEntry> entries;
        ehviewer::ScanLibrary(ehviewer::StorageLayout(kLibraryRoot), &entries);
        for (const ehviewer::LibraryEntry& entry : entries)
            if (entry.gid == m_resume_after_cancel_gid)
                ResumeLibraryDownload(entry, true);
        m_resume_after_cancel_gid = 0;
    }
    return true;
}

int App::LocalState(std::int64_t gid) const
{
    const ehviewer::StorageLayout layout(kLibraryRoot);
    if (ehviewer::file::Exists(layout.ManifestPath(gid)))
        return 2;
    return ehviewer::file::IsDirectory(layout.IncomingDirectory(gid)) ? 1 : 0;
}

void App::RefreshLibrary()
{
    std::string error;
    if (!ehviewer::ScanLibrary(ehviewer::StorageLayout(kLibraryRoot), &m_library, &error))
        m_status = T("读取书库失败: ") + error;
    if (m_library_selected >= m_library.size())
        m_library_selected = m_library.empty() ? 0 : m_library.size() - 1;
    LoadLibraryCovers();
}

void App::LoadLibraryCovers()
{
    const std::size_t first = m_library_selected > 3 ? m_library_selected - 3 : 0;
    const std::size_t last = std::min(m_library.size(), first + 8);
    for (std::size_t index = first; index < last; ++index)
    {
        const ehviewer::LibraryEntry& entry = m_library[index];
        if (!m_ui.HasThumbnail(entry.gid) && !entry.cover_path.empty())
            m_ui.LoadThumbnailFile(entry.gid, entry.cover_path);
    }
}

void App::OpenReader(std::int64_t gid, const std::string& title, int page_count,
                     Screen return_screen, int start_page)
{
    const ehviewer::StorageLayout layout(kLibraryRoot);
    if (start_page < 0)
    {
        // Resume from the saved position of a local copy, if any.
        start_page = 0;
        std::vector<ehviewer::LibraryEntry> entries;
        if (ehviewer::ScanLibrary(layout, &entries))
            for (const ehviewer::LibraryEntry& entry : entries)
                if (entry.gid == gid)
                    start_page = entry.manifest.current_page;
    }
    std::string error;
    static constexpr ehviewer::reader::Orientation kOrientations[] = {
        ehviewer::reader::Orientation::PortraitCounterClockwise,
        ehviewer::reader::Orientation::PortraitClockwise, ehviewer::reader::Orientation::Landscape};
    m_reader.Configure(kOrientations[std::max(0, std::min(m_settings.reader_orientation, 2))],
                       m_settings.reader_fit_width, m_settings.reader_double_page,
                       m_settings.reader_right_to_left, m_settings.prefetch_pages);
    ehviewer::ReaderView::Options options;
    options.show_clock = m_settings.reader_show_clock;
    options.show_battery = m_settings.reader_show_battery;
    options.auto_page_seconds = m_settings.reader_auto_page_seconds;
    // First time only, like Android EhViewer's gallery guide.
    options.show_guide = !m_settings.reader_guide_shown;
    m_reader.SetOptions(options);
    if (!m_reader.Open(title, ehviewer::PageDirectories(layout, gid), page_count, start_page, &error))
    {
        m_status = error;
        return;
    }
    m_reading_gid = gid;
    m_reader_return = return_screen;
    m_screen = Screen::Reader;
}

void App::CloseReader()
{
    std::string error;
    // The manifest may not exist yet if the download has not started; then
    // there is nothing to save.
    if (LocalState(m_reading_gid) != 0 &&
        !ehviewer::SaveReadingProgress(ehviewer::StorageLayout(kLibraryRoot), m_reading_gid,
                                       m_reader.CurrentPage(), &error))
        m_status = T("保存阅读进度失败: ") + error;
    m_reader.Close();
    m_screen = m_reader_return;
    if (m_screen == Screen::Library)
        RefreshLibrary();
}

bool App::UpdateDownloadSpeed()
{
    const unsigned now = SDL_GetTicks();
    const std::uint64_t bytes = m_downloader.BytesDownloaded();
    if (bytes != m_speed_last_bytes && m_last_byte_tick == 0)
        m_last_byte_tick = now;
    if (now - m_speed_last_tick < 1000)
        return false;
    const double seconds = (now - m_speed_last_tick) / 1000.0;
    const double instant = m_speed_last_tick == 0 ? 0.0 : (bytes - m_speed_last_bytes) / seconds;
    // Light smoothing so the number is readable but still reacts quickly.
    m_speed = m_speed <= 0.0 ? instant : m_speed * 0.4 + instant * 0.6;
    if (bytes != m_speed_last_bytes)
        m_last_byte_tick = now;
    m_speed_last_bytes = bytes;
    m_speed_last_tick = now;
    return m_download_active && (m_screen == Screen::Library || m_screen == Screen::GalleryDetail);
}

ehviewer::DownloadProgress App::DownloadSnapshot() const
{
    ehviewer::DownloadProgress progress = m_downloader.Snapshot();
    if (progress.active)
    {
        progress.speed_bytes_per_second = m_speed;
        progress.stalled_seconds = m_last_byte_tick == 0
            ? 0 : static_cast<int>((SDL_GetTicks() - m_last_byte_tick) / 1000);
    }
    return progress;
}

void App::DeleteSelectedLibraryEntry()
{
    if (m_library_selected >= m_library.size())
        return;
    const std::int64_t gid = m_library[m_library_selected].gid;
    const ehviewer::DownloadProgress progress = m_downloader.Snapshot();
    if (progress.active && progress.gid == gid)
    {
        // The worker may still be writing; cancel first, delete afterwards.
        m_downloader.CancelAll();
        m_status = ehviewer::i18n::T("已取消该图库的下载，请稍后再删除");
        return;
    }
    const ehviewer::StorageLayout layout(kLibraryRoot);
    std::string error;
    bool ok = true;
    for (const std::string& directory : ehviewer::PageDirectories(layout, gid))
        ok = ehviewer::file::RemoveTree(directory, &error) && ok;
    ehviewer::file::CommitDevice(&error);
    m_status = ok ? std::string(ehviewer::i18n::T("已删除")) : std::string(ehviewer::i18n::T("删除失败: ")) + error;
    RefreshLibrary();
}

void App::UpdateKeepAwake()
{
#ifdef __SWITCH__
    const bool want = m_settings.keep_awake && (m_screen == Screen::Reader || m_download_active);
    if (want == m_keep_awake_active)
        return;
    // Media playback state keeps the console from dimming and sleeping.
    appletSetMediaPlaybackState(want);
    m_keep_awake_active = want;
#endif
}

bool App::HandleReaderAction()
{
    int page = 0;
    switch (m_reader.TakeAction(&page))
    {
    case ehviewer::ReaderView::Action::Close: CloseReader(); return true;
    case ehviewer::ReaderView::Action::Jump: PromptReaderJump(); return true;
    case ehviewer::ReaderView::Action::GuideFinished:
        m_settings.reader_guide_shown = true;
        SaveSettings();
        return true;
    case ehviewer::ReaderView::Action::SettingsChanged: SaveReaderSettings(); return true;
    case ehviewer::ReaderView::Action::RefetchPage: RefetchReaderPage(page); return true;
    case ehviewer::ReaderView::Action::SavePage: SaveReaderPage(page); return true;
    default: return false;
    }
}

void App::SaveReaderSettings()
{
    // The reader menu changes the defaults, as in Android EhViewer.
    m_settings.reader_orientation = m_reader.OrientationIndex();
    m_settings.reader_fit_width = m_reader.FitWidth();
    m_settings.reader_double_page = m_reader.DoublePage();
    m_settings.reader_right_to_left = m_reader.RightToLeft();
    const ehviewer::ReaderView::Options& options = m_reader.GetOptions();
    m_settings.reader_show_clock = options.show_clock;
    m_settings.reader_show_battery = options.show_battery;
    m_settings.reader_auto_page_seconds = options.auto_page_seconds;
    std::string error;
    if (!m_settings.Save(kSettingsFile, &error))
        m_reader.ShowToast(T("保存设置失败: ") + error);
}

void App::RefetchReaderPage(int page)
{
    const ehviewer::StorageLayout layout(kLibraryRoot);
    const std::int64_t gid = m_reading_gid;
    std::string error;
    // A finished gallery goes back to .incoming; the downloader then fetches
    // only the missing page and publishes it again. The reader looks in both.
    if (LocalState(gid) == 2 && !ehviewer::file::IsDirectory(layout.IncomingDirectory(gid)) &&
        !ehviewer::file::Rename(layout.GalleryDirectory(gid), layout.IncomingDirectory(gid), &error))
    {
        m_reader.ShowToast(T("无法重新下载: ") + error);
        return;
    }
    const std::string path = m_reader.PageFile(page);
    if (!path.empty())
        ehviewer::file::Remove(path);
    ehviewer::file::CommitDevice(&error);
    m_reader.ReloadPage(page);
    m_downloader.SetFocus(gid, page);
    // A running job for this gallery has already passed the page: restart it.
    if (m_downloader.Pause(gid))
        m_resume_after_cancel_gid = gid;
    else
    {
        std::vector<ehviewer::LibraryEntry> entries;
        ehviewer::ScanLibrary(layout, &entries);
        for (const ehviewer::LibraryEntry& entry : entries)
            if (entry.gid == gid)
                ResumeLibraryDownload(entry, true);
    }
    m_reader.ShowToast(T("正在重新下载第 ") + std::to_string(page + 1) + T(" 页"));
}

void App::SaveReaderPage(int page)
{
    const std::string path = m_reader.PageFile(page);
    if (path.empty())
    {
        m_reader.ShowToast(T("这一页还没有下载"));
        return;
    }
    // ASCII names only on the SD card (see StorageLayout).
    const std::string directory = std::string(kLibraryRoot) + "/saved";
    const std::string target = directory + "/g" + std::to_string(m_reading_gid) + "_p" +
                               std::to_string(page + 1) + path.substr(path.find_last_of('.'));
    std::string bytes;
    std::string error;
    const bool ok = ehviewer::file::CreateDirectoryRecursive(directory, &error) &&
                    ehviewer::file::ReadAll(path, &bytes, &error, 64U * 1024U * 1024U) &&
                    ehviewer::file::WriteAllAtomic(target, bytes, &error) &&
                    ehviewer::file::CommitDevice(&error);
    m_reader.ShowToast(ok ? T("已保存到 ") + target.substr(target.find(':') + 1) : T("保存失败: ") + error);
}