<p align="center">
  <img src="../Drift_icon.png" alt="Drift 图标" width="128" height="128">
</p>

<h1 align="center">Drift</h1>

<p align="center">
  <strong>一款免费的桌面视频剪辑软件，让你的视频呈现专业质感 — 绝不向“凑合”妥协。</strong>
</p>

<p align="center">
  <a href="https://github.com/CutWire-Studios/Drift/releases/latest"><img src="https://img.shields.io/github/v/release/CutWire-Studios/Drift?label=release&style=flat&labelColor=28272f&color=feb504" alt="最新版本" height="24"></a>
  <a href="https://github.com/CutWire-Studios/Drift/releases"><img src="https://img.shields.io/github/downloads/CutWire-Studios/Drift/total?label=GitHub%20downloads&style=flat&labelColor=28272f&color=feb504" alt="GitHub 下载量" height="24"></a>
  <a href="https://flathub.org/apps/org.cutwire.Drift"><img src="https://img.shields.io/flathub/downloads/org.cutwire.Drift?label=Flathub%20installs&style=flat&labelColor=28272f&color=feb504" alt="Flathub 安装量" height="24"></a>
  <a href="https://discord.gg/J5ANFz6Z3y"><img src="https://img.shields.io/badge/dynamic/json?url=https%3A%2F%2Fdiscord.com%2Fapi%2Finvites%2FJ5ANFz6Z3y%3Fwith_counts%3Dtrue&query=%24.approximate_member_count&suffix=%20members&logo=discord&logoColor=F8F8F8&label=Discord&color=feb504&labelColor=28272f&style=flat" alt="加入 Drift Discord" height="24"></a>
  <a href="../LICENSE"><img src="https://img.shields.io/github/license/CutWire-Studios/Drift?style=flat&labelColor=28272f&color=feb504" alt="开源协议: GPL-3.0" height="24"></a>
  <img src="https://img.shields.io/badge/platform-Linux%20%7C%20Windows%20%7C%20macOS%20%7C%20Android-feb504?style=flat&labelColor=28272f" alt="支持平台: Linux | Windows | macOS | Android" height="24">
</p>

<p align="center">
  <a href="https://cutwire.org/drift"><strong>cutwire.org/drift</strong></a>
</p>

Drift 是由 CutWire Studios 打造的桌面端视频剪辑器。导入素材，添加特效、字幕、贴纸与音乐，
轻松导出精细打磨的视频 — **无订阅费用、无水印、无需注册账号**。

它专为创作者的真实需求而设计：短视频（Reels / Shorts）、游戏高光、课程作业、教学教程、产品演示、梗图视频，
以及任何你希望呈现高水准且不愿忍受网页卡顿或按月付费的内容。

所见即所得：预览界面呈现的画面就是导出的最终成品。单一合成管线，一致视觉效果，绝无偏差。

## 下载

<p align="center">
  <a href="https://flathub.org/apps/org.cutwire.Drift">
    <img src="https://flathub.org/api/badge?locale=zh-Hans" alt="在 Flathub 上获取" height="80">
  </a>
  <a href="https://apps.microsoft.com/detail/9PHHBZ07FRSZ">
    <img src="https://get.microsoft.com/images/en-us%20dark.svg" alt="从微软商店获取" width="293">
  </a>
  <a href="../docs/play-testing.md">
    <img src="../docs/btn-gplay-en.png" alt="在 Google Play 获取（内测中）" height="80">
  </a>
</p>

<p align="center">Google Play 处于封闭测试阶段 — <a href="../docs/play-testing.md">查看如何参与</a>。</p>

**Linux** — 通过 Flathub 安装：

```bash
flatpak install flathub org.cutwire.Drift
flatpak run org.cutwire.Drift
```
**macOS** — 通过 Homebrew 安装：

```bash
brew install --cask cutwire-studios/tap/drift
```

> **macOS 首次运行提示：** Drift 采用 ad-hoc 签名（未经 Apple 官方公证）。通过 Homebrew 安装会自动解除隔离属性。若通过 `.dmg` 手动安装或遇到 Gatekeeper 拦截，请在终端执行：
> ```bash
> xattr -dr com.apple.quarantine /Applications/Drift.app
> ```

或者从[最新版本发布页](https://github.com/CutWire-Studios/Drift/releases/latest)直接获取预编译安装包：

| 平台 | 安装包 |
|------|--------|
| Linux | [Flathub](https://flathub.org/apps/org.cutwire.Drift) · [AppImage](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Windows | [Microsoft Store](https://apps.microsoft.com/detail/9PHHBZ07FRSZ) · [安装程序 (.exe)](https://github.com/CutWire-Studios/Drift/releases/latest) · [绿色便携版 (.zip)](https://github.com/CutWire-Studios/Drift/releases/latest) |
| macOS | [Homebrew Tap](https://github.com/CutWire-Studios/homebrew-tap) · [磁盘映像 (.dmg, Apple Silicon)](https://github.com/CutWire-Studios/Drift/releases/latest) |
| Android | [Google Play (内测版)](../docs/play-testing.md) · [APK (arm64-v8a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (armeabi-v7a)](https://github.com/CutWire-Studios/Drift/releases/latest) · [APK (x86_64)](https://github.com/CutWire-Studios/Drift/releases/latest) |

在手机上，可加入 [Google Play 封闭测试](../docs/play-testing.md)，或直接下载最新版本的 `Drift-*-arm64-v8a.apk` 并安装（亦可使用 `adb install Drift-*-arm64-v8a.apk`）。模拟器请使用 `x86_64` 构建版本。

查看[历史版本一览](https://github.com/CutWire-Studios/Drift/releases)以获取过往发布和完整更新日志。

## 软件界面截图

<p align="center">
  <img src="../docs/screenshots/main-window.png" alt="Drift 视频剪辑器界面：左侧媒体库，预览窗口带 3D 摄像机，右侧文字属性面板，下方多轨道时间线" width="900">
</p>

## 功能特性

**AI Agent 可直接操作项目。** 在当前打开的项目中进行导入、剪辑、调色与导出。编辑器还支持无界面（headless）模式运行。

**时间线原生 3D 空间。** 在三维空间中倾斜片段、布置光源，并将标题放置于主体之后。拖入 3D 模型即可与剪辑内容同场渲染。

**Lottie 矢量动画支持。** 动态图形与矢量插画在任意缩放下均保持极致清晰。支持在时间线上直接更换颜色或修改文字。

**高级关键帧系统。** 随时间自由变换位置、缩放、旋转、不透明度及各项滤镜参数。支持手绘调节动画曲线。

**150+ 种转场与 40+ 款滤镜特效。** 任何预设均可在素材上实时预览。一键应用完整效果组合 — Beat Drop、Glitch Cut、Neon Cutout。支持前置帧拖尾与单素材独立调色。

**纯本地硬件计算。** 点击画面主体即可一键抠像。添加景深虚化与独立光效。自动语音识别生成字幕、超分辨率放大（Upscale）及实时人脸美颜 — 全部在你的电脑本地完成。

**通过文本剪辑视频。** 语音实时转为文本。删除多余词句、剔除口癖与无声片段、挑选最佳镜头、标记说话人，并一键依据时间戳生成字幕。

**风格化标题设计。** 内置 33 款风格预设：卡拉OK、Hormozi 风格、霓虹、金属镀铬、全息投影与手写风。一键将统一风格应用至整条字幕轨道。

**多轨道层级联动。** 变换图层可整体控制下方所有轨道的位置、缩放、旋转、倾斜与淡入淡出。支持轨道嵌套与独立合成时间线。

**卡点节拍剪辑。** 时间线自动吸附音乐节拍，片段自动在节拍点精准切分。背景音乐在人声出现时自动避让闪避（Audio Ducking），并在说话结束后平滑恢复。横屏拍摄素材可自动识别人脸重构为竖屏短视频。

**稳健而高效的时间线体验。** 多轨道胶片序列展示、波纹剪辑（Ripple）、磁性吸附、遮罩蒙版、定格帧、时间标记点与内置混音台。切分片段保留独立调色。即便软件意外退出，最后一次自动保存亦完好无损。

**专业级音频调音。** 人声降噪净化、响度电平计量、EQ 均衡与压缩处理、变速变调保持原声音高，支持软件内直接录制画外音。

**镜头快速检索与多机位切换。** 根据画面内容智能搜索镜头。同时监看所有机位视角，随时切中最佳画面。

**画面防抖、高效导出与项目打包。** 修正镜头抖动、AI 画质放大、流畅播放高码率超清素材。支持 MP4、GIF、纯音频或指定片段区间导出。一键打包工程与素材，资产路径永不丢失。

**Android 移动端一致体验。** 手机端同样具备完整时间线、特效系统与导出功能，剪辑完成直接快捷分享。

**素材库、语音生成与轻巧体积。** 直接在媒体库中搜索免版税素材。快速合成画外音与环境音效。字体、贴纸及模型按需即时下载。界面完整支持阿拉伯语、孟加拉语、西班牙语（西班牙与哥伦比亚）、法语（加拿大）、意大利语、日语、葡萄牙语（巴西与葡萄牙）、俄语、僧伽罗语、他加禄语、越南语和简体中文。

## 为什么选择 Drift

市面上大多数所谓“免费”剪辑器，往往在你视频刚见成效时便要求注册、打上巨大水印或诱导付费订阅。Drift 秉持完全相反的理念：**完全属于你、运行于你的本地电脑、GPLv3 开源协议、无任何强制登录壁垒。**

它既能满足 30 秒短视频的高效产出，亦能胜任复杂影视级创作 — 时间线 AI 协作、3D 与 Lottie、自动字幕、特效合成、母带级音频、智能抠像与多机位。

## 协助我们翻译 Drift

[![翻译状态](https://hosted.weblate.org/widget/cutwire-drift/drift-desktop/multi-auto.svg)](https://hosted.weblate.org/engage/cutwire-drift/)

## 开发者专区

构建、打包、软件架构及 Agent 通信协议文档位于 `docs/` 目录：

- [构建、测试、打包与整体架构](../docs/BUILDING.md)
- [GPU 特效实现](../docs/gpu-effects.md)
- [GPU 转场实现](../docs/gpu-transitions.md)
- [Agent 交互接口 / MCP](../docs/MCP.md)

## 帮助与反馈

发现 bug 或有功能建议？欢迎提交
[GitHub Issue](https://github.com/CutWire-Studios/Drift/issues)。

## 参与贡献

[CONTRIBUTING.md](../CONTRIBUTING.md) 是提交 Pull Request 的指导规范：大范围修改前请先提 Issue，每个 PR 保持专注，确保通过测试套件，并以 GPLv3 协议开源。

特效、转场、模板及音频效果属于插件扩展。请提交至 [Drift-Addons](https://github.com/CutWire-Studios/Drift-Addons) 仓库。运行插件的底层引擎改动保留在本仓库中。

下载[官方版本](https://github.com/CutWire-Studios/Drift/releases)并在实际剪辑中使用，本身就是对项目的巨大支持；欢迎反馈使用体验与问题。在 [Discord](https://discord.gg/J5ANFz6Z3y) 频道中，贡献者可申请 `@Contributor` 身份组。

## 贡献者

<a href="https://github.com/CutWire-Studios/Drift/graphs/contributors">
  <img alt="贡献者列表" src="https://contrib.rocks/image?repo=CutWire-Studios/Drift">
</a>

## 开源协议

GPLv3 — 详见 [LICENSE](../LICENSE)。

## Star 增长趋势

<a href="https://www.star-history.com/?repos=CutWire-Studios%2FDrift&type=date&legend=top-left">
 <picture>
   <source media="(prefers-color-scheme: dark)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&theme=dark&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <source media="(prefers-color-scheme: light)" srcset="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
   <img alt="Star 增长趋势图" src="https://api.star-history.com/chart?repos=CutWire-Studios/Drift&type=date&legend=top-left&sealed_token=zHW_d2jon9Wn-HYP2SWQWLC7qDRaY7qwsvHS0Cp0Ywk1Rf1UvyxhWsakIrx2c11OijPJQ9o52W99jdigV7MOz5RuvLsyWQmBiMvMdk99mcfbgb591WtzNXQO8_K2YhgdbiPD9by00lwl69ZgCZnThFKBwhRbK7IQzIeFkIFnb2o0r5GhJh0HAX6Q8yTM" />
 </picture>
</a>
