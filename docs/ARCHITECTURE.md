# Switch 端架构

## 目标与边界

SwitchPort 是 EhViewer 的独立原生前端，目标依次为在线浏览、图库详情、下载、竖屏阅读、Switch→PC 导出。登录只使用 Cookie，不实现 WebView。Android 代码是协议和解析行为参考，不作为 Switch 构建依赖。

## 模块

- `source/App.*`：同一个 `App` 类按职责分到多个文件（`AppShared.h` 放路径常量）：
  - `App.cpp`：初始化、主循环、退出、`Draw` 分发。
  - `AppInput.cpp`：各页面按键（`Handle*Input`）、触屏、侧栏导航、弹出菜单、系统键盘。
  - `AppNetwork.cpp`：异步网络任务（列表/详情/收藏/评分/登录）、任务取代与取消、站点 URL。
  - `AppList.cpp`：列表平滑滚动、惯性、自动加载下一页。
  - `AppLibrary.cpp`：下载入队、缩略图、书库、阅读器开关、下载速度、防休眠。
  - `AppSettings.cpp`：设置行与修改、历史、订阅。
- `source/ui/Ui.*`：SDL2/SDL_ttf Material 风格界面基础（字体回退、文字缓存、缩略图 LRU、侧栏、状态栏、弹出菜单）；
  `UiGallery.cpp` 画列表/详情/评论/标签，`UiLocal.cpp` 画设置/订阅/书库，`UiShared.h` 放布局常量、配色和格式化函数。
- `source/ui/ReaderView.*`：SDL 阅读器；后台解码、UI 线程建纹理、逻辑画布旋转输出。
- `source/net/HttpClient.*`：libcurl、TLS、Cookie、代理、内置 hosts、DoH、Referer、取消回调和响应大小限制；`GetWithFallback` 逐线路回退。
- `source/net/NetworkPlan.*`：纯函数的线路尝试计划、成功线路优先、Cookie 目标域判断 `IsSiteHost`。
- `source/download/Downloader.*`：下载队列工作线程，`.incoming` 续传与发布。
- `source/download/ThumbnailLoader.*`：缩略图后台抓取，可整体替换待办队列。
- `source/core/GalleryParser.*`：无 DOM 依赖的图库列表（含缩略图）/详情（封面、预览分页、页面 token）/图片页解析。测试使用 Android 仓库真实 HTML fixture。
- `source/core/Json.*`：小型 JSON DOM，用于回读 manifest。
- `source/core/Library.*`：扫描已发布图库、按 manifest 生成页序、保存阅读进度。
- `source/core/CookieConfig.*`：Cookie 与网络选项解析、校验和脱敏摘要。
- `source/core/StorageLayout.*`：ASCII 实体路径规则。
- `source/core/GalleryManifest.*`：实体名到真实 UTF-8 文件名映射及导出元数据。
- `source/core/SpiderInfo.*`：Android `.ehviewer` VERSION1/VERSION2 兼容。
- `source/reader/ReaderCore.*`：平台无关的 CCW 竖屏坐标、缩放、单双页、预读和内存预算。

## 运行流程

```text
Pad/SDL events -> App state machine -> Ui render / ReaderView
                       |
                       +-> std::async (page requests) ----+
                       +-> ThumbnailLoader thread --------+-> HttpClient::GetWithFallback
                       +-> Downloader thread -------------+        |
                                                     proxy -> built-in hosts -> DoH -> system DNS
```

网络任务永远不阻塞 UI。任务开始时复制 Cookie 和网络配置；工作线程只返回 `TaskResult`，主线程轮询 future 并应用状态。取消由原子标志传给 libcurl progress callback。缩略图和阅读页的 SDL 纹理只在 UI 线程创建；工作线程只产出编码字节或 `SDL_Surface`。

## 网络顺序

`cookies.ini` 支持：

```ini
proxy=http://<pc-ip>:7890
builtin_hosts=true
doh=true
```

内置 hosts 来自 Android `EhHosts`，覆盖 e-hentai、exhentai、upld、s.exhentai 和 ehgt；TLS 仍使用真实域名/SNI。DoH 端点为 `https://77.88.8.1/dns-query`。默认仅 IPv4，连接/总超时为 10/20 秒（图片 120 秒）。

每个启用的机制单独作为一次尝试：代理 → 内置 hosts 直连 → DoH → 系统 DNS。只有传输层失败才换下一条；成功线路记入 `HttpClient`，后续请求优先使用。UI 状态栏显示当前线路。对非站点域名（H@H 图片服务器）只尝试代理和直连，且不发送 Cookie。

## 存储事务

根目录为 `sdmc:/manga/EhViewerSwitch`。完整图库位于 `galleries/g_<gid>`，下载中的内容位于 `.incoming/g_<gid>`。页图采用 `p_00000001.jpg`。写文件使用 `.new`、flush/fsync、`fsdevCommitDevice("sdmc")` 后发布。真实标题与文件名只存在 manifest，避免 FAT/libnx Unicode 路径差异。

下载器把收集到的页面 token 存为 `.incoming/g_<gid>/spider_info.ehviewer`（Android VERSION2 格式），页面文件按魔数决定扩展名，全部完成后写 manifest 并把整个目录改名到 `galleries/`。续传以“页面文件已存在且非空”为准；页面写入本身是 `.new` + rename 原子操作，所以不会留下半张图。

## 尚未实现

- 搜索、分页、收藏。
- `api.php showpage` 快速取图（当前每页请求 HTML 图片页）。
- 下载 hash 校验（manifest `sha256` 为空）、限速。
- 阅读器触摸操作。
- Switch→PC 导出 UI/客户端。
