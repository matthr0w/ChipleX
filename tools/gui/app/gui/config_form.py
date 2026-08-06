"""Editable config form; returns only values that differ from the default."""

from __future__ import annotations

from typing import Any, Dict, List

from PySide6.QtWidgets import (QFormLayout, QHBoxLayout, QLabel, QLineEdit,
                               QWidget)

from ..system_model import _get_dotted, _set_dotted
from ..type_catalog import TypeParam
from .theme import GEM5_MARK_STYLE


def _parse(value_type: str, text: str) -> Any:
    text = text.strip()
    if value_type == "bool":
        return text.lower() in ("1", "true", "yes", "on")
    if value_type == "int":
        return int(float(text))
    return float(text)


class ConfigForm(QWidget):
    def __init__(self, params: List[TypeParam], parent=None):
        super().__init__(parent)
        self._params = params
        self._editors: Dict[str, QLineEdit] = {}

        form = QFormLayout(self)
        form.setContentsMargins(0, 0, 0, 0)
        for param in params:
            editor = QLineEdit()
            editor.setPlaceholderText(str(param.default))
            self._editors[param.dotted_key] = editor

            row = QWidget()
            row_layout = QHBoxLayout(row)
            row_layout.setContentsMargins(0, 0, 0, 0)
            row_layout.addWidget(editor, 1)
            if param.unit:
                row_layout.addWidget(QLabel(param.unit))
            if getattr(param, "gem5", False):
                warn = QLabel("gem5")
                warn.setStyleSheet(GEM5_MARK_STYLE)
                warn.setToolTip(
                    "Changing this parameter invalidates cached cycle estimates, so the next "
                    "run re-runs gem5 and takes longer."
                )
                row_layout.addWidget(warn)
            form.addRow(param.dotted_key, row)

    def set_config(self, config: Dict[str, Any]) -> None:
        for param in self._params:
            current = _get_dotted(config or {}, param.dotted_key, None)
            self._editors[param.dotted_key].setText("" if current is None else str(current))

    def config(self) -> Dict[str, Any]:
        result: Dict[str, Any] = {}
        for param in self._params:
            text = self._editors[param.dotted_key].text().strip()
            if not text:
                continue
            try:
                value = _parse(param.value_type, text)
            except ValueError:
                continue
            if value != param.default:
                _set_dotted(result, param.dotted_key, value)
        return result
