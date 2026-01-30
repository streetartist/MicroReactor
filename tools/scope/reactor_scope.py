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
    QMessageBox, QProgressBar, QFrame, QScrollArea
)
from PySide6.QtCore import Qt, Signal, QTimer, QThread, QMutex
from PySide6.QtGui import (
    QAction, QPainter, QPen, QBrush, QColor, QFont,
    QKeySequence, QPainterPath
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

        while self.running and self.port:
            try:
                if self.port.in_waiting:
                    data = self.port.read(self.port.in_waiting)
                    buffer.extend(data)

                    # Try to parse frames
                    while len(buffer) >= 16:  # Minimum frame size
                        # Find sync byte
                        try:
                            idx = buffer.index(self.SYNC_BYTE)
                            if idx > 0:
                                buffer = buffer[idx:]
                        except ValueError:
                            buffer.clear()
                            break

                        if len(buffer) < 16:
                            break

                        # Parse trace event (simplified)
                        frame = bytes(buffer[:16])
                        self.data_received.emit(frame)
                        buffer = buffer[16:]

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

        self.events: Deque[TraceEvent] = deque(maxlen=1000)
        self.entity_colors: Dict[int, QColor] = {}
        self.time_window_us = 100000  # 100ms window

        # Colors for entities
        self.palette = [
            QColor(70, 130, 180),   # Steel Blue
            QColor(60, 179, 113),   # Medium Sea Green
            QColor(255, 165, 0),    # Orange
            QColor(186, 85, 211),   # Medium Orchid
            QColor(220, 20, 60),    # Crimson
            QColor(0, 206, 209),    # Dark Turquoise
        ]

    def add_event(self, event: TraceEvent):
        self.events.append(event)

        # Assign color if new entity
        if event.entity_id not in self.entity_colors:
            idx = len(self.entity_colors) % len(self.palette)
            self.entity_colors[event.entity_id] = self.palette[idx]

        self.update()

    def clear(self):
        self.events.clear()
        self.update()

    def paintEvent(self, event):
        if not self.events:
            return

        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        width = self.width()
        height = self.height()

        # Background
        painter.fillRect(0, 0, width, height, QColor(30, 30, 30))

        # Get time range
        if not self.events:
            return

        max_time = max(e.timestamp_us for e in self.events)
        min_time = max_time - self.time_window_us

        # Draw grid
        painter.setPen(QPen(QColor(60, 60, 60), 1))
        for i in range(10):
            x = int(width * i / 10)
            painter.drawLine(x, 0, x, height)
            t_ms = (min_time + self.time_window_us * i / 10) / 1000
            painter.drawText(x + 2, height - 5, f"{t_ms:.1f}ms")

        # Draw entity lanes
        entities = sorted(self.entity_colors.keys())
        if not entities:
            return

        lane_height = max(20, (height - 30) // len(entities))

        for lane_idx, entity_id in enumerate(entities):
            y_base = lane_idx * lane_height + 10

            # Lane background
            painter.fillRect(0, y_base, width, lane_height - 2,
                           QColor(40, 40, 40))

            # Entity label
            painter.setPen(QPen(QColor(200, 200, 200)))
            painter.drawText(5, y_base + lane_height // 2 + 5, f"E{entity_id}")

            # Draw dispatch blocks
            color = self.entity_colors[entity_id]
            painter.setBrush(QBrush(color))
            painter.setPen(QPen(color.darker(120), 1))

            start_time = None
            for evt in self.events:
                if evt.entity_id != entity_id:
                    continue

                if evt.timestamp_us < min_time:
                    continue

                if evt.event_type == TraceEvent.DISPATCH_START:
                    start_time = evt.timestamp_us
                elif evt.event_type == TraceEvent.DISPATCH_END and start_time:
                    # Draw block
                    x1 = int((start_time - min_time) / self.time_window_us * width)
                    x2 = int((evt.timestamp_us - min_time) / self.time_window_us * width)
                    painter.drawRect(x1, y_base + 2, max(2, x2 - x1), lane_height - 6)
                    start_time = None


class SignalFlowWidget(QWidget):
    """Visual signal flow diagram"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumHeight(300)

        self.signals: Deque[SignalRecord] = deque(maxlen=50)
        self.entity_positions: Dict[int, int] = {}

    def add_signal(self, record: SignalRecord):
        self.signals.append(record)

        # Track entity positions
        if record.src_id not in self.entity_positions:
            self.entity_positions[record.src_id] = len(self.entity_positions)
        if record.dst_id not in self.entity_positions:
            self.entity_positions[record.dst_id] = len(self.entity_positions)

        self.update()

    def clear(self):
        self.signals.clear()
        self.entity_positions.clear()
        self.update()

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)

        width = self.width()
        height = self.height()

        # Background
        painter.fillRect(0, 0, width, height, QColor(250, 250, 250))

        if not self.entity_positions:
            painter.drawText(width // 2 - 50, height // 2, "No signals yet")
            return

        # Draw entity columns
        num_entities = len(self.entity_positions)
        col_width = width // (num_entities + 1)

        painter.setPen(QPen(QColor(100, 100, 100), 2))
        font = QFont("Arial", 9, QFont.Bold)
        painter.setFont(font)

        for entity_id, idx in self.entity_positions.items():
            x = (idx + 1) * col_width
            painter.drawLine(x, 50, x, height - 10)
            painter.drawText(x - 20, 30, f"E{entity_id}")

        # Draw signal arrows
        if not self.signals:
            return

        row_height = max(20, (height - 60) // len(self.signals))

        for i, sig in enumerate(self.signals):
            y = 60 + i * row_height

            src_x = (self.entity_positions.get(sig.src_id, 0) + 1) * col_width
            dst_x = (self.entity_positions.get(sig.dst_id, 0) + 1) * col_width

            # Arrow line
            painter.setPen(QPen(QColor(70, 130, 180), 2))
            painter.drawLine(src_x, y, dst_x, y)

            # Arrow head
            direction = 1 if dst_x > src_x else -1
            painter.drawLine(dst_x, y, dst_x - direction * 8, y - 5)
            painter.drawLine(dst_x, y, dst_x - direction * 8, y + 5)

            # Label
            mid_x = (src_x + dst_x) // 2
            painter.setPen(QPen(QColor(50, 50, 50)))
            painter.drawText(mid_x - 30, y - 5, sig.signal_name[:15])


class StatsWidget(QWidget):
    """Statistics display widget"""

    def __init__(self, parent=None):
        super().__init__(parent)

        layout = QVBoxLayout(self)

        # Create stat labels
        self.stats_labels: Dict[str, QLabel] = {}

        stats_group = QGroupBox("Performance Statistics")
        stats_layout = QFormLayout(stats_group)

        for name in ["Total Signals", "Signals/sec", "Max Dispatch (μs)",
                     "Avg Dispatch (μs)", "Active Entities", "Memory Used"]:
            label = QLabel("--")
            label.setStyleSheet("font-weight: bold; font-size: 14px;")
            self.stats_labels[name] = label
            stats_layout.addRow(name + ":", label)

        layout.addWidget(stats_group)

        # Progress bars for per-entity load
        self.entity_bars: Dict[int, QProgressBar] = {}
        self.entity_group = QGroupBox("Entity Load")
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
        self.signal_records: Deque[SignalRecord] = deque(maxlen=10000)
        self.trace_events: Deque[TraceEvent] = deque(maxlen=10000)
        self.entity_status: Dict[int, EntityStatus] = {}

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

        # Update timer
        self.update_timer = QTimer()
        self.update_timer.timeout.connect(self._update_stats)
        self.update_timer.start(100)  # 10 Hz

    def _create_actions(self):
        self.connect_action = QAction("Connect", self)
        self.connect_action.setCheckable(True)
        self.connect_action.triggered.connect(self.toggle_connection)

        self.clear_action = QAction("Clear", self)
        self.clear_action.setShortcut("Ctrl+L")
        self.clear_action.triggered.connect(self.clear_data)

        self.pause_action = QAction("Pause", self)
        self.pause_action.setCheckable(True)
        self.pause_action.setShortcut("Space")

        self.export_action = QAction("Export Data...", self)
        self.export_action.triggered.connect(self.export_data)

    def _create_menus(self):
        menubar = self.menuBar()

        file_menu = menubar.addMenu("File")
        file_menu.addAction(self.export_action)
        file_menu.addSeparator()
        file_menu.addAction("Exit", self.close)

        view_menu = menubar.addMenu("View")
        view_menu.addAction(self.clear_action)
        view_menu.addAction(self.pause_action)

    def _create_toolbar(self):
        toolbar = QToolBar()
        self.addToolBar(toolbar)

        # Port selection
        toolbar.addWidget(QLabel("Port:"))
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(150)
        self._refresh_ports()
        toolbar.addWidget(self.port_combo)

        refresh_btn = QPushButton("↻")
        refresh_btn.setMaximumWidth(30)
        refresh_btn.clicked.connect(self._refresh_ports)
        toolbar.addWidget(refresh_btn)

        # Baudrate
        toolbar.addWidget(QLabel(" Baud:"))
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
        left_layout.addWidget(QLabel("Entities:"))
        self.entity_tree = QTreeWidget()
        self.entity_tree.setHeaderLabels(["Entity", "State", "Inbox"])
        self.entity_tree.setColumnWidth(0, 100)
        left_layout.addWidget(self.entity_tree)

        # Signal injection
        inject_group = QGroupBox("Signal Injection")
        inject_layout = QFormLayout(inject_group)

        self.inject_target = QSpinBox()
        self.inject_target.setRange(1, 255)
        inject_layout.addRow("Target ID:", self.inject_target)

        self.inject_signal = QLineEdit("0x0100")
        inject_layout.addRow("Signal:", self.inject_signal)

        self.inject_payload = QLineEdit("0")
        inject_layout.addRow("Payload:", self.inject_payload)

        inject_btn = QPushButton("Inject Signal")
        inject_btn.clicked.connect(self._inject_signal)
        inject_layout.addRow(inject_btn)

        left_layout.addWidget(inject_group)

        # Command input
        cmd_group = QGroupBox("Shell Command")
        cmd_layout = QVBoxLayout(cmd_group)
        self.cmd_input = QLineEdit()
        self.cmd_input.setPlaceholderText("Enter command...")
        self.cmd_input.returnPressed.connect(self._send_command)
        cmd_layout.addWidget(self.cmd_input)
        left_layout.addWidget(cmd_group)

        main_splitter.addWidget(left_panel)

        # Center - Tab widget with visualizations
        center_tabs = QTabWidget()

        # Gantt tab
        self.gantt_widget = GanttWidget()
        center_tabs.addTab(self.gantt_widget, "Timing (Gantt)")

        # Signal flow tab
        self.flow_widget = SignalFlowWidget()
        center_tabs.addTab(self.flow_widget, "Signal Flow")

        # Plot tab (if pyqtgraph available)
        if HAS_PYQTGRAPH:
            self.plot_widget = pg.PlotWidget()
            self.plot_widget.setBackground('w')
            self.plot_widget.setLabel('left', 'Dispatch Time', 'μs')
            self.plot_widget.setLabel('bottom', 'Time', 's')
            self.plot_curve = self.plot_widget.plot(pen=pg.mkPen('b', width=2))
            center_tabs.addTab(self.plot_widget, "Dispatch Plot")

        # Log tab
        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setFontFamily("Consolas")
        center_tabs.addTab(self.log_text, "Signal Log")

        main_splitter.addWidget(center_tabs)

        # Right panel - Statistics
        self.stats_widget = StatsWidget()
        main_splitter.addWidget(self.stats_widget)

        # Set splitter sizes
        main_splitter.setSizes([250, 850, 300])

    def _create_statusbar(self):
        self.status_label = QLabel("Disconnected")
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
                    self.connect_action.setText("Disconnect")
                else:
                    self.connect_action.setChecked(False)
            else:
                QMessageBox.warning(self, "Warning", "No port selected")
                self.connect_action.setChecked(False)
        else:
            self.serial_worker.disconnect()
            self.connect_action.setText("Connect")

    def _on_connection_changed(self, connected: bool):
        if connected:
            self.status_label.setText("Connected")
            self.status_label.setStyleSheet("color: green;")
        else:
            self.status_label.setText("Disconnected")
            self.status_label.setStyleSheet("color: red;")

    def _on_error(self, error: str):
        self.log_text.append(f"[ERROR] {error}")

    def _on_data_received(self, data: bytes):
        if self.pause_action.isChecked():
            return

        # Parse trace event
        if len(data) >= 16 and data[0] == 0x55:
            try:
                timestamp, entity_id, event_type, flags = struct.unpack('<IHBB', data[1:9])
                signal_id, src_id = struct.unpack('<HH', data[9:13])

                event = TraceEvent(
                    timestamp_us=timestamp,
                    entity_id=entity_id,
                    event_type=event_type,
                    signal_id=signal_id,
                    src_id=src_id
                )

                self.trace_events.append(event)
                self.gantt_widget.add_event(event)

                # Track dispatch times
                if event.event_type == TraceEvent.DISPATCH_END:
                    # Find matching start
                    for e in reversed(self.trace_events):
                        if (e.entity_id == event.entity_id and
                            e.event_type == TraceEvent.DISPATCH_START):
                            duration = event.timestamp_us - e.timestamp_us
                            self.dispatch_times.append(duration)
                            break

                # Log signals
                if event.event_type in [TraceEvent.SIGNAL_EMIT, TraceEvent.SIGNAL_RECV]:
                    self.total_signals += 1
                    self.signal_times.append(time.time())

                    record = SignalRecord(
                        timestamp=time.time(),
                        src_id=event.src_id,
                        src_name=f"E{event.src_id}",
                        dst_id=event.entity_id,
                        dst_name=f"E{event.entity_id}",
                        signal_id=event.signal_id,
                        signal_name=f"0x{event.signal_id:04X}",
                        payload=b''
                    )
                    self.signal_records.append(record)
                    self.flow_widget.add_signal(record)

                    # Log
                    self.log_text.append(
                        f"[{datetime.now().strftime('%H:%M:%S.%f')[:-3]}] "
                        f"E{event.src_id} -> E{event.entity_id}: 0x{event.signal_id:04X}"
                    )

            except Exception as e:
                pass

    def _update_stats(self):
        # Calculate signal rate
        now = time.time()
        recent = [t for t in self.signal_times if now - t < 1.0]
        rate = len(recent)
        self.rate_label.setText(f"{rate} sig/s")

        # Update stats widget
        self.stats_widget.update_stat("Total Signals", str(self.total_signals))
        self.stats_widget.update_stat("Signals/sec", str(rate))

        if self.dispatch_times:
            max_dispatch = max(self.dispatch_times)
            avg_dispatch = sum(self.dispatch_times) // len(self.dispatch_times)
            self.stats_widget.update_stat("Max Dispatch (μs)", str(max_dispatch))
            self.stats_widget.update_stat("Avg Dispatch (μs)", str(avg_dispatch))

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
            self.log_text.append(f"[INJECT] {cmd}")

        except Exception as e:
            QMessageBox.warning(self, "Error", f"Invalid input: {e}")

    def _send_command(self):
        cmd = self.cmd_input.text().strip()
        if cmd:
            self.serial_worker.send_command(cmd)
            self.log_text.append(f"[CMD] {cmd}")
            self.cmd_input.clear()

    def clear_data(self):
        self.signal_records.clear()
        self.trace_events.clear()
        self.dispatch_times.clear()
        self.signal_times.clear()
        self.total_signals = 0
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

                QMessageBox.information(self, "Export", f"Exported to {filename}")

            except Exception as e:
                QMessageBox.critical(self, "Error", f"Export failed: {e}")

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
