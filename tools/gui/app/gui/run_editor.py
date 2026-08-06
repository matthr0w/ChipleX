"""Dialog for creating or editing a single run."""

from __future__ import annotations

from typing import List

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QComboBox, QDialog, QDialogButtonBox,
                               QDoubleSpinBox, QFormLayout, QGroupBox,
                               QLineEdit, QVBoxLayout)

from ..project import Project
from ..runspec import RunSpec
from ..system_model import SystemModel
from .instance_tree import InstanceParamTree


class RunEditorDialog(QDialog):
    def __init__(self, project: Project, setups: List[str], spec: RunSpec | None = None, parent=None):
        super().__init__(parent)
        self._project = project
        self.setWindowTitle("Edit Run" if spec else "New Run")
        # Restored size when un-maximized; the dialog opens maximized.
        self.resize(640, 700)
        self.setWindowState(Qt.WindowState.WindowMaximized)

        self._setup = QComboBox()
        self._setup.addItems(setups)
        self._setup.currentTextChanged.connect(self._reload_model)

        self._label = QLineEdit()
        self._label.setPlaceholderText("Auto-generated from setup if left blank")

        self._time = QDoubleSpinBox()
        self._time.setRange(0.0, 1e12)
        self._time.setDecimals(1)
        self._time.setSuffix(" ns")
        self._time.setSpecialValueText("until end")

        self._ber = QLineEdit()
        self._ber.setPlaceholderText("default (1e-12)")

        self._seed = QLineEdit()
        self._seed.setPlaceholderText("default")

        form = QFormLayout()
        form.addRow("Setup", self._setup)
        form.addRow("Label", self._label)
        form.addRow("Time", self._time)
        form.addRow("Bit error rate", self._ber)
        form.addRow("RNG seed", self._seed)

        self._tree = InstanceParamTree()
        overrides_box = QGroupBox("Per-Instance Overrides")
        box_layout = QVBoxLayout(overrides_box)
        box_layout.addWidget(self._tree)

        buttons = QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)

        layout = QVBoxLayout(self)
        layout.addLayout(form)
        layout.addWidget(overrides_box, 1)
        layout.addWidget(buttons)

        if spec is not None:
            self._load(spec)
        else:
            self._reload_model(self._setup.currentText())

    def _reload_model(self, setup: str) -> None:
        if not setup:
            return
        model = SystemModel(self._project, setup)
        self._tree.set_model(model)

    def _load(self, spec: RunSpec) -> None:
        idx = self._setup.findText(spec.setup)
        if idx >= 0:
            self._setup.setCurrentIndex(idx)
        self._reload_model(spec.setup)
        self._label.setText(spec.label)
        self._time.setValue(spec.time_ns or 0.0)
        if spec.ber is not None:
            self._ber.setText(repr(spec.ber))
        if spec.seed is not None:
            self._seed.setText(str(spec.seed))
        self._tree.set_overrides(spec.overrides)

    def result_spec(self) -> RunSpec:
        setup = self._setup.currentText()
        label = self._label.text().strip() or setup
        return RunSpec(
            label=label,
            setup=setup,
            time_ns=self._time.value(),
            ber=_parse_float(self._ber.text()),
            seed=_parse_int(self._seed.text()),
            overrides=self._tree.overrides(),
        )


def _parse_float(text: str):
    text = text.strip()
    if not text:
        return None
    try:
        return float(text)
    except ValueError:
        return None


def _parse_int(text: str):
    text = text.strip()
    if not text:
        return None
    try:
        return int(float(text))
    except ValueError:
        return None
