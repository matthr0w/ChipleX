"""A small in-app viewer for the bundled Markdown docs."""

from __future__ import annotations

from pathlib import Path
from typing import List

from PySide6.QtCore import QUrl
from PySide6.QtGui import QDesktopServices, QPalette, QTextDocument
from PySide6.QtWidgets import (QDialog, QHBoxLayout, QPushButton, QTextBrowser,
                               QVBoxLayout)


class MarkdownViewer(QDialog):
    def __init__(self, parent=None):
        super().__init__(parent)
        self.resize(820, 640)
        self._history: List[Path] = []

        self._browser = QTextBrowser()
        self._browser.setOpenLinks(False)
        self._browser.anchorClicked.connect(self._on_anchor)

        self._back = QPushButton("Back")
        self._back.clicked.connect(self._go_back)
        close = QPushButton("Close")
        close.clicked.connect(self.accept)

        bar = QHBoxLayout()
        bar.addWidget(self._back)
        bar.addStretch(1)
        bar.addWidget(close)

        layout = QVBoxLayout(self)
        layout.addWidget(self._browser, 1)
        layout.addLayout(bar)

    def load(self, path: Path) -> None:
        path = Path(path)
        self._history.append(path)
        self._back.setEnabled(len(self._history) > 1)
        self.setWindowTitle(path.name)
        try:
            text = path.read_text()
        except OSError as exc:
            self._browser.setPlainText(f"Could not open {path}:\n{exc}")
            return
        # Qt parses the Markdown well but styles it minimally. Re-render its HTML
        # through a stylesheet, which only applies via setHtml (not setMarkdown).
        parsed = QTextDocument()
        parsed.setMarkdown(text, QTextDocument.MarkdownDialectGitHub)
        self._browser.document().setDefaultStyleSheet(self._stylesheet())
        self._browser.setHtml(parsed.toHtml())

    def _stylesheet(self) -> str:
        base = self.palette().color(QPalette.Base)
        dark = base.lightness() < 128
        code_bg = "#33373e" if dark else "#f2f3f5"
        border = "#4a4f57" if dark else "#d5d8dc"
        return (
            f"pre {{ background-color: {code_bg}; padding: 8px;"
            f" border: 1px solid {border}; }}"
            f"code {{ background-color: {code_bg}; }}"
            f"table {{ border-collapse: collapse; }}"
            f"td, th {{ border: 1px solid {border}; padding: 4px 8px; }}"
            f"h1, h2, h3 {{ margin-top: 14px; }}"
        )

    def _on_anchor(self, url: QUrl) -> None:
        if url.scheme() in ("http", "https"):
            QDesktopServices.openUrl(url)
            return
        target = url.toLocalFile() or url.path()
        if not target:
            return
        path = Path(target)
        if not path.is_absolute():
            path = (self._history[-1].parent / target).resolve()
        if path.suffix.lower() == ".md" and path.is_file():
            self.load(path)
        elif path.is_file():
            QDesktopServices.openUrl(QUrl.fromLocalFile(str(path)))

    def _go_back(self) -> None:
        if len(self._history) > 1:
            self._history.pop()
            self.load(self._history.pop())


def show_markdown(parent, path) -> MarkdownViewer:
    viewer = MarkdownViewer(parent)
    viewer.load(Path(path))
    viewer.show()
    return viewer
