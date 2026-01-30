# MicroReactor 工具集

本目录包含 MicroReactor 框架的配套开发工具。

## 安装依赖

```bash
pip install -r requirements.txt
```

## 工具列表

### 1. Reactor Studio - 可视化状态机设计器

![Studio Screenshot](docs/studio_screenshot.png)

拖拽式状态机编辑器，可生成 MicroReactor 框架兼容的 C 代码。

**启动：**
```bash
python studio/reactor_studio.py
```

**功能：**

#### 可视化编辑
- 拖拽式状态创建和布局
- 可视化转换线连接状态
- 实时代码预览
- 支持层级状态（父子状态）

#### 状态编辑器（双击状态打开）
- **名称/ID**：设置状态名称和数字 ID
- **父状态**：设置层级关系，支持 HSM（层级状态机）
- **进入动作**：状态进入时执行的函数名
- **退出动作**：状态退出时执行的函数名
- **转换规则表**：
  - 信号名 → 目标状态 → 动作函数
  - 支持添加/删除多条规则
  - 目标状态可选择"保持"（内部转换）

#### 实体管理
- 支持多实体项目
- 左侧面板显示实体列表
- 点击切换不同实体的状态图

#### 代码生成
- 自动生成 C 头文件（.h）和源文件（.c）
- 生成状态枚举、信号处理、状态机框架
- 代码符合 MicroReactor 框架规范

#### 项目管理
- JSON 格式保存/加载项目
- 保存状态位置和所有属性

#### 双语界面
- 支持中文和英文界面
- 菜单栏 "语言 Language" 切换

**快捷键：**
- `S` - 添加新状态
- `T` - 切换转换绘制模式
- `Delete` - 删除选中项
- `Ctrl+S` - 保存项目
- `Ctrl+O` - 打开项目
- `Ctrl+E` - 导出 C 代码
- `ESC` - 取消当前操作

**添加转换的方式：**
1. **两次点击法（推荐）**：按 `T` 进入转换模式 → 点击源状态 → 点击目标状态
2. **右键菜单法**：右键点击源状态 → 选择 "从此状态添加转换" → 点击目标状态
3. 创建后可右键点击转换线编辑信号名称

**右键菜单：**
- **状态**：添加转换、编辑状态、设为初始状态、删除
- **转换线**：编辑信号名、删除

---

### 2. Reactor Scope - 实时监控示波器

![Scope Screenshot](docs/scope_screenshot.png)

实时监控 MicroReactor 系统运行状态，支持设备端元数据同步。

**启动：**
```bash
python scope/reactor_scope.py
```

**功能：**

#### Gantt 时序图
- 可视化各实体的 dispatch 时间线
- **光标功能**：点击并拖动显示时间线光标，显示当前时间点的活动信息
- **悬浮提示**：鼠标悬停在调度块上显示实体名、信号名、耗时
- **滚动浏览**：拖动滚动条或滚动鼠标滚轮浏览历史数据
- **自动暂停**：拖动滚动条时自动暂停，方便分析历史数据
- **时间窗口**：支持 10ms ~ 1s 多种时间窗口
- **设备重启检测**：自动检测设备重启并清除旧数据

#### 信号流图
- 显示实体间信号传递的序列图
- **暂停/继续**：点击 ⏸ 按钮暂停实时更新
- **滚动浏览**：支持垂直滚动查看历史信号
- **信号过滤**：点击"过滤"按钮选择隐藏特定信号（如 SYS_TICK）
- **显示数量**：可调节同时显示的信号条数

#### 性能统计
- 总信号数、信号速率 (sig/s)
- 最大/平均调度时间 (μs)
- 活跃实体数量
- **内存监控**：显示设备剩余堆内存和历史最小堆内存

#### 元数据显示
- **实体名称**：显示设备注册的实体名称（如 "System", "Audio"）
- **信号名称**：显示信号名称（如 "SIG_WAKE_DETECTED"）
- **状态名称**：显示状态名称（如 "Listening", "Speaking"）

#### 其他功能
- **信号注入**：手动发送信号测试系统
- **命令终端**：发送 Shell 命令
- **数据导出**：导出为 JSON 或 CSV 格式
- **无限存储**：事件和信号数据不限制条数

**使用步骤：**
1. 选择串口和波特率
2. 点击 "连接" 连接设备
3. 设备需启用 `ur_trace` 功能并通过串口输出

**快捷键：**
- `Ctrl+L` - 清除所有数据
- `Space` - 暂停/继续
- `ESC` - 清除 Gantt 光标
- 鼠标滚轮 - 调整 Gantt 时间窗口

---

### 3. Reactor CTL - 命令行控制工具

```bash
python rctl.py --help
```

**命令示例：**
```bash
# 列出所有实体
python rctl.py -p COM3 list

# 注入信号
python rctl.py -p COM3 inject 1 0x0100 --payload 42

# 监听信号
python rctl.py -p COM3 listen --filter "0x01*"

# 读取参数
python rctl.py -p COM3 param get 1

# 设置参数
python rctl.py -p COM3 param set 1 80
```

---

### 4. Crash Analyzer - 黑盒解码器

分析 MicroReactor 的黑盒 dump 数据。

**使用：**
```bash
# 基本分析
python crash_analyzer.py dump.bin

# 使用 ELF 文件解析符号
python crash_analyzer.py dump.bin --elf firmware.elf

# 生成 Mermaid 序列图
python crash_analyzer.py dump.bin --mermaid --output report.md
```

**输出示例：**
```
============================================================
MicroReactor Crash Dump Analysis
============================================================

## Summary
Total events: 16
Unique entities: 3
Unique signals: 5

## Potential Issues
  - [entity_dying] Audio reported dying at t=12345ms
  - [signal_storm] Very high signal rate (1500/sec)

## Event Timeline (last 50 events)
------------------------------------------------------------
[   12340ms] Audio           <- SIG_AUDIO_PLAY            from UI              (state=STATE_IDLE)
[   12341ms] Audio           <- SIG_SYS_ENTRY             from Audio           (state=STATE_PLAYING)
...
```

---

## 设备端配置

要使用 Scope 和 CTL 工具，设备端需要进行以下配置：

### 1. 启用 Trace 输出

```c
// 在 menuconfig 中启用
// Component config → MicroReactor → Performance Tracing → Enable

// 或在代码中初始化
UR_TRACE_INIT();
ur_trace_set_backend(&ur_trace_backend_uart);
ur_trace_enable(true);
```

### 2. 注册元数据（推荐）

为了在 Scope 中显示可读的名称，建议注册实体、信号和状态名称：

```c
#if UR_CFG_TRACE_ENABLE
// 注册实体名称
ur_trace_register_entity_name(ENT_ID_SYSTEM, "System");
ur_trace_register_entity_name(ENT_ID_AUDIO, "Audio");
ur_trace_register_entity_name(ENT_ID_SENSOR, "Sensor");

// 注册信号名称
ur_trace_register_signal_name(SIG_SYS_INIT, "SYS_INIT");
ur_trace_register_signal_name(SIG_SYS_TICK, "SYS_TICK");
ur_trace_register_signal_name(SIG_WAKE_DETECTED, "WAKE_DETECTED");
ur_trace_register_signal_name(SIG_AUDIO_PLAY, "AUDIO_PLAY");

// 注册状态名称
ur_trace_register_state_name(ENT_ID_SYSTEM, STATE_SYS_INIT, "Init");
ur_trace_register_state_name(ENT_ID_SYSTEM, STATE_SYS_READY, "Ready");
ur_trace_register_state_name(ENT_ID_SYSTEM, STATE_SYS_LISTENING, "Listening");
ur_trace_register_state_name(ENT_ID_AUDIO, STATE_AUDIO_IDLE, "Idle");
ur_trace_register_state_name(ENT_ID_AUDIO, STATE_AUDIO_PLAYING, "Playing");

// 同步元数据到 Scope（在所有名称注册完成后调用）
ur_trace_sync_metadata();
#endif
```

### 3. 定期发送系统信息（可选）

在主循环中定期发送堆内存信息：

```c
// 在 dispatch 循环中
static TickType_t last_sysinfo_time = 0;
const TickType_t sysinfo_interval = pdMS_TO_TICKS(1000);  // 每秒发送一次

TickType_t now = xTaskGetTickCount();
if ((now - last_sysinfo_time) >= sysinfo_interval) {
    last_sysinfo_time = now;
#if UR_CFG_TRACE_ENABLE
    ur_trace_send_sysinfo();
#endif
}
```

### 4. 实现 Shell 命令（可选）

```c
// 实现 ur_shell 或使用 ESP-IDF console 组件
// 支持的命令：list, inject, param, trace
```

---

## 通信协议

Scope 使用基于文本的协议与设备通信，消息格式为 `\x02<TYPE>:<DATA>\x03\n`：

| 消息类型 | 格式 | 说明 |
|---------|------|------|
| `UR:` | `UR:type,entity_id,data1,data2,timestamp` | Trace 事件 |
| `UN:` | `UN:entity_id,name` | 实体名称 |
| `UG:` | `UG:signal_id,name` | 信号名称 |
| `US:` | `US:entity_id,state_id,name` | 状态名称 |
| `UM:` | `UM:free_heap,min_heap` | 系统内存信息 |

**Trace 事件类型 (type)：**
- `0` - DISPATCH_START
- `1` - DISPATCH_END
- `2` - STATE_CHANGE
- `3` - SIGNAL_EMIT
- `4` - SIGNAL_RECV

---

## 开发说明

### 添加自定义信号名称

在 `rctl.py` 或 `crash_analyzer.py` 中添加信号定义：

```python
SIGNAL_NAMES = {
    0x0100: "SIG_BATTERY_LEVEL",
    0x0101: "SIG_BATTERY_LOW",
    # ...
}
```

### 自定义 Scope 视图

继承 `QWidget` 创建新的可视化组件，添加到 TabWidget 中。

---

## 许可证

GNU General Public License v3.0
