# lvgl-wifi

基于正点原子 ATK-DLT113IS 开发板，用 LVGL v8 做 UI 开发 WiFi 管理功能。  
**硬件平台**：ATK-DLT113IS（Allwinner T113-i，ARM Cortex-A7 双核）  
**软件环境**：Buildroot 2019 / Linux 内核 5.4.61

---

## 功能特性

| 功能 | 说明 |
|------|------|
| 扫描网络 | 点击 **Scan** 按钮，列出周边所有 WiFi 热点（SSID、加密方式、信号强度） |
| 一键连接 | 点击列表中的 AP 名称，加密网络弹出密码输入框，开放网络直接连接 |
| 获取 IP  | 连接成功后自动通过 udhcpc/dhclient 获取 DHCP 地址，并显示在状态栏 |
| 断开连接 | 点击 **Disconnect** 按钮，释放 IP 并解除关联 |
| 实时状态 | 顶部状态栏每 5 秒刷新一次，显示连接状态和 IP 地址 |

**UI 依赖**：LVGL v8.3，LCD 分辨率默认 800 × 480（可通过 Makefile 变量修改）。  
**WiFi 依赖**：`wpa_supplicant` + `wpa_cli`，DHCP 使用 Busybox `udhcpc`。

---

## 项目结构

```
lvgl-wifi/
├── src/
│   ├── main.c           # 应用入口：LVGL 初始化（fbdev + evdev）及主循环
│   ├── wifi_manager.c   # WiFi 管理层（扫描、连接、断开、状态查询）
│   ├── wifi_manager.h
│   ├── wifi_ui.c        # LVGL 界面：热点列表、密码弹窗、状态栏
│   └── wifi_ui.h
├── lv_conf.h            # LVGL v8 配置（16-bit RGB565，800×480）
├── lvgl/                # LVGL v8 源码（git 子模块）
├── lv_drivers/          # LVGL Linux 驱动（git 子模块）
├── Makefile             # 交叉编译构建脚本
└── README.md
```

---

## 快速开始

### 1. 拉取子模块

```bash
git submodule add https://github.com/lvgl/lvgl.git          lvgl
git submodule add https://github.com/lvgl/lv_drivers.git    lv_drivers
git submodule update --init --recursive
# 切换到 LVGL v8.3 稳定分支
git -C lvgl       checkout release/v8.3
git -C lv_drivers checkout master
```

### 2. 交叉编译

```bash
# 安装工具链（若使用 Buildroot，工具链已内置）
# Ubuntu 示例：
sudo apt-get install gcc-arm-linux-gnueabihf

# 编译
make CROSS_COMPILE=arm-linux-gnueabihf-

# 可选：指定 sysroot（Buildroot 生成的目标文件系统）
make CROSS_COMPILE=arm-linux-gnueabihf- \
     SYSROOT=/path/to/buildroot/output/host/arm-linux-gnueabihf/sysroot
```

编译产物：`build/wifi_app`

### 3. 部署到开发板

```bash
# 通过 SCP 上传（开发板已配置网络）
make install BOARD_IP=192.168.1.100

# 或通过 SD 卡手动拷贝
cp build/wifi_app /mnt/sdcard/
```

### 4. 在开发板上运行

```bash
# 确保 wpa_supplicant 运行（如未启用开机自启）
# wpa_supplicant 会由 wifi_init() 自动启动，也可手动启动：
# wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf

# 运行 WiFi 应用
/usr/bin/wifi_app
```

> **提示**：若显示设备不是 `/dev/fb0` 或触摸设备不是 `/dev/input/event0`，  
> 可在 Makefile 中修改 `EVDEV_DEV` 变量，或设置环境变量 `FB_DEV`。

---

## 可配置变量

| 变量 | 默认值 | 说明 |
|------|--------|------|
| `CROSS_COMPILE` | `arm-linux-gnueabihf-` | 工具链前缀 |
| `SYSROOT` | *(空)* | 交叉编译 sysroot 路径 |
| `DISP_HOR_RES` | `800` | 屏幕水平分辨率（像素） |
| `DISP_VER_RES` | `480` | 屏幕垂直分辨率（像素） |
| `WIFI_IFACE` | `wlan0` | WiFi 接口名称 |
| `EVDEV_DEV` | `/dev/input/event0` | 触摸屏 evdev 设备节点 |

---

## 界面截图（示意）

```
┌──────────────────────────────────────────────────────┐
│  WiFi Settings              [Scan]   [Disconnect]    │
├──────────────────────────────────────────────────────┤
│  ✓ Connected · HomeNetwork · 192.168.1.105           │
├──────────────────────────────────────────────────────┤
│  🔒  HomeNetwork                         ████  -62  │
│  🔒  OfficeWifi                          ███░  -71  │
│      OpenHotspot                         ██░░  -78  │
│  🔒  Neighbor's WiFi                     █░░░  -85  │
└──────────────────────────────────────────────────────┘
```

---

## License

Apache License 2.0 — 详见 [LICENSE](LICENSE)。
