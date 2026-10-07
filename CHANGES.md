# 本仓库相对原始 BUSMASTER 的修改说明

本工程基于开源 [BUSMASTER](https://github.com/rbei-etas/busmaster) / NeoBusmaster 本地树，在 **UDS 诊断、ECU 刷写、周立功 CAN/CANFD、诊断设置** 上做了功能增强。下文描述相对原始版本**新增了什么**、以及**怎么用**。

默认 CAN 驱动仍是 **STUB（仿真）**，不会自动选真实硬件。需要实车/台架时，请在 **Driver Selection** 里手动选择 PEAK 或 ZLG。

---

## 1. 周立功 ZLG USB CAN / CANFD

**新增：** `Sources/BUSMASTER/CAN_ZLG_USB` 插件，支持官方同款型号（含 **USBCANFD-200U** 等），波特率按 80% 采样点计算，可改 BRP / TSEG。

**用法：**

1. 安装周立功驱动，保证 `zlgcan.dll` 可被程序加载（与官方上位机相同环境即可）。
2. 打开 BUSMASTER → CAN 分类 → **Driver Selection** → 选择 **ZLG USB CAN**。
3. **Channel Configuration** 里选通道与波特率（CANFD 时同时配仲裁域 / 数据域）。
4. Connect 后再做诊断或刷写。

---

## 2. UDS 主窗口（ISO 14229）

**相对原始 Diagnostics 窗口的增强：**

| 能力 | 说明 |
|------|------|
| ISO 14229 服务树 | 按组展示：会话/复位/安全、通信控制 0x28、DID 0x22/0x2E、DTC 0x14、例程 0x31、传输 0x34/0x35 等 |
| 英文服务名 | 树节点使用 ISO 英文描述 |
| Custom | 子功能 `0xFF` 表示仅 SID、不发 SubFunction |
| 0x31 例程 | start / stop / requestResults 分组，RID 挂在对应组下，另有 Custom |
| SubFunction | 只要该服务定义了子功能即可选择（不再误灰掉 0x22/0x23/0x24） |
| 打开速度 | 树插入关重绘；避免重复 `RegisterClient` 和多余硬件查询 |
| 周期发送 | 可按周期循环发当前请求 |
| Vector Seed&Key | 选择 DLL + Variant，收到 seed 后可自动 `sendKey` |
| ECU Flash 入口 | 诊断主窗口上的 ECU Flash 按钮已去掉（原先会断言崩溃）；请用 Ribbon |

**用法：**

1. 先 **Connect** CAN。
2. Ribbon → **Diagnostics** 打开 Main Diagnostic Window。
3. 左侧选服务，中间填 SubFn / Data，点 **Send**。
4. 安全访问：先在窗口里指定 Seed&Key DLL 和目标等级，再发 `27` requestSeed。

---

## 3. 诊断 Settings（与 Diagnostic 地址同步）

**修复：** 点击 Settings 不再 Debug Assert。原先对话框缺少 Functional Addr 控件，初始化 `LimitText` 会空句柄崩溃。

**新增/行为：**

- Settings 里可配 Request / Response / **Functional Addr（默认 7DF）**、ISO-TP、P2、Tester Present 等。
- **Settings → Diagnostic：** Apply/OK 后，再开 Diagnostic，CAN ID 等直接带过去，不必重填。
- **Diagnostic → Settings：** 在 Diagnostic 改过 CAN ID / SA / TA 后，再开 Settings 会自动填 Request/Response（11 位时 Response 一般为请求 ID + 8）。

**默认 11 位：** Request `700`，Response `708`。

**用法：** Ribbon / 菜单打开 **Settings** → 改地址 → **Apply** 或 **OK** → 再开 Diagnostics。

---

## 4. ECU Flash（三栏刷写编排）

**新增独立窗口**，从 CAN Ribbon 上 Diagnostics **旁边的 ECU Flash** 进入（不要从诊断主窗口按钮进）。

布局对齐常见刷写工具：

| 区域 | 内容 |
|------|------|
| 左 | 服务列表（ISO 14229 英文名） |
| 中 | 当前服务参数；`0x34` 显示 File download；`0x27` 显示 Seed&Key DLL |
| 右 | 刷写步骤列表：上移/下移/删除/延时 |

**步骤类型：**

- 普通 UDS 报文
- **File download**：`34` RequestDownload + `36` TransferData + `37` Exit，可选擦除、CRC
- **Delay**
- **SecurityAccess**：先 requestSeed，再用主窗口同一套 Vector DLL 自动 sendKey

步骤可 XML 导入/导出。

**用法概要：**

1. Connect 硬件，打开 Diagnostics（用于底层 ISO-TP 发送），再开 **ECU Flash**。
2. 按刷写流程添加：扩展会话 → 预条件 → 关 DTC → 安全解锁 → 通信控制 → File download → 复位 等。
3. File download：选固件 → 确认地址/长度 → Add to list。
4. 右侧勾选步骤后点 **Start**。先停掉正在跑的旧 BUSMASTER 再换 DLL，否则会锁文件。

### 4.1 固件文件

支持导入并解析：

| 格式 | 扩展名 | 说明 |
|------|--------|------|
| Intel HEX | `.hex` | 含扩展地址 type 02/04；入口 type 03/05 |
| Motorola S-record | `.s19` `.s28` `.s37` `.mot` `.srec` | 数据 S1/S2/S3；入口 S7/S8/S9 |
| 二进制 | `.bin` | 起始地址用面板上的 Start |

加载后界面显示：

- **Start**：最低加载地址  
- **Length**：有效字节总数  
- **数据块列表**：`addr 0x…, length …`（有空洞会拆块，可勾选）  
- **Main**：有入口地址才显示  

下载时按勾选块分别发 `34`（该块地址/长度）+ `36` + `37`。CRC 可选：

- 每块 `$37` + CRC  
- 每块 `31 01 F1 A0` + CRC  
- 总校验指令（默认 `31010202` + 文件 CRC32）

CRC32 默认：多项式 `0x04C11DB7`，Init/XorOut `0xFFFFFFFF`，RefIn/RefOut。

---

## 5. Ribbon

CAN 分类在 Diagnostics 旁增加 **ECU Flash**。图标使用条带中未占用的下载类图标（与锤子诊断图标区分）。若仍看到锤子，需要重新编译并替换 `BUSMASTER.exe`（仅更新 `UDS_Protocol.dll` 不够）。

---

## 6. 编译

- Visual Studio 2022/2026，Win32，**Debug** 或 Release。  
- 解决方案：`Sources/BUSMASTER/BUSMASTER.sln`  
- 常用目标：`UDS_Protocol`、`CAN_ZLG_USB`、`BUSMASTER`  
- 链接报 **LNK1168**（无法写入 exe/dll）：先退出正在运行的 BUSMASTER。  
- 改界面资源后必须编对应工程：Settings/Flash 在 `UDS_Protocol.dll`；Ribbon 图标在 `BUSMASTER.exe`。

---

## 7. 主要涉及文件（便于对照原版）

- `Sources/BUSMASTER/UDS_Protocol/UDSMainWnd.*` — 诊断主窗口、ISO 树、周期发送、Seed&Key  
- `Sources/BUSMASTER/UDS_Protocol/UDSIso14229.h` — 服务/DID/RID 定义  
- `Sources/BUSMASTER/UDS_Protocol/UDSFlashWnd.*`、`uds.rc` — ECU Flash 与 File download  
- `Sources/BUSMASTER/UDS_Protocol/UDSSettingsWnd.*` — Settings 与地址同步  
- `Sources/BUSMASTER/UDS_Protocol/UDS_Protocol.cpp` — 窗口创建（避免错误 Attach 父窗口）  
- `Sources/BUSMASTER/CAN_ZLG_USB/` — 周立功驱动插件  
- `Sources/BUSMASTER/Application/Res/ribbon1.mfcribbon-ms` — ECU Flash Ribbon  

原版未包含上述刷写编排、HEX/S19/MOT 解析块列表、ZLG 插件与 Settings/Diagnostic 双向同步。

---

## 8. 关于 DBManager（官方 3.0.0 起闭源）

官方从 **BUSMASTER 3.0.0** 开始不再公开 `DBManager` 源码，3.x 树里只有发行用的 DLL，没有 `DBManager.sln`。本仓库同样没有该工程，编 **BUSMASTER.sln** 时跳过即可。

最后一份开源工程在 **2.6.4**：`busmaster-2.6.4/Sources/DBManager/DBManager.sln`（LIN LDF cluster 库，给当时的 LDFEditor / LDFViewer 用）。不要把 3.0 安装包里的 `DBManager.dll` 当成可编译源码。

---

## 9. 已修复：监控报文时打开 Diagnostic 崩溃

**现象**

- 使用 PCAN（或其它 CAN 驱动）连接并正在收发/监控报文时，打开 **Main Diagnostic Window**，Debug 版弹出 MFC 断言：`afxwin1.inl` / `mfc140d.dll`（`IsWindow(m_hWnd)`），随后生成 `BUSMASTER.dmp`。

**原因**

1. CAN 读线程里直接调用 `EvaluateMessage()`，对 Diagnostic 控件做 `lGetValue()` / `UpdateData()` 等 UI 操作（跨线程访问 MFC 窗口）。
2. `DIL_UDS_ShowWnd` 在 `Create` 完成前就把 `omMainWnd` 挂出去，读线程看到非空指针时控件 HWND 尚未就绪。

**修复要点**

- 非 UI 线程收到报文时，通过 `PostMessage(WM_UDS_EVALUATE_MSG)` 回到 Diagnostic 窗口线程再处理。
- 仅在 `Create` 成功后设置 `omMainWnd`；关闭窗口时清空队列并置空指针。
- `OnCtlColor`、`SetFont`、`CRadixEdit::OnChange` 增加 `IsWindow` 保护。

**涉及文件**

- `Sources/BUSMASTER/UDS_Protocol/UDS_Protocol.cpp`
- `Sources/BUSMASTER/UDS_Protocol/UDSMainWnd.*`
- `Sources/BUSMASTER/UDS_Protocol/UDSWnd_Defines.h`
- `Sources/BUSMASTER/Utility/RadixEdit.cpp`

---

## 10. 已修复：启动时加载开启 Logging 的配置崩溃

**现象**

- 一打开 BUSMASTER 即崩溃，提示保存 `BUSMASTER.dmp`。Message Window 可能已出现但尚未有报文。
- Dump 异常码：`0xE06D7363`（C++ `AfxThrowMemoryException`）。

**原因**

- 配置（`.cfx`）中 `IsLoggingEnabled=TRUE`，启动时 `nLoadConfigFile` → `vStartStopLogging` → `Der_SetChannelBaudRateDetails`。
- 此时尚未完成硬件选择，`CMainFrame::m_nNumChannels` **未初始化**（常为 `-1` / `0xFFFFFFFF`），`new SCONTROLLER_DETAILS[nNumChannels]` 申请非法大小导致内存异常。

**修复要点**

- 构造函数中将 `m_nNumChannels` / `m_nNumChannelsLIN` 初始化为 `0`。
- `vSetBaudRateInfo` 在通道数 ≤0 时跳过 baud 写入。
- `LogObjectCAN` / `J1939` / `LIN` 的 `Der_SetChannelBaudRateDetails` 对空指针或 `nNumChannels <= 0` 直接返回。

**涉及文件**

- `Sources/BUSMASTER/Application/MainFrm.cpp`
- `Sources/BUSMASTER/FrameProcessor/LogObjectCAN.cpp`
- `Sources/BUSMASTER/FrameProcessor/LogObjectJ1939.cpp`
- `Sources/BUSMASTER/FrameProcessor/LogObjectLIN.cpp`
