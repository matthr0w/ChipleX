"""The run list: one row per simulation to execute across cores."""

from __future__ import annotations

from typing import List

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (QAbstractItemView, QComboBox, QHBoxLayout,
                               QHeaderView, QLabel, QPushButton, QSplitter,
                               QTableWidget, QTableWidgetItem, QVBoxLayout,
                               QWidget)

from ..project import Project
from ..runspec import RunSpec
from .ansi_text import AnsiTextEdit
from .run_editor import RunEditorDialog

_COLUMNS = ["Enabled", "Label", "Setup", "Time (ns)", "BER", "Seed", "Overrides", "Status"]
_LOG_LEVELS = ["SILENT", "ERROR", "WARN", "INFO", "DEBUG", "DELAY"]


class RunsTab(QWidget):
    selection_changed = Signal()

    def __init__(self, project: Project, setups: List[str], parent=None):
        super().__init__(parent)
        self._project = project
        self._setups = setups
        self._specs: List[RunSpec] = []

        self._table = QTableWidget(0, len(_COLUMNS))
        self._table.setHorizontalHeaderLabels(_COLUMNS)
        self._table.verticalHeader().setVisible(False)
        self._table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self._table.setSelectionBehavior(QAbstractItemView.SelectRows)
        self._table.setSelectionMode(QAbstractItemView.ExtendedSelection)
        self._table.horizontalHeader().setSectionResizeMode(1, QHeaderView.Stretch)
        self._table.cellDoubleClicked.connect(lambda row, _col: self._edit_row(row))
        self._table.itemSelectionChanged.connect(self._show_selected_output)
        self._outputs: dict[str, str] = {}

        add_btn = QPushButton("Add...")
        edit_btn = QPushButton("Edit...")
        dup_btn = QPushButton("Duplicate")
        rm_btn = QPushButton("Remove")
        clear_btn = QPushButton("Clear")
        add_btn.clicked.connect(self._add)
        edit_btn.clicked.connect(self._edit_selected)
        dup_btn.clicked.connect(self._duplicate)
        rm_btn.clicked.connect(self._remove)
        clear_btn.clicked.connect(self.clear)

        self._log_level = QComboBox()
        self._log_level.addItems(_LOG_LEVELS)

        buttons = QHBoxLayout()
        for btn in (add_btn, edit_btn, dup_btn, rm_btn, clear_btn):
            buttons.addWidget(btn)
        buttons.addStretch(1)
        buttons.addWidget(QLabel("Log level"))
        buttons.addWidget(self._log_level)

        self._output = AnsiTextEdit()

        clear_output_btn = QPushButton("Clear")
        clear_output_btn.clicked.connect(self.clear_output)
        output_header = QHBoxLayout()
        output_header.addWidget(QLabel("Output"))
        output_header.addStretch(1)
        output_header.addWidget(clear_output_btn)

        splitter = QSplitter(Qt.Vertical)
        splitter.addWidget(self._table)
        output_box = QWidget()
        output_layout = QVBoxLayout(output_box)
        output_layout.setContentsMargins(0, 0, 0, 0)
        output_layout.addLayout(output_header)
        output_layout.addWidget(self._output)
        splitter.addWidget(output_box)
        splitter.setSizes([420, 220])

        layout = QVBoxLayout(self)
        layout.addLayout(buttons)
        layout.addWidget(splitter, 1)

    def log_level(self) -> str:
        return self._log_level.currentText()

    def clear_output(self) -> None:
        self._outputs.clear()
        self._output.set_ansi("")

    def start_output(self, label: str) -> None:
        self._outputs[label] = ""
        if self._current_label() == label:
            self._output.set_ansi("")

    def append_output_chunk(self, label: str, chunk: str) -> None:
        self._outputs[label] = self._outputs.get(label, "") + chunk
        if self._current_label() == label:
            self._output.append_ansi(chunk)

    def _current_label(self) -> "str | None":
        rows = self._selected_rows()
        if len(rows) != 1:
            return None
        return self._specs[rows[0]].label

    def _show_selected_output(self) -> None:
        label = self._current_label()
        self._output.set_ansi(self._outputs.get(label, "") if label else "")

    def set_setups(self, setups: List[str]) -> None:
        self._setups = list(setups)

    def add_specs(self, specs: List[RunSpec]) -> None:
        for spec in specs:
            spec.label = self._unique_label(spec.label)
            self._specs.append(spec)
        self._rebuild()
        self.selection_changed.emit()

    def clear(self) -> None:
        self._specs.clear()
        self._rebuild()
        self.selection_changed.emit()

    def enabled_specs(self) -> List[RunSpec]:
        result = []
        for row, spec in enumerate(self._specs):
            if self._table.item(row, 0).checkState() == Qt.Checked:
                result.append(spec)
        return result

    def all_specs(self) -> List[RunSpec]:
        return list(self._specs)

    def set_status(self, label: str, text: str) -> None:
        for row, spec in enumerate(self._specs):
            if spec.label == label:
                self._table.item(row, 7).setText(text)
                return

    def reset_statuses(self, text: str = "") -> None:
        for row in range(self._table.rowCount()):
            self._table.item(row, 7).setText(text)

    def _add(self) -> None:
        dialog = RunEditorDialog(self._project, self._setups, parent=self)
        if dialog.exec():
            self.add_specs([dialog.result_spec()])

    def _edit_selected(self) -> None:
        rows = self._selected_rows()
        if rows:
            self._edit_row(rows[0])

    def _edit_row(self, row: int) -> None:
        if not (0 <= row < len(self._specs)):
            return
        dialog = RunEditorDialog(self._project, self._setups, spec=self._specs[row], parent=self)
        if dialog.exec():
            new_spec = dialog.result_spec()
            if new_spec.label != self._specs[row].label:
                new_spec.label = self._unique_label(new_spec.label, ignore_index=row)
            self._specs[row] = new_spec
            self._rebuild()
            self.selection_changed.emit()

    def _duplicate(self) -> None:
        for row in self._selected_rows():
            original = self._specs[row]
            copy = RunSpec(
                label=self._unique_label(original.label),
                setup=original.setup,
                time_ns=original.time_ns,
                ber=original.ber,
                seed=original.seed,
                overrides=list(original.overrides),
            )
            self._specs.append(copy)
        self._rebuild()
        self.selection_changed.emit()

    def _remove(self) -> None:
        for row in sorted(self._selected_rows(), reverse=True):
            del self._specs[row]
        self._rebuild()
        self.selection_changed.emit()

    def _selected_rows(self) -> List[int]:
        return sorted({idx.row() for idx in self._table.selectionModel().selectedRows()})

    def _unique_label(self, label: str, ignore_index: int | None = None) -> str:
        existing = {
            spec.label for i, spec in enumerate(self._specs) if i != ignore_index
        }
        if label not in existing:
            return label
        n = 2
        while f"{label}#{n}" in existing:
            n += 1
        return f"{label}#{n}"

    def _rebuild(self) -> None:
        self._table.setRowCount(len(self._specs))
        for row, spec in enumerate(self._specs):
            enabled = QTableWidgetItem()
            enabled.setFlags(Qt.ItemIsUserCheckable | Qt.ItemIsEnabled)
            enabled.setCheckState(Qt.Checked)
            self._table.setItem(row, 0, enabled)

            self._table.setItem(row, 1, _ro(spec.label))
            self._table.setItem(row, 2, _ro(spec.setup))
            self._table.setItem(row, 3, _ro("end" if not spec.time_ns else f"{spec.time_ns:g}"))
            self._table.setItem(row, 4, _ro("default" if spec.ber is None else f"{spec.ber:g}"))
            self._table.setItem(row, 5, _ro("default" if spec.seed is None else str(spec.seed)))
            self._table.setItem(row, 6, _ro(spec.overrides_summary()))
            self._table.setItem(row, 7, _ro(""))
        self._table.resizeColumnToContents(0)


def _ro(text: str) -> QTableWidgetItem:
    item = QTableWidgetItem(text)
    item.setFlags(Qt.ItemIsEnabled | Qt.ItemIsSelectable)
    return item
