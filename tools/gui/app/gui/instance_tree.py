"""Tree of a setup's per-instance parameters with editable per-run overrides."""

from __future__ import annotations

from typing import Any, List, Tuple

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QLineEdit, QTreeWidget, QTreeWidgetItem,
                               QVBoxLayout, QWidget)

from ..system_model import Instance, ParamRef, SystemModel

_REF_ROLE = Qt.UserRole + 1


class InstanceParamTree(QWidget):
    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self._editors: List[Tuple[ParamRef, QLineEdit]] = []

        self._tree = QTreeWidget()
        self._tree.setColumnCount(4)
        self._tree.setHeaderLabels(["Instance / Parameter", "Default", "Override", "Unit"])
        self._tree.setAlternatingRowColors(True)
        self._tree.setUniformRowHeights(True)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self._tree)

    def set_model(self, model: SystemModel) -> None:
        self._tree.clear()
        self._editors.clear()
        for inst in model.instances:
            self._add_instance(inst, self._tree.invisibleRootItem())
        self._tree.expandToDepth(0)
        self._tree.resizeColumnToContents(0)
        self._tree.setColumnWidth(0, max(320, self._tree.columnWidth(0)))

    def set_overrides(self, overrides: List[Tuple[ParamRef, Any]]) -> None:
        by_id = {ref.id: value for ref, value in overrides}
        for ref, editor in self._editors:
            if ref.id in by_id:
                editor.setText(str(by_id[ref.id]))

    def overrides(self) -> List[Tuple[ParamRef, Any]]:
        result: List[Tuple[ParamRef, Any]] = []
        for ref, editor in self._editors:
            text = editor.text().strip()
            if not text:
                continue
            try:
                result.append((ref, ref.parse(text)))
            except ValueError:
                continue
        return result

    def _add_instance(self, inst: Instance, parent: QTreeWidgetItem) -> None:
        header = f"{inst.name}  [{inst.type_name}]" if inst.type_name else inst.name
        item = QTreeWidgetItem([header, "", "", ""])
        item.setFirstColumnSpanned(True)
        parent.addChild(item)
        for ref in inst.params:
            self._add_param(ref, item)
        for child in inst.children:
            self._add_instance(child, item)

    def _add_param(self, ref: ParamRef, parent: QTreeWidgetItem) -> None:
        key = ref.special or ref.dotted_key or ref.label
        leaf = QTreeWidgetItem([key, str(ref.default), "", ref.unit or ""])
        leaf.setData(0, _REF_ROLE, ref.id)
        parent.addChild(leaf)

        editor = QLineEdit()
        editor.setPlaceholderText(str(ref.default))
        self._tree.setItemWidget(leaf, 2, editor)
        self._editors.append((ref, editor))
