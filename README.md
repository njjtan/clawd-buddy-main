# Clawd Buddy 中文说明书

基于 ESP32-C3 的 Claude Code 桌面伴侣，带有动画表情和手机控制功能。

---

## 硬件需求

| 部件 | 规格 | 参考价格 |
|------|------|----------|
| ESP32-C3 Super Mini | WiFi 微控制器 | ~10元 |
| ST7789 1.54" TFT | 240×240 SPI 彩屏 | ~10元 |
| 8 根杜邦线 | 8-10cm | ~0.5元 |
| 2× M2×6mm 螺丝 | 固定屏幕 | ~0.1元 |
| 双面胶 | 固定元件 | ~0.1元 |
| USB-C 数据线 | 供电 | — |
| 3D 打印外壳 | PLA 或 PETG | ~0.5元 |

**总计约20-30元**

---

## 接线图

> ⚠️ VCC 只能接 3.3V，绝不能接 5V

| 屏幕引脚 | ESP32-C3 GPIO | 建议线色 |
|----------|---------------|----------|
| VCC | 3V3 | 红色 |
| GND | GND | 黑色 |
| SDA | GPIO 10 (MOSI) | 橙色 |
| SCL | GPIO 8 (SCK) | 绿色 |
| RES | GPIO 2 | 紫色 |
| DC | GPIO 1 | 蓝色 |
| CS | GPIO 4 | 白色 |
| BL | GPIO 3 | 黄色 |

---

## 软件配置

### 第一步：安装 Arduino IDE

下载 [Arduino IDE 2.x](https://www.arduino.cc/en/software)

### 第二步：添加 ESP32 开发板

1. 打开 Arduino IDE → **文件 → 首选项**
2. 在"附加开发板管理器网址"中粘贴：
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
3. 进入 **工具 → 开发板 → 开发板管理器**，搜索 `esp32`，安装 **"esp32 by Espressif Systems"**

### 第三步：安装库

进入 **工具 → 库管理器**，安装：
- `Adafruit GFX Library`
- `Adafruit ST7735 and ST7789 Library`

### 第四步：配置开发板参数

进入 **工具** 菜单：

| 设置项 | 值 |
|--------|-----|
| 开发板 | ESP32C3 Dev Module |
| USB CDC On Boot | **Enabled** ← 重要 |
| CPU 频率 | 160 MHz |
| 上传速度 | 921600 |

### 第五步：上传程序

1. 克隆或下载本仓库
2. 用 Arduino IDE 打开 `clawd-buddy-main.ino`
3. USB-C 连接 ESP32
4. 在 **工具 → 端口** 选择正确端口
5. 点击 **上传** (→ 箭头按钮)
6. 看到 "Hard resetting via RTS pin..." 表示成功

---

## 使用方法

### 连接与控制

1. 用 USB-C 给 ESP32 供电（充电宝、电脑 USB 均可）
2. 等待约 3 秒启动动画完成
3. 打开手机或电脑的 **WiFi 设置**
4. 连接热点：**`Clawd-Buddy`** · 密码：**`clawd1234`**
5. 打开浏览器访问 **`http://192.168.4.1`**

---

## 三大模式

### 模式一：Claude 模式

与 Claude Code 集成，显示实时状态：

| 状态 | 表情效果 |
|------|----------|
| thinking | 正常眼睛 + 眉毛，左右看 |
| coding | 眯眼专注 `> <` |
| error | 哭脸 T_T |
| done | 开心 `:)` |
| sleeping | 闭眼 + Zzz |
| idle | 正常眼睛 |

**Token 计数器**：右下角显示当前 token 消耗量（k 为单位）

### 模式二：待机/自动表情

- **随机眨眼**：3-8 秒间隔自动眨眼
- **打瞌睡**：30 秒无操作，眼睛慢慢闭合
- **睡眠**：完全闭眼 + Zzz 动画
- **名言提醒**：每 15 分钟显示一句 Claude 风格的提示语
- **深夜困脸**：23:00-6:00 自动进入困倦状态

### 模式三：实用功能

- **像素时钟**：显示当前时间（HH:MM:SS）
- **番茄钟**：可设置 5/15/25/45 分钟，时间到红眼闪烁提醒
- **秒表**：精确到 10ms 的计时器

---

## Web 控制器功能

| 控件 | 功能 |
|------|------|
| Normal eyes | 播放眨眼 + 摇摆动画 |
| Squish eyes | 播放眯眼动画 |
| Claude Code | 显示代码界面 |
| Speed slider | 控制动画速度（慢/正常/快） |
| Background color | 更改背景颜色 |
| Display on/off | 开关背光 |

---

## WiFi 配置

设备支持连接家庭 WiFi 以获取 NTP 时间同步：

1. 浏览器访问 `http://192.168.4.1/wifi`
2. 点击 **Scan WiFi** 扫描周围网络
3. 选择网络并输入密码
4. 点击 **Save & Connect**

连接成功后可获取准确时间用于时钟显示。

---

## Claude Code 集成

通过 Web API 接口与 Claude Code 钩子配合使用：

```
# 状态更新
GET /status?state=thinking&tokens=12345

# 可用状态：idle, thinking, coding, error, done, sleeping
```

`hooks/` 目录包含钩子安装脚本，可用于自动同步 Claude Code 状态。

---

## 3D 外壳

打印设置：

| 参数 | 值 |
|------|-----|
| 材料 | PLA 或 PETG |
| 层高 | 0.15-0.20 mm |
| 填充率 | 15% gyroid |
| 支撑 | 是 — 显示屏窗口悬垂部分 |
| 方向 | 面朝下，平背面放在热床上 |

推荐配色：橙色 PLA 做外壳，哑光黑色做背板。

模型文件位于 `models/` 目录，也可从 MakerWorld 下载。

---

## 组装步骤

1. 打印外壳（外壳 + 背板），先试装屏幕再粘胶
2. 焊接前先将 8 根线穿过背板槽
3. 用双面胶将 ESP32 固定在背板内侧
4. 用 2 颗 M2×6mm 螺丝固定显示屏
5. 将 USB-C 线穿过背板槽，扣上背板

---

## 自定义参数

修改 `clawd-buddy-main.ino` 顶部常量：

```cpp
// 眼睛尺寸和位置
#define EYE_W   30    // 眼睛宽度（像素）
#define EYE_H   60    // 眼睛高度（像素）
#define EYE_GAP 120   // 眼睛间距
#define EYE_OX  0     // 水平偏移
#define EYE_OY  40    // 垂直偏移

// 番茄钟时长（毫秒）
#define POMODORO_BREAK   300000   // 休息 5 分钟
```

---

## 许可证

代码：MIT License

3D 模型和媒体资源：CC BY-NC-SA 4.0

---

> ⚠️ 本项目为独立粉丝项目，与 Anthropic 无关联。"Claude" 和 "Clawd" 是 Anthropic 的商标。
