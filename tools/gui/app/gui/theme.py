"""Follow the desktop light/dark color scheme in bundled builds."""

from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QColor, QPalette
from PySide6.QtWidgets import QApplication

LINK_COLOR = "#58a6ff"


def link(href: str, text: str) -> str:
    """Return a rich-text anchor with the app's standard link color."""
    return f'<a href="{href}" style="color: {LINK_COLOR};">{text}</a>'


def _dark_palette() -> QPalette:
    """A dark palette tuned to pair with the Fusion style."""
    window = QColor(43, 43, 43)
    base = QColor(30, 30, 30)
    alt = QColor(51, 51, 51)
    text = QColor(220, 220, 220)
    disabled = QColor(110, 110, 110)
    highlight = QColor(74, 107, 138)

    palette = QPalette()
    palette.setColor(QPalette.Window, window)
    palette.setColor(QPalette.WindowText, text)
    palette.setColor(QPalette.Base, base)
    palette.setColor(QPalette.AlternateBase, alt)
    palette.setColor(QPalette.ToolTipBase, window)
    palette.setColor(QPalette.ToolTipText, text)
    palette.setColor(QPalette.Text, text)
    palette.setColor(QPalette.Button, window)
    palette.setColor(QPalette.ButtonText, text)
    palette.setColor(QPalette.BrightText, QColor(255, 80, 80))
    palette.setColor(QPalette.Link, QColor(LINK_COLOR))
    palette.setColor(QPalette.Highlight, highlight)
    palette.setColor(QPalette.HighlightedText, QColor(255, 255, 255))
    palette.setColor(QPalette.PlaceholderText, disabled)
    for role in (QPalette.WindowText, QPalette.Text, QPalette.ButtonText):
        palette.setColor(QPalette.Disabled, role, disabled)
    return palette


def apply_theme(app: QApplication) -> None:
    """Style the app with Fusion and follow the portal color scheme, live."""
    app.setStyle("Fusion")
    light = app.style().standardPalette()
    hints = app.styleHints()

    def sync() -> None:
        dark = hints.colorScheme() == Qt.ColorScheme.Dark
        app.setPalette(_dark_palette() if dark else light)

    sync()
    hints.colorSchemeChanged.connect(lambda _scheme: sync())
