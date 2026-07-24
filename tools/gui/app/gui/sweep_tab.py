"""Define a parameter sweep and expand it into concrete runs."""

from __future__ import annotations

from typing import Any, List

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (QComboBox, QFormLayout, QGroupBox, QHBoxLayout,
                               QLabel, QLineEdit, QMessageBox, QPushButton,
                               QScrollArea, QVBoxLayout, QWidget)

from ..project import Project
from ..schema import cli_parameters
from ..sweep import Sweep, SweepAxis
from ..system_model import ParamRef, SystemModel


class _AxisRow(QWidget):
    changed = Signal()
    removed = Signal(object)

    def __init__(self, parameters: List[ParamRef], parent=None):
        super().__init__(parent)
        self._param = QComboBox()
        self._param.currentIndexChanged.connect(self._on_param_changed)

        self._values = QLineEdit()
        self._values.setPlaceholderText("values to sweep, e.g. 128, 256, 512   or   start:stop:step")
        self._values.textChanged.connect(self.changed)

        self._unit = QLabel("")
        self._unit.setMinimumWidth(64)
        self._unit.setTextInteractionFlags(Qt.TextSelectableByMouse)

        remove = QPushButton("Remove")
        remove.clicked.connect(lambda: self.removed.emit(self))

        layout = QHBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.addWidget(self._param, 3)
        layout.addWidget(self._values, 4)
        layout.addWidget(self._unit)
        layout.addWidget(remove)

        self.set_parameters(parameters)

    def set_parameters(self, parameters: List[ParamRef]) -> None:
        previous = self.parameter()
        self._param.blockSignals(True)
        self._param.clear()
        for p in parameters:
            self._param.addItem(p.label, p)
        if previous is not None:
            match = next((i for i in range(self._param.count())
                          if self._param.itemData(i).id == previous.id), -1)
            if match >= 0:
                self._param.setCurrentIndex(match)
        self._param.blockSignals(False)

        # Reset the values to the new default only when the parameter actually
        # changed (a fresh row, or the previous parameter is gone); preserve a
        # user's typed values when the same parameter is retained.
        current = self.parameter()
        same = previous is not None and current is not None and current.id == previous.id
        if current is not None and not same:
            self._values.setText(str(current.default))
        self._refresh_unit()
        self.changed.emit()

    def _on_param_changed(self) -> None:
        param = self.parameter()
        if param is not None:
            self._values.setText(str(param.default))
        self._refresh_unit()
        self.changed.emit()

    def _refresh_unit(self) -> None:
        param = self.parameter()
        self._unit.setText(param.unit if param is not None and param.unit else "")

    def parameter(self) -> ParamRef | None:
        return self._param.currentData()

    def values(self) -> List[Any]:
        param = self.parameter()
        if param is None:
            return []
        return _parse_values(param, self._values.text())


class SweepTab(QWidget):
    runs_generated = Signal(list)  # List[RunSpec]

    def __init__(self, project: Project, setups: List[str], parent=None):
        super().__init__(parent)
        self._project = project
        self._axis_rows: List[_AxisRow] = []

        self._setup = QComboBox()
        self._setup.addItems(setups)
        self._setup.currentTextChanged.connect(self._on_setup_changed)

        self._time = QLineEdit()
        self._time.setPlaceholderText("until end")
        self._ber = QLineEdit()
        self._ber.setPlaceholderText("default")
        self._seed = QLineEdit()
        self._seed.setPlaceholderText("default")

        base_form = QFormLayout()
        base_form.addRow("Setup", self._setup)
        base_form.addRow("Base time (ns)", self._time)
        base_form.addRow("Base BER", self._ber)
        base_form.addRow("Base seed", self._seed)
        base_box = QGroupBox("Base configuration")
        base_box.setLayout(base_form)

        self._axes_container = QVBoxLayout()
        self._axes_container.addStretch(1)
        axes_host = QWidget()
        axes_host.setLayout(self._axes_container)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(axes_host)

        hint = QLabel(
            "Each axis sweeps one parameter over several values. Enter values as "
            "'128, 256, 512' or a range 'start:stop:step'."
        )
        hint.setWordWrap(True)

        add_axis = QPushButton("Add axis")
        add_axis.clicked.connect(self._add_axis)
        axes_box = QGroupBox("Sweep axes")
        axes_layout = QVBoxLayout(axes_box)
        axes_layout.addWidget(hint)
        axes_layout.addWidget(add_axis)
        axes_layout.addWidget(scroll)

        self._preview = QLabel("0 runs")
        generate = QPushButton("Add runs to list")
        generate.clicked.connect(self._generate)

        footer = QHBoxLayout()
        footer.addWidget(self._preview)
        footer.addStretch(1)
        footer.addWidget(generate)

        layout = QVBoxLayout(self)
        layout.addWidget(base_box)
        layout.addWidget(axes_box, 1)
        layout.addLayout(footer)

        self._parameters = self._parameters_for(self._setup.currentText())
        self._add_axis()

    def set_setups(self, setups: List[str]) -> None:
        current = self._setup.currentText()
        self._setup.blockSignals(True)
        self._setup.clear()
        self._setup.addItems(setups)
        idx = self._setup.findText(current)
        if idx >= 0:
            self._setup.setCurrentIndex(idx)
        self._setup.blockSignals(False)
        self._on_setup_changed(self._setup.currentText())

    def _parameters_for(self, setup: str) -> List[ParamRef]:
        params = list(cli_parameters())
        if setup:
            params.extend(SystemModel(self._project, setup).parameters())
        return params

    def _on_setup_changed(self, setup: str) -> None:
        self._parameters = self._parameters_for(setup)
        for row in self._axis_rows:
            row.set_parameters(self._parameters)
        self._update_preview()

    def _add_axis(self) -> None:
        row = _AxisRow(self._parameters)
        row.changed.connect(self._update_preview)
        row.removed.connect(self._remove_axis)
        self._axis_rows.append(row)
        self._axes_container.insertWidget(self._axes_container.count() - 1, row)
        self._update_preview()

    def _remove_axis(self, row: _AxisRow) -> None:
        if row in self._axis_rows:
            self._axis_rows.remove(row)
            row.setParent(None)
            self._update_preview()

    def _build_sweep(self) -> Sweep:
        sweep = Sweep(
            setup=self._setup.currentText(),
            base_time_ns=_to_float(self._time.text(), 0.0),
            base_ber=_to_float_or_none(self._ber.text()),
            base_seed=_to_int_or_none(self._seed.text()),
        )
        for row in self._axis_rows:
            values = row.values()
            param = row.parameter()
            if param is not None and values:
                sweep.axes.append(SweepAxis(parameter=param, values=values))
        return sweep

    def _update_preview(self) -> None:
        try:
            sweep = self._build_sweep()
            count = sweep.count() if sweep.axes else 1
            self._preview.setText(f"{count} Run(s)")
        except Exception:  # noqa: BLE001 - preview should never raise
            self._preview.setText("invalid axis values")

    def _generate(self) -> None:
        sweep = self._build_sweep()
        specs = sweep.expand()
        if not specs:
            QMessageBox.warning(self, "Sweep", "No runs to generate.")
            return
        self.runs_generated.emit(specs)


def _parse_values(param: ParamRef, text: str) -> List[Any]:
    text = text.strip()
    if not text:
        return []
    if ":" in text and "," not in text:
        parts = text.split(":")
        if len(parts) == 3:
            start, stop, step = (float(p) for p in parts)
            # Guard against a zero or wrong-signed step producing no progress.
            if step == 0 or (stop - start) * step < 0:
                return []
            values = []
            current = start
            while (current <= stop + 1e-9) if step > 0 else (current >= stop - 1e-9):
                values.append(current)
                current += step
            return [param.parse(str(v)) for v in values]
    return [param.parse(token) for token in text.split(",") if token.strip()]


def _to_float(text: str, default: float) -> float:
    try:
        return float(text.strip())
    except ValueError:
        return default


def _to_float_or_none(text: str):
    text = text.strip()
    if not text:
        return None
    try:
        return float(text)
    except ValueError:
        return None


def _to_int_or_none(text: str):
    text = text.strip()
    if not text:
        return None
    try:
        return int(float(text))
    except ValueError:
        return None
