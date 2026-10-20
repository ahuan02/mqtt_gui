# MQTT 调试面板 · 节点图版

> 一个把 MQTT 调试做成"连线即授权"的桌面工具。
> 没有表单、没有按钮堆砌 —— 拖一根线，功能就生效；扯断它，功能就撤销。
> 单文件 exe，1.5 MB，静态链入，双击即用。

---

## 一句话原理

在这个面板里，**节点是功能，连线是授权，引脚是类型**。

```
┌──────────┐  会话   ┌──────────┐  消息   ┌──────────┐
│  Broker  │●───────▶│   订阅   │●───────▶│ 消息面板 │
└──────────┘         └──────────┘         └──────────┘
      │  事件                                   ▲
      └─────────────▶┌──────────┐              │
                     │ 事件日志 │              │
                     └──────────┘              │
┌──────────┐  脉冲   ┌──────────┐              │
│   脉冲   │●───────▶│   发布   │──────────────┘（发布的内容会流入消息面板）
└──────────┘         └──────────┘
```

- `Broker.会话` 连到任意节点 → 自动 CONNECT；所有会话线扯断 → 自动 DISCONNECT
- `订阅.会话` 接通 → 立刻 SUBSCRIBE；线断 → UNSUBSCRIBE
- `发布` 必须同时接上 `会话` 和 `触发`，脉冲一次就发一条
- 类型不匹配的引脚接不上，松手没落在引脚上就取消

**没有"连接按钮"。** 想连就把线拖上去，想断就把线扯掉。

---

## 特性

### 画布

| 能力 | 说明 |
| --- | --- |
| 拖拽连线 | 三次贝塞尔曲线，带流动光点指示数据方向；悬停高亮，右键断开 |
| 类型约束 | 会话 / 消息 / 事件 / 脉冲四种类型，异色异类型不可连 |
| 平移缩放 | 空白处拖拽平移，滚轮缩放（0.7 ~ 1.5），锚定光标 |
| 节点操作 | 拖标题移动，右下角把手拖拽改尺寸（180×100 起） |
| 增删节点 | 双击空白或右键菜单添加，右键节点删除 |
| 状态持久化 | 节点位置、尺寸、连线、文本实时落盘 |

### 消息面板

- 1000 条环形缓冲，时间 / 方向 / 主题 / QoS / 长度 / 负载 六列
- 主题与内容双重过滤、方向筛选、跟随最新
- 点行展开全文，一键复制负载
- 面板可拖拽放大，表格随节点尺寸自适应

### 协议与连接

- 精简 MQTT 3.1.1 客户端：CONNECT / CONNACK / PUBLISH(QoS 0~2) / ACK 族 / SUBSCRIBE / UNSUBSCRIBE / PING / DISCONNECT
- 单后台线程 + 非阻塞 socket，UI 线程只做一次加锁批量取数据
- 自动重连、心跳保活、clean session 可配

### 界面

- Catppuccin Mocha 配色，窗体 `#1e1e2e`、卡片 `#181825`、描边 `#313244`
- 中文字体随 exe 分发，**换字体只需往 `fonts/` 丢一个 TTF**，无需重新编译
- 窗口几何、过滤条件、连接参数全部 ini 持久化，关掉再开原样恢复
- 顶部一条 24px 状态条：状态灯 + broker + 收发计数 + 缓冲占用，不放任何按钮

---

## 快速开始

### 构建

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mqtt_gui
```

### 一键打包

```powershell
powershell -ExecutionPolicy Bypass -File .\package.ps1          # 出目录 + zip
powershell -ExecutionPolicy Bypass -File .\package.ps1 -NoZip   # 只出目录
```

产物在 `dist/mqtt_gui/`：

| 文件 | 大小 |
| --- | --- |
| `mqtt_gui.exe` | 1.5 MB |
| `fonts/` | 中文字体 |
| `libwinpthread-1.dll` | 70 KB |

脚本自动探测 CMake、Ninja、MinGW（优先 PATH，其次 CLion 自带工具链），构建完用 `install()` 只收运行必需文件。

---

## 操作手册

| 操作 | 效果 |
| --- | --- |
| 引脚拖到引脚 | 连线（类型必须一致） |
| 右键连线 | 断开 |
| 拖节点标题 | 移动节点 |
| 拖节点右下角 | 改尺寸 |
| 空白处拖拽 / 滚轮 | 平移 / 缩放 |
| 双击空白 | 添加节点菜单 |
| 右键节点 | 删除节点 |
| 点脉冲节点圆点 | 发一次脉冲（触发所有已接线的发布） |

---

## 配置文件

两个文件都生成在 exe 同目录，纯文本，可直接编辑。

**`mqtt_gui.ini`** —— 连接参数与 UI 状态

```ini
host=192.168.1.5
port=1883
client_id=mqtt-gui-3FE3
username=
password=
keep_alive=60
clean_session=1
auto_reconnect=1
pub_topic=test/topic
pub_payload=
pub_qos=0
pub_retain=0
font=
font_size=17
filter=
direction=0
auto_scroll=1
window=228,171,1184,721,0
```

**`mqtt_gui.graph`** —— 节点图拓扑

```ini
view=48.0,40.0,1.000
node=1,0,40,60,302,240      # id,类型,x,y,宽,高
node=2,1,392,60,302,132
sub=2,0,getDataPc/#         # 节点, QoS, 主题
pub=4,0,0,test/topic,       # 节点, QoS, retain, 主题, 负载
link=2,0,3,0,1              # 起节点,起引脚,终节点,终引脚,类型
```

订阅列表由节点图唯一决定，不再单独维护 —— 拖线即订阅，扯线即退订。

---

## 目录结构

```
src/
  main.cpp              窗口、GL 与 ImGui 后端装配
  app/                  业务层：连接状态机、消息入库、配置读写
  core/
    mqtt_client.{h,cpp} 精简 MQTT 3.1.1 客户端（后台线程 + 非阻塞 socket）
    message_store.{h,cpp} 环形缓冲消息仓库
    file_io.{h,cpp}     路径与文件读写
  ui/
    ui.{h,cpp}          六类业务节点、节点图持久化、状态条
    node_graph.{h,cpp}  通用节点画布：连线、平移缩放、命中检测
    palette.h           全局取色板
res/
  imgui/  glfw/         第三方（只编译用到的源文件）
  fonts/                中文字体，可自由替换
  icon/                 图标与 ICO 生成脚本
package.ps1             一键打包
```

---

## 实现要点

**节点编辑器自己画。** 没有引入第三方节点库：曲线是 `ImDrawList::AddBezierCubic`，命中检测用的是"引脚欧氏距离 + 连线折线距离"，平移缩放只是一次 `to_world/to_screen` 换算。整个画布 `node_graph.{h,cpp}` 与业务完全解耦，业务层只认"节点 + 引脚 + 连线"三种数据结构。

**体积是怎么压到 1.5 MB 的：** `-ffunction-sections -fdata-sections` 分段 + `--gc-sections` 回收 + `-s` 去符号 + 静态链入 libgcc/libstdc++；GLFW 只编 Win32 平台源码，ImGui 只编核心 4 个源文件加 2 个后端。

**中文不预生成字形。** 用 ImGui 1.92 的动态字形图集，中文按需光栅化，不枚举 GB2312 范围，启动零预热、内存随用随涨。

**配色只有一个入口。** 所有颜色集中在 `src/ui/palette.h`，UI 层与画布共用，改一个文件就能换整套主题。

---

## 已知边界

- 面向调试，不支持 TLS、MQTT 5、遗嘱消息
- 消息缓冲固定 1000 条，超出丢弃最早的记录
- 单实例运行，配置与节点图不支持多窗口共享

---

## 许可

见 `LICENSE`。
