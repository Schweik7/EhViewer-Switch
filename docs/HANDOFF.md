# 开发交接（2026-10-04，0.3.0）

## 已完成且实机确认（0.2.1）

- NRO 可由 Sphaira NetLoader/nxlink 启动。
- SDL2 Material 风格 UI 正常显示。
- `padInitializeAny` 读取全部玩家槽，方向键和 INPUT 计数即时响应。
- 登录、列表、详情请求已异步化；网络请求期间 UI 不再卡住，`B` 可取消。
- FTP 部署位置：唯一一份 `<switch-ip>:5000/switch/EhViewerSwitch/EhViewerSwitch.nro`
  （2026-10-04 已按用户要求删除 `/switch/EhViewerSwitch.nro` 旧副本，不要再在别处放第二份）。
- 0.3.1 起 NRO 内嵌 NACP（Makefile `NROFLAGS --nacp/--icon`），Sphaira 可显示版本；此前的
  NRO 没有 ASET 段，所以显示 Unknown。版本号只在 Makefile `APP_VERSION` 维护，界面侧栏同步显示。

## 0.5.2（2026-10-05，已部署，待实机验证）

- **单项启停下载**：`Downloader::Pause(gid)` 从队列移除或取消正在运行的该任务（在锁内置取消标志，
  不会误伤下一个任务）；`DownloadProgress::queued_gids` 让书库显示“下载中/排队中/已暂停”。
  书库按键：`Y` 启停所选，`R` 全部开始，`−` 全部暂停，`X` 删除。
- **图标**：`tools/make_icon.py` 用 Android EhViewer 自适应图标合成，右下角叠加 45° 的小 Switch 掌机，
  生成 `icon.jpg`（NRO）和 `icon.png`（商店）。
- **独立公共仓库**：SwitchPort 以 GPL-3.0（与 Android EhViewer 一致）发布到 GitHub `Schweik7/EhViewer-Switch`。

## 0.5.1（2026-10-05）

- **版本号**：用户要求不要涨得太快——小改动只加补丁号（0.5.x），大的功能阶段才加次版本号。
- **快速切换菜单可取消旧请求**：每个网络任务有独立的取消标志（`BeginNetworkTask`）。列表/详情/读收藏夹/登录
  属于可取代任务：导航到别的菜单或发起新列表时，旧任务被取消并移入 `m_retired_tasks`（最多 3 个在退出中，
  结果丢弃），新请求立即开始。收藏/评分写操作不会被打断。
- **切换菜单立即换页**：`OpenListSource` 先清空列表、切到列表页并显示加载圈，再发请求，所以 ZL/ZR 连按时
  从“正在加载的那一项”继续步进，不会因为页面还停在旧处而跳错。
- **收藏夹分组**：`ParseFavoriteFolders` 解析 favorites.php 顶部 10 个收藏夹的名称与数量（`fp fps` 为当前项）。
  收藏页按 `X` 弹出“全部 + 10 个收藏夹（数量）”，选择后以 `?favcat=N` 加载。
- **拆分大文件**：App.cpp（约 1970 行）按职责拆为 6 个文件，Ui.cpp 拆为 3 个，逻辑未改；删除未用的首页
  （`Screen::Home`/`DrawHome`）和 `SwitchSource`。宿主测试的翻译检查覆盖新文件。

## 0.5.0（2026-10-04）

- **下载速度**：`HttpRequestOptions::download_counter` 在 curl 进度回调里实时累加字节；
  App 每秒采样得到速度，书库卡片与详情页显示“x MB/s · 已下载 y MB”，8 秒无数据显示“等待服务器响应”。
- **代理改为设置项**：`settings.ini` 的 `proxy_enabled`（默认关）与 `proxy_url`（为空时用 cookies.ini 的地址）。
  设置页可用系统键盘输入地址，含账号密码时界面只显示 `***@host`。
- **左侧菜单 = EhViewer 抽屉**：首页 / 关注(watched) / 热门 / 排行榜 / 收藏 / 我的订阅 / 下载 / 历史 / 设置
  （`SidebarItem`）。点按 → `kTouchNav`；顶层页面 ZL/ZR 依次切换。启动直接进入“下载”书库，原首页删除，
  测试登录与重读 cookies.ini 移入设置页“账户”。
- **我的订阅**：`core/Subscriptions`（subscriptions.json，最多 100 条）。搜索结果按 X 保存，订阅页 X 新建、Y 删除、A 打开。
- **列表平滑滚动 + 自动加载**：`core/ListLayout` 计算列表/网格几何（宿主测试覆盖）。按像素滚动、
  选中项动画跟随、触屏拖动与惯性；接近末尾自动以 `?next=` 追加下一页（失败 5 秒后再试）。
  设置“列表样式”：网格（默认，5 列大缩略图，类似安卓瀑布）/ 列表。
- **评分**：列表解析星级精灵图 `RatingFromStarSprite`，列表/网格显示“★ 4.5”，历史中也保存。
- **详情页重排**：左侧封面、分类、收藏状态；右侧标题、评分/页数/大小/语言、可点的上传者（`−` 键同效，
  搜索 `uploader:名字`）、按命名空间优先级排列的核心标签（点按即搜索），5 个操作按钮及下载进度条。

## 0.4.0（2026-10-04）

用户确认 0.3.3 全部生效，要求继续实现其余功能（暂不做 Switch→PC 迁移）。本版新增：

- **设置页**（侧栏“设置”/首页 `R`）：`core/Settings` 存 `sdmc:/config/EhViewerSwitch/settings.ini`。
  语言（跟随系统/中文/English）、站点、阅读默认方向/缩放/横屏双页/双页方向、预读页数、
  阅读和下载时防休眠（`appletSetMediaPlaybackState`）、缩略图开关、分类筛选（`f_cats`，首页与搜索）、
  历史开关/清空、域前置/内置 hosts/DoH（-1 = 跟随 cookies.ini）、按键调试框开关（默认关）。
  `ChangeSetting` 的行号必须与 `BuildSettingRows` 对应。
- **中英双语**：`core/I18n` 以中文原文为键。`Ui::DrawText` 对整串查表，所以界面上的字面量自动翻译；
  拼接消息用 `T()` 包住片段。宿主测试 `TestTranslations` 扫描源码，任何新增中文字面量没有英文都会失败
  （仅状态栏配色关键词例外）。注意：`static` 数组里不要用 `T()`，否则语言在首次调用时就被固定。
- **浏览历史**：`core/History`（JSON，最多 200 条），打开详情时记录；侧栏“历史”/首页 `L` 打开，复用列表页。
- **标签**：详情解析 `#taglist`，详情 `ZL` 打开标签页，`A` 以 `ns:"tag$"` 精确搜索。
- **书库删除**：`ZR` 确认后删除 `galleries/` 和 `.incoming/` 下该图库；正在下载时先取消。
- **阅读器**：`−` 用数字键盘跳页；设置中的默认值通过 `ReaderView::Configure` 生效。
- **触屏**：`Ui` 绘制时登记 `HitRegion`（坐标 → 虚拟按键 + 可选列表下标），`App::HandleTouch`
  把轻点转换为按键；拖动在列表中逐行移动、评论逐行滚动、阅读器内滚动页面；阅读器轻点左/右三分之一翻页、
  中间显示信息条。侧栏四项与左下“控制提示”卡片（= B）都可点；浮层外点击 = 取消。
- 侧栏改为 发现 / 书库 / 历史 / 设置。

## 0.3.3（2026-10-04）

用户确认 0.3.2 “非常好”。本版新增：

- `HttpRequestOptions::post_body/post_content_type`：POST 支持（自动加 Origin；域前置下手动
  重定向时 POST 转 GET）。
- 收藏：详情 `R` → GET `gallerypopups.php?act=addfav` 读取 10 个收藏夹名称和当前所在夹
  （`ParseFavoriteSlots`，有 `favdel` 选项才表示已收藏）→ 浮层选择 → POST
  `favcat=<n|favdel>&favnote=&submit=Apply+Changes&update=1`（与 Android `EhEngine.addFavorites` 相同）。
- 评分：详情 `L` → 浮层选 0.5–5 星 → POST JSON `rategallery` 到页面里的 `api_url`
  （exhentai 为 `s.exhentai.org/api.php`，可域前置），`apiuid/apikey` 取自详情页脚本。
  **未在用户真实账号上做提交测试**，避免改动其收藏和评分；需实机确认。
- 排行榜：列表来源新增“排行榜”（toplist.php，仅 e-hentai.org，**需要代理**），`X` 切换
  昨日/本月/本年/总榜，L/R 用 `&p=` 翻页（每页 50）。
- 边下边读优先级：阅读器每帧 `Downloader::SetFocus(gid, page)`，下载从当前页往后，再补前面。
- `Ui::SetOverlay(OverlayMenu*)`：通用模态列表浮层。

## 0.3.2（2026-10-04）

用户确认 0.3.1：版本号显示、域前置直连、关闭代理可用均正常。本版处理其反馈：

- 阅读方向修正为左手十字键在下（见 READER.md）。
- 韩文：主字体简体中文缺 Hangul；`Ui` 按字形把文本切分为多段，依次回退到 KO、Standard、
  繁中、NintendoExt 系统字体后拼接渲染；`FitText`/`WrapText` 也按回退字体测量，结果有缓存。
- 单页失败不再阻止阅读：下载器每页重试并跳过失败页，最后报告失败数，未完成的图库留在
  `.incoming`（开始时就写入无页面的 manifest 和封面）；书库同时列出未完成图库，`−` 继续下载。
  详情页 `A` 为“边下边读”：自动加入下载并打开阅读器，未到的页面转圈等待。阅读进度在发布时保留。
- 列表去掉每行“x/25”，标题栏显示“第 N 页 · 本页 25 个”；缩略图诊断只在失败时显示。
- 新增：ZL/ZR 切换 最新/热门/订阅/收藏（搜索后加入循环），L/R 翻页（`?next=` 游标），
  `−` 打开系统键盘搜索；详情页请求 `?hc=1`，`X` 查看全部评论。

尚未实现：评分、收藏操作（需要 POST + api.php 的 apiuid/apikey）、排行榜（toplist.php 只在
e-hentai.org，直连不可用，需要代理）、按标签订阅管理、下载按阅读位置优先。

## 0.3.1：域前置直连（2026-10-04 在大陆网络实测）

- 内置 hosts 方式（真实 IP + 真实 SNI）在 TLS 阶段 0.2 秒内被重置。
- 无 SNI 直连 IP（Android 的 domain fronting）：exhentai.org、s.exhentai.org、ehgt.org 可用；
  列表、缩略图、详情、`/s/` 图片页、`*.hath.network` 图片整链路无代理成功。
- e-hentai.org 在 Cloudflare 后，无 SNI 握手失败，**没有代理无法直连**。
- 实现：`NetworkPlan` 新增 `RouteKind::Fronted`（每个请求最多试 2 个地址，连接超时 6 秒），
  `HttpClient` 把 URL 主机换成 IP、加 `Host` 头、保留证书链校验但关闭主机名校验，
  并手动处理重定向，避免自定义 Host 头被带到其他主机。配置项 `domain_fronting=true|false`（默认开）。
- 列表补充分类/页数/发布时间；详情补充大小/语言/上传者/发布时间/评分/收藏。
- 登录测试在配置了 `igneous` 时改为检测 exhentai.org。
- 配置位置：`sdmc:/config/EhViewerSwitch/cookies.ini` 和 `cacert.pem`。

## 0.3.0 新增（宿主测试 + devkitA64 构建通过，尚未实机验证）

1. **网络线路逐级回退**：`net/NetworkPlan.*` 把配置展开为单一机制的尝试序列
   代理 → 内置 hosts 直连 → DoH → 系统 DNS。`HttpClient::GetWithFallback` 只在
   传输层失败（DNS/连接/TLS/超时）时换下一条线路，HTTP 状态码不会触发回退；
   最后一次成功的线路会被优先使用，`B` 重载配置时清除。状态栏显示当前线路。
2. **列表缩略图**：`ParseGalleryList` 解析 `thumb_url`（Compact/Minimal 的
   `id="it<gid>"` 悬浮层和 Extended/Thumbnail 的链接内 `<img>`，优先 `data-src`），
   同时支持 Thumbnail 布局的 `gl4t glname` 标题。`download/ThumbnailLoader` 在单个
   后台线程下载，UI 线程每帧最多解码 3 张为纹理，缓存上限 96 张（LRU）。
3. **详情解析**：封面 `cover_url`、`Length:` 页数、`ptt` 预览分页数、页面 token
   映射 `page_tokens`（替代旧 `preview_paths`）。`ParseGalleryPage` 解析 `/s/` 图片页的
   图片 URL、`nl()` 换源 key、showkey、原图链接和站点文件名/尺寸。
4. **下载队列**：`download/Downloader` 单工作线程。流程：详情（如未提供）→ 收集全部
   预览分页 token（写入 `.incoming/g_<gid>/spider_info.ehviewer` 便于续传）→ 逐页取
   图片页和图片 → 魔数校验 → 原子写 `p_00000001.<ext>` → 封面 → `manifest.json` →
   整个目录改名发布到 `galleries/g_<gid>`。已存在的页面直接复用，所以取消/断电后重新
   加入队列即可续传。图片失败时用 `?nl=<key>` 换源重试一次；遇到 509 配额图停止。
5. **Cookie 隔离**：`IsSiteHost()` 只允许向 https 的 e-hentai.org / exhentai.org /
   ehgt.org 及其子域发送 Cookie。H@H 图片服务器（第三方 IP）永远不带 Cookie。
6. **本地书库与阅读器**：`core/Library` 只信任 manifest 扫描 `galleries/g_*`。
   `ui/ReaderView` 默认 CCW 竖屏，后台线程解码，UI 线程建纹理，按
   `BuildPrefetchPlan` 预读并受 `MemoryBudget` 约束；退出时把当前页写回 manifest。
7. socket 初始化申请 6 个 BSD 会话（失败回退默认 3 个），因为主请求、缩略图和下载
   三个线程可能同时阻塞在网络调用上。

## 立即下一步（需要实机）

1. 部署 0.3.0，确认 `proxy=<set>` 时状态栏线路显示与回退行为；若代理不可达，
   第二条线路应在约 10 秒后接管，之后的请求直接走成功线路。
2. 验证列表缩略图（exhentai 缩略图需要 Cookie，已对 `s.exhentai.org` 发送）。
3. 下载一本小图库（<20 页），检查 `sdmc:/manga/EhViewerSwitch/galleries/g_<gid>/`
   内容、manifest，和下载中按书库页 `Y` 取消后再次下载能否续传。
4. 阅读器：确认 CCW 方向（左摇杆/十字键在下方）、`Y` 适应宽度/整页、`X` 旋转、
   右摇杆连续滚动、长图上下翻屏、退出后进度保存。
5. 若 6 个 BSD 会话初始化失败，观察回退后三线程并发是否出现阻塞。

## 后续开发

- `api.php showpage` 解析（当前每页都请求 HTML 图片页，稳定但多一次请求）。
- 下载 hash（manifest `sha256` 字段目前为空）、限速、多图库并发策略。
- 搜索、分页、收藏；阅读器触摸翻页。
- Switch→PC 导出页面（见 PC_EXPORT.md）。

## 构建与部署

```powershell
cd D:\0-code\9-android\Ehviewer_CN_SXJ\SwitchPort
.\tests\build_host_tests.ps1
.\build.ps1 -Target rebuild
curl.exe --ftp-create-dirs -T .\EhViewerSwitch.nro ftp://<switch-ip>:5000/switch/EhViewerSwitch/EhViewerSwitch.nro
D:\devkitPro\tools\bin\nxlink.exe -a <switch-ip> -r 10 .\EhViewerSwitch.nro
```

nxlink 需要 Switch 正停留在 Sphaira 的 NetLoader 页面。输出的“sent 约 43%”是压缩后传输比率，不代表传输截断。

devkitPro 的 libcurl 是 7.69.1：不要使用 7.73 之后才有的 `CURLE_PROXY` 等常量。

## 相关参考

- Android DNS/hosts：`app/src/main/java/com/hippo/ehviewer/client/EhHosts.java`
- Android 代理：`app/src/main/java/com/hippo/ehviewer/EhProxySelector.java`
- Android 图片页解析：`app/src/main/java/com/hippo/ehviewer/client/parser/GalleryPageParser.java`
- PC 接收端：`D:\0-code\0-python\ehviewer_managa_manager\android-exporter`
- MusicPlayer Switch SDL 示例：`D:\0-code\5-cpp\MusicPlayer2\SwitchPort`
