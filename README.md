<div align="center">
  <img alt="JetKVM logo" src="https://jetkvm.com/logo-blue.png" height="28">
  <p><strong>JetKVM - 开源 KVM over IP 解决方案</strong></p>

  <p>
    <a href="README.md">简体中文</a>
  </p>

  <p>
    <a href="https://github.com/mofeng-git/jetkvm/stargazers"><img src="https://img.shields.io/github/stars/mofeng-git/jetkvm?style=social" alt="GitHub stars"></a>
    <a href="https://github.com/mofeng-git/jetkvm/network/members"><img src="https://img.shields.io/github/forks/mofeng-git/jetkvm?style=social" alt="GitHub forks"></a>
    <a href="https://github.com/mofeng-git/jetkvm/issues"><img src="https://img.shields.io/github/issues/mofeng-git/jetkvm" alt="GitHub issues"></a>
    <a href="LICENSE"><img src="https://img.shields.io/github/license/mofeng-git/jetkvm" alt="License"></a>
  </p>
</div>

## 📖 项目概述

JetKVM 是一套高性能、开源的 KVM over IP（Keyboard/Video/Mouse）软件方案，用于远程管理服务器、工作站和个人电脑。无论目标设备系统是否能正常启动，都可以通过 JetKVM 获取显示画面并控制键盘鼠标，完成 BIOS 配置、系统安装、故障排查等工作，无需在被控端安装任何软件。

本仓库为 One-KVM 社区对优秀开源软件 JetKVM 在通用平台的的适配，旨在拓展 JetKVM 系统在非官方硬件平台上的兼容性与运行能力。

如您对系统稳定性、高可用性或商业级技术支持有较高要求，建议优先考虑购买 [JetKVM 官方硬件套装](https://jetkvm.com/) 以获得完整保障。

> 说明：为尊重原项目且避免冲突，本适配版本不提供对官方已支持硬件（如 RV1106）的支持需求。

## 应用场景

- 家庭实验室与开发设备的远程管理
- 服务器无人值守维护与故障排障
- BIOS/引导层面的安装、配置与恢复

## 📊 功能特性

- 低时延远程视频：H.264 编码，，流畅画面与交互
- WebRTC 远程访问：可选对接 JetKVM Cloud，安全、穿透友好
- 开源&可定制：后端 Go、前端 TypeScript，支持通过 SSH 登录设备调试
- 外设虚拟化：
  - USB HID 键盘/鼠标（USB Gadget）
  - 虚拟大容量存储（用于远程装系统/挂载镜像）
  - 串口控制台（可选）

> 目前未包含音频采集/转发能力。

> 目前为 libx264 软件编码，尚未支持硬件编码。

## 项目状态与限制

此 JetKVM 移植项目有以下限制，请您在使用前悉知：

- 如若未提供内置免费内网穿透服务，相关问题请自行解决
- 不提供24×7小时技术支持服务
- 不承诺系统稳定性和合规性，使用风险需自行承担
- 尽力优化用户体验，但仍需要一定的技术基础

## ⚡ 快速开始

支持运行在使用 USB UVC 采集卡、开启了 OTG 功能的 armv7/arm64 架构设备上。 

### 方式一：Docker 镜像部署（推荐）

推荐使用 --net=host 网络模式以获得更好的 wol 功能和 webrtc 通信支持。

docker host 模式：

```bash
sudo docker run --name jetkvm -itd --privileged=true \
    -v /lib/modules:/lib/modules:ro -v /dev:/dev \
    -v /sys/kernel/config:/sys/kernel/config \
    --net=host \
    silentwind0/jetkvm
```

docker bridge 模式：

```bash
sudo docker run --name jetkvm -itd --privileged=true \
    -v /lib/modules:/lib/modules:ro -v /dev:/dev \
    -v /sys/kernel/config:/sys/kernel/config \
    -p 8080:8080 -p 4430:4430 \
    silentwind0/jetkvm
```

如果网络条件不佳，可使用阿里云镜像仓库加速下载：

将命令中的 `silentwind0/jetkvm` 替换为 `registry.cn-hangzhou.aliyuncs.com/silentwind/jetkvm`

部署完成后访问访问地址：`http://<主机IP>:8080`。

### Docker 环境变量

容器入口脚本会读取以下环境变量并覆盖配置文件：


- `VIDEONUM`：UVC 设备编号， `/dev/video{N}`；默认 `0`。
- `VIDEOFORMAT`：采集格式，`mjpeg|mjpg` → `MJPG`；`yuyv|yuy2` → `YUYV`；默认 `mjpeg`。
- `VIDEOWIDTH`：视频宽度，默认 `1280`。
- `VIDEOHEIGHT`：视频高度，默认 `720`。
- `VIDEOFPS`：帧率，默认 `30`。
- `VIDEOBITRATE`：码率（kbps），默认 `8000`。
- `HTTPPORT`：HTTP 端口，默认 `8080`。

### 报告问题

如果您发现了问题，请：
1. 使用 [GitHub Issues](https://github.com/mofeng-git/jetkvm/issues) 报告
2. 提供详细的错误信息和复现步骤
3. 包含您的硬件配置和系统信息
