"""The run list: one row per simulation to execute across cores."""

from __future__ import annotations

from pathlib import Path
from typing import List

from PySide6.QtCore import Qt, Signal
from PySide6.QtGui import QFontMetrics
from PySide6.QtWidgets import (
    QAbstractItemView,
    QComboBox,
    QFileDialog,
    QHBoxLayout,
    QHeaderView,
    QLabel,
    QPushButton,
    QSizePolicy,
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from ..project import Project
from ..runspec import RunSpec
from .ansi_text import AnsiTextEdit
from .run_editor import RunEditorDialog

_COLUMNS = [
    "Enabled",
    "Label",
    "Setup",
    "Time (ns)",
    "BER",
    "Seed",
    "Overrides",
    "Status",
]
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
        self._table.itemSelectionChanged.connect(self._update_log_controls)
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

        self._output = AnsiTextEdit()

        clear_output_btn = QPushButton("Clear")
        clear_output_btn.clicked.connect(self.clear_output)
        output_header = QHBoxLayout()
        output_header.addWidget(QLabel("Output"))
        output_header.addStretch(1)
        output_header.addWidget(clear_output_btn)

        self._log_file_text = _ElidedLabel()
        self._choose_log_btn = QPushButton("Choose...")
        self._choose_log_btn.setToolTip("Write the selected run's output to a file")
        self._choose_log_btn.clicked.connect(self._choose_log_file)
        self._remove_log_btn = QPushButton("Unset")
        self._remove_log_btn.setToolTip(
            "Stop writing the selected run's output to a file"
        )
        self._remove_log_btn.clicked.connect(self._remove_log_file)

        log_header = QHBoxLayout()
        log_header.addWidget(QLabel("Log file:"))
        log_header.addWidget(self._log_file_text, 1)
        log_header.addWidget(self._choose_log_btn)
        log_header.addWidget(self._remove_log_btn)
        log_header.addSpacing(16)
        log_header.addWidget(QLabel("Log level:"))
        log_header.addWidget(self._log_level)

        splitter = QSplitter(Qt.Vertical)
        splitter.addWidget(self._table)
        output_box = QWidget()
        output_layout = QVBoxLayout(output_box)
        output_layout.setContentsMargins(0, 0, 0, 0)
        output_layout.addLayout(log_header)
        output_layout.addLayout(output_header)
        output_layout.addWidget(self._output)
        splitter.addWidget(output_box)
        splitter.setSizes([420, 220])

        layout = QVBoxLayout(self)
        layout.addLayout(buttons)
        layout.addWidget(splitter, 1)

        self._last_log_dir: Path | None = None
        self._update_log_controls()

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

    def _current_spec(self) -> "RunSpec | None":
        """The single selected run, or None when the selection is not exactly one."""
        rows = self._selected_rows()
        if len(rows) != 1:
            return None
        return self._specs[rows[0]]

    def _current_label(self) -> "str | None":
        spec = self._current_spec()
        return spec.label if spec is not None else None

    def _show_selected_output(self) -> None:
        label = self._current_label()
        self._output.set_ansi(self._outputs.get(label, "") if label else "")

    def _update_log_controls(self) -> None:
        spec = self._current_spec()
        self._choose_log_btn.setEnabled(spec is not None)
        self._remove_log_btn.setEnabled(spec is not None and spec.log_path is not None)
        if spec is None:
            self._log_file_text.set_full_text("Select a run")
        elif spec.log_path is None:
            self._log_file_text.set_full_text("Output is not written to a file")
        else:
            self._log_file_text.set_full_text(str(spec.log_path))

    def _choose_log_file(self) -> None:
        spec = self._current_spec()
        if spec is None:
            return
        path, _filter = QFileDialog.getSaveFileName(
            self,
            f"Log file for '{spec.label}'",
            str(self._log_dialog_start(spec)),
            "Log files (*.log);;All files (*)",
        )
        if not path:
            return
        spec.log_path = Path(path)
        self._last_log_dir = spec.log_path.parent
        self._update_log_controls()

    def _remove_log_file(self) -> None:
        spec = self._current_spec()
        if spec is None:
            return
        spec.log_path = None
        self._update_log_controls()

    def _log_dialog_start(self, spec: RunSpec) -> Path:
        if spec.log_path is not None:
            return spec.log_path
        directory = self._last_log_dir or Path.home()
        return directory / f"{_slug(spec.label)}.log"

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
        dialog = RunEditorDialog(
            self._project, self._setups, spec=self._specs[row], parent=self
        )
        if dialog.exec():
            new_spec = dialog.result_spec()
            # The editor does not cover the log file, so keep the row's choice.
            new_spec.log_path = self._specs[row].log_path
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
                # The log file is deliberately not copied: two runs writing to
                # the same file would overwrite each other's output.
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
        return sorted(
            {idx.row() for idx in self._table.selectionModel().selectedRows()}
        )

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
            self._table.setItem(
                row, 3, _ro("end" if not spec.time_ns else f"{spec.time_ns:g}")
            )
            self._table.setItem(
                row, 4, _ro("default" if spec.ber is None else f"{spec.ber:g}")
            )
            self._table.setItem(
                row, 5, _ro("default" if spec.seed is None else str(spec.seed))
            )
            self._table.setItem(row, 6, _ro(spec.overrides_summary()))
            self._table.setItem(row, 7, _ro(""))
        self._table.resizeColumnToContents(0)
        self._update_log_controls()


class _ElidedLabel(QLabel):
    """Label that elides long text on the left so full paths stay readable."""

    def __init__(self, parent=None):
        super().__init__(parent)
        # An ignored horizontal policy keeps a long path from widening the tab.
        self.setSizePolicy(QSizePolicy.Ignored, QSizePolicy.Preferred)
        self.setTextInteractionFlags(Qt.TextSelectableByMouse)
        self._full_text = ""

    def set_full_text(self, text: str) -> None:
        self._full_text = text
        self.setToolTip(text)
        self._apply_elision()

    def resizeEvent(self, event) -> None:  # noqa: N802 - Qt naming
        super().resizeEvent(event)
        self._apply_elision()

    def _apply_elision(self) -> None:
        metrics = QFontMetrics(self.font())
        self.setText(
            metrics.elidedText(self._full_text, Qt.ElideLeft, max(0, self.width()))
        )


def _ro(text: str) -> QTableWidgetItem:
    item = QTableWidgetItem(text)
    item.setFlags(Qt.ItemIsEnabled | Qt.ItemIsSelectable)
    return item


def _slug(text: str) -> str:
    keep = [c if c.isalnum() or c in ("-", "_") else "_" for c in text]
    return "".join(keep) or "run"
