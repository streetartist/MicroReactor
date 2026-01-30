#!/usr/bin/env python3
"""
MicroReactor Scope - Real-time Monitoring & Debugging Tool

A visual oscilloscope-like tool for monitoring MicroReactor systems in real-time.

Features:
- Real-time signal flow visualization
- Entity state monitoring
- Dispatch timing analysis (Gantt chart)
- Signal injection for testing
- Black box history view
- Performance statistics
"""

import sys
import time
import struct
import json
from dataclasses import dataclass
from typing import List, Dict, Optional, Deque
from collections import deque
from datetime import datetime
import threading

from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QSplitter, QTabWidget, QTreeWidget, QTreeWidgetItem,
    QTableWidget, QTableWidgetItem, QHeaderView,
    QToolBar, QStatusBar, QLabel, QComboBox, QPushButton,
    QLineEdit, QSpinBox, QGroupBox, QFormLayout,
    QTextEdit, QDockWidget, QMenu, QDialog, QDialogButtonBox,
    QMessageBox, QProgressBar, QFrame, QScrollArea, QScrollBar
)
from PySide6.QtCore import Qt, Signal, QTimer, QThread, QMutex, QPoint
from PySide6.QtGui import (
    QAction, QPainter, QPen, QBrush, QColor, QFont,
    QKeySequence, QPainterPath, QPolygon
)

try:
    import serial
    import serial.tools.list_ports
    HAS_SERIAL = True
except ImportError:
    HAS_SERIAL = False
    print("Warning: pyserial not installed. Serial features disabled.")

try:
    import pyqtgraph as pg
    HAS_PYQTGRAPH = True
    pg.setConfigOptions(antialias=True)
except ImportError:
    HAS_PYQTGRAPH = False
    print("Warning: pyqtgraph not installed. Plotting features disabled.")


# =============================================================================
# i18n - Internationalization
# =============================================================================

_lang = "zh"  # Default language

_TR = {
    # Menu
    "file": {"zh": "文件", "en": "File"},
    "view": {"zh": "视图", "en": "View"},
    "export": {"zh": "导出数据...", "en": "Export Data..."},
    "exit": {"zh": "退出", "en": "Exit"},
    "clear": {"zh": "清除", "en": "Clear"},
    "pause": {"zh": "暂停", "en": "Pause"},

    # Toolbar
    "port": {"zh": "端口:", "en": "Port:"},
    "baud": {"zh": "波特率:", "en": "Baud:"},
    "connect": {"zh": "连接", "en": "Connect"},
    "disconnect": {"zh": "断开", "en": "Disconnect"},

    # Status
    "connected": {"zh": "已连接", "en": "Connected"},
    "disconnected": {"zh": "未连接", "en": "Disconnected"},

    # Left panel
    "entities": {"zh": "实体列表:", "en": "Entities:"},
    "col_entity": {"zh": "实体", "en": "Entity"},
    "col_state": {"zh": "状态", "en": "State"},
    "col_inbox": {"zh": "收件箱", "en": "Inbox"},

    # Signal injection
    "inject_group": {"zh": "信号注入", "en": "Signal Injection"},
    "inject_target": {"zh": "目标 ID:", "en": "Target ID:"},
    "inject_signal": {"zh": "信号:", "en": "Signal:"},
    "inject_payload": {"zh": "负载:", "en": "Payload:"},
    "inject_btn": {"zh": "发送信号", "en": "Inject Signal"},

    # Shell
    "shell_group": {"zh": "Shell 命令", "en": "Shell Command"},
    "shell_hint": {"zh": "输入命令...", "en": "Enter command..."},

    # Tabs
    "tab_gantt": {"zh": "时序图 (Gantt)", "en": "Timing (Gantt)"},
    "tab_flow": {"zh": "信号流图", "en": "Signal Flow"},
    "tab_plot": {"zh": "调度时间图", "en": "Dispatch Plot"},
    "tab_log": {"zh": "信号日志", "en": "Signal Log"},

    # Statistics
    "stats_group": {"zh": "性能统计", "en": "Performance Statistics"},
    "stats_total": {"zh": "总信号数", "en": "Total Signals"},
    "stats_rate": {"zh": "信号/秒", "en": "Signals/sec"},
    "stats_max": {"zh": "最大调度 (μs)", "en": "Max Dispatch (μs)"},
    "stats_avg": {"zh": "平均调度 (μs)", "en": "Avg Dispatch (μs)"},
    "stats_active": {"zh": "活跃实体", "en": "Active Entities"},
    "stats_memory": {"zh": "内存使用", "en": "Memory Used"},
    "entity_load": {"zh": "实体负载", "en": "Entity Load"},

    # Messages
    "no_signals": {"zh": "暂无信号", "en": "No signals yet"},
    "msg_inject": {"zh": "[注入]", "en": "[INJECT]"},
    "msg_cmd": {"zh": "[命令]", "en": "[CMD]"},
    "msg_error": {"zh": "[错误]", "en": "[ERROR]"},
    "msg_invalid": {"zh": "输入无效: {e}", "en": "Invalid input: {e}"},
    "msg_exported": {"zh": "已导出到 {path}", "en": "Exported to {path}"},
    "msg_export_fail": {"zh": "导出失败: {e}", "en": "Export failed: {e}"},
    "msg_no_serial": {"zh": "pyserial 未安装", "en": "pyserial not installed"},
    "msg_no_port": {"zh": "未选择端口", "en": "No port selected"},
    "warning": {"zh": "警告", "en": "Warning"},
    "error": {"zh": "错误", "en": "Error"},
    "info": {"zh": "信息", "en": "Information"},
}

def tr(key: str, **kw) -> str:
    """Get translated string."""
    s = _TR.get(key, {}).get(_lang, key)
    return s.format(**kw) if kw else s

def set_lang(lang: str):
    """Set language: 'zh' or 'en'."""
    global _lang
    if lang in ("zh", "en"):
        _lang = lang


# =============================================================================
# Data Models
# =============================================================================

@dataclass
class TraceEvent:
    """Single trace event"""
    timestamp_us: int
    entity_id: int
    event_type: int
    signal_id: int = 0
    src_id: int = 0
    from_state: int = 0
    to_state: int = 0

    # Event types
    DISPATCH_START = 0
    DISPATCH_END = 1
    STATE_CHANGE = 2
    SIGNAL_EMIT = 3
    SIGNAL_RECV = 4


@dataclass
class EntityStatus:
    """Entity runtime status"""
    id: int
    name: str
    state: int
    state_name: str
    inbox_count: int
    signal_count: int
    last_signal: str
    last_dispatch_us: int


@dataclass
class SignalRecord:
    """Recorded signal"""
    timestamp: float
    src_id: int
    src_name: str
    dst_id: int
    dst_name: str
    signal_id: int
    signal_name: str
    payload: bytes


# =============================================================================
# Serial Communication
# =============================================================================

class SerialWorker(QThread):
    """Background thread for serial communication"""

    data_received = Signal(bytes)
    connection_changed = Signal(bool)
    error_occurred = Signal(str)
    entity_name_received = Signal(int, str)   # entity_id, name
    signal_name_received = Signal(int, str)   # signal_id, name
    sysinfo_received = Signal(int, int)       # free_heap, min_heap
    state_name_received = Signal(int, int, str)  # entity_id, state_id, name

    SYNC_BYTE = 0x55

    def __init__(self):
        super().__init__()
        self.port: Optional[serial.Serial] = None
        self.running = False
        self.mutex = QMutex()

    def connect(self, port: str, baudrate: int = 115200) -> bool:
        if not HAS_SERIAL:
            self.error_occurred.emit("pyserial not installed")
            return False

        try:
            self.port = serial.Serial(port, baudrate, timeout=0.1)
            self.running = True
            self.connection_changed.emit(True)
            self.start()
            return True
        except Exception as e:
            self.error_occurred.emit(str(e))
            return False

    def disconnect(self):
        self.running = False
        if self.port:
            self.port.close()
            self.port = None
        self.connection_changed.emit(False)

    def run(self):
        buffer = bytearray()
        STX = 0x02  # Start of text
        ETX = 0x03  # End of text

        while self.running and self.port:
            try:
                if self.port.in_waiting:
                    data = self.port.read(self.port.in_waiting)
                    buffer.extend(data)

                    # Parse text-based trace messages: \x02UR:type,ent,d1,d2,ts\x03
                    while True:
                        try:
                            start = buffer.index(STX)
                            end = buffer.index(ETX, start)
                        except ValueError:
                            # Keep only data after last STX if found
                            try:
                                idx = buffer.index(STX)
                                buffer = buffer[idx:]
                            except ValueError:
                                if len(buffer) > 1024:
                                    buffer.clear()
                            break

                        # Extract message between STX and ETX
                        msg = buffer[start+1:end]
                        buffer = buffer[end+1:]

                        # Parse messages: UR:, UN:, UG:, UM:, US:
                        try:
                            text = msg.decode('ascii', errors='ignore')
                            if text.startswith('UR:'):
                                # Trace event: UR:type,entity_id,data1,data2,timestamp
                                parts = text[3:].split(',')
                                if len(parts) >= 5:
                                    evt_type = int(parts[0])
                                    entity_id = int(parts[1])
                                    data1 = int(parts[2])
                                    data2 = int(parts[3])
                                    timestamp = int(parts[4])
                                    # Pack into bytes for compatibility
                                    frame = struct.pack('<BHHIII',
                                        evt_type, entity_id, 0,
                                        data1, data2, timestamp)
                                    self.data_received.emit(frame)
                            elif text.startswith('UN:'):
                                # Entity name: UN:id,name
                                parts = text[3:].split(',', 1)
                                if len(parts) >= 2:
                                    entity_id = int(parts[0])
                                    name = parts[1].strip()
                                    self.entity_name_received.emit(entity_id, name)
                            elif text.startswith('UG:'):
                                # Signal name: UG:id,name
                                parts = text[3:].split(',', 1)
                                if len(parts) >= 2:
                                    signal_id = int(parts[0])
                                    name = parts[1].strip()
                                    self.signal_name_received.emit(signal_id, name)
                            elif text.startswith('US:'):
                                # State name: US:entity_id,state_id,name
                                parts = text[3:].split(',', 2)
                                if len(parts) >= 3:
                                    entity_id = int(parts[0])
                                    state_id = int(parts[1])
                                    name = parts[2].strip()
                                    self.state_name_received.emit(entity_id, state_id, name)
                            elif text.startswith('UM:'):
                                # System info: UM:free_heap,min_heap
                                parts = text[3:].split(',')
                                if len(parts) >= 2:
                                    free_heap = int(parts[0])
                                    min_heap = int(parts[1])
                                    self.sysinfo_received.emit(free_heap, min_heap)
                        except (ValueError, IndexError):
                            pass

                else:
                    time.sleep(0.01)

            except Exception as e:
                self.error_occurred.emit(str(e))
                break

    def send(self, data: bytes):
        if self.port and self.port.is_open:
            self.mutex.lock()
            try:
                self.port.write(data)
            finally:
                self.mutex.unlock()

    def send_command(self, cmd: str):
        self.send((cmd + '\n').encode())


# =============================================================================
# Custom Widgets
# =============================================================================

class GanttWidget(QWidget):
    """Gantt chart widget for dispatch timing visualization"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumHeight(200)
        self.setMouseTracking(True)

        self.events: List[TraceEvent] = []
        self.entity_colors: Dict[int, QColor] = {}
        self.entity_names: Dict[int, str] = {}
        self.signal_names: Dict[int, str] = {}
        self.time_window_us = 100000  # 100ms
        self._paused = False
        self._scroll_offset = 0
        self._hover_block = None
        self._dispatch_blocks: List[tuple] = []
        self._updating_scrollbar = False  # Flag to prevent recursion

        self.palette = [
            QColor(70, 130, 180), QColor(60, 179, 113), QColor(255, 165, 0),
            QColor(186, 85, 211), QColor(220, 20, 60), QColor(0, 206, 209),
        ]

        # Layout
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(2)

        # Toolbar
        toolbar = QWidget()
        toolbar.setFixedHeight(28)
        tb_layout = QHBoxLayout(toolbar)
        tb_layout.setContentsMargins(5, 2, 5, 2)

        tb_layout.addWidget(QLabel("窗口:"))
        self.window_combo = QComboBox()
        self.window_combo.addItems(["10ms", "50ms", "100ms", "200ms", "500ms", "1s"])
        self.window_combo.setCurrentIndex(2)
        self.window_combo.currentIndexChanged.connect(self._on_window_change)
        tb_layout.addWidget(self.window_combo)

        tb_layout.addSpacing(10)

        self.pause_btn = QPushButton("⏸")
        self.pause_btn.setFixedWidth(30)
        self.pause_btn.setCheckable(True)
        self.pause_btn.setToolTip("暂停/继续")
        self.pause_btn.toggled.connect(self._on_pause)
        tb_layout.addWidget(self.pause_btn)

        tb_layout.addStretch()
        self.info_label = QLabel("")
        tb_layout.addWidget(self.info_label)

        layout.addWidget(toolbar)

        # Canvas
        self._canvas = _GanttCanvas(self)
        layout.addWidget(self._canvas, 1)

        # Scrollbar
        self.scrollbar = QScrollBar(Qt.Horizontal)
        self.scrollbar.setMinimum(0)
        self.scrollbar.setMaximum(100)
        self.scrollbar.setPageStep(100)
        self.scrollbar.setSingleStep(10)
        self.scrollbar.valueChanged.connect(self._on_scroll)
        layout.addWidget(self.scrollbar)

        # Refresh timer
        self._dirty = False
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._on_timer)
        self._timer.start(33)

    def _on_window_change(self, idx):
        vals = [10000, 50000, 100000, 200000, 500000, 1000000]
        self.time_window_us = vals[idx]
        self._canvas.update()

    def _on_pause(self, checked):
        self._paused = checked
        self.pause_btn.setText("▶" if checked else "⏸")
        if not checked:
            self._scroll_offset = 0
            self._update_scrollbar()
            self._canvas.update()

    def _on_scroll(self, val):
        if self._updating_scrollbar:
            return  # Ignore programmatic updates
        # Auto-pause when user scrolls
        if not self._paused:
            self._paused = True
            self.pause_btn.setChecked(True)
            self.pause_btn.setText("▶")
        max_val = self.scrollbar.maximum()
        self._scroll_offset = (max_val - val) * 1000
        self._canvas.update()

    def _on_timer(self):
        if self._dirty and not self._paused:
            self._dirty = False
            self._canvas.update()
            self._update_scrollbar()

    def _update_scrollbar(self):
        self._updating_scrollbar = True
        if self.events and len(self.events) > 1:
            span_ms = (self.events[-1].timestamp_us - self.events[0].timestamp_us) // 1000
            window_ms = self.time_window_us // 1000
            max_val = max(window_ms, span_ms)
            self.scrollbar.setMaximum(max_val)
            self.scrollbar.setPageStep(window_ms)
            if not self._paused:
                self.scrollbar.setValue(max_val)
        else:
            # No data yet
            window_ms = self.time_window_us // 1000
            self.scrollbar.setMaximum(window_ms)
            self.scrollbar.setPageStep(window_ms)
            self.scrollbar.setValue(window_ms)
        self._updating_scrollbar = False

    def register_entity_name(self, eid: int, name: str):
        self.entity_names[eid] = name

    def register_signal_name(self, sig_id: int, name: str):
        self.signal_names[sig_id] = name

    def add_event(self, event: TraceEvent):
        if self._paused:  # Don't accept new events when paused
            return

        # Detect timestamp reset (device restarted)
        if self.events and event.timestamp_us < self.events[-1].timestamp_us - 1000000:
            # New timestamp is more than 1 second before last - device likely restarted
            self.events.clear()
            self.entity_colors.clear()
            self._dispatch_blocks.clear()
            self._scroll_offset = 0

        self.events.append(event)
        if event.entity_id not in self.entity_colors:
            self.entity_colors[event.entity_id] = self.palette[len(self.entity_colors) % len(self.palette)]
        self._dirty = True

    def clear(self):
        self.events.clear()
        self.entity_colors.clear()
        # Keep entity_names and signal_names (metadata)
        self._dispatch_blocks.clear()
        self._scroll_offset = 0
        self._updating_scrollbar = True
        window_ms = self.time_window_us // 1000
        self.scrollbar.setMaximum(window_ms)
        self.scrollbar.setPageStep(window_ms)
        self.scrollbar.setValue(window_ms)
        self._updating_scrollbar = False
        self._canvas.update()


class _GanttCanvas(QWidget):
    """Internal canvas for GanttWidget"""

    def __init__(self, parent: GanttWidget):
        super().__init__(parent)
        self.g = parent
        self.setMouseTracking(True)
        self.setFocusPolicy(Qt.StrongFocus)  # Enable key events
        self._cursor_x = None  # Cursor x position (None = no cursor)
        self._cursor_time = None  # Time at cursor
        self._dragging = False

    def wheelEvent(self, e):
        delta = e.angleDelta().y()
        idx = self.g.window_combo.currentIndex()
        if delta > 0 and idx > 0:
            self.g.window_combo.setCurrentIndex(idx - 1)
        elif delta < 0 and idx < 5:
            self.g.window_combo.setCurrentIndex(idx + 1)

    def mousePressEvent(self, e):
        if e.button() == Qt.LeftButton:
            self._dragging = True
            self._update_cursor(e.position().toPoint())
        elif e.button() == Qt.RightButton:
            # Clear cursor
            self._cursor_x = None
            self._cursor_time = None
            self.update()

    def mouseReleaseEvent(self, e):
        self._dragging = False

    def keyPressEvent(self, e):
        if e.key() == Qt.Key_Escape:
            self._cursor_x = None
            self._cursor_time = None
            self.update()

    def mouseMoveEvent(self, e):
        pos = e.position().toPoint()
        if self._dragging:
            self._update_cursor(pos)
        else:
            # Hover on blocks
            self.g._hover_block = None
            for b in self.g._dispatch_blocks:
                if b[0] <= pos.x() <= b[2] and b[1] <= pos.y() <= b[3]:
                    self.g._hover_block = b
                    break
            # Clear cursor when not dragging
            if not self._cursor_x:
                pass  # Keep cursor if set
        self.update()

    def _update_cursor(self, pos):
        g = self.g
        if not g.events:
            return
        left = 70
        dw = self.width() - left - 5
        if pos.x() >= left and dw > 0:
            self._cursor_x = pos.x()
            max_t = g.events[-1].timestamp_us - g._scroll_offset
            min_t = max_t - g.time_window_us
            ratio = (pos.x() - left) / dw
            self._cursor_time = int(min_t + ratio * g.time_window_us)
        self.update()

    def paintEvent(self, e):
        g = self.g
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        w, h = self.width(), self.height()

        painter.fillRect(0, 0, w, h, QColor(28, 28, 32))

        if not g.events:
            painter.setPen(QColor(80, 80, 80))
            painter.drawText(w // 2 - 30, h // 2, "暂无数据")
            return

        max_t = g.events[-1].timestamp_us - g._scroll_offset
        min_t = max_t - g.time_window_us

        left, bottom = 70, 18
        dw, dh = w - left - 5, h - bottom

        # Grid
        painter.setPen(QPen(QColor(45, 45, 50), 1))
        for i in range(11):
            x = left + dw * i // 10
            painter.drawLine(x, 0, x, dh)

        # Time labels
        painter.setPen(QColor(90, 90, 90))
        for i in range(0, 11, 2):
            x = left + dw * i // 10
            t = min_t + g.time_window_us * i // 10
            lbl = f"{t/1e6:.1f}s" if g.time_window_us >= 1000000 else f"{t/1000:.0f}ms"
            painter.drawText(x - 12, h - 2, lbl)

        # Lanes
        ents = sorted(g.entity_colors.keys())
        if not ents:
            return

        lane_h = min(40, max(24, (dh - 5) // len(ents)))
        g._dispatch_blocks.clear()

        for i, eid in enumerate(ents):
            y = i * lane_h + 3
            if y + lane_h > dh:
                break

            painter.fillRect(left, y, dw, lane_h - 2, QColor(38, 38, 42) if i % 2 else QColor(34, 34, 38))

            name = g.entity_names.get(eid, f"E{eid}")
            painter.setPen(QColor(160, 160, 160))
            painter.drawText(3, y + lane_h // 2 + 5, name[:8])

            color = g.entity_colors[eid]
            st, sig = None, 0

            for ev in g.events:
                if ev.entity_id != eid:
                    continue
                if ev.event_type == TraceEvent.DISPATCH_START:
                    st, sig = ev.timestamp_us, ev.signal_id
                elif ev.event_type == TraceEvent.DISPATCH_END and st is not None:
                    if ev.timestamp_us >= min_t and st <= max_t:
                        x1 = left + int((max(st, min_t) - min_t) * dw / g.time_window_us)
                        x2 = left + int((min(ev.timestamp_us, max_t) - min_t) * dw / g.time_window_us)
                        bw = max(3, x2 - x1)
                        by, bh = y + 3, lane_h - 6

                        painter.fillRect(x1, by, bw, bh, color)
                        painter.setPen(QPen(color.darker(140), 1))
                        painter.drawRect(x1, by, bw, bh)

                        dur = ev.timestamp_us - st
                        g._dispatch_blocks.append((x1, by, x1 + bw, by + bh, eid, dur, sig, st, ev.timestamp_us))

                        if bw > 32:
                            painter.setPen(Qt.white)
                            painter.drawText(x1 + 2, by + bh - 2, f"{dur}μs")
                    st = None

        # Draw cursor line and info panel
        if self._cursor_x is not None and self._cursor_time is not None:
            # Draw cursor line
            painter.setPen(QPen(QColor(255, 100, 100), 2))
            painter.drawLine(self._cursor_x, 0, self._cursor_x, dh)

            # Find events at cursor time
            cursor_info = []
            for block in g._dispatch_blocks:
                x1, by, x2, y2, eid, dur, sig, t_start, t_end = block
                if t_start <= self._cursor_time <= t_end:
                    name = g.entity_names.get(eid, f"E{eid}")
                    sig_name = g.signal_names.get(sig, f"0x{sig:04X}")
                    cursor_info.append(f"{name}: {sig_name} ({dur}μs)")

            # Draw info panel
            if g.time_window_us >= 1000000:
                time_str = f"{self._cursor_time/1e6:.3f}s"
            else:
                time_str = f"{self._cursor_time/1000:.2f}ms"

            panel_lines = [f"时间: {time_str}"] + cursor_info
            if not cursor_info:
                panel_lines.append("(无活动)")

            panel_w = max(len(l) for l in panel_lines) * 8 + 12
            panel_h = len(panel_lines) * 16 + 8
            px = min(self._cursor_x + 10, w - panel_w - 5)
            py = 5

            painter.fillRect(px, py, panel_w, panel_h, QColor(40, 40, 50, 240))
            painter.setPen(QPen(QColor(255, 100, 100), 1))
            painter.drawRect(px, py, panel_w, panel_h)
            painter.setPen(QColor(220, 220, 220))
            for i, line in enumerate(panel_lines):
                painter.drawText(px + 6, py + 14 + i * 16, line)

        # Tooltip (for hover)
        elif g._hover_block:
            x1, y1, x2, y2, eid, dur, sig, _, _ = g._hover_block
            name = g.entity_names.get(eid, f"E{eid}")
            sig_name = g.signal_names.get(sig, f"0x{sig:04X}")
            txt = f"{name} | {sig_name} | {dur}μs"
            tw = len(txt) * 7 + 8
            tx, ty = min(x2 + 5, w - tw - 3), max(y1 - 18, 3)
            painter.fillRect(tx, ty, tw, 16, QColor(55, 55, 65, 230))
            painter.setPen(QColor(210, 210, 210))
            painter.drawText(tx + 4, ty + 12, txt)

        # Info
        g.info_label.setText(f"事件:{len(g.events)} 实体:{len(ents)}")


class SignalFlowWidget(QWidget):
    """Visual signal flow diagram with pause and scroll"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumHeight(300)

        self.signals: List[SignalRecord] = []
        self.entity_positions: Dict[int, int] = {}
        self.entity_names: Dict[int, str] = {}
        self.signal_names: Dict[int, str] = {}
        self.hidden_signals: set = set()  # Signal IDs to hide
        self._dirty = False
        self._paused = False
        self._scroll_offset = 0
        self._visible_count = 20
        self._updating_scrollbar = False

        # Layout
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(2)

        # Toolbar
        toolbar = QWidget()
        toolbar.setFixedHeight(28)
        tb_layout = QHBoxLayout(toolbar)
        tb_layout.setContentsMargins(5, 2, 5, 2)

        tb_layout.addWidget(QLabel("显示:"))
        self.count_combo = QComboBox()
        self.count_combo.addItems(["10", "20", "30", "50"])
        self.count_combo.setCurrentIndex(1)
        self.count_combo.currentIndexChanged.connect(self._on_count_change)
        tb_layout.addWidget(self.count_combo)

        tb_layout.addSpacing(10)

        # Filter button
        self.filter_btn = QPushButton("过滤")
        self.filter_btn.setFixedWidth(50)
        self.filter_btn.setToolTip("选择要隐藏的信号")
        self.filter_btn.clicked.connect(self._show_filter_menu)
        tb_layout.addWidget(self.filter_btn)

        tb_layout.addSpacing(10)

        self.pause_btn = QPushButton("⏸")
        self.pause_btn.setFixedWidth(30)
        self.pause_btn.setCheckable(True)
        self.pause_btn.setToolTip("暂停/继续")
        self.pause_btn.toggled.connect(self._on_pause)
        tb_layout.addWidget(self.pause_btn)

        tb_layout.addStretch()
        self.info_label = QLabel("")
        tb_layout.addWidget(self.info_label)

        layout.addWidget(toolbar)

        # Canvas
        self._canvas = _SignalFlowCanvas(self)
        layout.addWidget(self._canvas, 1)

        # Scrollbar
        self.scrollbar = QScrollBar(Qt.Vertical)
        self.scrollbar.setMinimum(0)
        self.scrollbar.setMaximum(100)
        self.scrollbar.setPageStep(20)
        self.scrollbar.valueChanged.connect(self._on_scroll)

        # Main content with scrollbar
        content = QWidget()
        content_layout = QHBoxLayout(content)
        content_layout.setContentsMargins(0, 0, 0, 0)
        content_layout.setSpacing(0)
        content_layout.addWidget(self._canvas, 1)
        content_layout.addWidget(self.scrollbar)

        # Replace canvas with content
        layout.removeWidget(self._canvas)
        layout.addWidget(content, 1)

        # Throttled refresh
        self._refresh_timer = QTimer(self)
        self._refresh_timer.timeout.connect(self._do_refresh)
        self._refresh_timer.start(50)

    def _on_count_change(self, idx):
        vals = [10, 20, 30, 50]
        self._visible_count = vals[idx]
        self._update_scrollbar()
        self._canvas.update()

    def _on_pause(self, checked):
        self._paused = checked
        self.pause_btn.setText("▶" if checked else "⏸")
        if not checked:
            self._scroll_offset = 0
            self._update_scrollbar()
            self._canvas.update()

    def _on_scroll(self, val):
        if self._updating_scrollbar:
            return
        if not self._paused:
            self._paused = True
            self.pause_btn.setChecked(True)
            self.pause_btn.setText("▶")
        max_val = self.scrollbar.maximum()
        self._scroll_offset = max_val - val
        self._canvas.update()

    def _update_scrollbar(self):
        self._updating_scrollbar = True
        filtered = [s for s in self.signals if s.signal_id not in self.hidden_signals]
        total = len(filtered)
        max_val = max(0, total - self._visible_count)
        self.scrollbar.setMaximum(max_val)
        self.scrollbar.setPageStep(self._visible_count)
        if not self._paused:
            self.scrollbar.setValue(max_val)
        self._updating_scrollbar = False

    def _do_refresh(self):
        if self._dirty and not self._paused:
            self._dirty = False
            self._update_scrollbar()
            self._canvas.update()
        filtered_count = len([s for s in self.signals if s.signal_id not in self.hidden_signals])
        hidden_count = len(self.signals) - filtered_count
        if hidden_count > 0:
            self.info_label.setText(f"信号:{filtered_count} (隐藏:{hidden_count})")
        else:
            self.info_label.setText(f"信号:{len(self.signals)}")

    def _show_filter_menu(self):
        """Show filter menu to select signals to hide"""
        menu = QMenu(self)
        menu.setStyleSheet("QMenu { background-color: #3a3a40; color: white; }"
                          "QMenu::item:selected { background-color: #5a5a60; }")

        # Collect all known signal IDs from records
        known_signals = {}
        for sig in self.signals:
            if sig.signal_id not in known_signals:
                known_signals[sig.signal_id] = sig.signal_name

        # Add signal names from registry
        for sig_id, name in self.signal_names.items():
            if sig_id not in known_signals:
                known_signals[sig_id] = name

        if not known_signals:
            action = menu.addAction("(暂无信号)")
            action.setEnabled(False)
        else:
            # Sort by signal name
            for sig_id, sig_name in sorted(known_signals.items(), key=lambda x: x[1]):
                action = menu.addAction(sig_name)
                action.setCheckable(True)
                action.setChecked(sig_id not in self.hidden_signals)
                action.setData(sig_id)
                action.triggered.connect(lambda checked, sid=sig_id: self._toggle_signal(sid, checked))

        menu.addSeparator()
        show_all = menu.addAction("显示全部")
        show_all.triggered.connect(self._show_all_signals)
        hide_all = menu.addAction("隐藏全部")
        hide_all.triggered.connect(lambda: self._hide_all_signals(known_signals.keys()))

        menu.exec(self.filter_btn.mapToGlobal(QPoint(0, self.filter_btn.height())))

    def _toggle_signal(self, signal_id: int, show: bool):
        """Toggle signal visibility"""
        if show:
            self.hidden_signals.discard(signal_id)
        else:
            self.hidden_signals.add(signal_id)
        self._canvas.update()

    def _show_all_signals(self):
        """Show all signals"""
        self.hidden_signals.clear()
        self._canvas.update()

    def _hide_all_signals(self, signal_ids):
        """Hide all signals"""
        self.hidden_signals = set(signal_ids)
        self._canvas.update()

    def register_entity_name(self, eid: int, name: str):
        self.entity_names[eid] = name

    def register_signal_name(self, sig_id: int, name: str):
        self.signal_names[sig_id] = name

    def add_signal(self, record: SignalRecord):
        if self._paused:
            return

        self.signals.append(record)

        # Track entity positions
        if record.src_id not in self.entity_positions:
            self.entity_positions[record.src_id] = len(self.entity_positions)
        if record.dst_id not in self.entity_positions:
            self.entity_positions[record.dst_id] = len(self.entity_positions)

        self._dirty = True

    def clear(self):
        self.signals.clear()
        self.entity_positions.clear()
        self.hidden_signals.clear()
        self._scroll_offset = 0
        self._update_scrollbar()
        self._canvas.update()


class _SignalFlowCanvas(QWidget):
    """Internal canvas for SignalFlowWidget"""

    def __init__(self, parent: 'SignalFlowWidget'):
        super().__init__(parent)
        self.g = parent

    def paintEvent(self, event):
        g = self.g
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        width = self.width()
        height = self.height()

        # Background
        painter.fillRect(0, 0, width, height, QColor(35, 35, 40))

        if not g.entity_positions:
            painter.setPen(QColor(100, 100, 100))
            painter.drawText(width // 2 - 30, height // 2, "暂无信号")
            return

        # Calculate visible signals (excluding hidden ones)
        filtered_signals = [s for s in g.signals if s.signal_id not in g.hidden_signals]
        total = len(filtered_signals)
        start_idx = max(0, total - g._visible_count - g._scroll_offset)
        end_idx = min(total, start_idx + g._visible_count)
        visible_signals = filtered_signals[start_idx:end_idx]

        if not visible_signals:
            painter.setPen(QColor(100, 100, 100))
            painter.drawText(width // 2 - 30, height // 2, "暂无信号")
            return

        # Draw entity columns
        num_entities = len(g.entity_positions)
        col_width = width // (num_entities + 1)

        painter.setPen(QPen(QColor(80, 80, 90), 1))
        font = QFont("Consolas", 9, QFont.Bold)
        painter.setFont(font)

        for entity_id, idx in g.entity_positions.items():
            x = (idx + 1) * col_width
            painter.drawLine(x, 40, x, height - 5)
            name = g.entity_names.get(entity_id, f"E{entity_id}")
            painter.setPen(QColor(180, 180, 180))
            painter.drawText(x - len(name) * 3, 25, name)
            painter.setPen(QPen(QColor(80, 80, 90), 1))

        # Draw signal arrows
        row_height = max(18, (height - 50) // len(visible_signals))

        for i, sig in enumerate(visible_signals):
            y = 50 + i * row_height

            src_x = (g.entity_positions.get(sig.src_id, 0) + 1) * col_width
            dst_x = (g.entity_positions.get(sig.dst_id, 0) + 1) * col_width

            # Arrow line
            color = QColor(70, 150, 200)
            painter.setPen(QPen(color, 2))
            painter.drawLine(src_x, y, dst_x, y)

            # Arrow head
            direction = 1 if dst_x > src_x else -1
            if src_x != dst_x:
                painter.drawLine(dst_x, y, dst_x - direction * 8, y - 4)
                painter.drawLine(dst_x, y, dst_x - direction * 8, y + 4)
            else:
                # Self signal - draw loop
                painter.drawArc(dst_x - 15, y - 10, 30, 20, 0, 180 * 16)

            # Label
            mid_x = (src_x + dst_x) // 2
            painter.setPen(QColor(220, 220, 220))
            label = sig.signal_name[:20]
            painter.drawText(mid_x - len(label) * 3, y - 4, label)


class StatsWidget(QWidget):
    """Statistics display widget"""

    def __init__(self, parent=None):
        super().__init__(parent)

        layout = QVBoxLayout(self)

        # Create stat labels
        self.stats_labels: Dict[str, QLabel] = {}

        stats_group = QGroupBox(tr("stats_group"))
        stats_layout = QFormLayout(stats_group)

        # Map internal keys to display names
        self._stat_keys = ["total", "rate", "max", "avg", "active", "memory"]
        self._stat_names = {
            "total": tr("stats_total"),
            "rate": tr("stats_rate"),
            "max": tr("stats_max"),
            "avg": tr("stats_avg"),
            "active": tr("stats_active"),
            "memory": tr("stats_memory"),
        }

        for key in self._stat_keys:
            label = QLabel("--")
            label.setStyleSheet("font-weight: bold; font-size: 14px;")
            self.stats_labels[key] = label
            stats_layout.addRow(self._stat_names[key] + ":", label)

        layout.addWidget(stats_group)

        # Progress bars for per-entity load
        self.entity_bars: Dict[int, QProgressBar] = {}
        self.entity_group = QGroupBox(tr("entity_load"))
        self.entity_layout = QVBoxLayout(self.entity_group)
        layout.addWidget(self.entity_group)

        layout.addStretch()

    def update_stat(self, name: str, value: str):
        if name in self.stats_labels:
            self.stats_labels[name].setText(value)

    def update_entity_load(self, entity_id: int, name: str, load_percent: int):
        if entity_id not in self.entity_bars:
            bar = QProgressBar()
            bar.setMaximum(100)
            bar.setFormat(f"{name}: %p%")
            self.entity_bars[entity_id] = bar
            self.entity_layout.addWidget(bar)

        self.entity_bars[entity_id].setValue(load_percent)


# =============================================================================
# Main Window
# =============================================================================

class ReactorScope(QMainWindow):
    """Main window for MicroReactor Scope"""

    def __init__(self):
        super().__init__()
        self.setWindowTitle("MicroReactor Scope")
        self.setMinimumSize(1400, 900)

        # Data
        self.serial_worker = SerialWorker()
        self.signal_records: List[SignalRecord] = []
        self.trace_events: List[TraceEvent] = []
        self.entity_status: Dict[int, EntityStatus] = {}
        self.entity_names: Dict[int, str] = {}   # entity_id -> name
        self.signal_names: Dict[int, str] = {}   # signal_id -> name
        self.state_names: Dict[tuple, str] = {}  # (entity_id, state_id) -> name
        self.free_heap: int = 0
        self.min_heap: int = 0

        # Statistics
        self.total_signals = 0
        self.signal_times: Deque[float] = deque(maxlen=100)
        self.dispatch_times: Deque[int] = deque(maxlen=100)

        self._create_actions()
        self._create_menus()
        self._create_toolbar()
        self._create_ui()
        self._create_statusbar()

        # Connect signals
        self.serial_worker.data_received.connect(self._on_data_received)
        self.serial_worker.connection_changed.connect(self._on_connection_changed)
        self.serial_worker.error_occurred.connect(self._on_error)
        self.serial_worker.entity_name_received.connect(self._on_entity_name)
        self.serial_worker.signal_name_received.connect(self._on_signal_name)
        self.serial_worker.state_name_received.connect(self._on_state_name)
        self.serial_worker.sysinfo_received.connect(self._on_sysinfo)

        # Update timer
        self.update_timer = QTimer()
        self.update_timer.timeout.connect(self._update_stats)
        self.update_timer.start(100)  # 10 Hz

    def _create_actions(self):
        self.connect_action = QAction(tr("connect"), self)
        self.connect_action.setCheckable(True)
        self.connect_action.triggered.connect(self.toggle_connection)

        self.clear_action = QAction(tr("clear"), self)
        self.clear_action.setShortcut("Ctrl+L")
        self.clear_action.triggered.connect(self.clear_data)

        self.pause_action = QAction(tr("pause"), self)
        self.pause_action.setCheckable(True)
        self.pause_action.setShortcut("Space")

        self.export_action = QAction(tr("export"), self)
        self.export_action.triggered.connect(self.export_data)

    def _create_menus(self):
        menubar = self.menuBar()

        file_menu = menubar.addMenu(tr("file"))
        file_menu.addAction(self.export_action)
        file_menu.addSeparator()
        file_menu.addAction(tr("exit"), self.close)

        view_menu = menubar.addMenu(tr("view"))
        view_menu.addAction(self.clear_action)
        view_menu.addAction(self.pause_action)

        # Language menu
        lang_menu = menubar.addMenu("语言/Lang")
        zh_action = QAction("中文", self)
        zh_action.triggered.connect(lambda: self._set_language("zh"))
        en_action = QAction("English", self)
        en_action.triggered.connect(lambda: self._set_language("en"))
        lang_menu.addAction(zh_action)
        lang_menu.addAction(en_action)

    def _set_language(self, lang: str):
        set_lang(lang)
        QMessageBox.information(self, "Language", "请重启应用以应用语言更改\nPlease restart to apply language change")

    def _create_toolbar(self):
        toolbar = QToolBar()
        self.addToolBar(toolbar)

        # Port selection
        toolbar.addWidget(QLabel(tr("port")))
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(150)
        self._refresh_ports()
        toolbar.addWidget(self.port_combo)

        refresh_btn = QPushButton("↻")
        refresh_btn.setMaximumWidth(30)
        refresh_btn.clicked.connect(self._refresh_ports)
        toolbar.addWidget(refresh_btn)

        # Baudrate
        toolbar.addWidget(QLabel(" " + tr("baud")))
        self.baud_combo = QComboBox()
        self.baud_combo.addItems(["115200", "230400", "460800", "921600"])
        toolbar.addWidget(self.baud_combo)

        toolbar.addSeparator()
        toolbar.addAction(self.connect_action)
        toolbar.addSeparator()
        toolbar.addAction(self.clear_action)
        toolbar.addAction(self.pause_action)

    def _create_ui(self):
        # Main splitter
        main_splitter = QSplitter(Qt.Horizontal)
        self.setCentralWidget(main_splitter)

        # Left panel - Entity tree and controls
        left_panel = QWidget()
        left_layout = QVBoxLayout(left_panel)

        # Entity tree
        left_layout.addWidget(QLabel(tr("entities")))
        self.entity_tree = QTreeWidget()
        self.entity_tree.setHeaderLabels([tr("col_entity"), tr("col_state"), tr("col_inbox")])
        self.entity_tree.setColumnWidth(0, 100)
        left_layout.addWidget(self.entity_tree)

        # Signal injection
        inject_group = QGroupBox(tr("inject_group"))
        inject_layout = QFormLayout(inject_group)

        self.inject_target = QSpinBox()
        self.inject_target.setRange(1, 255)
        inject_layout.addRow(tr("inject_target"), self.inject_target)

        self.inject_signal = QLineEdit("0x0100")
        inject_layout.addRow(tr("inject_signal"), self.inject_signal)

        self.inject_payload = QLineEdit("0")
        inject_layout.addRow(tr("inject_payload"), self.inject_payload)

        inject_btn = QPushButton(tr("inject_btn"))
        inject_btn.clicked.connect(self._inject_signal)
        inject_layout.addRow(inject_btn)

        left_layout.addWidget(inject_group)

        # Command input
        cmd_group = QGroupBox(tr("shell_group"))
        cmd_layout = QVBoxLayout(cmd_group)
        self.cmd_input = QLineEdit()
        self.cmd_input.setPlaceholderText(tr("shell_hint"))
        self.cmd_input.returnPressed.connect(self._send_command)
        cmd_layout.addWidget(self.cmd_input)
        left_layout.addWidget(cmd_group)

        main_splitter.addWidget(left_panel)

        # Center - Tab widget with visualizations
        center_tabs = QTabWidget()

        # Gantt tab
        self.gantt_widget = GanttWidget()
        center_tabs.addTab(self.gantt_widget, tr("tab_gantt"))

        # Signal flow tab
        self.flow_widget = SignalFlowWidget()
        center_tabs.addTab(self.flow_widget, tr("tab_flow"))

        # Plot tab (if pyqtgraph available)
        if HAS_PYQTGRAPH:
            self.plot_widget = pg.PlotWidget()
            self.plot_widget.setBackground('w')
            self.plot_widget.setLabel('left', 'Dispatch Time', 'μs')
            self.plot_widget.setLabel('bottom', 'Time', 's')
            self.plot_curve = self.plot_widget.plot(pen=pg.mkPen('b', width=2))
            center_tabs.addTab(self.plot_widget, tr("tab_plot"))

        # Log tab
        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setFontFamily("Consolas")
        center_tabs.addTab(self.log_text, tr("tab_log"))

        main_splitter.addWidget(center_tabs)

        # Right panel - Statistics
        self.stats_widget = StatsWidget()
        main_splitter.addWidget(self.stats_widget)

        # Set splitter sizes
        main_splitter.setSizes([250, 850, 300])

    def _create_statusbar(self):
        self.status_label = QLabel(tr("disconnected"))
        self.statusBar().addWidget(self.status_label)

        self.rate_label = QLabel("0 sig/s")
        self.statusBar().addPermanentWidget(self.rate_label)

    def _refresh_ports(self):
        self.port_combo.clear()
        if HAS_SERIAL:
            ports = serial.tools.list_ports.comports()
            for port in ports:
                self.port_combo.addItem(f"{port.device} - {port.description}", port.device)

    def toggle_connection(self, checked: bool):
        if checked:
            port = self.port_combo.currentData()
            baud = int(self.baud_combo.currentText())
            if port:
                if self.serial_worker.connect(port, baud):
                    self.connect_action.setText(tr("disconnect"))
                else:
                    self.connect_action.setChecked(False)
            else:
                QMessageBox.warning(self, tr("warning"), tr("msg_no_port"))
                self.connect_action.setChecked(False)
        else:
            self.serial_worker.disconnect()
            self.connect_action.setText(tr("connect"))

    def _on_connection_changed(self, connected: bool):
        if connected:
            self.status_label.setText(tr("connected"))
            self.status_label.setStyleSheet("color: green;")
        else:
            self.status_label.setText(tr("disconnected"))
            self.status_label.setStyleSheet("color: red;")

    def _on_error(self, error: str):
        self.log_text.append(f"{tr('msg_error')} {error}")

    def _on_entity_name(self, entity_id: int, name: str):
        """Handle entity name from device"""
        self.entity_names[entity_id] = name
        self.gantt_widget.register_entity_name(entity_id, name)
        self.flow_widget.register_entity_name(entity_id, name)
        # Update entity status if exists
        if entity_id in self.entity_status:
            self.entity_status[entity_id].name = name
        self.log_text.append(f"[META] 实体 {entity_id} = {name}")

    def _on_signal_name(self, signal_id: int, name: str):
        """Handle signal name from device"""
        self.signal_names[signal_id] = name
        self.gantt_widget.register_signal_name(signal_id, name)
        self.flow_widget.register_signal_name(signal_id, name)
        self.log_text.append(f"[META] 信号 0x{signal_id:04X} = {name}")

    def _on_sysinfo(self, free_heap: int, min_heap: int):
        """Handle system info from device"""
        self.free_heap = free_heap
        self.min_heap = min_heap

    def _on_state_name(self, entity_id: int, state_id: int, name: str):
        """Handle state name from device"""
        self.state_names[(entity_id, state_id)] = name
        ent_name = self.entity_names.get(entity_id, f"E{entity_id}")
        self.log_text.append(f"[META] 状态 {ent_name}:{state_id} = {name}")

    def _on_data_received(self, data: bytes):
        if self.pause_action.isChecked():
            return

        # Parse trace event (new format from text protocol)
        # Format: event_type(B), entity_id(H), pad(H), data1(I), data2(I), timestamp(I)
        if len(data) >= 17:
            try:
                event_type, entity_id, _, data1, data2, timestamp = struct.unpack('<BHHIII', data[:17])

                # Interpret data based on event type
                signal_id = data1 & 0xFFFF if event_type in [0, 1, 3, 4] else 0
                src_id = data2 & 0xFFFF if event_type in [0, 1, 3, 4] else 0
                from_state = data1 & 0xFFFF if event_type == 2 else 0
                to_state = data2 & 0xFFFF if event_type == 2 else 0

                event = TraceEvent(
                    timestamp_us=timestamp,
                    entity_id=entity_id,
                    event_type=event_type,
                    signal_id=signal_id,
                    src_id=src_id,
                    from_state=from_state,
                    to_state=to_state
                )

                self.trace_events.append(event)
                self.gantt_widget.add_event(event)

                # Track entity status
                if entity_id not in self.entity_status:
                    self.entity_status[entity_id] = EntityStatus(
                        id=entity_id, name=f"E{entity_id}", state=0,
                        state_name="--", inbox_count=0, signal_count=0,
                        last_signal="--", last_dispatch_us=0
                    )

                # Update entity state on state change
                if event_type == TraceEvent.STATE_CHANGE:
                    self.entity_status[entity_id].state = to_state
                    # Use state name if available
                    state_name = self.state_names.get((entity_id, to_state), f"S{to_state}")
                    self.entity_status[entity_id].state_name = state_name

                # Track dispatch times
                if event.event_type == TraceEvent.DISPATCH_END:
                    # Find matching start
                    for e in reversed(self.trace_events):
                        if (e.entity_id == event.entity_id and
                            e.event_type == TraceEvent.DISPATCH_START):
                            duration = event.timestamp_us - e.timestamp_us
                            self.dispatch_times.append(duration)
                            self.entity_status[entity_id].last_dispatch_us = duration
                            break

                # Track signals
                if event_type == TraceEvent.DISPATCH_START:
                    self.entity_status[entity_id].signal_count += 1
                    sig_name = self.signal_names.get(signal_id, f"0x{signal_id:04X}")
                    self.entity_status[entity_id].last_signal = sig_name

                    # Add to signal flow (use dispatch as signal flow)
                    self.total_signals += 1
                    self.signal_times.append(time.time())

                    src_name = self.entity_names.get(src_id, f"E{src_id}") if src_id else "?"
                    dst_name = self.entity_names.get(entity_id, f"E{entity_id}")

                    record = SignalRecord(
                        timestamp=time.time(),
                        src_id=src_id,
                        src_name=src_name,
                        dst_id=entity_id,
                        dst_name=dst_name,
                        signal_id=signal_id,
                        signal_name=sig_name,
                        payload=b''
                    )
                    self.signal_records.append(record)
                    self.flow_widget.add_signal(record)

                    # Log
                    self.log_text.append(
                        f"[{datetime.now().strftime('%H:%M:%S.%f')[:-3]}] "
                        f"{src_name} -> {dst_name}: {sig_name}"
                    )

                # Log signals (legacy SIGNAL_EMIT/RECV - keep for compatibility)
                elif event.event_type in [TraceEvent.SIGNAL_EMIT, TraceEvent.SIGNAL_RECV]:
                    self.total_signals += 1
                    self.signal_times.append(time.time())

                    # Use names if available
                    src_name = self.entity_names.get(event.src_id, f"E{event.src_id}")
                    dst_name = self.entity_names.get(event.entity_id, f"E{event.entity_id}")
                    sig_name = self.signal_names.get(event.signal_id, f"0x{event.signal_id:04X}")

                    record = SignalRecord(
                        timestamp=time.time(),
                        src_id=event.src_id,
                        src_name=src_name,
                        dst_id=event.entity_id,
                        dst_name=dst_name,
                        signal_id=event.signal_id,
                        signal_name=sig_name,
                        payload=b''
                    )
                    self.signal_records.append(record)
                    self.flow_widget.add_signal(record)

                    # Log
                    self.log_text.append(
                        f"[{datetime.now().strftime('%H:%M:%S.%f')[:-3]}] "
                        f"{src_name} -> {dst_name}: {sig_name}"
                    )

            except Exception as e:
                pass

    def _update_stats(self):
        # Calculate signal rate
        now = time.time()
        recent = [t for t in self.signal_times if now - t < 1.0]
        rate = len(recent)
        self.rate_label.setText(f"{rate} sig/s")

        # Update stats widget (using internal keys)
        self.stats_widget.update_stat("total", str(self.total_signals))
        self.stats_widget.update_stat("rate", str(rate))
        self.stats_widget.update_stat("active", str(len(self.entity_status)))

        # Update memory stats
        if self.free_heap > 0:
            mem_str = f"{self.free_heap // 1024}KB / min {self.min_heap // 1024}KB"
            self.stats_widget.update_stat("memory", mem_str)

        if self.dispatch_times:
            max_dispatch = max(self.dispatch_times)
            avg_dispatch = sum(self.dispatch_times) // len(self.dispatch_times)
            self.stats_widget.update_stat("max", str(max_dispatch))
            self.stats_widget.update_stat("avg", str(avg_dispatch))

        # Update entity tree (use entity names if available)
        for eid, status in self.entity_status.items():
            name = self.entity_names.get(eid, f"E{eid}")
            items = self.entity_tree.findItems(name, Qt.MatchExactly, 0)
            # Also try old name format
            if not items:
                items = self.entity_tree.findItems(f"E{eid}", Qt.MatchExactly, 0)
            if items:
                item = items[0]
                item.setText(0, name)  # Update name in case it changed
                item.setText(1, status.state_name)
                item.setText(2, str(status.signal_count))
            else:
                item = QTreeWidgetItem([name, status.state_name, str(status.signal_count)])
                self.entity_tree.addTopLevelItem(item)
                self.entity_tree.addTopLevelItem(item)

        # Update plot
        if HAS_PYQTGRAPH and self.dispatch_times:
            times = list(range(len(self.dispatch_times)))
            values = list(self.dispatch_times)
            self.plot_curve.setData(times, values)

    def _inject_signal(self):
        try:
            target = self.inject_target.value()
            sig_text = self.inject_signal.text()
            signal_id = int(sig_text, 0)  # Supports hex
            payload = int(self.inject_payload.text(), 0)

            cmd = f"inject {target} {signal_id} {payload}"
            self.serial_worker.send_command(cmd)
            self.log_text.append(f"{tr('msg_inject')} {cmd}")

        except Exception as e:
            QMessageBox.warning(self, tr("error"), tr("msg_invalid", e=e))

    def _send_command(self):
        cmd = self.cmd_input.text().strip()
        if cmd:
            self.serial_worker.send_command(cmd)
            self.log_text.append(f"{tr('msg_cmd')} {cmd}")
            self.cmd_input.clear()

    def clear_data(self):
        self.signal_records.clear()
        self.trace_events.clear()
        self.dispatch_times.clear()
        self.signal_times.clear()
        self.total_signals = 0
        self.entity_status.clear()
        self.entity_names.clear()
        self.signal_names.clear()
        self.state_names.clear()
        self.free_heap = 0
        self.min_heap = 0
        self.entity_tree.clear()
        self.gantt_widget.clear()
        self.flow_widget.clear()
        self.log_text.clear()

    def export_data(self):
        from PySide6.QtWidgets import QFileDialog

        filename, _ = QFileDialog.getSaveFileName(
            self, "Export Data", "", "JSON (*.json);;CSV (*.csv)"
        )

        if filename:
            try:
                if filename.endswith('.json'):
                    data = {
                        'signals': [
                            {
                                'timestamp': r.timestamp,
                                'src_id': r.src_id,
                                'dst_id': r.dst_id,
                                'signal_id': r.signal_id,
                                'signal_name': r.signal_name
                            }
                            for r in self.signal_records
                        ],
                        'dispatch_times': list(self.dispatch_times)
                    }
                    with open(filename, 'w') as f:
                        json.dump(data, f, indent=2)
                else:
                    with open(filename, 'w') as f:
                        f.write("timestamp,src_id,dst_id,signal_id,signal_name\n")
                        for r in self.signal_records:
                            f.write(f"{r.timestamp},{r.src_id},{r.dst_id},"
                                   f"{r.signal_id},{r.signal_name}\n")

                QMessageBox.information(self, tr("info"), tr("msg_exported", path=filename))

            except Exception as e:
                QMessageBox.critical(self, tr("error"), tr("msg_export_fail", e=e))

    def closeEvent(self, event):
        self.serial_worker.disconnect()
        self.update_timer.stop()
        super().closeEvent(event)


# =============================================================================
# Main
# =============================================================================

def main():
    app = QApplication(sys.argv)
    app.setStyle('Fusion')

    # Dark theme
    palette = app.palette()
    palette.setColor(palette.ColorRole.Window, QColor(53, 53, 53))
    palette.setColor(palette.ColorRole.WindowText, QColor(255, 255, 255))
    palette.setColor(palette.ColorRole.Base, QColor(25, 25, 25))
    palette.setColor(palette.ColorRole.AlternateBase, QColor(53, 53, 53))
    palette.setColor(palette.ColorRole.Text, QColor(255, 255, 255))
    palette.setColor(palette.ColorRole.Button, QColor(53, 53, 53))
    palette.setColor(palette.ColorRole.ButtonText, QColor(255, 255, 255))
    palette.setColor(palette.ColorRole.Highlight, QColor(42, 130, 218))
    app.setPalette(palette)

    window = ReactorScope()
    window.show()

    sys.exit(app.exec())


if __name__ == '__main__':
    main()
