# MicroReactor 工具集

本目录包含 MicroReactor 框架的配套开发工具。

## 安装依赖

```bash
pip install -r requirements.txt
```

## 工具列表

### 1. Reactor Studio - 可视化状态机设计器

![Studio Screenshot](docs/studio_screenshot.png)

拖拽式状态机编辑器，可生成 C 代码。

**启动：**
```bash
python studio/reactor_studio.py
```

**功能：**
- 可视化绘制状态和转换
- 拖拽编辑状态位置
- 双击编辑状态属性和规则
- 自动生成 C 头文件和源文件
- 项目保存/加载 (JSON 格式)

**快捷键：**
- `S` - 添加新状态
- `T` - 切换转换绘制模式
- `Delete` - 删除选中项
- `Ctrl+S` - 保存项目
- `Ctrl+E` - 导出 C 代码

---

### 2. Reactor Scope - 实时监控示波器

![Scope Screenshot](docs/scope_screenshot.png)

实时监控 MicroReactor 系统运行状态。

**启动：**
```bash
python scope/reactor_scope.py
```

**功能：**
- **Gantt 视图**：可视化各实体的 dispatch 时间线
- **信号流图**：显示实体间信号传递
- **性能统计**：dispatch 时间、信号速率等
- **信号注入**：手动发送信号测试系统
- **命令终端**：发送 Shell 命令

**使用步骤：**
1. 选择串口和波特率
2. 点击 "Connect" 连接设备
3. 设备需启用 `ur_trace` 功能并通过串口输出

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

要使用 Scope 和 CTL 工具，设备端需要：

### 1. 启用 Trace 输出

```c
// 在 menuconfig 中启用
// Component config → MicroReactor → Performance Tracing → Enable

// 或在代码中初始化
UR_TRACE_INIT();
ur_trace_set_backend(&ur_trace_backend_uart);
```

### 2. 实现 Shell 命令（可选）

```c
// 实现 ur_shell 或使用 ESP-IDF console 组件
// 支持的命令：list, inject, param, trace
```

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
