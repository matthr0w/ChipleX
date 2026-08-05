"""Per-chiplet gem5 model editor for the setup editor.

Selects the CPU model used for cycle estimation and overrides its exposed
parameters. The parameter list and defaults come from the selected model's
manifest, not the chiplet default config. The resulting gem5 block is written
to the chiplet in system.yaml; an empty block (default model, no overrides) is
omitted.
"""

from __future__ import annotations

from typing import Any, Dict

from PySide6.QtWidgets import (QComboBox, QFormLayout, QLabel, QLineEdit,
                               QVBoxLayout, QWidget)

from ..gem5_models import DEFAULT_MODEL, list_models, model_params


def _coerce(text: str, default: Any) -> Any:
    if isinstance(default, bool):
        return text.lower() in ("1", "true", "yes", "on")
    if isinstance(default, int):
        try:
            return int(float(text))
        except ValueError:
            return text
    if isinstance(default, float):
        try:
            return float(text)
        except ValueError:
            return text
    return text


class Gem5Form(QWidget):
    def __init__(self, project, gem5_block: Dict[str, Any], parent=None):
        super().__init__(parent)
        self._project = project
        self._models = list_models(project)
        self._editors: Dict[str, tuple] = {}  # key -> (QLineEdit, default)

        block = gem5_block or {}
        self._initial_model = block.get("cpu_model", DEFAULT_MODEL)
        self._initial_params = dict(block.get("params") or {})

        self._combo = QComboBox()
        self._combo.addItems(self._models)
        if self._initial_model in self._models:
            self._combo.setCurrentText(self._initial_model)
        self._combo.currentTextChanged.connect(self._reload_params)

        self._param_form = QFormLayout()
        self._param_form.setContentsMargins(0, 0, 0, 0)
        param_host = QWidget()
        param_host.setLayout(self._param_form)

        note = QLabel(
            "Changing the model or a parameter invalidates cached cycle "
            "estimates, so the next build re-runs gem5 and takes longer."
        )
        note.setWordWrap(True)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        model_row = QFormLayout()
        model_row.setContentsMargins(0, 0, 0, 0)
        model_row.addRow("CPU model", self._combo)
        layout.addLayout(model_row)
        layout.addWidget(param_host)
        layout.addWidget(note)

        self._reload_params(self._combo.currentText())

    def _reload_params(self, model_name: str) -> None:
        while self._param_form.rowCount():
            self._param_form.removeRow(0)
        self._editors.clear()
        for key, default in model_params(self._project, model_name).items():
            editor = QLineEdit()
            editor.setPlaceholderText(str(default))
            # Prefill only the model that owned the saved overrides; switching
            # models resets to that model's defaults.
            if model_name == self._initial_model and key in self._initial_params:
                editor.setText(str(self._initial_params[key]))
            self._editors[key] = (editor, default)
            self._param_form.addRow(key, editor)

    def gem5(self) -> Dict[str, Any]:
        block: Dict[str, Any] = {}
        model = self._combo.currentText()
        if model != DEFAULT_MODEL:
            block["cpu_model"] = model
        params: Dict[str, Any] = {}
        for key, (editor, default) in self._editors.items():
            text = editor.text().strip()
            if not text:
                continue
            value = _coerce(text, default)
            if value != default:
                params[key] = value
        if params:
            block["params"] = params
        return block
