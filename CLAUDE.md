# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

WebP 图像解码插件，供 ACDSee Pro 5 使用。构建产物是 `ID_WebP.apl`（本质 Windows DLL，扩展名改为 `.apl`），实现 ACDSee 在 `plugin/ID_Plugin.h` 里定义的 Image Decoder Plug-in API。

## 构建

CMake + Visual Studio 生成器（MSVC），C++20，全局 `/source-charset:utf-8`。

```
cmake -B build
cmake --build build --config Release
```

产物位于 `build/bin/`：
- `ID_WebP.apl` — 插件，放入 ACDSee 的 Plugins 目录由宿主加载
- `WebP_Test.exe` — 独立测试可执行文件

`.gitignore` 仅忽略 `build/`，不要 commit 构建产物。

## 运行测试

没有单元测试框架。`test/` 是端到端 CLI，直接链接 `WebPCodec`：

```
build/bin/WebP_Test.exe <input.webp>
```

它打印尺寸 / 帧数 / 每帧 delay 并把解码帧写到 `D:\<i>.bmp`。运行前确保 `D:\` 可写（路径硬编码）。

`.vscode/launch.json` 配了 `cppvsdbg` 启动 CMake launch target（即 `WebP_Test`），参数是一个固定的本地 WebP 路径，切机器时需改。

调试插件本身：把 `ID_WebP.apl` 拷到 ACDSee Plugins 目录，VS 附加到 `ACDSee Pro 5` 进程。

## 架构

三层：`codec`（解码核心，纯 C++）→ `plugin`（ACDSee 适配层，导出 C API）→ `test`（绕开 ACDSee 直接验证 codec）。修改时按依赖方向定位。

### codec/ — 静态库 `WebPCodec`

封装了仓库内 vendored 的 `codec/libwebp/`（libwebp 构建时禁用了所有示例/工具/测试/mux，见 `codec/CMakeLists.txt`）。对外只暴露 `WebPDecoder`：

- `decode()` 识别静态 vs 动画后分派；静态走 `WebPDecodeBGR/BGRA`，动画走 `WebPAnimDecoder`。
- **动画解码器必须用 `MODE_BGRA`** — libwebp 的 `ApplyDecoderOptions` 显式拒绝非 alpha 模式。动画每帧的 `duration` 用单独的 `WebPDemux` 预扫一次拿齐（与像素解码分离）。
- **输出统一为 24-bit BGR 底朝上 DIB**，不保留 alpha。这是 ACDSee 的硬约束（只吃 24-bit BGR DIB），所以 alpha 在解码阶段就被合成到棋盘格背景（8 像素格，灰度 224/192）——这不是展示策略，是数据格式约束。
- **`getFrame(i)` 对动画有顺序语义**：内部用 `WebPAnimDecoderGetNext` 单向迭代，不支持随机访问，也不做缓存。`PageDecode` 按 0..n-1 顺序调用才安全。

### plugin/ — 共享库 `ID_WebP.apl`

CMake 通过 `SUFFIX ".apl"` 直接把 DLL 后缀改成 `.apl`，这是 ACDSee 插件的约定。

入口组织（三个文件协同，改任一个都要对齐其他两个）：
- `ID_WebP.def` — DLL 导出表，列出 ACDSee 要求的 C 函数 `IDP_Init` / `IDP_OpenImage` / `IDP_GetPlugInInfo` / `IDP_ShowPlugInDialog` / `IDP_CloseImage` / `IDP_GetImageInfo` / `IDP_GetPageInfo` / `IDP_PageDecode`
- `ID_WebP.cpp` — 以上函数的极薄 shim，全部转发到全局 `g_pAPIWrapper`
- `dllmain.cpp` — `DLL_PROCESS_ATTACH` 保存 `HMODULE` 到 `ID_APIWrapper::g_hModule`（资源加载用）；`DLL_PROCESS_DETACH` 释放 wrapper
- `ID_APIWrapper` — 持有 `ID_PlugInInfo` / `ID_FormatInfo`，管理 `WebPDecoder` 生命周期，把帧封装成 `HGLOBAL` DIB 返回

ACDSee 的调用序列：`IDP_Init` → `IDP_OpenImage`（拿到 `ID_StateHdl`，实际是 `WebPDecoder*`）→ `IDP_GetImageInfo` / `IDP_GetPageInfo(i)` → `IDP_PageDecode(i)` → `IDP_CloseImage`。单线程串行。

`OpenImage` 里必须先调 `psi->pfFillBuffer(dwLen)` 把整包数据拉进缓冲，否则打开 ZIP 内的 WebP 会读不全。

### 两个非显然的 Windows 副作用

`ID_APIWrapper` 构造函数做了两件注册表操作（不是可选的清理项，删掉会退化功能或引入已知 bug）：

1. **`HKCU\Software\ACD Systems\ACDSee Pro\50\ViewerSharpen = 0`** — 绕过 ACDSee "Sharpen subsampled images" 在大动画 WebP 上会卡死的 bug。次次 ACDSee 启动才生效。
2. **为 `.webp` 注册 Explorer 图标** — 优先改现有 ProgID 的 `DefaultIcon`（HKCU 覆写，不动 HKLM），若无 ProgID 则建 `WebP.Image` 最小关联。图标引用的是插件自身资源 `IDI_ICON_WEBP`，随插件走。

### 版本号同步

`plugin/res/resource.h` 里的 `VER_STR` 同时显示在"关于"对话框、Windows 版本资源块、和 `VERSIONINFO` 的 `FileVersion` / `ProductVersion` 字段。发布前要与 `plugin/CMakeLists.txt` 的 `project(... VERSION ...)` 对齐。`resources.rc` 里的 `FILEVERSION` / `PRODUCTVERSION` 宏字段是独立的数字常量，同样需要同步。

## 不要动

- `plugin/ID_Plugin.h` 是 ACDSee SDK 头，非本项目所有。除非确定目标 ACDSee 版本变了，否则不要改结构体/枚举。
- `codec/libwebp/` 是 vendored 第三方，本地魔改会在下次同步上游时丢失；需要扩展功能请写在 `codec/src/` 里。
