"""Manage the workspace setups: create, edit, duplicate, and remove.

Setups are built and cycle-estimated automatically as part of running them
(see runner.py).
"""

from __future__ import annotations

import shutil

from PySide6.QtCore import Qt, Signal
from PySide6.QtWidgets import (QAbstractItemView, QHBoxLayout, QInputDialog,
                               QLabel, QListWidget, QMessageBox, QPushButton,
                               QVBoxLayout, QWidget)

from .. import setup_writer
from ..project import Project, missing_build_tools
from .setup_editor import SetupEditorDialog
from .theme import link

_TOOL_URLS = {
    "cmake": "https://cmake.org/download/",
    "C++ compiler (g++ or clang++)": "https://gcc.gnu.org/install/",
}


def _tool_item(name: str) -> str:
    url = _TOOL_URLS.get(name)
    return f"<li>{link(url, name)}</li>" if url else f"<li>{name}</li>"


def _warning_banner(intro: str, tools: list, outro: str) -> QLabel:
    items = "".join(_tool_item(t) for t in tools)
    label = QLabel(f"Warning: {intro}<ul>{items}</ul>{outro}")
    label.setWordWrap(True)
    label.setOpenExternalLinks(True)
    label.setTextInteractionFlags(Qt.TextBrowserInteraction)
    label.setStyleSheet(
        "QLabel { color: #9a6700; border: 1px solid #c8a000;"
        " border-radius: 4px; padding: 6px; }"
    )
    return label


def _valid_name(name: str) -> bool:
    return bool(name) and all(c.isalnum() or c == "_" for c in name)


class SetupsTab(QWidget):
    setups_changed = Signal()
    status = Signal(str)

    def __init__(self, project: Project, parent=None):
        super().__init__(parent)
        self.project = project

        self._list = QListWidget()
        self._list.setSelectionMode(QAbstractItemView.SingleSelection)
        self._list.itemDoubleClicked.connect(lambda _i: self._edit())

        new_btn = QPushButton("New...")
        edit_btn = QPushButton("Edit...")
        dup_btn = QPushButton("Duplicate")
        rm_btn = QPushButton("Remove")
        new_btn.clicked.connect(self._new)
        edit_btn.clicked.connect(self._edit)
        dup_btn.clicked.connect(self._duplicate)
        rm_btn.clicked.connect(self._remove)

        buttons = QHBoxLayout()
        for btn in (new_btn, edit_btn, dup_btn, rm_btn):
            buttons.addWidget(btn)
        buttons.addStretch(1)

        layout = QVBoxLayout(self)
        for banner in self._environment_banners():
            layout.addWidget(banner)
        layout.addLayout(buttons)
        layout.addWidget(self._list, 1)

        self.refresh()

    def _environment_banners(self) -> list:
        """Warning banner for missing build tools."""
        banners = []
        build_missing = missing_build_tools()
        if build_missing:
            banners.append(_warning_banner(
                "Setups cannot be built or run without:",
                build_missing,
                "You can still create and edit setups; install these to build and run them.",
            ))
        return banners

    def refresh(self) -> None:
        current = self._selected()
        self._list.clear()
        self._list.addItems(self.project.list_setups())
        if current:
            matches = self._list.findItems(current, Qt.MatchExactly)
            if matches:
                self._list.setCurrentItem(matches[0])

    def _selected(self) -> str | None:
        item = self._list.currentItem()
        return item.text() if item is not None else None

    def _new(self) -> None:
        name, ok = QInputDialog.getText(self, "New Setup", "Setup name:")
        name = name.strip()
        if not ok or not name:
            return
        if not _valid_name(name):
            QMessageBox.warning(self, "New Setup", "Use only letters, digits, or underscores.")
            return
        if (self.project.setups_dir / name).exists():
            QMessageBox.warning(self, "New Setup", f"A setup named '{name}' already exists.")
            return
        dialog = SetupEditorDialog(self.project, name, is_new=True, parent=self)
        if dialog.exec() and dialog.saved_name:
            self.refresh()
            self.setups_changed.emit()
            self.status.emit(f"Created setup '{dialog.saved_name}'.")

    def _edit(self) -> None:
        name = self._selected()
        if not name:
            return
        dialog = SetupEditorDialog(self.project, name, is_new=False, parent=self)
        if dialog.exec():
            self.refresh()
            self.setups_changed.emit()
            self.status.emit(f"Edited setup '{name}'.")

    def _duplicate(self) -> None:
        name = self._selected()
        if not name:
            return
        new_name, ok = QInputDialog.getText(self, "Duplicate Setup", "Setup name:", text=f"{name}_copy")
        new_name = new_name.strip()
        if not ok or not new_name:
            return
        if not _valid_name(new_name):
            QMessageBox.warning(self, "Duplicate", "Use only letters, digits, or underscores.")
            return
        if (self.project.setups_dir / new_name).exists():
            QMessageBox.warning(self, "Duplicate", f"A setup named '{new_name}' already exists.")
            return
        shutil.copytree(self.project.setup_dir(name), self.project.setup_dir(new_name))
        self.refresh()
        self.setups_changed.emit()

    def _remove(self) -> None:
        name = self._selected()
        if not name:
            return
        confirm = QMessageBox.question(
            self, "Remove setup",
            f"Remove setup '{name}' from the workspace?",
        )
        if confirm == QMessageBox.Yes:
            setup_writer.delete_setup(self.project.setups_dir, name)
            self.refresh()
            self.setups_changed.emit()
