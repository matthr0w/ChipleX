"""Manage the workspace setups: create, edit, duplicate, remove, and build."""

from __future__ import annotations

import shutil

from PySide6.QtCore import Qt, QThread, Signal
from PySide6.QtWidgets import (QAbstractItemView, QDialog, QDialogButtonBox,
                               QHBoxLayout, QInputDialog, QListWidget,
                               QMessageBox, QPlainTextEdit, QPushButton,
                               QVBoxLayout, QWidget)

from .. import setup_writer
from ..project import Project
from ..setup_builder import BuildResult, build_setup
from .setup_editor import SetupEditorDialog


class _BuildWorker(QThread):
    done = Signal(object)

    def __init__(self, project: Project, name: str):
        super().__init__()
        self._project = project
        self._name = name

    def run(self) -> None:
        self.done.emit(build_setup(self._project, self._name))


def _valid_name(name: str) -> bool:
    return bool(name) and all(c.isalnum() or c == "_" for c in name)


class SetupsTab(QWidget):
    setups_changed = Signal()
    status = Signal(str)

    def __init__(self, project: Project, parent=None):
        super().__init__(parent)
        self.project = project
        self._worker: _BuildWorker | None = None

        self._list = QListWidget()
        self._list.setSelectionMode(QAbstractItemView.SingleSelection)
        self._list.itemDoubleClicked.connect(lambda _i: self._edit())

        new_btn = QPushButton("New...")
        edit_btn = QPushButton("Edit...")
        dup_btn = QPushButton("Duplicate")
        rm_btn = QPushButton("Remove")
        self._build_btn = QPushButton("Build")
        new_btn.clicked.connect(self._new)
        edit_btn.clicked.connect(self._edit)
        dup_btn.clicked.connect(self._duplicate)
        rm_btn.clicked.connect(self._remove)
        self._build_btn.clicked.connect(self._build)

        buttons = QHBoxLayout()
        for btn in (new_btn, edit_btn, dup_btn, rm_btn, self._build_btn):
            buttons.addWidget(btn)
        buttons.addStretch(1)

        layout = QVBoxLayout(self)
        layout.addLayout(buttons)
        layout.addWidget(self._list, 1)

        self.refresh()

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
        name, ok = QInputDialog.getText(self, "New setup", "Setup name:")
        name = name.strip()
        if not ok or not name:
            return
        if not _valid_name(name):
            QMessageBox.warning(self, "New setup", "Use only letters, digits, or underscores.")
            return
        if (self.project.setups_dir / name).exists():
            QMessageBox.warning(self, "New setup", f"A setup named '{name}' already exists.")
            return
        dialog = SetupEditorDialog(self.project, name, is_new=True, parent=self)
        if dialog.exec() and dialog.saved_name:
            self.refresh()
            self.setups_changed.emit()
            self.status.emit(f"Created setup '{dialog.saved_name}'. Build it before running.")

    def _edit(self) -> None:
        name = self._selected()
        if not name:
            return
        dialog = SetupEditorDialog(self.project, name, is_new=False, parent=self)
        if dialog.exec():
            self.refresh()
            self.setups_changed.emit()
            self.status.emit(f"Edited setup '{name}'. Rebuild it before running.")

    def _duplicate(self) -> None:
        name = self._selected()
        if not name:
            return
        new_name, ok = QInputDialog.getText(self, "Duplicate setup", "Setup name:", text=f"{name}_copy")
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

    def _build(self) -> None:
        name = self._selected()
        if not name or self._worker is not None:
            return
        self._build_btn.setEnabled(False)
        self.status.emit(f"Building '{name}'...")
        self._worker = _BuildWorker(self.project, name)
        self._worker.done.connect(self._build_done)
        self._worker.start()

    def _build_done(self, result: BuildResult) -> None:
        self._worker = None
        self._build_btn.setEnabled(True)
        name = self._selected() or ""
        if result.ok:
            self.status.emit(f"Built '{name}' successfully.")
            QMessageBox.information(self, "Build", f"Built '{name}' successfully.")
        else:
            self.status.emit(f"Build of '{name}' failed.")
            _show_log("Build failed", result.log, self)


def _show_log(title: str, text: str, parent) -> None:
    dialog = QDialog(parent)
    dialog.setWindowTitle(title)
    dialog.resize(760, 480)
    view = QPlainTextEdit()
    view.setReadOnly(True)
    view.setPlainText(text)
    buttons = QDialogButtonBox(QDialogButtonBox.Close)
    buttons.rejected.connect(dialog.reject)
    buttons.accepted.connect(dialog.accept)
    layout = QVBoxLayout(dialog)
    layout.addWidget(view)
    layout.addWidget(buttons)
    dialog.exec()
