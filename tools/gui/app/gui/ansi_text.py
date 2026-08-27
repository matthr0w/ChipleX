"""Read-only terminal-style view that renders ANSI SGR color codes."""

from __future__ import annotations

import re

from PySide6.QtGui import QColor, QFont, QFontDatabase, QTextCharFormat, QTextCursor
from PySide6.QtWidgets import QPlainTextEdit

_SGR = re.compile("\x1b\\[([0-9;]*)m")
_DEFAULT_FG = "#dcdfe4"
_COLORS = {
    30: "#5c6370",
    31: "#e06c75",
    32: "#98c379",
    33: "#e5c07b",
    34: "#61afef",
    35: "#c678dd",
    36: "#56b6c2",
    37: "#dcdfe4",
    90: "#7f848e",
    91: "#ff7b86",
    92: "#b5e08f",
    93: "#f0d08b",
    94: "#7cc5ff",
    95: "#d79bee",
    96: "#6fd0dc",
    97: "#ffffff",
}


class AnsiTextEdit(QPlainTextEdit):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.setReadOnly(True)
        self.setLineWrapMode(QPlainTextEdit.NoWrap)
        font = QFontDatabase.systemFont(QFontDatabase.FixedFont)
        font.setStyleHint(QFont.Monospace)
        self.setFont(font)
        self.setStyleSheet(
            "QPlainTextEdit { background-color: #1e1e1e; color: %s;"
            " border: none; }" % _DEFAULT_FG
        )
        self._reset_format()

    def _reset_format(self) -> None:
        self._fmt = QTextCharFormat()
        self._fmt.setForeground(QColor(_DEFAULT_FG))
        self._fmt.setFontWeight(QFont.Normal)

    def set_ansi(self, text: str) -> None:
        self.clear()
        self._reset_format()
        if text:
            self.append_ansi(text)

    def append_ansi(self, text: str) -> None:
        cursor = self.textCursor()
        cursor.movePosition(QTextCursor.End)
        pos = 0
        for match in _SGR.finditer(text):
            if match.start() > pos:
                cursor.insertText(text[pos : match.start()], self._fmt)
            self._apply_codes(match.group(1))
            pos = match.end()
        if pos < len(text):
            cursor.insertText(text[pos:], self._fmt)

    def _apply_codes(self, params: str) -> None:
        for token in (params.split(";") if params else ["0"]):
            code = int(token) if token.isdigit() else 0
            if code == 0:
                self._reset_format()
            elif code == 1:
                self._fmt.setFontWeight(QFont.Bold)
            elif code == 22:
                self._fmt.setFontWeight(QFont.Normal)
            elif code == 39:
                self._fmt.setForeground(QColor(_DEFAULT_FG))
            elif code in _COLORS:
                self._fmt.setForeground(QColor(_COLORS[code]))
