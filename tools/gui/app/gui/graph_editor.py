"""Node-graph setup editor: draggable chiplet nodes, port-to-port connections,
and a property panel that edits the selected chiplet or connection."""

from __future__ import annotations

from typing import Any, Dict, List, Optional

from PySide6.QtCore import Qt
from PySide6.QtGui import QPainter
from PySide6.QtWidgets import (QComboBox, QDialog, QDialogButtonBox,
                               QFormLayout, QGraphicsView, QGroupBox,
                               QHBoxLayout, QInputDialog, QLabel, QLineEdit,
                               QListWidget, QMessageBox, QPushButton,
                               QScrollArea, QSplitter, QVBoxLayout, QWidget)

from .. import setup_doc
from ..project import Project
from ..type_catalog import list_types, type_params
from .config_form import ConfigForm
from .graph_items import ChipletNode, ConnectionEdge
from .graph_scene import GraphScene


class GraphView(QGraphicsView):
    """Graph canvas with wheel zoom, rubber-band multi-select, and Delete."""

    _ZOOM_STEP = 1.15
    _MIN_SCALE = 0.25
    _MAX_SCALE = 4.0

    def __init__(self, scene: GraphScene, on_delete, parent=None):
        super().__init__(scene, parent)
        self._on_delete = on_delete
        self._scale = 1.0
        self.setRenderHint(QPainter.Antialiasing)
        self.setDragMode(QGraphicsView.RubberBandDrag)
        self.setTransformationAnchor(QGraphicsView.AnchorUnderMouse)
        self.setFocusPolicy(Qt.StrongFocus)

    def wheelEvent(self, event):
        delta = event.angleDelta().y()
        if delta == 0:
            return
        factor = self._ZOOM_STEP if delta > 0 else 1.0 / self._ZOOM_STEP
        target = self._scale * factor
        if target < self._MIN_SCALE or target > self._MAX_SCALE:
            return
        self._scale = target
        self.scale(factor, factor)

    def keyPressEvent(self, event):
        if event.key() in (Qt.Key_Delete, Qt.Key_Backspace):
            self._on_delete()
            event.accept()
            return
        super().keyPressEvent(event)


class GraphEditor(QWidget):
    def __init__(self, project: Project, doc: Dict[str, Any], layout: Optional[Dict] = None, parent=None):
        super().__init__(parent)
        self.project = project
        self.doc = doc
        self._updating = False
        self._panel: Optional[QWidget] = None

        self._chiplet_types = list_types(project.configs_dir, "chiplets")
        self._accel_types = list_types(project.configs_dir, "accelerators")
        self._ic_types = list_types(project.configs_dir, "interconnects")

        self.scene = GraphScene(doc, layout)
        self.scene.node_selected.connect(self._show_chiplet_panel)
        self.scene.connection_selected.connect(self._show_connection_panel)
        self.scene.selection_empty.connect(self._show_empty)
        self.scene.connection_rejected.connect(self._on_rejected)

        view = GraphView(self.scene, self._delete_selected)

        self._chiplet_type = QComboBox()
        self._chiplet_type.addItems(self._chiplet_types)
        add_btn = QPushButton("Add")
        delete_btn = QPushButton("Delete")
        add_btn.clicked.connect(self._add_chiplet)
        delete_btn.clicked.connect(self._delete_selected)
        self._status = QLabel("")

        toolbar = QHBoxLayout()
        toolbar.addWidget(QLabel("Chiplet"))
        toolbar.addWidget(self._chiplet_type)
        toolbar.addWidget(add_btn)
        toolbar.addWidget(delete_btn)
        toolbar.addStretch(1)
        toolbar.addWidget(self._status)

        left = QWidget()
        left_layout = QVBoxLayout(left)
        left_layout.addLayout(toolbar)
        left_layout.addWidget(view, 1)

        self._panel_host = QScrollArea()
        self._panel_host.setWidgetResizable(True)
        self._panel_host.setMinimumWidth(320)

        splitter = QSplitter(Qt.Horizontal)
        splitter.addWidget(left)
        splitter.addWidget(self._panel_host)
        splitter.setSizes([720, 340])

        outer = QVBoxLayout(self)
        outer.addWidget(splitter)

        self._show_empty()

    # -- layout / renames -------------------------------------------------

    def layout_positions(self) -> Dict[str, List[float]]:
        return self.scene.capture_layout()

    def commit(self) -> None:
        self._commit_current()

    def _on_rejected(self, message: str) -> None:
        QMessageBox.information(self, "Connection not allowed", message)

    # -- toolbar actions --------------------------------------------------

    def _add_chiplet(self) -> None:
        self._commit_current()
        self._status.setText("")
        type_name = self._chiplet_type.currentText()
        name = setup_doc.add_chiplet(self.doc, type_name)
        self._refresh()
        self.scene.select_node(name)

    def _delete_selected(self) -> None:
        items = self.scene.selectedItems()
        if not items:
            return
        self._status.setText("")
        chiplet_names = [i.name for i in items if isinstance(i, ChipletNode)]
        edge_endpoints = [i.endpoints for i in items if isinstance(i, ConnectionEdge)]

        # Remove connections first (by matching endpoints, since indices shift as
        # the list shrinks); removing a chiplet then also drops any it still touches.
        for endpoints in edge_endpoints:
            index = self.scene._connection_index(endpoints)
            if index >= 0:
                setup_doc.remove_connection(self.doc, index)
        for name in chiplet_names:
            setup_doc.remove_chiplet(self.doc, name)

        self._refresh()
        self._show_empty()

    def _refresh(self) -> None:
        self._updating = True
        self.scene.rebuild()
        self._updating = False

    # -- panels -----------------------------------------------------------

    def _set_panel(self, widget: QWidget) -> None:
        # Release (do not delete) the previous panel, install the new one, then
        # defer deletion. A panel action (rename, type change, add/remove) is
        # emitted from a widget inside the old panel; deleting it synchronously
        # would destroy the sender mid-signal and crash.
        old = self._panel_host.takeWidget()
        self._panel = widget
        self._panel_host.setWidget(widget)
        if old is not None:
            old.deleteLater()

    def _commit_current(self) -> None:
        if self._panel is not None and hasattr(self._panel, "commit"):
            self._panel.commit()

    def _show_empty(self) -> None:
        if self._updating:
            return
        self._commit_current()
        label = QLabel(
            "Select a chiplet or connection to edit its properties. "
            "Use the toolbar to add or remove chiplets.\n\n"
            "To create a connection, drag from one interconnect port to another."
        )
        label.setWordWrap(True)
        label.setAlignment(Qt.AlignTop)
        label.setContentsMargins(12, 12, 12, 12)
        self._set_panel(label)

    def _show_chiplet_panel(self, name: str) -> None:
        if self._updating:
            return
        self._commit_current()
        self._set_panel(ChipletPanel(self, name))

    def _show_connection_panel(self, index: int) -> None:
        if self._updating:
            return
        self._commit_current()
        self._set_panel(ConnectionPanel(self, index))


class SubConfigDialog(QDialog):
    """Edit the config of an accelerator or interconnect sub-instance."""

    def __init__(self, project: Project, kind: str, type_name: str, config: Dict[str, Any], title: str, parent=None):
        super().__init__(parent)
        self.setWindowTitle(title)
        self.resize(420, 480)
        self._form = ConfigForm(type_params(project.configs_dir, kind, type_name))
        self._form.set_config(config or {})

        box = QGroupBox(f"{type_name} config (blank = default)")
        box_layout = QVBoxLayout(box)
        box_layout.addWidget(self._form)

        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)

        layout = QVBoxLayout(self)
        layout.addWidget(box, 1)
        layout.addWidget(buttons)

    def result_config(self) -> Dict[str, Any]:
        return self._form.config()


class ChipletPanel(QWidget):
    def __init__(self, editor: GraphEditor, name: str):
        super().__init__()
        self.editor = editor
        self.name = name
        self.doc = editor.doc
        chiplet = setup_doc.chiplet_by_name(self.doc, name) or {}

        self._name = QLineEdit(name)
        self._name.editingFinished.connect(self._rename)

        self._type = QComboBox()
        self._type.addItems(editor._chiplet_types)
        if chiplet.get("type") in editor._chiplet_types:
            self._type.setCurrentText(chiplet["type"])
        self._type.currentTextChanged.connect(self._change_type)

        header = QFormLayout()
        header.addRow("Name", self._name)
        header.addRow("Type", self._type)

        params = type_params(editor.project.configs_dir, "chiplets", chiplet.get("type", ""))
        self._config = ConfigForm(params)
        self._config.set_config(chiplet.get("config", {}))
        config_box = QGroupBox("Config (blank = default)")
        cbl = QVBoxLayout(config_box)
        cbl.addWidget(self._config)

        accel_box = self._build_list_box(
            "Accelerators", editor._accel_types,
            [f"{a.get('name')} [{a.get('type')}]" for a in chiplet.get("accelerators", []) or []],
            self._add_accel, self._remove_accel, self._edit_accel, self._rename_accel,
        )
        ic_box = self._build_list_box(
            "Interconnects", editor._ic_types,
            [f"{i.get('name')} [{i.get('type')}]" for i in chiplet.get("interconnects", []) or []],
            self._add_ic, self._remove_ic, self._edit_ic, self._rename_ic,
        )

        layout = QVBoxLayout(self)
        layout.addLayout(header)
        layout.addWidget(config_box)
        layout.addWidget(accel_box)
        layout.addWidget(ic_box)
        layout.addStretch(1)

    def _build_list_box(self, title, types, items, on_add, on_remove, on_edit, on_rename) -> QGroupBox:
        box = QGroupBox(title)
        layout = QVBoxLayout(box)
        listw = QListWidget()
        listw.addItems(items)
        listw.itemDoubleClicked.connect(lambda _i: on_edit(listw))
        layout.addWidget(listw)
        combo = QComboBox()
        combo.addItems(types)
        add = QPushButton("Add")
        edit = QPushButton("Edit config...")
        rename = QPushButton("Rename...")
        remove = QPushButton("Remove")
        add.clicked.connect(lambda: on_add(combo.currentText()))
        edit.clicked.connect(lambda: on_edit(listw))
        rename.clicked.connect(lambda: on_rename(listw))
        remove.clicked.connect(lambda: on_remove(listw))
        row = QHBoxLayout()
        row.addWidget(combo, 1)
        row.addWidget(add)
        row.addWidget(edit)
        row.addWidget(rename)
        row.addWidget(remove)
        layout.addLayout(row)
        return box

    def _rename_accel(self, listw: QListWidget) -> None:
        self._rename_sub(listw, "accelerators")

    def _rename_ic(self, listw: QListWidget) -> None:
        self._rename_sub(listw, "interconnects")

    def _rename_sub(self, listw: QListWidget, kind: str) -> None:
        old = self._selected_name(listw)
        if old is None:
            return
        new, ok = QInputDialog.getText(self, "Rename", "New name:", text=old)
        new = new.strip()
        if not ok or not new or new == old:
            return
        chiplet = setup_doc.chiplet_by_name(self.doc, self.name) or {}
        existing = {e.get("name") for e in chiplet.get(kind, []) or []}
        if new in existing:
            QMessageBox.warning(self, "Rename", f"'{new}' already exists on this chiplet.")
            return
        self.commit()
        if kind == "accelerators":
            setup_doc.rename_accelerator(self.doc, self.name, old, new)
        else:
            setup_doc.rename_interconnect(self.doc, self.name, old, new)
        self.editor._refresh()
        self.editor._show_chiplet_panel(self.name)

    @staticmethod
    def _selected_name(listw: QListWidget) -> Optional[str]:
        item = listw.currentItem()
        return item.text().split(" [")[0] if item is not None else None

    def _rename(self) -> None:
        new = self._name.text().strip()
        if not new or new == self.name:
            return
        existing = {c.get("name") for c in setup_doc.chiplets(self.doc)}
        if new in existing:
            self._name.setText(self.name)
            return
        self.commit()
        setup_doc.rename_chiplet(self.doc, self.name, new)
        self.name = new
        self.editor._refresh()
        self.editor.scene.select_node(new)

    def _change_type(self, new_type: str) -> None:
        chiplet = setup_doc.chiplet_by_name(self.doc, self.name)
        if chiplet is None or chiplet.get("type") == new_type:
            return
        chiplet["type"] = new_type
        chiplet.pop("config", None)
        self.editor._refresh()
        self.editor._show_chiplet_panel(self.name)

    def _add_accel(self, accel_type: str) -> None:
        self.commit()
        setup_doc.add_accelerator(self.doc, self.name, accel_type)
        self.editor._refresh()
        self.editor._show_chiplet_panel(self.name)

    def _remove_accel(self, listw: QListWidget) -> None:
        name = self._selected_name(listw)
        if name is None:
            return
        self.commit()
        setup_doc.remove_accelerator(self.doc, self.name, name)
        self.editor._refresh()
        self.editor._show_chiplet_panel(self.name)

    def _edit_accel(self, listw: QListWidget) -> None:
        self._edit_sub_config(listw, "accelerators")

    def _add_ic(self, ic_type: str) -> None:
        self.commit()
        setup_doc.add_interconnect(self.doc, self.name, ic_type)
        self.editor._refresh()
        self.editor._show_chiplet_panel(self.name)

    def _remove_ic(self, listw: QListWidget) -> None:
        name = self._selected_name(listw)
        if name is None:
            return
        self.commit()
        setup_doc.remove_interconnect(self.doc, self.name, name)
        self.editor._refresh()
        self.editor._show_chiplet_panel(self.name)

    def _edit_ic(self, listw: QListWidget) -> None:
        self._edit_sub_config(listw, "interconnects")

    def _edit_sub_config(self, listw: QListWidget, kind: str) -> None:
        name = self._selected_name(listw)
        if name is None:
            return
        chiplet = setup_doc.chiplet_by_name(self.doc, self.name) or {}
        key = "accelerators" if kind == "accelerators" else "interconnects"
        entry = next((e for e in chiplet.get(key, []) or [] if e.get("name") == name), None)
        if entry is None:
            return
        dialog = SubConfigDialog(
            self.editor.project, kind, entry.get("type", ""),
            entry.get("config", {}), f"{name} [{entry.get('type')}] config", self,
        )
        if dialog.exec():
            cfg = dialog.result_config()
            if cfg:
                entry["config"] = cfg
            else:
                entry.pop("config", None)
            self.editor._refresh()

    def commit(self) -> None:
        chiplet = setup_doc.chiplet_by_name(self.doc, self.name)
        if chiplet is None:
            return
        cfg = self._config.config()
        if cfg:
            chiplet["config"] = cfg
        else:
            chiplet.pop("config", None)


class ConnectionPanel(QWidget):
    def __init__(self, editor: GraphEditor, index: int):
        super().__init__()
        self.editor = editor
        self.doc = editor.doc
        self.index = index
        conn = setup_doc.connections(self.doc)[index]
        eps = conn.get("endpoints", ["", ""])
        ic_type = setup_doc.interconnect_type_of(self.doc, eps[0]) or ""

        header = QFormLayout()
        header.addRow("Endpoint A", QLabel(eps[0]))
        header.addRow("Endpoint B", QLabel(eps[1]))
        header.addRow("Interconnect", QLabel(ic_type))

        params = type_params(editor.project.configs_dir, "interconnects", ic_type)
        self._config = ConfigForm(params)
        self._config.set_config(conn.get("config", {}))
        config_box = QGroupBox("Config (blank = default)")
        cbl = QVBoxLayout(config_box)
        cbl.addWidget(self._config)

        self._ber = QLineEdit()
        self._ber.setPlaceholderText("1.0")
        if "ber_scalar" in conn:
            self._ber.setText(str(conn["ber_scalar"]))
        self._wire = QLineEdit()
        self._wire.setPlaceholderText("1.0")
        if "wire_length_mm" in conn:
            self._wire.setText(str(conn["wire_length_mm"]))

        extra = QFormLayout()
        extra.addRow("ber_scalar", self._ber)
        extra.addRow("wire_length_mm (mm)", self._wire)

        delete = QPushButton("Delete connection")
        delete.clicked.connect(self._delete)

        layout = QVBoxLayout(self)
        layout.addLayout(header)
        layout.addWidget(config_box)
        layout.addLayout(extra)
        layout.addWidget(delete)
        layout.addStretch(1)

    def _delete(self) -> None:
        setup_doc.remove_connection(self.doc, self.index)
        self.editor._refresh()
        self.editor._show_empty()

    def commit(self) -> None:
        conns = setup_doc.connections(self.doc)
        if not (0 <= self.index < len(conns)):
            return
        conn = conns[self.index]
        cfg = self._config.config()
        if cfg:
            conn["config"] = cfg
        else:
            conn.pop("config", None)
        _apply_float(conn, "ber_scalar", self._ber.text(), 1.0)
        _apply_float(conn, "wire_length_mm", self._wire.text(), 1.0)


def _apply_float(target: Dict[str, Any], key: str, text: str, default: float) -> None:
    text = text.strip()
    if not text:
        target.pop(key, None)
        return
    try:
        value = float(text)
    except ValueError:
        return
    if value == default:
        target.pop(key, None)
    else:
        target[key] = value
