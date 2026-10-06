# EhViewer Switch

A homebrew E-Hentai / ExHentai client for Nintendo Switch, written in C++17 with libnx and SDL2. It is a standalone frontend inspired by the Android app [EhViewer](https://github.com/xiaojieonly/Ehviewer_CN_SXJ); the Android UI is not cross-compiled.

**Adult content warning:** the sites this app browses host adult material. Use it only where that is legal and you are of age.

**Features**

- Browse the front page, watched, popular, toplists, favorites (all 10 folders) and search, with saved searches.
- Gallery details: tags, comments, rating, adding to favorites, and searching by uploader or tag.
- Background downloads with resume and a live speed display. You can read while a gallery is still downloading.
- Reader with portrait (console turned sideways), landscape and double-page modes, and saved reading progress.
- Touch support and a Chinese/English UI.
- Works without a proxy where possible (domain fronting for exhentai.org), with optional HTTP/SOCKS proxy, built-in hosts and DoH.

**Setup:** put the NRO at `sdmc:/switch/EhViewerSwitch/EhViewerSwitch.nro`. Put your site cookies in `sdmc:/config/EhViewerSwitch/cookies.ini` (`ipb_member_id`, `ipb_pass_hash`, and `igneous` for ExHentai). Put a CA bundle (`cacert.pem`, for example from curl.se) next to it. Details are below, in Chinese.

License: GPL-3.0, the same license as the Android EhViewer this port derives from.

---

EhViewer 的独立 Nintendo Switch 前端。它不会尝试交叉编译 Android UI；网络、解析、下载、存储和阅读逻辑在本仓库内以 C++17/libnx 实现。

> 当前版本：0.5.3。接手开发前请先阅读 [架构](docs/ARCHITECTURE.md)、[交接状态](docs/HANDOFF.md)、[阅读器](docs/READER.md) 和 [PC 导出](docs/PC_EXPORT.md)。

## 当前可用

- 首页、关注、热门、排行榜、收藏（10 个收藏夹可切换）、搜索与“我的订阅”；网格/列表两种样式，平滑滚动，自动加载下一页。
- 图库详情：核心标签、评论、评分、收藏，点上传者或标签直接搜索。
- 后台下载（可续传、实时速度），边下边读；本地书库与阅读器（默认竖屏，十字键在下），自动保存进度。
- 触屏、中英双语、设置页（阅读默认值、分类筛选、代理、网络开关等）。
- 线路回退：代理（默认关）→ 域前置直连 → 内置 hosts → DoH → 系统 DNS，记住成功线路。
- SD 卡实体目录和文件名只使用 ASCII；真实文件名写入 manifest，供 Switch → PC 导出。

尚未实现：Switch → PC 导出、`api.php` 快速取图、下载校验。
## 构建与测试

在 PowerShell 中运行：

```powershell
cd D:\0-code\9-android\Ehviewer_CN_SXJ\SwitchPort
.\tests\build_host_tests.ps1
.\build.ps1 -Target rebuild
```

产物为 `EhViewerSwitch.nro`。依赖 devkitA64、libnx、SDL2、SDL2_ttf、SDL2_image、curl、mbedtls 和 zlib（devkitPro portlibs）。宿主测试的部分 HTML fixture 来自 Android 仓库，需要把本仓库放在 Ehviewer_CN_SXJ 仓库的 `SwitchPort/` 目录下才能完整运行。

## SD 卡安装

复制 NRO 到：

```text
sdmc:/switch/EhViewerSwitch/EhViewerSwitch.nro
```

创建 Cookie 文件：

```text
sdmc:/config/EhViewerSwitch/cookies.ini
```

文件可写成逐行格式：

```ini
ipb_member_id=你的值
ipb_pass_hash=你的值
igneous=你的值
```

也接受从浏览器复制的单行 Cookie：

```text
Cookie: ipb_member_id=...; ipb_pass_hash=...; igneous=...
```

`igneous` 可省略；省略时访问 e-hentai.org，存在时访问 exhentai.org。应用只显示 `<set>` 脱敏状态，不向日志打印 Cookie 值。

中国大陆网络建议增加：

```ini
# 代理默认关闭，可在应用“设置 → 网络”开启或填写地址；这里的 proxy= 仅作为默认地址
builtin_hosts=true
doh=true
domain_fronting=true
```

`domain_fronting`（默认开启）在代理不可用时以无 SNI 方式直连 exhentai.org 和缩略图服务器 ehgt.org；e-hentai.org 位于 Cloudflare 后，仍然需要代理。Clash 等代理必须开启“允许局域网连接”。本机开发配置放在被 Git 忽略的 `local-config/cookies.ini`，禁止提交真实 Cookie。

TLS 证书包放在：

```text
sdmc:/config/EhViewerSwitch/cacert.pem
```

按键：

- 启动后进入“下载”书库。左侧菜单与 EhViewer 抽屉一致：首页 / 关注 / 热门 / 排行榜 / 收藏 / 我的订阅 / 下载 / 历史 / 设置，可点按；在这些页面按 `ZL`/`ZR` 依次切换。所有按钮、列表项、标签都支持触屏，`+` 退出。
- 设置：上下选择，左右或 `A` 修改；含语言（中文/English）、列表样式、阅读默认值、分类筛选、代理（默认关）、网络开关、测试登录、重读 cookies.ini。
- 图库列表：方向键移动（网格模式左右也可用），平滑滚动，到底自动加载下一页；`A` 打开详情，`Y` 加入下载，`−` 搜索，`X` 刷新（排行榜中切换昨日/本月/本年/总榜，收藏中切换收藏夹，搜索结果中保存为订阅），`L`/`R` 翻页，`B` 回到书库。
- 我的订阅：`A` 打开，`X` 新建，`Y` 删除。
- 图库详情：`A` 边下边读（已下载则直接阅读），`Y` 下载，`X` 评论，`ZL` 全部标签，`ZR` 预览缩略图（选一页 `A` 从该页开始读），`−` 搜索上传者（或点上传者），点标签直接搜索，`L` 评分，`R` 收藏/移动/移除，`B` 返回。
- 下载（书库）：`A` 阅读（未完成的也可读），`Y` 开始/暂停所选任务，`R` 全部开始，`−` 全部暂停，`X` 删除。每项显示下载中/排队中/已暂停，下载中显示实时速度。
- 阅读器（与 Android 一致）：首次打开显示操作引导。触屏轻点左/右三分之一翻页，中间上半打开菜单，中间下半打开进度条，长按图片打开页面菜单（重新载入、重新下载这一页、保存到 SD 卡 `manga/EhViewerSwitch/saved/`、页面信息）。按键：`L`/`R`/十字键翻页，`−` 菜单（屏幕方向、缩放、双页、自动翻页、时钟/电量、跳页、操作引导），按下右摇杆为页面菜单，`Y` 缩放，`X` 旋转，`B` 返回。右下角显示时间、电量和页码。
- 更新：设置 → 关于 → 检查更新。先查更新服务器（`download.psyventures.cn/ehviewer/latest.json`），失败再查 GitHub Release；下载后校验 NRO 头和大小再替换，重新打开应用即生效。默认启动时检查。
- 阅读器：见 [docs/READER.md](docs/READER.md)，`B` 返回并保存进度。

建议从相册/游戏的 title override 环境启动，而不是受限的 applet 模式；后续图片解码和大图阅读需要更多内存。

## 存储约定

图库根目录：

```text
sdmc:/manga/EhViewerSwitch/
  galleries/
    g_123456/
      manifest.json
      spider_info.ehviewer
      cover.jpg
      p_00000001.jpg
      p_00000002.webp
  .incoming/
    g_123456/
```

所有实体目录和文件名都限制为 `[A-Za-z0-9._-]`。中文标题和真实导出文件名只写入 UTF-8 `manifest.json`，例如：

```json
{
  "physicalName": "p_00000001.jpg",
  "originalName": "001 第一页.jpg",
  "role": "page",
  "pageIndex": 0
}
```

下载先进入 `.incoming/g_<gid>`，写完并提交 SD 卡后再发布到 `galleries/g_<gid>`。不以标题作为 Switch 端目录名，避免 FAT/libnx 路径兼容问题。

## 下一阶段

1. Switch → PC Wi-Fi 导出，复用 `ehviewer_managa_manager/android-exporter` 的接收端接口。
2. 下载 SHA-256 校验、`api.php showpage` 快速取图。

导出协议和文件名恢复规则见 [docs/PC_EXPORT.md](docs/PC_EXPORT.md)。
