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

## 6. 下载后编译

GitHub 的 zip / 克隆**不含**上次编译产物。按下面顺序编，主程序才能起来。

### 环境

- Visual Studio 2022 或 2026。工作负载选 **使用 C++ 的桌面开发**，在右侧安装详细信息里勾选 **MFC**（多字节支持已包含在 MFC 组件中）。
- 平台 **Win32**。日常用 **Debug**。
- `Sources/BUSMASTER/Directory.Build.targets` 会关掉已废弃的 `/Gm`，并给编译追加 `/FS`。这样多个 `cl.exe` 同时写同一个 PDB 时不会 C1041，也不会把编译器直接打退出。

### 顺序

1. **先编内核** `Sources/Kernel/BusmasterKernel.sln`，配置 **Debug | Win32**。压缩包里没有 `Utilities.lib`。生成后应能在 `Sources/Kernel/Bin/Debug/` 看到 `Utilities.lib` 和 `BusmasterDBNetwork.lib`。工程里输出目录和 Lib 路径写法不一致，日志里出现 **MSB8012** 时，只要这两个 lib 在该目录即可继续。
2. **再编主解决方案** `Sources/BUSMASTER/BUSMASTER.sln`，同样 **Debug | Win32**。`CAN_PEAK_USB` 已在这个解决方案里，重新生成会带上 PCAN 驱动 DLL。
3. **跳过 DBManager**。官方 3.0 起闭源，本仓库没有 `DBManager.sln`。详见 §8。
4. 语言包、LDF Editor/Viewer、Format Converter 按需再编，不编也不影响主程序启动。

改界面资源后要编对应工程：Settings / ECU Flash 在 `UDS_Protocol.dll`，Ribbon 图标在 `BUSMASTER.exe`。

### 启动前放到 exe 旁边的文件

运行 `Sources/BUSMASTER/BIN/Debug/BUSMASTER.exe`。若弹出 **Unable to Load Database Manager. Please Reinstall BUSMASTER**：

| 文件 | 从哪来 |
|------|--------|
| `DBManager.dll` | 官方 3.x 安装目录，或本机已经能启动的 BUSMASTER 3.x 的 `BIN\Debug`。仓库不包含这个闭源 DLL。 |
| `libxml2.dll` | 复制 `Sources/BUSMASTER/EXTERNAL/libxml2/bin/libxml2.dll` 到 exe 同一目录。 |

`DBManager.dll` 还依赖 VC2013 运行库 `MSVCR120.dll`、`MSVCP120.dll`。64 位系统上它们一般在 `C:\Windows\SysWOW64`，32 位程序可以直接加载。

换过 `CAN_PEAK_USB.dll` 或 `FrameProcessor.dll` 之后，先退出正在运行的 BUSMASTER 再启动，否则进程里仍是旧 DLL。

### 可以失败、且不影响主程序的驱动工程

- **CAN_IXXAT_VCI**：要装 IXXAT VCI SDK，并有环境变量 `VciSDKDir`，否则找不到 `vcinpl.h`。不用这块卡可以不编它。
- **Kvaser**：需要厂商的 `canlib32.lib`，放到 `Sources/BUSMASTER/BIN/Libs/Debug/`。仓库不含该库。
- **MHS**：`mhsbmcfg.lib` 由解决方案里的 MHS 配置工程生成。链接报找不到时，先单独生成该工程，再编 CAN_MHS。

这些失败只是少了对应硬件 DLL，已经编出来的 `BUSMASTER.exe` 仍可运行。默认驱动是 STUB，实车时在 Driver Selection 里改选 PEAK 或 ZLG。

### 重新生成时

「重新生成解决方案」是当前配置的全量编译，工程之间仍然并行。`/FS` 解决的是 PDB 被几个编译器同时写。各工程是按**路径**去链 `DataTypes.lib`、`Utils.lib`，不是项目引用。基础库还在重写时，其它工程会成片报 **LNK1104**（打不开 `DataTypes.lib` 或 `Utils.lib`）。后面那一串失败都是这个库还没落盘，不是几十个源文件各自写错。

处理：

1. 先单独生成 **DataTypes**、**Utils**（需要的话再加上 CommonClass、Filter、ProjectConfiguration）。
2. 再生成解决方案。库已经在磁盘上时，第二次通常就能过。
3. **LNK1168**（无法写入 exe/dll）：先退出正在运行的 BUSMASTER。
4. 清单不要再指到 `BIN/Release/BUSMASTER.exe.manifest`。源文件在 `Application/res/BUSMASTER.exe.manifest`，见 §12。

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

---

## 11. 已修复：Message Window 可导入 CAN `.log`，并修正误报 Protocol Mismatch

**现象**

- Message Window 右键 **Import Log File** 原先一直灰掉，或导入后提示 `Unable to Load. Protocol Mismatch`。
- 即使能打开，Overwrite 模式下可能只显示 1 条报文。

**原因**

1. 菜单被硬编码禁用；且 CAN 侧 `getLogFileImporter()` 返回空，`S_FALSE(1)` 被进度条映射成 Protocol Mismatch。
2. 缺少 CAN 日志解析实现（`CImportLogFileCAN`）。
3. `LoadPage` 只对最后一帧调用 `onRxMsg`，Overwrite 列表只剩一行。

**修复要点**

- 未连接总线时可启用 Import Log。
- 实现 `ImportLogFileCAN`，解析 BUSMASTER CAN `.log`（`***PROTOCOL CAN***` 等）。
- importer 为空时返回明确的 invalid，不再误报 Protocol Mismatch。
- `LoadPage` 对每一帧写入 Append 缓冲并通知 UI。

**用法**

1. 断开 CAN 连接。
2. Message Window 右键 → **Import Log File** → 选 `.log`。
3. 查看全部历史帧请用 **Append**；Overwrite 下同一 ID 只保留最新一帧。

**涉及文件**

- `Sources/BUSMASTER/PSDI_CAN/ImportLogFileCAN.*`
- `Sources/BUSMASTER/PSDI_CAN/MsgContainer_CAN.*`
- `Sources/BUSMASTER/CommonClass/MsgContainerBase.cpp`
- `Sources/BUSMASTER/Utility/BaseImportLogFile.cpp`
- `Sources/BUSMASTER/Application/MsgFrmtWnd.cpp`

---

## 12. 已修复：干净检出无法编译（清单路径、导入库重名）

**现象**

- 编 `BUSMASTER` 时报 **RC2135**：`BUSMASTER.rc` 找不到 `..\BIN\Release\BUSMASTER.exe.manifest`。GitHub 压缩包里没有 Release 产物。
- 并行链接时报 **LNK1104**，打不开 `CAN_Vector_XL.exp`（或同名 `.lib`）。

**原因**

- 清单被写死在上一次 Release 的输出目录。
- `LIN_ETAS_BOA` 的 ImportLibrary 写成了 `CAN_Vector_XL.lib`，和 CAN Vector 工程抢同一个导出库。`LIN_PEAK_USB` 有同样问题，写成了 `CAN_PEAK_USB.lib`。`CAN_NSI` 有一处写成了 `CAN_Kvaser_CAN.lib`。
- 运行时 `LoadLibrary` 用的是各自 DLL 的输出文件名，所以已经编出来的 PCAN 抓包仍然正常。错误只在重新链接、两个工程同时写同一个 `.lib` / `.exp` 时出现。

**修复**

- 清单源文件改为 `Application/res/BUSMASTER.exe.manifest`，`BUSMASTER.rc` 和 `BUSMASTER.vcxproj` 都指向它。
- 导入库改回本工程名字：`LIN_ETAS_BOA.lib`、`LIN_PEAK_USB.lib`、`CAN_NSI.lib`。
- `CAN_NSI` 不在 `BUSMASTER.sln` 里，避免以后单独编时再撞库名。

---

## 13. 已修复：PCAN Hardware Selection 把未选通道填进 Configured

**现象**

- 打开 Hardware Selection 时，Available 里有通道，Configured 却已经被占满。官方行为是：**还没选的时候 Configured 为空**，用 `>>` 只加入选中的通道。

**原因**

- `anSelList` 被初始化成 `{0}`。对话框把 `0` 当成“已选中的列表下标”，并且只在遇到 `-1` 时停止，于是每个槽位都看成通道 0。

**修复**

- 全部初始化为 `-1`。
- 再次打开对话框时，只把当前已经选过的 PCAN 句柄填回 Configured。

**涉及文件**

- `Sources/BUSMASTER/CAN_PEAK_USB/CAN_PEAK_USB.cpp`
- `Sources/BUSMASTER/BUSMASTER.sln`（把 `CAN_PEAK_USB` 加入解决方案，重新生成时会编这个 DLL）

---

## 14. 已修复：PCAN 通道显示成 Driver Id 4294967295

**现象**

- Available 里两行都是 `PCAN-USB Driver Id 4294967295`，分不出 CH1 和 CH2。Driver ID 显示 `-1`。固件名（例如 PCAN-USB Pro FD）两边相同。

**原因**

- 设备没有设置 Device ID 时，PCAN 返回 `0` 或 `0xFFFFFFFF`。旧代码用无符号数打印，就变成 `4294967295`。

**修复**

- 通道号从句柄计算：`PCAN_USBBUS1`–`USBBUS8`、`USBBUS9`–`USBBUS16`。
- 名称显示为 `PCAN-USB CH1`、`PCAN-USB CH2`。只有读到有效 Device ID（不是 0，也不是 `0xFFFFFFFF`）时才附加 `(Id n)`。
- 列表里的接口号在没有设备 ID 时用通道号。连接仍使用原来的 PCAN 句柄。

改过 `CAN_PEAK_USB.dll` 后要退出并重新打开 BUSMASTER，才能看到新名称。

---

## 15. 已修复：Logging 点 Add 总是复用 BUSMASTERLogFile_0.log

**现象**

- Logging 里点 **Add**，每次都落到同一个 `BUSMASTERLogFile_0.log`。

**修复**

目录不变，仍是原来的日志目录。文件名按当前时间和已加载的配置生成：

| 情况 | 文件名 |
|------|--------|
| 已加载配置，例如 `0C6N02TA.cfx` | `BUSMASTERLogFile_YYYYMMDDHHMMSS_0C6N02TA.log` |
| 没有加载配置，或用的是默认空配置 `DefaultConfig` | `BUSMASTERLogFile_YYYYMMDDHHMMSS.log` |

同一秒再点 Add，或这个名字已经在日志列表里、或磁盘上已有同名文件，则追加 `_1`、`_2`。

不改写日志的 Append / Overwrite，也不自动给日志绑定过滤器。过滤器仍在 Logging 关闭时，于 Configure → Filters 里指定。

**涉及文件**

- `Sources/BUSMASTER/FrameProcessor/ConfigMsgLogDlg.cpp`
- `Sources/BUSMASTER/FrameProcessor/FrameProcessor.vcxproj`（链接 `ProjectConfiguration.lib`，用来读取当前 cfx 路径）

---

## 16. 编译器：关闭 `/Gm`，打开 `/FS`

VS 2026 的编译器对 `/Gm`（Minimal Rebuild）给出 **D9035**。并行「重新生成」时，多个 `cl.exe` 写同一个 PDB，会出现 **C1041**，或者编译器异常退出，接着基础库编失败，其它工程成片 **LNK1104**。

`Sources/BUSMASTER/Directory.Build.targets` 对 BUSMASTER 下的工程关掉 Minimal Rebuild，并追加 `/FS`，让 PDB 可以共享。

这不改变「重新生成 = 当前配置全量编译」。基础库仍按路径链接，所以 DataTypes / Utils 还在生成时，其它工程仍可能暂时 LNK1104。处理办法见 §6。
