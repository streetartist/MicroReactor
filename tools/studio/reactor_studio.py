#!/usr/bin/env python3
"""
MicroReactor Studio - Visual State Machine Designer

A drag-and-drop visual editor for designing MicroReactor state machines.
Generates C code that integrates with the MicroReactor framework.

Features:
- Visual state diagram editing
- Drag & drop states and transitions
- Code generation (C header and source)
- Project save/load (JSON format)
- Bilingual UI (Chinese/English, default: Chinese)
"""

import sys
import json
import math
from dataclasses import dataclass, field, asdict
from typing import List, Dict, Optional, Tuple
from pathlib import Path

from PySide6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QGraphicsScene, QGraphicsView, QGraphicsItem, QGraphicsEllipseItem,
    QGraphicsLineItem, QGraphicsTextItem, QGraphicsPathItem,
    QToolBar, QDockWidget, QTreeWidget, QTreeWidgetItem,
    QDialog, QDialogButtonBox, QFormLayout, QLineEdit, QSpinBox,
    QComboBox, QTextEdit, QListWidget, QListWidgetItem, QPushButton,
    QFileDialog, QMessageBox, QSplitter, QLabel, QGroupBox,
    QTableWidget, QTableWidgetItem, QHeaderView, QMenu, QInputDialog,
    QStatusBar, QToolButton
)
from PySide6.QtCore import Qt, QPointF, QRectF, QLineF, Signal, QTimer
from PySide6.QtGui import (
    QAction, QPainter, QPen, QBrush, QColor, QFont,
    QPainterPath, QPolygonF, QKeySequence, QIcon, QTransform
)


# =============================================================================
# Internationalization (i18n)
# =============================================================================

_lang = "zh"  # Default: Chinese

_TR = {
    # Window
    "title": {"zh": "MicroReactor Studio - 状态机设计器", "en": "MicroReactor Studio"},
    "ready": {"zh": "就绪", "en": "Ready"},

    # Menu - File
    "file": {"zh": "文件", "en": "File"},
    "new_project": {"zh": "新建项目", "en": "New Project"},
    "open_project": {"zh": "打开项目...", "en": "Open Project..."},
    "save_project": {"zh": "保存项目", "en": "Save Project"},
    "save_as": {"zh": "另存为...", "en": "Save As..."},
    "export_code": {"zh": "导出 C 代码...", "en": "Export C Code..."},
    "exit": {"zh": "退出", "en": "Exit"},

    # Menu - Edit
    "edit": {"zh": "编辑", "en": "Edit"},
    "add_state": {"zh": "添加状态", "en": "Add State"},
    "add_transition": {"zh": "添加转换", "en": "Add Transition"},
    "delete": {"zh": "删除", "en": "Delete"},

    # Menu - Entity
    "entity": {"zh": "实体", "en": "Entity"},
    "new_entity": {"zh": "新建实体", "en": "New Entity"},

    # Menu - Language
    "language": {"zh": "语言 Language", "en": "语言 Language"},
    "lang_zh": {"zh": "中文", "en": "中文 Chinese"},
    "lang_en": {"zh": "English", "en": "English"},

    # Left panel
    "entities": {"zh": "实体列表:", "en": "Entities:"},
    "btn_new_entity": {"zh": "+ 新建实体", "en": "+ New Entity"},

    # Right panel
    "properties": {"zh": "属性:", "en": "Properties:"},
    "code_preview": {"zh": "代码预览:", "en": "Code Preview:"},

    # Transition mode
    "trans_hint": {"zh": "点击源状态，然后点击目标状态（ESC 取消）", "en": "Click source, then target (ESC to cancel)"},
    "trans_source": {"zh": "源: {name} → 点击目标状态（ESC 取消）", "en": "Source: {name} → Click target (ESC to cancel)"},
    "trans_done": {"zh": "转换已创建！继续添加或按 ESC 退出", "en": "Transition created! Continue or ESC to exit"},
    "trans_cleared": {"zh": "已清除，点击源状态或按 ESC 退出", "en": "Cleared. Click source or ESC to exit"},

    # Context menu - State
    "ctx_add_trans": {"zh": "从此状态添加转换", "en": "Add Transition From Here"},
    "ctx_edit_state": {"zh": "编辑状态...", "en": "Edit State..."},
    "ctx_set_initial": {"zh": "设为初始状态", "en": "Set as Initial State"},
    "ctx_delete_state": {"zh": "删除状态", "en": "Delete State"},

    # Context menu - Transition
    "ctx_edit_signal": {"zh": "编辑信号名...", "en": "Edit Signal Name..."},
    "ctx_delete_trans": {"zh": "删除转换", "en": "Delete Transition"},

    # State editor dialog
    "dlg_edit_state": {"zh": "编辑状态: {name}", "en": "Edit State: {name}"},
    "dlg_name": {"zh": "名称:", "en": "Name:"},
    "dlg_id": {"zh": "ID:", "en": "ID:"},
    "dlg_parent": {"zh": "父状态:", "en": "Parent State:"},
    "dlg_on_entry": {"zh": "进入动作:", "en": "On Entry:"},
    "dlg_on_exit": {"zh": "退出动作:", "en": "On Exit:"},
    "dlg_rules": {"zh": "转换规则:", "en": "Transition Rules:"},
    "dlg_add_rule": {"zh": "添加规则", "en": "Add Rule"},
    "dlg_signal": {"zh": "信号", "en": "Signal"},
    "dlg_next_state": {"zh": "下一状态", "en": "Next State"},
    "dlg_action": {"zh": "动作", "en": "Action"},
    "dlg_none": {"zh": "(无)", "en": "(None)"},
    "dlg_stay": {"zh": "(保持)", "en": "(Stay)"},

    # New entity dialog
    "dlg_new_entity": {"zh": "新建实体", "en": "New Entity"},
    "dlg_entity_name": {"zh": "实体名称:", "en": "Entity Name:"},
    "dlg_entity_id": {"zh": "实体 ID:", "en": "Entity ID:"},

    # Signal edit dialog
    "dlg_edit_signal": {"zh": "编辑信号", "en": "Edit Signal"},
    "dlg_signal_name": {"zh": "信号名:", "en": "Signal Name:"},

    # Messages
    "msg_no_entity": {"zh": "未选择实体", "en": "No entity selected"},
    "msg_saved": {"zh": "已保存到 {path}", "en": "Saved to {path}"},
    "msg_opened": {"zh": "已打开 {path}", "en": "Opened {path}"},
    "msg_export_done": {"zh": "导出完成", "en": "Export Complete"},
    "msg_export_files": {"zh": "已生成:\n{h}\n{c}", "en": "Generated:\n{h}\n{c}"},
    "msg_open_failed": {"zh": "打开失败: {e}", "en": "Failed to open: {e}"},
    "msg_save_failed": {"zh": "保存失败: {e}", "en": "Failed to save: {e}"},
    "msg_export_failed": {"zh": "导出失败: {e}", "en": "Failed to export: {e}"},
    "warning": {"zh": "警告", "en": "Warning"},
    "error": {"zh": "错误", "en": "Error"},
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
class SignalDef:
    """Signal definition"""
    id: int
    name: str
    description: str = ""

@dataclass
class RuleDef:
    """Transition rule definition"""
    signal_id: int
    signal_name: str
    next_state: int
    next_state_name: str
    action_name: str = ""

@dataclass
class StateDef:
    """State definition"""
    id: int
    name: str
    parent_id: int = 0
    on_entry: str = ""
    on_exit: str = ""
    rules: List[RuleDef] = field(default_factory=list)
    x: float = 0
    y: float = 0

@dataclass
class EntityDef:
    """Entity definition"""
    id: int
    name: str
    initial_state: int = 1
    states: List[StateDef] = field(default_factory=list)
    signals: List[SignalDef] = field(default_factory=list)

@dataclass
class Project:
    """Project definition"""
    name: str = "Untitled"
    version: str = "1.0"
    entities: List[EntityDef] = field(default_factory=list)


# =============================================================================
# Graphics Items
# =============================================================================

class StateItem(QGraphicsEllipseItem):
    """Visual representation of a state"""

    def __init__(self, state: StateDef, parent=None):
        super().__init__(-50, -30, 100, 60, parent)
        self.state = state
        self.setPos(state.x, state.y)

        # Appearance
        self.setBrush(QBrush(QColor(240, 248, 255)))
        self.setPen(QPen(QColor(70, 130, 180), 2))

        # Label
        self.label = QGraphicsTextItem(state.name, self)
        self.label.setDefaultTextColor(QColor(0, 0, 0))
        font = QFont("Arial", 10, QFont.Bold)
        self.label.setFont(font)
        self._center_label()

        # Make interactive
        self.setFlags(
            QGraphicsItem.ItemIsMovable |
            QGraphicsItem.ItemIsSelectable |
            QGraphicsItem.ItemSendsGeometryChanges
        )
        self.setAcceptHoverEvents(True)

        # Enable context menu
        self.setAcceptedMouseButtons(Qt.LeftButton | Qt.RightButton)

        # Connected transitions
        self.transitions_out: List['TransitionItem'] = []
        self.transitions_in: List['TransitionItem'] = []

    def _center_label(self):
        rect = self.label.boundingRect()
        self.label.setPos(-rect.width() / 2, -rect.height() / 2)

    def set_name(self, name: str):
        self.state.name = name
        self.label.setPlainText(name)
        self._center_label()

    def set_initial(self, is_initial: bool):
        if is_initial:
            self.setPen(QPen(QColor(34, 139, 34), 3))
        else:
            self.setPen(QPen(QColor(70, 130, 180), 2))

    def itemChange(self, change, value):
        if change == QGraphicsItem.ItemPositionHasChanged:
            # Update state position
            self.state.x = self.pos().x()
            self.state.y = self.pos().y()
            # Update connected transitions
            for trans in self.transitions_out + self.transitions_in:
                trans.update_position()
        return super().itemChange(change, value)

    def hoverEnterEvent(self, event):
        self.setBrush(QBrush(QColor(173, 216, 230)))
        super().hoverEnterEvent(event)

    def hoverLeaveEvent(self, event):
        self.setBrush(QBrush(QColor(240, 248, 255)))
        super().hoverLeaveEvent(event)

    def mouseDoubleClickEvent(self, event):
        # Open state editor
        if self.scene() and hasattr(self.scene(), 'edit_state'):
            self.scene().edit_state(self)
        super().mouseDoubleClickEvent(event)

    def contextMenuEvent(self, event):
        """Right-click context menu for quick actions"""
        menu = QMenu()

        # Transition action
        add_trans_action = menu.addAction(tr("studio.ctx_add_trans"))
        add_trans_action.triggered.connect(lambda: self._start_transition_from_here())

        menu.addSeparator()

        # Edit action
        edit_action = menu.addAction(tr("studio.ctx_edit_state"))
        edit_action.triggered.connect(lambda: self.scene().edit_state(self) if self.scene() else None)

        # Set as initial
        set_initial_action = menu.addAction(tr("studio.ctx_set_initial"))
        set_initial_action.triggered.connect(lambda: self._set_as_initial())

        menu.addSeparator()

        # Delete action
        delete_action = menu.addAction(tr("studio.ctx_delete_state"))
        delete_action.triggered.connect(lambda: self._delete_self())

        menu.exec(event.screenPos())

    def _start_transition_from_here(self):
        """Start transition creation from this state"""
        if self.scene():
            self.scene().set_transition_mode(True)
            self.scene()._set_transition_source(self)

    def _set_as_initial(self):
        """Set this state as the initial state"""
        if self.scene() and self.scene().entity:
            # Reset all states' visual
            for item in self.scene().state_items.values():
                item.set_initial(False)
            # Set this as initial
            self.scene().entity.initial_state = self.state.id
            self.set_initial(True)

    def _delete_self(self):
        """Delete this state"""
        if self.scene():
            self.scene()._delete_state(self)


class TransitionItem(QGraphicsPathItem):
    """Visual representation of a transition (arrow)"""

    def __init__(self, rule: RuleDef, from_state: StateItem, to_state: StateItem):
        super().__init__()
        self.rule = rule
        self.from_state = from_state
        self.to_state = to_state

        # Appearance
        self.setPen(QPen(QColor(100, 100, 100), 2))

        # Arrow head
        self.arrow_size = 10

        # Base curve offset
        self.base_offset = 20

        # Label
        self.label = QGraphicsTextItem(rule.signal_name, self)
        self.label.setDefaultTextColor(QColor(100, 100, 100))
        font = QFont("Arial", 8)
        self.label.setFont(font)

        # Connect to states
        from_state.transitions_out.append(self)
        to_state.transitions_in.append(self)

        self.update_position()

        # Make selectable
        self.setFlags(QGraphicsItem.ItemIsSelectable)

    def _get_all_transitions_between_states(self):
        """Get all transitions between these two states (both directions)"""
        state_a = self.from_state
        state_b = self.to_state

        transitions = []

        # A → B transitions
        for trans in state_a.transitions_out:
            if trans.to_state == state_b:
                transitions.append(('ab', trans))

        # B → A transitions
        for trans in state_b.transitions_out:
            if trans.to_state == state_a:
                transitions.append(('ba', trans))

        return transitions

    def _get_perpendicular_offset(self, p1: QPointF, p2: QPointF, offset: float) -> QPointF:
        """Get perpendicular offset vector"""
        dx = p2.x() - p1.x()
        dy = p2.y() - p1.y()
        length = math.sqrt(dx * dx + dy * dy)
        if length < 1:
            return QPointF(0, 0)
        # Perpendicular unit vector (rotate 90 degrees)
        return QPointF(-dy / length * offset, dx / length * offset)

    def update_position(self):
        """Update arrow path based on state positions"""
        p1 = self.from_state.scenePos()
        p2 = self.to_state.scenePos()

        # Calculate direction
        dx = p2.x() - p1.x()
        dy = p2.y() - p1.y()
        length = math.sqrt(dx * dx + dy * dy)

        if length < 1:
            return

        # Unit vector
        ux, uy = dx / length, dy / length

        # Shorten line to end at state edges
        start = QPointF(p1.x() + ux * 50, p1.y() + uy * 30)
        end = QPointF(p2.x() - ux * 50, p2.y() - uy * 30)

        # Get all transitions between these two states
        all_transitions = self._get_all_transitions_between_states()
        total_count = len(all_transitions)

        path = QPainterPath()

        if total_count <= 1:
            # Single transition - straight line
            path.moveTo(start)
            path.lineTo(end)
            arrow_angle = math.atan2(-dy, dx)
            label_pos = QPointF((start.x() + end.x()) / 2, (start.y() + end.y()) / 2)
        else:
            # Multiple transitions - find our index and calculate offset
            my_direction = 'ab'  # This transition goes from_state -> to_state

            # Separate by direction
            ab_transitions = [t for d, t in all_transitions if d == 'ab']
            ba_transitions = [t for d, t in all_transitions if d == 'ba']

            # Find my index within my direction group
            try:
                my_index = ab_transitions.index(self)
            except ValueError:
                my_index = 0

            ab_count = len(ab_transitions)
            ba_count = len(ba_transitions)

            # Calculate offset for this transition
            # AB transitions curve one way, BA transitions curve the other
            # Within each group, spread them out

            if ab_count > 0 and ba_count > 0:
                # Bidirectional - AB goes positive, BA goes negative
                # Spread within each group
                if ab_count == 1:
                    offset = self.base_offset
                else:
                    # Multiple in same direction: spread from base_offset outward
                    offset = self.base_offset + my_index * 15
            else:
                # All same direction - spread around center
                # Center the group: offsets like -20, 0, 20 for 3 items
                center_offset = (ab_count - 1) / 2
                offset = (my_index - center_offset) * 20

            # Get perpendicular offset for the control point
            perp = self._get_perpendicular_offset(p1, p2, offset)

            # Control point at midpoint + perpendicular offset
            mid = QPointF((p1.x() + p2.x()) / 2, (p1.y() + p2.y()) / 2)
            ctrl = QPointF(mid.x() + perp.x(), mid.y() + perp.y())

            # Draw quadratic bezier curve
            path.moveTo(start)
            path.quadTo(ctrl, end)

            # Arrow head angle - tangent at end of curve
            arrow_angle = math.atan2(-(end.y() - ctrl.y()), end.x() - ctrl.x())

            # Label position - at the control point
            label_pos = ctrl

        # Draw arrow head
        arrow_p1 = QPointF(
            end.x() + math.sin(arrow_angle - math.pi / 3) * self.arrow_size,
            end.y() + math.cos(arrow_angle - math.pi / 3) * self.arrow_size
        )
        arrow_p2 = QPointF(
            end.x() + math.sin(arrow_angle - math.pi + math.pi / 3) * self.arrow_size,
            end.y() + math.cos(arrow_angle - math.pi + math.pi / 3) * self.arrow_size
        )

        path.moveTo(end)
        path.lineTo(arrow_p1)
        path.moveTo(end)
        path.lineTo(arrow_p2)

        self.setPath(path)

        # Position label
        label_rect = self.label.boundingRect()
        self.label.setPos(
            label_pos.x() - label_rect.width() / 2,
            label_pos.y() - label_rect.height() - 2
        )

    def set_signal_name(self, name: str):
        self.rule.signal_name = name
        self.label.setPlainText(name)

    def contextMenuEvent(self, event):
        """Right-click context menu for transition"""
        menu = QMenu()

        # Edit signal name
        edit_action = menu.addAction(tr("studio.ctx_edit_signal"))
        edit_action.triggered.connect(lambda: self._edit_signal_name())

        menu.addSeparator()

        # Delete transition
        delete_action = menu.addAction(tr("studio.ctx_delete_trans"))
        delete_action.triggered.connect(lambda: self._delete_self())

        menu.exec(event.screenPos())

    def _edit_signal_name(self):
        """Quick edit signal name"""
        name, ok = QInputDialog.getText(
            None, tr("studio.dlg_edit_signal"),
            tr("studio.dlg_signal_name"),
            QLineEdit.Normal,
            self.rule.signal_name
        )
        if ok and name:
            self.set_signal_name(name)

    def _delete_self(self):
        """Delete this transition"""
        if self.scene():
            self.scene()._delete_transition(self)


# =============================================================================
# Scene
# =============================================================================

class StateMachineScene(QGraphicsScene):
    """Graphics scene for state machine editing"""

    state_selected = Signal(StateDef)
    transition_selected = Signal(RuleDef)
    status_message = Signal(str)  # For status bar updates

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setSceneRect(-2000, -2000, 4000, 4000)
        self.entity: Optional[EntityDef] = None
        self.state_items: Dict[int, StateItem] = {}
        self.transition_items: List[TransitionItem] = []

        # Transition mode (two-click method)
        self.transition_mode = False
        self.transition_source: Optional[StateItem] = None

    def load_entity(self, entity: EntityDef):
        """Load entity into scene"""
        self.clear()
        self.entity = entity
        self.state_items.clear()
        self.transition_items.clear()

        # Create state items
        for state in entity.states:
            item = StateItem(state)
            self.addItem(item)
            self.state_items[state.id] = item

            # Mark initial state
            if state.id == entity.initial_state:
                item.set_initial(True)

        # Create transition items
        for state in entity.states:
            for rule in state.rules:
                if rule.next_state > 0 and rule.next_state in self.state_items:
                    from_item = self.state_items[state.id]
                    to_item = self.state_items[rule.next_state]
                    trans = TransitionItem(rule, from_item, to_item)
                    self.addItem(trans)
                    self.transition_items.append(trans)

    def add_state(self, x: float, y: float) -> StateItem:
        """Add new state at position"""
        if not self.entity:
            return None

        # Generate unique ID
        max_id = max([s.id for s in self.entity.states], default=0)
        new_id = max_id + 1

        state = StateDef(
            id=new_id,
            name=f"STATE_{new_id}",
            x=x,
            y=y
        )
        self.entity.states.append(state)

        item = StateItem(state)
        self.addItem(item)
        self.state_items[state.id] = item

        return item

    def delete_selected(self):
        """Delete selected items"""
        for item in self.selectedItems():
            if isinstance(item, StateItem):
                self._delete_state(item)
            elif isinstance(item, TransitionItem):
                self._delete_transition(item)

    def _delete_state(self, item: StateItem):
        # Remove connected transitions
        for trans in item.transitions_out + item.transitions_in:
            self._delete_transition(trans)

        # Remove from entity
        self.entity.states = [s for s in self.entity.states if s.id != item.state.id]
        del self.state_items[item.state.id]
        self.removeItem(item)

    def _delete_transition(self, item: TransitionItem):
        # Store references before removal
        from_state = item.from_state
        to_state = item.to_state

        # Remove from states
        if item in from_state.transitions_out:
            from_state.transitions_out.remove(item)
        if item in to_state.transitions_in:
            to_state.transitions_in.remove(item)

        # Remove rule from state
        for state in self.entity.states:
            state.rules = [r for r in state.rules if r is not item.rule]

        if item in self.transition_items:
            self.transition_items.remove(item)
        self.removeItem(item)

        # Update all remaining transitions between these states
        self._refresh_transitions_between(from_state, to_state)

    def set_transition_mode(self, enabled: bool):
        """Enable/disable transition creation mode"""
        self.transition_mode = enabled
        if not enabled:
            self._clear_transition_source()
        else:
            self.status_message.emit(tr("trans_hint"))

    def _clear_transition_source(self):
        """Clear the transition source selection"""
        if self.transition_source:
            # Reset visual highlight
            self.transition_source.setPen(QPen(QColor(70, 130, 180), 2))
            self.transition_source = None

    def _set_transition_source(self, state_item: StateItem):
        """Set the source state for transition"""
        self._clear_transition_source()
        self.transition_source = state_item
        # Highlight source state
        state_item.setPen(QPen(QColor(255, 165, 0), 3))  # Orange highlight
        self.status_message.emit(tr("trans_source", name=state_item.state.name))

    def _complete_transition(self, target_item: StateItem):
        """Complete the transition to target state"""
        if not self.transition_source or self.transition_source == target_item:
            return False

        # Create new rule
        rule = RuleDef(
            signal_id=0,
            signal_name="SIG_???",
            next_state=target_item.state.id,
            next_state_name=target_item.state.name
        )
        self.transition_source.state.rules.append(rule)

        # Create visual transition
        trans = TransitionItem(rule, self.transition_source, target_item)
        self.addItem(trans)
        self.transition_items.append(trans)

        # Update ALL transitions between these two states (both directions)
        self._refresh_transitions_between(self.transition_source, target_item)

        # Reset for next transition
        self._clear_transition_source()
        self.status_message.emit(tr("trans_done"))
        return True

    def _refresh_transitions_between(self, state_a: StateItem, state_b: StateItem):
        """Refresh all transitions between two states"""
        # A → B
        for trans in state_a.transitions_out:
            if trans.to_state == state_b:
                trans.update_position()
        # B → A
        for trans in state_b.transitions_out:
            if trans.to_state == state_a:
                trans.update_position()

    def cancel_transition_mode(self):
        """Cancel transition mode entirely"""
        self._clear_transition_source()
        self.transition_mode = False
        self.status_message.emit(tr("ready"))

    def mousePressEvent(self, event):
        if self.transition_mode and event.button() == Qt.LeftButton:
            # Find clicked item
            item = self.itemAt(event.scenePos(), QTransform())
            state_item = None

            if isinstance(item, StateItem):
                state_item = item
            elif isinstance(item, QGraphicsTextItem) and isinstance(item.parentItem(), StateItem):
                state_item = item.parentItem()

            if state_item:
                if self.transition_source is None:
                    # First click - select source
                    self._set_transition_source(state_item)
                else:
                    # Second click - complete transition
                    self._complete_transition(state_item)
                return  # Don't propagate event

        super().mousePressEvent(event)

    def keyPressEvent(self, event):
        if event.key() == Qt.Key_Escape and self.transition_mode:
            if self.transition_source:
                # Just clear source, stay in transition mode
                self._clear_transition_source()
                self.status_message.emit(tr("trans_cleared"))
            else:
                # Exit transition mode
                self.cancel_transition_mode()
            return
        super().keyPressEvent(event)

    def edit_state(self, item: StateItem):
        """Called when state is double-clicked"""
        self.state_selected.emit(item.state)


# =============================================================================
# Dialogs
# =============================================================================

class StateEditorDialog(QDialog):
    """Dialog for editing state properties"""

    def __init__(self, state: StateDef, entity: EntityDef, parent=None):
        super().__init__(parent)
        self.state = state
        self.entity = entity
        self.setWindowTitle(tr("dlg_edit_state", name=state.name))
        self.setMinimumWidth(500)

        layout = QVBoxLayout(self)

        # Basic properties
        form = QFormLayout()

        self.name_edit = QLineEdit(state.name)
        form.addRow(tr("dlg_name"), self.name_edit)

        self.id_spin = QSpinBox()
        self.id_spin.setRange(1, 255)
        self.id_spin.setValue(state.id)
        form.addRow(tr("dlg_id"), self.id_spin)

        self.parent_combo = QComboBox()
        self.parent_combo.addItem(tr("dlg_none"), 0)
        for s in entity.states:
            if s.id != state.id:
                self.parent_combo.addItem(s.name, s.id)
        idx = self.parent_combo.findData(state.parent_id)
        if idx >= 0:
            self.parent_combo.setCurrentIndex(idx)
        form.addRow(tr("dlg_parent"), self.parent_combo)

        self.entry_edit = QLineEdit(state.on_entry)
        self.entry_edit.setPlaceholderText("e.g., on_idle_entry")
        form.addRow(tr("dlg_on_entry"), self.entry_edit)

        self.exit_edit = QLineEdit(state.on_exit)
        self.exit_edit.setPlaceholderText("e.g., on_idle_exit")
        form.addRow(tr("dlg_on_exit"), self.exit_edit)

        layout.addLayout(form)

        # Rules table
        layout.addWidget(QLabel(tr("dlg_rules")))

        self.rules_table = QTableWidget()
        self.rules_table.setColumnCount(4)
        self.rules_table.setHorizontalHeaderLabels([tr("dlg_signal"), tr("dlg_next_state"), tr("dlg_action"), ""])
        self.rules_table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self._populate_rules()
        layout.addWidget(self.rules_table)

        # Add rule button
        add_rule_btn = QPushButton(tr("dlg_add_rule"))
        add_rule_btn.clicked.connect(self._add_rule)
        layout.addWidget(add_rule_btn)

        # Dialog buttons
        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

    def _populate_rules(self):
        self.rules_table.setRowCount(len(self.state.rules))
        for i, rule in enumerate(self.state.rules):
            self.rules_table.setItem(i, 0, QTableWidgetItem(rule.signal_name))

            state_combo = QComboBox()
            state_combo.addItem(tr("dlg_stay"), 0)
            for s in self.entity.states:
                state_combo.addItem(s.name, s.id)
            idx = state_combo.findData(rule.next_state)
            if idx >= 0:
                state_combo.setCurrentIndex(idx)
            self.rules_table.setCellWidget(i, 1, state_combo)

            self.rules_table.setItem(i, 2, QTableWidgetItem(rule.action_name))

            del_btn = QPushButton("×")
            del_btn.setMaximumWidth(30)
            del_btn.clicked.connect(lambda checked, row=i: self._delete_rule(row))
            self.rules_table.setCellWidget(i, 3, del_btn)

    def _add_rule(self):
        rule = RuleDef(
            signal_id=0,
            signal_name="SIG_???",
            next_state=0,
            next_state_name=""
        )
        self.state.rules.append(rule)
        self._populate_rules()

    def _delete_rule(self, row: int):
        if 0 <= row < len(self.state.rules):
            del self.state.rules[row]
            self._populate_rules()

    def accept(self):
        # Update state
        self.state.name = self.name_edit.text()
        self.state.id = self.id_spin.value()
        self.state.parent_id = self.parent_combo.currentData()
        self.state.on_entry = self.entry_edit.text()
        self.state.on_exit = self.exit_edit.text()

        # Update rules from table
        for i, rule in enumerate(self.state.rules):
            rule.signal_name = self.rules_table.item(i, 0).text()
            state_combo = self.rules_table.cellWidget(i, 1)
            rule.next_state = state_combo.currentData()
            rule.next_state_name = state_combo.currentText()
            rule.action_name = self.rules_table.item(i, 2).text() if self.rules_table.item(i, 2) else ""

        super().accept()


class NewEntityDialog(QDialog):
    """Dialog for creating new entity"""

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setWindowTitle(tr("dlg_new_entity"))

        layout = QFormLayout(self)

        self.name_edit = QLineEdit("MyEntity")
        layout.addRow(tr("dlg_entity_name"), self.name_edit)

        self.id_spin = QSpinBox()
        self.id_spin.setRange(1, 255)
        self.id_spin.setValue(1)
        layout.addRow(tr("dlg_entity_id"), self.id_spin)

        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout.addRow(buttons)

    def get_entity(self) -> EntityDef:
        return EntityDef(
            id=self.id_spin.value(),
            name=self.name_edit.text(),
            initial_state=1,
            states=[
                StateDef(id=1, name="STATE_IDLE", x=0, y=0)
            ]
        )


# =============================================================================
# Code Generator
# =============================================================================

class CodeGenerator:
    """Generates C code from entity definition"""

    @staticmethod
    def generate_header(entity: EntityDef) -> str:
        lines = [
            f"/**",
            f" * @file {entity.name.lower()}.h",
            f" * @brief {entity.name} entity definition",
            f" * @note Auto-generated by MicroReactor Studio",
            f" */",
            f"",
            f"#ifndef {entity.name.upper()}_H",
            f"#define {entity.name.upper()}_H",
            f"",
            f"#include \"ur_types.h\"",
            f"",
            f"/* Entity ID */",
            f"#define ID_{entity.name.upper()} {entity.id}",
            f"",
            f"/* State IDs */",
        ]

        for state in entity.states:
            lines.append(f"#define {state.name} {state.id}")

        lines.extend([
            f"",
            f"/* Entity declaration */",
            f"extern ur_entity_t {entity.name.lower()}_entity;",
            f"",
            f"/* Initialization */",
            f"ur_err_t {entity.name.lower()}_init(void);",
            f"",
            f"#endif /* {entity.name.upper()}_H */",
        ])

        return "\n".join(lines)

    @staticmethod
    def generate_source(entity: EntityDef) -> str:
        lines = [
            f"/**",
            f" * @file {entity.name.lower()}.c",
            f" * @brief {entity.name} entity implementation",
            f" * @note Auto-generated by MicroReactor Studio",
            f" */",
            f"",
            f"#include \"{entity.name.lower()}.h\"",
            f"#include \"ur_core.h\"",
            f"",
        ]

        # Generate action function prototypes
        actions = set()
        for state in entity.states:
            if state.on_entry:
                actions.add(state.on_entry)
            if state.on_exit:
                actions.add(state.on_exit)
            for rule in state.rules:
                if rule.action_name:
                    actions.add(rule.action_name)

        if actions:
            lines.append("/* Action function prototypes */")
            for action in sorted(actions):
                lines.append(f"static uint16_t {action}(ur_entity_t *ent, const ur_signal_t *sig);")
            lines.append("")

        # Generate rules for each state
        for state in entity.states:
            lines.append(f"/* Rules for {state.name} */")
            lines.append(f"static const ur_rule_t {state.name.lower()}_rules[] = {{")

            for rule in state.rules:
                action = rule.action_name if rule.action_name else "NULL"
                next_state = rule.next_state_name if rule.next_state else "0"
                lines.append(f"    UR_RULE({rule.signal_name}, {next_state}, {action}),")

            lines.append("    UR_RULE_END")
            lines.append("};")
            lines.append("")

        # Generate state definitions
        lines.append("/* State definitions */")
        lines.append(f"static const ur_state_def_t {entity.name.lower()}_states[] = {{")

        for state in entity.states:
            entry = state.on_entry if state.on_entry else "NULL"
            exit_fn = state.on_exit if state.on_exit else "NULL"
            parent = state.parent_id if state.parent_id else "0"
            lines.append(f"    UR_STATE({state.name}, {parent}, {entry}, {exit_fn}, {state.name.lower()}_rules),")

        lines.append("};")
        lines.append("")

        # Generate entity instance
        lines.extend([
            f"/* Entity instance */",
            f"ur_entity_t {entity.name.lower()}_entity;",
            f"",
            f"/* Initialization */",
            f"ur_err_t {entity.name.lower()}_init(void) {{",
            f"    ur_entity_config_t config = {{",
            f"        .id = ID_{entity.name.upper()},",
            f"        .name = \"{entity.name}\",",
            f"        .states = {entity.name.lower()}_states,",
            f"        .state_count = sizeof({entity.name.lower()}_states) / sizeof({entity.name.lower()}_states[0]),",
            f"        .initial_state = {entity.states[0].name if entity.states else '1'},",
            f"        .user_data = NULL,",
            f"    }};",
            f"    return ur_init(&{entity.name.lower()}_entity, &config);",
            f"}}",
            f"",
        ])

        # Generate action function stubs
        if actions:
            lines.append("/* Action function implementations */")
            for action in sorted(actions):
                lines.extend([
                    f"static uint16_t {action}(ur_entity_t *ent, const ur_signal_t *sig) {{",
                    f"    (void)ent;",
                    f"    (void)sig;",
                    f"    // TODO: Implement {action}",
                    f"    return 0;",
                    f"}}",
                    f"",
                ])

        return "\n".join(lines)


# =============================================================================
# Main Window
# =============================================================================

class ReactorStudio(QMainWindow):
    """Main window for MicroReactor Studio"""

    def __init__(self):
        super().__init__()
        self.setWindowTitle(tr("title"))
        self.setMinimumSize(1200, 800)

        self.project = Project()
        self.current_entity: Optional[EntityDef] = None
        self.current_file: Optional[str] = None

        self._create_actions()
        self._create_menus()
        self._create_toolbar()
        self._create_ui()
        self._create_statusbar()

    def _create_actions(self):
        # File actions
        self.new_action = QAction(tr("new_project"), self)
        self.new_action.setShortcut(QKeySequence.New)
        self.new_action.triggered.connect(self.new_project)

        self.open_action = QAction(tr("open_project"), self)
        self.open_action.setShortcut(QKeySequence.Open)
        self.open_action.triggered.connect(self.open_project)

        self.save_action = QAction(tr("save_project"), self)
        self.save_action.setShortcut(QKeySequence.Save)
        self.save_action.triggered.connect(self.save_project)

        self.save_as_action = QAction(tr("save_as"), self)
        self.save_as_action.setShortcut(QKeySequence.SaveAs)
        self.save_as_action.triggered.connect(self.save_project_as)

        self.export_action = QAction(tr("export_code"), self)
        self.export_action.setShortcut("Ctrl+E")
        self.export_action.triggered.connect(self.export_code)

        # Edit actions
        self.add_state_action = QAction(tr("add_state"), self)
        self.add_state_action.setShortcut("S")
        self.add_state_action.triggered.connect(self.add_state)

        self.add_transition_action = QAction(tr("add_transition"), self)
        self.add_transition_action.setShortcut("T")
        self.add_transition_action.setCheckable(True)
        self.add_transition_action.triggered.connect(self.toggle_transition_mode)

        self.delete_action = QAction(tr("delete"), self)
        self.delete_action.setShortcut(QKeySequence.Delete)
        self.delete_action.triggered.connect(self.delete_selected)

        # Entity actions
        self.new_entity_action = QAction(tr("new_entity"), self)
        self.new_entity_action.triggered.connect(self.new_entity)

        # Language actions
        self.lang_zh_action = QAction(tr("lang_zh"), self)
        self.lang_zh_action.triggered.connect(lambda: self._set_language("zh"))
        self.lang_en_action = QAction(tr("lang_en"), self)
        self.lang_en_action.triggered.connect(lambda: self._set_language("en"))

    def _create_menus(self):
        menubar = self.menuBar()

        # File menu
        file_menu = menubar.addMenu(tr("file"))
        file_menu.addAction(self.new_action)
        file_menu.addAction(self.open_action)
        file_menu.addAction(self.save_action)
        file_menu.addAction(self.save_as_action)
        file_menu.addSeparator()
        file_menu.addAction(self.export_action)
        file_menu.addSeparator()
        file_menu.addAction(tr("exit"), self.close)

        # Edit menu
        edit_menu = menubar.addMenu(tr("edit"))
        edit_menu.addAction(self.add_state_action)
        edit_menu.addAction(self.add_transition_action)
        edit_menu.addSeparator()
        edit_menu.addAction(self.delete_action)

        # Entity menu
        entity_menu = menubar.addMenu(tr("entity"))
        entity_menu.addAction(self.new_entity_action)

        # Language menu
        lang_menu = menubar.addMenu(tr("language"))
        lang_menu.addAction(self.lang_zh_action)
        lang_menu.addAction(self.lang_en_action)

    def _create_toolbar(self):
        toolbar = QToolBar()
        self.addToolBar(toolbar)

        toolbar.addAction(self.new_action)
        toolbar.addAction(self.open_action)
        toolbar.addAction(self.save_action)
        toolbar.addSeparator()
        toolbar.addAction(self.add_state_action)
        toolbar.addAction(self.add_transition_action)
        toolbar.addSeparator()
        toolbar.addAction(self.export_action)

    def _create_ui(self):
        # Central widget with splitter
        splitter = QSplitter()
        self.setCentralWidget(splitter)

        # Left panel - Entity tree
        left_panel = QWidget()
        left_layout = QVBoxLayout(left_panel)
        left_layout.setContentsMargins(5, 5, 5, 5)

        self.entities_label = QLabel(tr("entities"))
        left_layout.addWidget(self.entities_label)
        self.entity_tree = QTreeWidget()
        self.entity_tree.setHeaderHidden(True)
        self.entity_tree.itemClicked.connect(self._on_entity_selected)
        left_layout.addWidget(self.entity_tree)

        self.new_entity_btn = QPushButton(tr("btn_new_entity"))
        self.new_entity_btn.clicked.connect(self.new_entity)
        left_layout.addWidget(self.new_entity_btn)

        splitter.addWidget(left_panel)

        # Center - Graphics view
        self.scene = StateMachineScene()
        self.scene.state_selected.connect(self._on_state_selected)
        self.scene.status_message.connect(self._on_status_message)

        self.view = QGraphicsView(self.scene)
        self.view.setRenderHint(QPainter.Antialiasing)
        self.view.setDragMode(QGraphicsView.RubberBandDrag)
        self.view.setViewportUpdateMode(QGraphicsView.FullViewportUpdate)
        splitter.addWidget(self.view)

        # Right panel - Properties
        right_panel = QWidget()
        right_layout = QVBoxLayout(right_panel)
        right_layout.setContentsMargins(5, 5, 5, 5)

        self.properties_label = QLabel(tr("properties"))
        right_layout.addWidget(self.properties_label)
        self.properties_text = QTextEdit()
        self.properties_text.setReadOnly(True)
        right_layout.addWidget(self.properties_text)

        # Code preview
        self.code_preview_label = QLabel(tr("code_preview"))
        right_layout.addWidget(self.code_preview_label)
        self.code_preview = QTextEdit()
        self.code_preview.setReadOnly(True)
        self.code_preview.setFontFamily("Consolas")
        right_layout.addWidget(self.code_preview)

        splitter.addWidget(right_panel)

        # Set splitter sizes
        splitter.setSizes([200, 700, 300])

    def _create_statusbar(self):
        self.statusBar().showMessage(tr("ready"))

    def _set_language(self, lang: str):
        """Switch UI language and refresh"""
        set_lang(lang)
        QMessageBox.information(self, "Language",
            "语言已切换，重启后生效。\nLanguage changed. Restart to apply.")

    def _update_entity_tree(self):
        self.entity_tree.clear()
        for entity in self.project.entities:
            item = QTreeWidgetItem([entity.name])
            item.setData(0, Qt.UserRole, entity)

            for state in entity.states:
                state_item = QTreeWidgetItem([state.name])
                state_item.setData(0, Qt.UserRole, state)
                item.addChild(state_item)

            self.entity_tree.addTopLevelItem(item)
            item.setExpanded(True)

    def _on_entity_selected(self, item: QTreeWidgetItem, column: int):
        data = item.data(0, Qt.UserRole)
        if isinstance(data, EntityDef):
            self.current_entity = data
            self.scene.load_entity(data)
            self._update_code_preview()
        elif isinstance(data, StateDef):
            # Find parent entity
            parent = item.parent()
            if parent:
                entity = parent.data(0, Qt.UserRole)
                if isinstance(entity, EntityDef):
                    self.current_entity = entity
                    self.scene.load_entity(entity)
                    self._update_code_preview()

    def _on_state_selected(self, state: StateDef):
        if self.current_entity:
            dialog = StateEditorDialog(state, self.current_entity, self)
            if dialog.exec():
                self.scene.load_entity(self.current_entity)
                self._update_entity_tree()
                self._update_code_preview()

    def _on_status_message(self, message: str):
        """Handle status messages from the scene"""
        self.statusBar().showMessage(message)
        # Also update the toggle button state if transition mode was cancelled
        if message == tr("ready") and self.add_transition_action.isChecked():
            self.add_transition_action.setChecked(False)

    def _update_code_preview(self):
        if self.current_entity:
            code = CodeGenerator.generate_source(self.current_entity)
            self.code_preview.setPlainText(code)

    # Actions
    def new_project(self):
        self.project = Project()
        self.current_entity = None
        self.current_file = None
        self.scene.clear()
        self._update_entity_tree()
        self.code_preview.clear()
        self.setWindowTitle(tr("title") + " - " + tr("new_project"))

    def open_project(self):
        filename, _ = QFileDialog.getOpenFileName(
            self, tr("open_project"), "", "MicroReactor Project (*.mrp);;All Files (*)"
        )
        if filename:
            try:
                with open(filename, 'r') as f:
                    data = json.load(f)

                self.project = Project(
                    name=data.get('name', 'Untitled'),
                    version=data.get('version', '1.0')
                )

                for ent_data in data.get('entities', []):
                    entity = EntityDef(
                        id=ent_data['id'],
                        name=ent_data['name'],
                        initial_state=ent_data.get('initial_state', 1)
                    )

                    for state_data in ent_data.get('states', []):
                        state = StateDef(
                            id=state_data['id'],
                            name=state_data['name'],
                            parent_id=state_data.get('parent_id', 0),
                            on_entry=state_data.get('on_entry', ''),
                            on_exit=state_data.get('on_exit', ''),
                            x=state_data.get('x', 0),
                            y=state_data.get('y', 0)
                        )

                        for rule_data in state_data.get('rules', []):
                            rule = RuleDef(
                                signal_id=rule_data.get('signal_id', 0),
                                signal_name=rule_data.get('signal_name', ''),
                                next_state=rule_data.get('next_state', 0),
                                next_state_name=rule_data.get('next_state_name', ''),
                                action_name=rule_data.get('action_name', '')
                            )
                            state.rules.append(rule)

                        entity.states.append(state)

                    self.project.entities.append(entity)

                self.current_file = filename
                self._update_entity_tree()
                self.setWindowTitle(f"{tr('title')} - {Path(filename).name}")
                self.statusBar().showMessage(tr("msg_opened", path=filename))

            except Exception as e:
                QMessageBox.critical(self, tr("error"), tr("msg_open_failed", e=e))

    def save_project(self):
        if self.current_file:
            self._save_to_file(self.current_file)
        else:
            self.save_project_as()

    def save_project_as(self):
        filename, _ = QFileDialog.getSaveFileName(
            self, tr("save_as"), "", "MicroReactor Project (*.mrp);;All Files (*)"
        )
        if filename:
            if not filename.endswith('.mrp'):
                filename += '.mrp'
            self._save_to_file(filename)
            self.current_file = filename
            self.setWindowTitle(f"{tr('title')} - {Path(filename).name}")

    def _save_to_file(self, filename: str):
        try:
            data = {
                'name': self.project.name,
                'version': self.project.version,
                'entities': []
            }

            for entity in self.project.entities:
                ent_data = {
                    'id': entity.id,
                    'name': entity.name,
                    'initial_state': entity.initial_state,
                    'states': []
                }

                for state in entity.states:
                    state_data = {
                        'id': state.id,
                        'name': state.name,
                        'parent_id': state.parent_id,
                        'on_entry': state.on_entry,
                        'on_exit': state.on_exit,
                        'x': state.x,
                        'y': state.y,
                        'rules': []
                    }

                    for rule in state.rules:
                        rule_data = {
                            'signal_id': rule.signal_id,
                            'signal_name': rule.signal_name,
                            'next_state': rule.next_state,
                            'next_state_name': rule.next_state_name,
                            'action_name': rule.action_name
                        }
                        state_data['rules'].append(rule_data)

                    ent_data['states'].append(state_data)

                data['entities'].append(ent_data)

            with open(filename, 'w') as f:
                json.dump(data, f, indent=2)

            self.statusBar().showMessage(tr("msg_saved", path=filename))

        except Exception as e:
            QMessageBox.critical(self, tr("error"), tr("msg_save_failed", e=e))

    def export_code(self):
        if not self.current_entity:
            QMessageBox.warning(self, tr("warning"), tr("msg_no_entity"))
            return

        directory = QFileDialog.getExistingDirectory(self, tr("export_code"))
        if directory:
            try:
                # Generate header
                header = CodeGenerator.generate_header(self.current_entity)
                header_path = Path(directory) / f"{self.current_entity.name.lower()}.h"
                with open(header_path, 'w') as f:
                    f.write(header)

                # Generate source
                source = CodeGenerator.generate_source(self.current_entity)
                source_path = Path(directory) / f"{self.current_entity.name.lower()}.c"
                with open(source_path, 'w') as f:
                    f.write(source)

                QMessageBox.information(
                    self, tr("msg_export_done"),
                    tr("msg_export_files", h=header_path, c=source_path)
                )

            except Exception as e:
                QMessageBox.critical(self, tr("error"), tr("msg_export_failed", e=e))

    def new_entity(self):
        dialog = NewEntityDialog(self)
        if dialog.exec():
            entity = dialog.get_entity()
            self.project.entities.append(entity)
            self._update_entity_tree()
            self.current_entity = entity
            self.scene.load_entity(entity)

    def add_state(self):
        if self.current_entity:
            # Add at center of view
            center = self.view.mapToScene(self.view.viewport().rect().center())
            self.scene.add_state(center.x(), center.y())
            self._update_entity_tree()
            self._update_code_preview()

    def toggle_transition_mode(self, checked: bool):
        """Toggle transition creation mode (two-click method)"""
        self.scene.set_transition_mode(checked)
        if checked:
            # Disable rubber band selection in transition mode
            self.view.setDragMode(QGraphicsView.NoDrag)
        else:
            self.view.setDragMode(QGraphicsView.RubberBandDrag)
            self.statusBar().showMessage(tr("ready"))

    def _transition_mouse_press(self, event):
        # This method is no longer used - transition handling is now in the scene
        pass

    def delete_selected(self):
        self.scene.delete_selected()
        self._update_entity_tree()
        self._update_code_preview()


# =============================================================================
# Main
# =============================================================================

def main():
    app = QApplication(sys.argv)
    app.setStyle('Fusion')

    window = ReactorStudio()
    window.show()

    sys.exit(app.exec())


if __name__ == '__main__':
    main()
