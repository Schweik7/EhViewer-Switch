# Switch → PC 漫画导出约定

目标接收端是：

```text
D:\0-code\0-python\ehviewer_managa_manager\android-exporter
```

仅支持 Switch 向 PC 导出；Android 数据不会导入 Switch。

## 发现与握手

首版直接让用户输入 PC 的局域网地址或读取配置，随后复用接收端现有 HTTP 接口：

```text
GET  /api/hello
POST /api/check
PUT  /api/file
POST /api/commit
```

发送前读取每个图库的 `manifest.json`。Switch 打开 `physicalName` 对应的 ASCII 文件，但上传请求中的相对路径使用 `originalName`。因此 PC 落盘后恢复为真实标题/中文文件名，不会出现 `g_123`、`p_00000001.jpg` 这一层内部命名。

## 安全约束

- `physicalName` 必须是单个 ASCII 安全组件，禁止 `/`、`\\` 和 `..`。
- `originalName` 必须是相对路径，禁止绝对路径、盘符和 `..` 组件。
- 同一 manifest 中实体名不得重复。
- 导出只遍历 manifest，不递归信任 SD 卡上的未知文件。
- Cookie、图库 token 和 `spider_info.ehviewer` 默认不导出；如 PC 管理器确实需要元数据，使用单独的 metadata 角色并由用户显式开启。

这些校验已经在 `GalleryManifest::Validate()` 中实现。

## 完整性与断点续传

现有 PC 接收端以文件大小判断是否已存在。首版保持兼容；协议 v2 增加：

- manifest 内记录 `size` 和 `sha256`；
- `/api/check` 返回缺失、大小不匹配和 hash 不匹配三类结果；
- `PUT /api/file` 支持 offset 或 Content-Range；
- 每个文件上传到 PC 临时名，校验完成后原子改名；
- `/api/commit` 只在整本漫画全部校验成功后发布目标目录。

Switch 端上传任务状态另存为 ASCII 文件名，例如 `export_state.json`，断电或网络中断后可以继续。

## 冲突策略

PC 已存在同名图库时默认合并缺失文件，不静默覆盖内容不同的文件。若标题改动导致目录名变化，以 `gid` 作为身份键，由 PC 端决定最终人类可读目录名。冲突文件保存为带 gid/hash 后缀的副本并报告给用户。
