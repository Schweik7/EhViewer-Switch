# EhViewer Switch 开发约定

本目录是独立的 Nintendo Switch C++17/libnx 前端，不交叉编译 Android UI。修改前先阅读 `docs/ARCHITECTURE.md`、`docs/HANDOFF.md`、`docs/READER.md` 和 `docs/PC_EXPORT.md`。

- Windows 构建：`./build.ps1 -Target rebuild`。
- 宿主测试：`./tests/build_host_tests.ps1`。
- 交付前两者必须都通过。
- Cookie 只允许放在已忽略的 `local-config/cookies.ini`，禁止提交、打印或写入诊断日志。
- Switch SD 卡实体文件名必须为 ASCII `[A-Za-z0-9._-]`；UTF-8 原始文件名放进 `manifest.json`。
- 网络请求不得在 UI 线程同步执行；`B` 必须可以取消，`+` 必须可以退出。
- 阅读器默认 `PortraitCounterClockwise`：指**机身**逆时针转 90°，左手十字键在下方（用户 2026-10-04 实机确认的需求）；720×1280 逻辑画布因此以顺时针 90°（SDL 角度 +90）输出到屏幕。不要改变这个默认。
- Switch→PC 是唯一迁移方向，Android 数据不导入 Switch。
- 不要提交 `build/`、`build-host/`、NRO/ELF/NACP 或 `local-config/`。
