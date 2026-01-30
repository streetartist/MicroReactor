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
- Signal and rule editing
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
    QTableWidget, QTableWidgetItem, QHeaderView, QMenu,
    QStatusBar, QToolButton
)
from PySide6.QtCore import Qt, QPointF, QRectF, QLineF, Signal, QTimer
from PySide6.QtGui import (
    QAction, QPainter, QPen, QBrush, QColor, QFont,
    QPainterPath, QPolygonF, QKeySequence, QIcon, QTransform
)


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

    def update_position(self):
        """Update arrow path based on state positions"""
        p1 = self.from_state.scenePos()
        p2 = self.to_state.scenePos()

        # Calculate direction
        line = QLineF(p1, p2)
        length = line.length()

        if length < 1:
            return

        # Shorten line to end at state edges
        unit = (p2 - p1) / length
        start = p1 + unit * 50  # State radius
        end = p2 - unit * 50

        # Create path
        path = QPainterPath()
        path.moveTo(start)
        path.lineTo(end)

        # Arrow head
        angle = math.atan2(-(p2.y() - p1.y()), p2.x() - p1.x())
        arrow_p1 = end + QPointF(
            math.sin(angle - math.pi / 3) * self.arrow_size,
            math.cos(angle - math.pi / 3) * self.arrow_size
        )
        arrow_p2 = end + QPointF(
            math.sin(angle - math.pi + math.pi / 3) * self.arrow_size,
            math.cos(angle - math.pi + math.pi / 3) * self.arrow_size
        )

        path.moveTo(end)
        path.lineTo(arrow_p1)
        path.moveTo(end)
        path.lineTo(arrow_p2)

        self.setPath(path)

        # Position label at midpoint
        mid = (start + end) / 2
        self.label.setPos(mid.x(), mid.y() - 15)

    def set_signal_name(self, name: str):
        self.rule.signal_name = name
        self.label.setPlainText(name)


# =============================================================================
# Scene
# =============================================================================

class StateMachineScene(QGraphicsScene):
    """Graphics scene for state machine editing"""

    state_selected = Signal(StateDef)
    transition_selected = Signal(RuleDef)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setSceneRect(-2000, -2000, 4000, 4000)
        self.entity: Optional[EntityDef] = None
        self.state_items: Dict[int, StateItem] = {}
        self.transition_items: List[TransitionItem] = []

        # Drawing mode
        self.drawing_transition = False
        self.transition_start: Optional[StateItem] = None
        self.temp_line: Optional[QGraphicsLineItem] = None

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
        # Remove from states
        if item in item.from_state.transitions_out:
            item.from_state.transitions_out.remove(item)
        if item in item.to_state.transitions_in:
            item.to_state.transitions_in.remove(item)

        # Remove rule from state
        for state in self.entity.states:
            state.rules = [r for r in state.rules if r is not item.rule]

        if item in self.transition_items:
            self.transition_items.remove(item)
        self.removeItem(item)

    def start_transition_draw(self, from_state: StateItem):
        """Start drawing a transition"""
        self.drawing_transition = True
        self.transition_start = from_state
        self.temp_line = QGraphicsLineItem()
        self.temp_line.setPen(QPen(QColor(150, 150, 150), 2, Qt.DashLine))
        self.addItem(self.temp_line)

    def finish_transition_draw(self, to_state: StateItem):
        """Finish drawing a transition"""
        if self.temp_line:
            self.removeItem(self.temp_line)
            self.temp_line = None

        if self.transition_start and to_state and self.transition_start != to_state:
            # Create new rule
            rule = RuleDef(
                signal_id=0,
                signal_name="SIG_???",
                next_state=to_state.state.id,
                next_state_name=to_state.state.name
            )
            self.transition_start.state.rules.append(rule)

            # Create visual
            trans = TransitionItem(rule, self.transition_start, to_state)
            self.addItem(trans)
            self.transition_items.append(trans)

        self.drawing_transition = False
        self.transition_start = None

    def cancel_transition_draw(self):
        """Cancel transition drawing"""
        if self.temp_line:
            self.removeItem(self.temp_line)
            self.temp_line = None
        self.drawing_transition = False
        self.transition_start = None

    def mouseMoveEvent(self, event):
        if self.drawing_transition and self.temp_line and self.transition_start:
            start = self.transition_start.scenePos()
            end = event.scenePos()
            self.temp_line.setLine(QLineF(start, end))
        super().mouseMoveEvent(event)

    def mouseReleaseEvent(self, event):
        if self.drawing_transition:
            # Check if released on a state
            item = self.itemAt(event.scenePos(), QTransform())
            if isinstance(item, StateItem):
                self.finish_transition_draw(item)
            elif isinstance(item, QGraphicsTextItem) and isinstance(item.parentItem(), StateItem):
                self.finish_transition_draw(item.parentItem())
            else:
                self.cancel_transition_draw()
        super().mouseReleaseEvent(event)

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
        self.setWindowTitle(f"Edit State: {state.name}")
        self.setMinimumWidth(500)

        layout = QVBoxLayout(self)

        # Basic properties
        form = QFormLayout()

        self.name_edit = QLineEdit(state.name)
        form.addRow("Name:", self.name_edit)

        self.id_spin = QSpinBox()
        self.id_spin.setRange(1, 255)
        self.id_spin.setValue(state.id)
        form.addRow("ID:", self.id_spin)

        self.parent_combo = QComboBox()
        self.parent_combo.addItem("(None)", 0)
        for s in entity.states:
            if s.id != state.id:
                self.parent_combo.addItem(s.name, s.id)
        idx = self.parent_combo.findData(state.parent_id)
        if idx >= 0:
            self.parent_combo.setCurrentIndex(idx)
        form.addRow("Parent State:", self.parent_combo)

        self.entry_edit = QLineEdit(state.on_entry)
        self.entry_edit.setPlaceholderText("Function name (e.g., on_idle_entry)")
        form.addRow("On Entry:", self.entry_edit)

        self.exit_edit = QLineEdit(state.on_exit)
        self.exit_edit.setPlaceholderText("Function name (e.g., on_idle_exit)")
        form.addRow("On Exit:", self.exit_edit)

        layout.addLayout(form)

        # Rules table
        layout.addWidget(QLabel("Transition Rules:"))

        self.rules_table = QTableWidget()
        self.rules_table.setColumnCount(4)
        self.rules_table.setHorizontalHeaderLabels(["Signal", "Next State", "Action", ""])
        self.rules_table.horizontalHeader().setSectionResizeMode(QHeaderView.Stretch)
        self._populate_rules()
        layout.addWidget(self.rules_table)

        # Add rule button
        add_rule_btn = QPushButton("Add Rule")
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
            state_combo.addItem("(Stay)", 0)
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
        self.setWindowTitle("New Entity")

        layout = QFormLayout(self)

        self.name_edit = QLineEdit("MyEntity")
        layout.addRow("Entity Name:", self.name_edit)

        self.id_spin = QSpinBox()
        self.id_spin.setRange(1, 255)
        self.id_spin.setValue(1)
        layout.addRow("Entity ID:", self.id_spin)

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
        self.setWindowTitle("MicroReactor Studio")
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
        self.new_action = QAction("New Project", self)
        self.new_action.setShortcut(QKeySequence.New)
        self.new_action.triggered.connect(self.new_project)

        self.open_action = QAction("Open Project...", self)
        self.open_action.setShortcut(QKeySequence.Open)
        self.open_action.triggered.connect(self.open_project)

        self.save_action = QAction("Save Project", self)
        self.save_action.setShortcut(QKeySequence.Save)
        self.save_action.triggered.connect(self.save_project)

        self.save_as_action = QAction("Save Project As...", self)
        self.save_as_action.setShortcut(QKeySequence.SaveAs)
        self.save_as_action.triggered.connect(self.save_project_as)

        self.export_action = QAction("Export C Code...", self)
        self.export_action.setShortcut("Ctrl+E")
        self.export_action.triggered.connect(self.export_code)

        # Edit actions
        self.add_state_action = QAction("Add State", self)
        self.add_state_action.setShortcut("S")
        self.add_state_action.triggered.connect(self.add_state)

        self.add_transition_action = QAction("Add Transition", self)
        self.add_transition_action.setShortcut("T")
        self.add_transition_action.setCheckable(True)
        self.add_transition_action.triggered.connect(self.toggle_transition_mode)

        self.delete_action = QAction("Delete", self)
        self.delete_action.setShortcut(QKeySequence.Delete)
        self.delete_action.triggered.connect(self.delete_selected)

        # Entity actions
        self.new_entity_action = QAction("New Entity", self)
        self.new_entity_action.triggered.connect(self.new_entity)

    def _create_menus(self):
        menubar = self.menuBar()

        # File menu
        file_menu = menubar.addMenu("File")
        file_menu.addAction(self.new_action)
        file_menu.addAction(self.open_action)
        file_menu.addAction(self.save_action)
        file_menu.addAction(self.save_as_action)
        file_menu.addSeparator()
        file_menu.addAction(self.export_action)
        file_menu.addSeparator()
        file_menu.addAction("Exit", self.close)

        # Edit menu
        edit_menu = menubar.addMenu("Edit")
        edit_menu.addAction(self.add_state_action)
        edit_menu.addAction(self.add_transition_action)
        edit_menu.addSeparator()
        edit_menu.addAction(self.delete_action)

        # Entity menu
        entity_menu = menubar.addMenu("Entity")
        entity_menu.addAction(self.new_entity_action)

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

        left_layout.addWidget(QLabel("Entities:"))
        self.entity_tree = QTreeWidget()
        self.entity_tree.setHeaderHidden(True)
        self.entity_tree.itemClicked.connect(self._on_entity_selected)
        left_layout.addWidget(self.entity_tree)

        new_entity_btn = QPushButton("+ New Entity")
        new_entity_btn.clicked.connect(self.new_entity)
        left_layout.addWidget(new_entity_btn)

        splitter.addWidget(left_panel)

        # Center - Graphics view
        self.scene = StateMachineScene()
        self.scene.state_selected.connect(self._on_state_selected)

        self.view = QGraphicsView(self.scene)
        self.view.setRenderHint(QPainter.Antialiasing)
        self.view.setDragMode(QGraphicsView.RubberBandDrag)
        self.view.setViewportUpdateMode(QGraphicsView.FullViewportUpdate)
        splitter.addWidget(self.view)

        # Right panel - Properties
        right_panel = QWidget()
        right_layout = QVBoxLayout(right_panel)
        right_layout.setContentsMargins(5, 5, 5, 5)

        right_layout.addWidget(QLabel("Properties:"))
        self.properties_text = QTextEdit()
        self.properties_text.setReadOnly(True)
        right_layout.addWidget(self.properties_text)

        # Code preview
        right_layout.addWidget(QLabel("Code Preview:"))
        self.code_preview = QTextEdit()
        self.code_preview.setReadOnly(True)
        self.code_preview.setFontFamily("Consolas")
        right_layout.addWidget(self.code_preview)

        splitter.addWidget(right_panel)

        # Set splitter sizes
        splitter.setSizes([200, 700, 300])

    def _create_statusbar(self):
        self.statusBar().showMessage("Ready")

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
        self.setWindowTitle("MicroReactor Studio - New Project")

    def open_project(self):
        filename, _ = QFileDialog.getOpenFileName(
            self, "Open Project", "", "MicroReactor Project (*.mrp);;All Files (*)"
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
                self.setWindowTitle(f"MicroReactor Studio - {Path(filename).name}")
                self.statusBar().showMessage(f"Opened {filename}")

            except Exception as e:
                QMessageBox.critical(self, "Error", f"Failed to open project: {e}")

    def save_project(self):
        if self.current_file:
            self._save_to_file(self.current_file)
        else:
            self.save_project_as()

    def save_project_as(self):
        filename, _ = QFileDialog.getSaveFileName(
            self, "Save Project", "", "MicroReactor Project (*.mrp);;All Files (*)"
        )
        if filename:
            if not filename.endswith('.mrp'):
                filename += '.mrp'
            self._save_to_file(filename)
            self.current_file = filename
            self.setWindowTitle(f"MicroReactor Studio - {Path(filename).name}")

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

            self.statusBar().showMessage(f"Saved to {filename}")

        except Exception as e:
            QMessageBox.critical(self, "Error", f"Failed to save project: {e}")

    def export_code(self):
        if not self.current_entity:
            QMessageBox.warning(self, "Warning", "No entity selected")
            return

        directory = QFileDialog.getExistingDirectory(self, "Export Directory")
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
                    self, "Export Complete",
                    f"Generated:\n{header_path}\n{source_path}"
                )

            except Exception as e:
                QMessageBox.critical(self, "Error", f"Failed to export: {e}")

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
        if checked:
            self.view.setDragMode(QGraphicsView.NoDrag)
            self.statusBar().showMessage("Click and drag from source state to target state")
            # Connect mouse press
            self.scene.mousePressEvent = self._transition_mouse_press
        else:
            self.view.setDragMode(QGraphicsView.RubberBandDrag)
            self.statusBar().showMessage("Ready")

    def _transition_mouse_press(self, event):
        item = self.scene.itemAt(event.scenePos(), QTransform())
        if isinstance(item, StateItem):
            self.scene.start_transition_draw(item)
        elif isinstance(item, QGraphicsTextItem) and isinstance(item.parentItem(), StateItem):
            self.scene.start_transition_draw(item.parentItem())
        QGraphicsScene.mousePressEvent(self.scene, event)

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
