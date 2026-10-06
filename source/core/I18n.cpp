#include "I18n.h"

#include <atomic>
#include <unordered_map>

namespace ehviewer::i18n {
namespace {

std::atomic_bool g_english{false};

struct Entry {
    const char* chinese;
    const char* english;
};

// Keep entries grouped by screen. Pieces of composed messages (leading or
// trailing spaces, brackets) are translated individually where they are built.
constexpr Entry kEntries[] = {
    // Sidebar, header and status bar.
    {"浏览", "Browse"}, {"发现", "Discover"}, {"书库", "Library"}, {"历史", "History"}, {"设置", "Settings"},
    {"控制提示", "Controls"}, {"A 确认   B 返回", "A OK   B Back"},
    {"首页", "Home"}, {"列表", "List"}, {"详情", "Detail"},
    {"网络请求", "Network"}, {"完成", "Done"}, {"需要处理", "Attention"}, {"状态", "Status"},
    {"已就绪", "Ready"}, {"按键  %s   #%llu", "Input  %s   #%llu"},
    {"上下选择 · A 确定 · B 取消", "Up/Down select · A OK · B Cancel"},

    // Home.
    {"在线浏览 E-Hentai / ExHentai", "Browse E-Hentai / ExHentai online"},
    {"在线服务", "Online"}, {"欢迎回来", "Welcome back"},
    {"使用 Cookie 登录，无需 WebView。手柄输入会在网络请求期间保持响应。",
     "Signed in with cookies, no WebView. Input stays responsive during requests."},
    {"站点尚未配置", "Site not configured"}, {"新", "New"}, {"最新图库", "Latest galleries"},
    {"获取最新发布内容，选择条目后可查看\n图库标题、分类与页数。",
     "Load the latest uploads, then open one to\nsee its title, category and pages."},
    {"X  开始浏览", "X  Browse"}, {"帐", "Me"}, {"账户与连接", "Account & network"},
    {"测试 Cookie 与站点连接", "Test cookies and connection"},
    {"重读配置 / 取消请求", "Reload config / cancel request"},
    {"本地书库与下载进度", "Library and downloads"},
    {"− 搜索 · L 历史 · R 设置 · + 退出", "− Search · L History · R Settings · + Quit"},

    // Gallery list.
    {"最新", "Latest"}, {"热门", "Popular"}, {"订阅", "Watched"}, {"收藏", "Favorites"},
    {"排行榜", "Toplist"}, {"搜索", "Search"}, {"搜索：", "Search: "},
    {"昨日", "Yesterday"}, {"本月", "Past month"}, {"本年", "Past year"}, {"总榜", "All time"},
    {"（X 切换）", " (X to switch)"}, {"第 ", "Page "}, {" 页 · 本页 ", " · "}, {" 个", " items"},
    {"ZL/ZR 切换分类 · L/R 翻页 · − 搜索 · Y 下载", "ZL/ZR source · L/R page · − search · Y download"},
    {" 条记录", " entries"}, {"A 详情 · Y 下载 · ZR 回到在线列表", "A detail · Y download · ZR online lists"},
    {"还没有图库内容", "No galleries yet"},
    {"按 X 从站点加载，网络请求期间可以继续操作或按 B 取消。",
     "Press X to load from the site. You can keep using the app or press B to cancel."},
    {"X  加载图库", "X  Load"}, {"无标题图库", "Untitled gallery"}, {" 页  ·  ", " pages  ·  "},
    {"缩略图失败 ", "Thumbnails failed: "}, {" 张", ""}, {" · 解码: ", " · decode: "},
    {"还没有浏览记录", "No history yet"},
    {"搜索图库", "Search galleries"}, {"关键词或标签，例如 language:chinese", "Keywords or tags, e.g. language:chinese"},
    {"无法打开系统键盘", "Cannot open the system keyboard"}, {"没有找到结果", "No results"},
    {"正在加载「", "Loading \""}, {"」，B 取消", "\", B to cancel"},
    {"已加载（线路：", "Loaded (route: "}, {"）", ")"}, {"（线路：", " (route: "},

    // Gallery detail.
    {"图库详情", "Gallery"}, {"图库信息", "Information"}, {"页数", "Pages"}, {"大小", "Size"},
    {"语言", "Language"}, {"上传者", "Uploader"}, {"发布时间", "Posted"}, {"评分", "Rating"},
    {"A 阅读 · X 评论 · L 评分 · R 收藏 · B 返回", "A read · X comments · L rate · R favorite · B back"},
    {"A 边下边读 · Y 下载 · X 评论 · L 评分 · R 收藏 · B 返回",
     "A read now · Y download · X comments · L rate · R favorite · B back"},
    {"A  开始阅读", "A  Read"}, {"A  边下边读", "A  Read now"}, {"已在本地书库", "In library"},
    {"下载中 %d/%d", "Downloading %d/%d"}, {"Y  继续下载", "Y  Resume"}, {"Y  下载到书库", "Y  Download"},
    {"X  评论 ", "X  Comments "}, {"ZL 标签 ", "ZL Tags "}, {"未收藏", "Not a favorite"},
    {"★ 已收藏于 ", "★ In "}, {"   ·   被收藏 ", "   ·   favorited "}, {"图库详情加载完成", "Gallery loaded"},
    {"这个页面没有评分所需的 API 参数，请刷新详情后再试", "This page has no rating API key; reload the gallery"},

    // Comments and tags.
    {"评论 ", "Comments "}, {"上下滚动 · L/R 快速翻页 · B 返回详情", "Up/Down scroll · L/R page · B back"},
    {"上传者  ", "Uploader  "}, {"匿名", "Anonymous"}, {"这个图库还没有评论", "No comments yet"},
    {"标签", "Tags"}, {"方向键选择 · A 按标签搜索 · B 返回详情", "D-pad select · A search tag · B back"},

    // Favorites and rating.
    {"加入收藏夹", "Add to favorites"}, {"（当前）", " (current)"}, {"从收藏中移除", "Remove from favorites"},
    {"给这个图库评分", "Rate this gallery"}, {"上下选择（☆ 为半星） · A 提交 · B 取消",
                                               "Up/Down select (☆ = half star) · A submit · B cancel"},
    {"正在读取收藏夹", "Reading favorite folders"}, {"选择收藏夹", "Choose a folder"},
    {"读取收藏夹失败: ", "Cannot read favorites: "}, {"收藏夹 HTTP ", "Favorites HTTP "},
    {"收藏夹页面解析失败", "Cannot parse the favorites page"}, {"正在加入收藏", "Adding to favorites"},
    {"正在移出收藏", "Removing from favorites"}, {"收藏 HTTP ", "Favorite HTTP "}, {"收藏失败: ", "Favorite failed: "},
    {"已从收藏中移除", "Removed from favorites"}, {"已加入收藏：", "Added to favorites: "},
    {"正在提交评分", "Submitting rating"}, {"评分参数无效", "Invalid rating parameters"},
    {"评分 HTTP ", "Rating HTTP "}, {"评分失败: ", "Rating failed: "}, {"已评分 %.1f 星", "Rated %.1f stars"},

    // Library and downloads.
    {"本地书库", "Library"}, {"A 阅读 · − 继续下载 · ZR 删除 · Y 取消全部下载 · X 刷新 · B 返回",
                               "A read · − resume · ZR delete · Y cancel downloads · X refresh · B back"},
    {"正在下载：", "Downloading: "}, {"%s  %d / %d   排队 %zu", "%s  %d / %d   queued %zu"},
    {"下载队列空闲", "Download queue idle"}, {"书库还是空的", "The library is empty"},
    {"在图库列表或详情页按 Y 下载，完成后会出现在这里。",
     "Press Y in a list or gallery to download; finished galleries appear here."},
    {"%d 页 · 读到第 %d 页 · GID %lld", "%d pages · at page %d · GID %lld"},
    {"%s · 已下载 %d/%d 页 · 读到第 %d 页", "%s · %d/%d pages downloaded · at page %d"},
    {"下载中", "Downloading"}, {"未完成（− 继续下载）", "Incomplete (− to resume)"}, {"A 阅读", "A Read"},
    {"已加入下载队列: ", "Queued: "}, {"该图库已在下载队列中", "Already in the download queue"},
    {"图库已在本地书库中", "Already in the library"}, {"已请求取消全部下载", "Cancelling all downloads"},
    {"删除「", "Delete \""}, {"」？", "\"?"}, {"删除本地文件", "Delete local files"}, {"取消", "Cancel"},
    {"删除后无法恢复", "This cannot be undone"}, {"已删除", "Deleted"}, {"删除失败: ", "Delete failed: "},
    {"已取消该图库的下载，请稍后再删除", "Download cancelled; delete again in a moment"},
    {"读取书库失败: ", "Cannot read library: "},
    {"准备中", "Preparing"}, {"读取图库详情", "Reading gallery"}, {"收集页面链接", "Collecting page links"},
    {"下载封面", "Downloading cover"}, {"下载页面", "Downloading pages"}, {"下载页面（", "Downloading pages ("},
    {" 页失败）", " failed)"}, {"写入清单", "Writing manifest"}, {"下载完成: ", "Download finished: "},
    {"下载已取消，已完成的页面会在下次继续使用", "Download cancelled; finished pages will be reused"},
    {"下载异常: ", "Download error: "}, {"下载发生未知异常", "Unknown download error"},
    {" 页下载失败（", " pages failed ("}, {"）。已下载的页面可以阅读，再次下载会只重试失败页",
                                       "). Downloaded pages can be read; downloading again retries only failed pages"},
    {" 页: ", ": "}, {"缺少第 ", "Missing link for page "}, {" 页的链接", ""},
    {"没有找到任何页面", "No pages found"}, {"已取消", "Cancelled"},
    // Drawer, subscriptions, infinite list, detail redesign.
    {"关注", "Watched"}, {"我的订阅", "Subscriptions"}, {"下载", "Downloads"},
    {"还没有订阅", "No subscriptions yet"},
    {"在搜索结果里按 X 保存为订阅，或在这里按 X 新建。", "Press X in search results to save one, or X here to add."},
    {"X  新建订阅", "X  New subscription"}, {"新建订阅", "New subscription"},
    {"A 打开 · X 新建 · Y 删除 · ZL/ZR 切换菜单", "A open · X new · Y delete · ZL/ZR menu"},
    {"删除订阅「", "Delete subscription \""}, {"删除", "Delete"},
    {"已加入我的订阅：", "Subscribed: "}, {"这个搜索已在我的订阅中", "Already subscribed to this search"},
    {"保存订阅失败: ", "Cannot save subscriptions: "}, {"读取订阅失败: ", "Cannot read subscriptions: "},
    {"正在加载更多…", "Loading more…"}, {"正在加载…", "Loading…"}, {" 改名为 ", " to "}, {"，但没有附带 NRO", ", but it has no NRO"},
    {"，可在 设置 → 关于 中安装", ", install it in Settings → About"}, {"；备用源: ", "; other source: "},
    {"按 A 检查", "A to check"}, {"按 A 下载并安装", "A to download and install"}, {"保存更新失败：", "Saving the update failed: "},
    {"发现 ", "Found "}, {"发现新版本 ", "New version "}, {"服务器: ", "Server: "}, {"改用备用源下载…", "Downloading from the other source…"},
    {"更新文件不完整，已丢弃", "The update file was incomplete and was removed"},
    {"更新写入失败，已还原原有版本", "Writing the update failed; the old version was restored"},
    {"检查更新", "Check for updates"}, {"检查更新失败：", "Update check failed: "}, {"启动时检查更新", "Check for updates at start"},
    {"退出后重新打开即完成更新", "Restart the app to finish"}, {"文件大小不对 ", "Wrong file size "},
    {"无法替换旧版本，请手动把 ", "Cannot replace the old version; rename "}, {"下载到的文件不是 NRO", "The download is not an NRO"},
    {"下载失败：", "Download failed: "}, {"先查更新服务器，失败再查 GitHub", "Update server first, then GitHub"},
    {"已安装新版本，下次启动生效", "New version installed; it starts next time"}, {"已是最新版本 ", "Up to date "},
    {"正在检查更新…", "Checking for updates…"}, {"正在下载新版本…", "Downloading the new version…"},
    {"已更新到 ", "Updated to "}, {"，退出后重新打开即可使用", "; restart the app to use it"}, {"，下次启动时安装", "; it is installed at the next start"}, {"%d 页 · 已载入 %zu 张预览 · A 从这一页阅读 · B 返回", "%d pages · %zu previews loaded · A read from here · B back"}, {"预览 HTTP ", "Previews HTTP "}, {"预览：", "Previews: "}, {"预览页面解析失败", "Could not parse the preview page"}, {"载入预览失败: ", "Loading previews failed: "}, {"这个图库没有预览", "This gallery has no previews"}, {"正在载入更多预览", "Loading more previews"}, {"ZR 预览", "ZR previews"}, {"   − 菜单 · B 返回 · 点中间上方 菜单 · 长按 页面菜单", "   − menu · B back · tap top middle: menu · hold: page menu"},
    {" 秒", " s"}, {"按 A 重新显示", "A to show again"},
    {"按键：L/R 或十字键翻页 · − 菜单 · 按下右摇杆 页面菜单 · Y 缩放 · X 旋转 · B 返回", "Buttons: L/R or D-pad turn pages · − menu · press right stick: page menu · Y zoom · X rotate · B back"},
    {"保存到 SD 卡", "Save to SD card"}, {"保存失败: ", "Save failed: "}, {"菜单", "Menu"}, {"查看操作引导", "Show the guide"},
    {"尺寸：", "Size: "}, {"大小：", "File size: "}, {"第 %d / %zu 页", "Page %d / %zu"}, {"第 %d 页", "Page %d"},
    {"第 %zu / %zu 页", "Page %zu / %zu"}, {"点击屏幕继续（1/2）", "Tap to continue (1/2)"}, {"点击屏幕完成（2/2）", "Tap to finish (2/2)"},
    {"进度条", "Progress bar"}, {"屏幕方向", "Orientation"}, {"上一页", "Previous page"}, {"下一页", "Next page"},
    {"跳页…", "Go to page…"}, {"退出阅读", "Close reader"},
    {"拖动或点按跳转 · ←→ 1 页 · L/R 10 页 · B 关闭", "Drag or tap to jump · ←→ 1 page · L/R 10 pages · B close"},
    {"文件：", "File: "}, {"无法重新下载: ", "Cannot re-download: "}, {"下次阅读时显示", "Show next time"},
    {"显示电量", "Show battery"}, {"显示时钟", "Show clock"}, {"页面缩放", "Page scaling"}, {"页面信息", "Page info"},
    {"已保存到 ", "Saved to "}, {"已到最后一页", "Last page reached"}, {"已看过", "Seen"}, {"已重新载入", "Reloaded"},
    {"阅读操作引导", "Reader guide"}, {"长按图片：页面菜单", "Hold on a page: page menu"},
    {"这一页还没有下载", "This page is not downloaded yet"}, {"正在重新下载第 ", "Re-downloading page "},
    {"重新下载这一页", "Re-download this page"}, {"重新载入 · 重新下载这一页 · 保存到 SD 卡 · 页面信息", "Reload · re-download this page · save to SD card · page info"},
    {"重新载入", "Reload"}, {"自动", "Auto"}, {"自动翻页", "Auto page turn"}, {"A / B 关闭", "A / B close"}, {"已暂停全部下载", "All downloads paused"}, {"已暂停：", "Paused: "}, {"已开始 ", "Started "}, {" 个下载任务", " download(s)"}, {"没有需要继续的下载", "Nothing to resume"}, {"已继续下载：", "Resumed: "}, {"A 阅读 · Y 开始/暂停 · R 全部开始 · − 全部暂停 · X 删除 · ZL/ZR 切换菜单", "A read · Y start/pause · R start all · − pause all · X delete · ZL/ZR menu"}, {"下载中（Y 暂停）", "Downloading (Y pause)"}, {"排队中（Y 暂停）", "Queued (Y pause)"}, {"已暂停（Y 继续）", "Paused (Y resume)"}, {"下载已暂停，已完成的页面会在继续时沿用", "Download paused; finished pages are kept for later"}, {"切换收藏夹", "Favorite folders"}, {"全部", "All"}, {"A 打开 · B 取消", "A open · B cancel"}, {"没有更多了", "No more results"},
    {"A 详情 · Y 下载", "A detail · Y download"},
    {"X 加入我的订阅 · − 重新搜索 · Y 下载", "X subscribe · − new search · Y download"},
    {"ZL/ZR 切换菜单 · − 搜索 · Y 下载", "ZL/ZR menu · − search · Y download"},
    {"A 阅读 · − 继续下载 · X 删除 · Y 取消全部下载 · ZL/ZR 切换菜单",
     "A read · − resume · X delete · Y cancel downloads · ZL/ZR menu"},
    {"A 阅读 · Y 下载 · X 评论 · ZL 全部标签 · − 搜索上传者 · L 评分 · R 收藏 · B 返回",
     "A read · Y download · X comments · ZL tags · − uploader · L rate · R favorite · B back"},
    {"A 边下边读", "A Read now"}, {"Y 下载", "Y Download"}, {"Y 继续下载", "Y Resume"}, {"已下载", "Downloaded"},
    {"X 评论 ", "X Comments "}, {"L 评分", "L Rate"}, {"R 收藏", "R Favorite"}, {"R 改收藏", "R Move fav"},
    {"♡ 未收藏", "♡ Not a favorite"}, {"被收藏 ", "Favorited "}, {" 页", " pages"},
    {"ZL 查看全部 ", "ZL all "}, {" 个标签", " tags"}, {"没有标签", "No tags"},
    {"部分页面已下载，可以直接阅读", "Some pages are downloaded and can be read now"},
    {"作者", "Artist"}, {"社团", "Group"}, {"原作", "Parody"}, {"角色", "Character"}, {"女性", "Female"},
    {"男性", "Male"}, {"混合", "Mixed"}, {"其他", "Other"}, {"重分类", "Reclass"}, {"临时", "Temp"},
    {"列表样式", "List style"}, {"网格（大缩略图）", "Grid (large thumbnails)"}, {"账户", "Account"},
    {"测试 Cookie 登录", "Test cookie login"}, {"按 A 测试", "Press A to test"},
    {"重新读取 cookies.ini", "Reload cookies.ini"}, {"未配置", "Not configured"}, {"已配置", "Configured"},
    {"按 A 读取", "Press A to reload"},
    {"等待服务器响应 %d 秒", "Waiting for the server for %d s"}, {"已下载 ", "received "},
    {"使用代理", "Use proxy"}, {"仅 e-hentai.org 和排行榜需要", "Only needed for e-hentai.org and toplists"},
    {"代理地址", "Proxy address"}, {"未设置", "Not set"}, {"按 A 输入", "Press A to edit"},
    {"例如 http://192.168.1.10:7890，留空表示不用代理", "e.g. http://192.168.1.10:7890, empty = no proxy"},
    {"代理地址无效，需要 http:// 或 socks5:// 开头", "Invalid proxy address; use http:// or socks5://"}, {"写入 manifest 失败: ", "Cannot write manifest: "},
    {"发布图库失败: ", "Cannot publish gallery: "}, {"图片配额已用尽 (509)，请稍后再试",
                                                       "Image quota exceeded (509), try again later"},
    {"服务器返回的不是图片", "The server did not return an image"}, {"图片", "Image"}, {"图片页", "Image page"},
    {"预览页 ", "Preview page "}, {"保存阅读进度失败: ", "Cannot save reading progress: "},

    // Reader.
    {"正在载入第 %zu 页…", "Loading page %zu…"}, {"等待下载第 %zu 页…", "Waiting for page %zu…"},
    {"第 %zu 页无法解码", "Cannot decode page %zu"}, {"竖屏（十字键在下）", "Portrait (D-pad bottom)"},
    {"竖屏（十字键在上）", "Portrait (D-pad top)"}, {"横屏", "Landscape"}, {" · 适应宽度", " · fit width"},
    {" · 整页", " · fit page"}, {" · 双页", " · two pages"}, {" · 单页", " · one page"},
    {"   B 返回 · Y 缩放 · X 旋转 · − 跳页", "   B back · Y zoom · X rotate · − jump"},
    {"跳到第几页", "Go to page"}, {"这个图库没有可阅读的页面", "This gallery has no readable pages"},

    // Settings.
    {"上下选择 · 左右或 A 修改 · B 返回", "Up/Down select · Left/Right or A change · B back"},
    {"通用", "General"}, {"界面语言", "Language"}, {"跟随系统", "System"}, {"简体中文", "简体中文"},
    {"站点", "Site"}, {"自动（有 igneous 时用 ExHentai）", "Auto (ExHentai with igneous)"},
    {"显示按键调试信息", "Show input debug"}, {"阅读", "Reading"}, {"默认阅读方向", "Default orientation"},
    {"竖屏 · 十字键在下", "Portrait · D-pad bottom"}, {"竖屏 · 十字键在上", "Portrait · D-pad top"},
    {"默认缩放", "Default zoom"}, {"适应宽度", "Fit width"}, {"整页", "Fit page"},
    {"横屏双页", "Two pages in landscape"}, {"双页翻页方向", "Two-page direction"},
    {"从右到左", "Right to left"}, {"从左到右", "Left to right"}, {"预读页数", "Pages to preload"},
    {"阅读和下载时防止休眠", "Stay awake while reading/downloading"}, {"显示缩略图", "Show thumbnails"},
    {"分类筛选", "Category filter"}, {"全部显示", "All shown"}, {" 个已隐藏", " hidden"},
    {"首页和搜索生效", "Front page and search"}, {"记录浏览历史", "Record history"},
    {"清空浏览历史", "Clear history"}, {" 条", " entries"}, {"按 A 清空", "Press A to clear"},
    {"浏览历史已清空", "History cleared"}, {"网络", "Network"}, {"域前置直连", "Domain fronting"},
    {"代理不可用时直连 exhentai", "Direct exhentai access without proxy"}, {"内置 hosts", "Built-in hosts"},
    {"跟随 cookies.ini", "From cookies.ini"}, {"开", "On"}, {"关", "Off"}, {"关于", "About"}, {"版本", "Version"},
    {"显示的分类", "Shown categories"}, {"A 切换显示 · B 完成", "A toggle · B done"},
    {"读取设置失败: ", "Cannot read settings: "}, {"保存设置失败: ", "Cannot save settings: "},
    {"读取浏览历史失败: ", "Cannot read history: "},

    // Network tasks and routes.
    {"代理", "Proxy"}, {"域前置直连", "Domain fronting"}, {"系统 DNS", "System DNS"},
    {"正在后台测试 Cookie，B 取消", "Testing cookies, B to cancel"},
    {"正在后台加载详情，B 取消", "Loading gallery, B to cancel"},
    {"，B 取消", ", B to cancel"}, {"正在取消网络请求...", "Cancelling request..."},
    {"已有网络请求正在进行；按 B 可取消", "A request is running; press B to cancel"},
    {"登录测试失败: ", "Login test failed: "}, {"登录测试 HTTP ", "Login test HTTP "},
    {"Cookie 已失效：服务器返回登录页", "Cookies expired: the site returned the login page"},
    {"Cookie 无效或账号没有 ExHentai 权限", "Invalid cookies or no ExHentai access"},
    {"Cookie 登录验证成功（线路：", "Cookies OK (route: "},
    {"图库列表 HTTP ", "Gallery list HTTP "}, {"图库列表失败: ", "Gallery list failed: "},
    {"图库列表解析失败: ", "Cannot parse gallery list: "}, {"图库详情 HTTP ", "Gallery HTTP "},
    {"图库详情失败: ", "Gallery failed: "}, {"图库详情解析失败: ", "Cannot parse gallery: "},
    {"后台请求异常: ", "Background request error: "}, {"后台请求发生未知异常", "Unknown background error"},
};

const std::unordered_map<std::string, const char*>& Table() {
    static const std::unordered_map<std::string, const char*> table = [] {
        std::unordered_map<std::string, const char*> built;
        for (const Entry& entry : kEntries) built.emplace(entry.chinese, entry.english);
        return built;
    }();
    return table;
}

}  // namespace

void SetEnglish(bool english) { g_english.store(english); }
bool IsEnglish() { return g_english.load(); }

const char* T(const char* chinese) {
    if (!g_english.load() || chinese == nullptr) return chinese;
    const auto& table = Table();
    const auto found = table.find(chinese);
    return found == table.end() ? chinese : found->second;
}

std::string T(const std::string& chinese) {
    if (!g_english.load()) return chinese;
    const auto& table = Table();
    const auto found = table.find(chinese);
    return found == table.end() ? chinese : std::string(found->second);
}

std::size_t TableSize() { return Table().size(); }
bool HasTranslation(const std::string& chinese) { return Table().count(chinese) != 0; }

bool TableLooksTranslated(std::string* offending_key) {
    for (const Entry& entry : kEntries) {
        if (entry.chinese[0] == '\0') {
            if (offending_key) *offending_key = "(empty key)";
            return false;
        }
        // Han characters are U+4E00..U+9FFF, i.e. UTF-8 lead bytes E4..E9.
        // "简体中文" is the only intentional Chinese value (language name).
        if (std::string(entry.chinese) == "简体中文") continue;
        for (const char* c = entry.english; *c != '\0'; ++c) {
            const unsigned char byte = static_cast<unsigned char>(*c);
            if (byte >= 0xe4 && byte <= 0xe9) {
                if (offending_key) *offending_key = entry.chinese;
                return false;
            }
        }
    }
    return true;
}

}  // namespace ehviewer::i18n
