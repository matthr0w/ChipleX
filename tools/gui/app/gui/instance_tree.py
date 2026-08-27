"""Tree of a setup's per-instance parameters with editable per-run overrides."""

from __future__ import annotations

from typing import Any, List, Tuple

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QStyle,
    QTreeWidget,
    QTreeWidgetItem,
    QVBoxLayout,
    QWidget,
)

from ..system_model import Instance, ParamRef, SystemModel
from .theme import GEM5_MARK_STYLE

_REF_ROLE = Qt.UserRole + 1


class InstanceParamTree(QWidget):
    def __init__(self, parent: QWidget | None = None):
        super().__init__(parent)
        self._editors: List[Tuple[ParamRef, QLineEdit]] = []

        self._tree = QTreeWidget()
        self._tree.setColumnCount(4)
        self._tree.setHeaderLabels(
            ["Instance / Parameter", "Default", "Override", "Unit"]
        )
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
        # The unit cell also carries the gem5 mark, so leave it blank here and
        # fill it with a widget below when the parameter is gem5-relevant.
        leaf = QTreeWidgetItem(
            [key, str(ref.default), "", "" if ref.gem5 else (ref.unit or "")]
        )
        leaf.setData(0, _REF_ROLE, ref.id)
        parent.addChild(leaf)

        editor = QLineEdit()
        editor.setPlaceholderText(str(ref.default))
        self._tree.setItemWidget(leaf, 2, editor)
        self._editors.append((ref, editor))

        if ref.gem5:
            cell = QWidget()
            cell_layout = QHBoxLayout(cell)
            hmargin = self._tree.style().pixelMetric(QStyle.PM_FocusFrameHMargin) + 1
            cell_layout.setContentsMargins(hmargin, 0, 0, 0)
            if ref.unit:
                cell_layout.addWidget(QLabel(ref.unit))
            mark = QLabel("gem5")
            mark.setStyleSheet(GEM5_MARK_STYLE)
            mark.setToolTip(
                "Changing this parameter invalidates cached cycle estimates, so the next "
                "run re-runs gem5 and takes longer."
            )
            cell_layout.addWidget(mark)
            cell_layout.addStretch(1)
            self._tree.setItemWidget(leaf, 3, cell)
