#pragma once

#include "reader/ReaderCore.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

struct SDL_Surface;
struct SDL_Texture;

namespace ehviewer {

class Ui;

// Full-screen reader for downloaded galleries. Pages are decoded on a worker
// thread and turned into textures on the UI thread. The page is laid out on a
// logical canvas (720x1280 in portrait) and rotated onto the 1280x720 screen;
// the default is counter-clockwise so the left D-pad ends up at the bottom.
class ReaderView {
public:
    // Reader extras, like Android EhViewer's reader settings.
    struct Options {
        bool show_clock = true;
        bool show_battery = true;
        int auto_page_seconds = 0;  // 0 = off
        bool show_guide = false;    // tap-zone guide on the next Open()
    };
    // Requests for the App, collected with TakeAction().
    enum class Action { None, Close, Jump, RefetchPage, SavePage, GuideFinished, SettingsChanged };

    explicit ReaderView(Ui* ui);
    ~ReaderView();

    // directories are searched in order for p_<n>.<ext>; pages not on disk yet
    // show a waiting indicator and are looked up again periodically.
    bool Open(const std::string& title, std::vector<std::string> directories, int page_count,
              int start_page, std::string* error = nullptr);
    void Close();
    bool IsOpen() const { return open_; }
    int CurrentPage() const { return static_cast<int>(state_.current_page); }
    int PageCount() const { return static_cast<int>(page_count_); }

    // Defaults applied by the next Open().
    void Configure(reader::Orientation orientation, bool fit_width, bool double_page,
                   bool right_to_left, int prefetch_pages);
    void JumpTo(int page);
    void SetOptions(const Options& options) { options_ = options; }
    const Options& GetOptions() const { return options_; }
    // Returns the pending request and clears it; page is the page it is about.
    Action TakeAction(int* page = nullptr);
    // The current reading mode, for saving it as the default.
    int OrientationIndex() const;
    bool FitWidth() const { return state_.scale_mode == reader::ScaleMode::FitWidth; }
    bool DoublePage() const { return state_.requested_page_mode == reader::PageMode::Double; }
    bool RightToLeft() const { return state_.reading_direction == reader::ReadingDirection::RightToLeft; }
    // File of a page on disk, or empty while it is not downloaded.
    std::string PageFile(int page) const;
    // Forgets a page so it is looked up and decoded again.
    void ReloadPage(int page);
    // Short message drawn on the reader canvas.
    void ShowToast(const std::string& text);
    bool OverlayOpen() const { return overlay_ != Overlay::None; }

    // Touch input in panel coordinates (see reader::ClassifyTap for the zones).
    // Drag scrolls the page, or moves the progress bar when it is open.
    bool Tap(int x, int y);
    bool LongPress(int x, int y);
    bool Drag(int dx, int dy, int x, int y);

    // Physical button masks from libnx (HidNpadButton_*). Returns true when
    // the view changed and needs a redraw.
    bool HandleInput(std::uint64_t down, std::uint64_t held);
    // Moves decoded pages into textures and schedules prefetching. Returns
    // true when a redraw is needed.
    bool Update();
    void Draw();

private:
    struct PageTexture {
        SDL_Texture* texture = nullptr;
        int width = 0;
        int height = 0;
        std::uint64_t bytes = 0;
    };
    struct Decoded {
        std::size_t page = 0;
        SDL_Surface* surface = nullptr;
        bool failed = false;
        bool missing = false;
    };

    void WorkerLoop();
    void StopWorker();
    void SchedulePrefetch();
    void GoToPage(std::size_t page, bool scroll_to_end);
    bool Scroll(double logical_delta);
    reader::PageTransform CurrentTransform() const;
    void EnsureCanvas();
    void ReleaseTextures();

    // Overlays drawn on the canvas: the two-step tap guide, the reader menu,
    // the progress bar, the long-press page menu and page information.
    enum class Overlay { None, Guide1, Guide2, Menu, Progress, PageMenu, PageInfo };
    struct MenuItem {
        std::string label;
        std::string value;
        int id = 0;
    };
    bool NextPage();
    bool PreviousPage();
    bool MirroredTaps() const;
    void OpenOverlay(Overlay overlay, int page = -1);
    void BuildMenu();
    bool ActivateMenuItem(int delta);
    bool HandleOverlayInput(std::uint64_t down);
    bool TapOverlay(reader::Point logical);
    bool SetProgressFromPoint(reader::Point logical);
    void DrawOverlay(int width, int height);
    void DrawGuide(int width, int height);
    void DrawStatus(int width, int height);
    void FinishGuide();

    Ui* ui_;
    bool open_ = false;
    std::string title_;
    std::vector<std::string> directories_;
    std::size_t page_count_ = 0;
    reader::ReaderState state_;
    reader::ReaderState defaults_;
    reader::PrefetchPolicy prefetch_policy_;
    std::map<std::size_t, PageTexture> textures_;
    std::set<std::size_t> failed_;
    // Page -> SDL tick after which a not-yet-downloaded page is looked up again.
    std::map<std::size_t, unsigned> missing_;
    unsigned last_spinner_tick_ = 0;
    SDL_Texture* canvas_ = nullptr;
    reader::Orientation canvas_orientation_ = reader::Orientation::PortraitCounterClockwise;
    unsigned hint_until_ = 0;
    unsigned last_hint_tick_ = 0;
    int max_texture_size_ = 4096;

    Options options_;
    Overlay overlay_ = Overlay::None;
    std::vector<MenuItem> menu_;
    std::size_t menu_selected_ = 0;
    std::vector<reader::Rect> menu_rects_;  // logical, filled while drawing
    reader::Rect progress_rect_;
    int overlay_page_ = 0;
    std::string page_info_;
    Action action_ = Action::None;
    int action_page_ = 0;
    std::string toast_;
    unsigned toast_until_ = 0;
    unsigned last_turn_tick_ = 0;   // auto page turning
    int last_minute_ = -1;          // clock redraw
    int battery_percent_ = -1;
    unsigned battery_tick_ = 0;

    std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<std::size_t> wanted_;
    std::set<std::size_t> in_flight_;
    std::deque<Decoded> decoded_;
    bool stopping_ = false;
    std::thread worker_;
};

}  // namespace ehviewer
