"""Manage the workspace setups: create, edit, duplicate, remove, and build."""

from __future__ import annotations

import shutil

from PySide6.QtCore import Qt, QThread, Signal
from PySide6.QtWidgets import (QAbstractItemView, QDialog, QDialogButtonBox,
                               QHBoxLayout, QInputDialog, QLabel, QListWidget,
                               QMessageBox, QPlainTextEdit, QPushButton,
                               QVBoxLayout, QWidget)

from .. import setup_writer
from ..cycle_estimation import run_cycle_estimation
from ..project import Project, missing_build_tools, missing_cycle_tools
from ..setup_builder import BuildResult, build_setup
from .setup_editor import SetupEditorDialog


_TOOL_URLS = {
    "cmake": "https://cmake.org/download/",
    "C++ compiler (g++ or clang++)": "https://gcc.gnu.org/install/",
    "LLVM": "https://llvm.org",
    "RISC-V GNU Compiler Toolchain": "https://github.com/riscv-collab/riscv-gnu-toolchain",
    "RISC-V Proxy Kernel and Boot Loader": "https://github.com/riscv-software-src/riscv-pk",
    "Spike RISC-V ISA Simulator": "https://github.com/riscv-software-src/riscv-isa-sim",
}


def _tool_item(name: str) -> str:
    url = _TOOL_URLS.get(name)
    return f'<li><a href="{url}">{name}</a></li>' if url else f"<li>{name}</li>"


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


class _BuildWorker(QThread):
    done = Signal(object)

    def __init__(self, project: Project, name: str):
        super().__init__()
        self._project = project
        self._name = name

    def run(self) -> None:
        # Refresh workload cycle counts, then compile the setup plugin.
        cycles = run_cycle_estimation(self._project, self._name)
        result = build_setup(self._project, self._name)
        log = f"=== Cycle estimation ===\n{cycles.log}\n\n=== Build ===\n{result.log}"
        note = "Cycle estimation failed; see log." if cycles.status == "failed" else ""
        self.done.emit(BuildResult(result.ok, log, note))


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
        for banner in self._environment_banners():
            layout.addWidget(banner)
        layout.addLayout(buttons)
        layout.addWidget(self._list, 1)

        self.refresh()

    def _environment_banners(self) -> list:
        """Warning banners for missing build and cycle-estimation tools."""
        banners = []
        build_missing = missing_build_tools()
        if build_missing:
            self._build_btn.setEnabled(False)
            self._build_btn.setToolTip("Install " + ", ".join(build_missing))
            banners.append(_warning_banner(
                "setups cannot be built without:",
                build_missing,
                "You can still create and edit setups; install these to build and run them.",
            ))
        cycle_missing = missing_cycle_tools()
        if cycle_missing:
            banners.append(_warning_banner(
                "cycle estimation is disabled and will be skipped without:",
                cycle_missing,
                "Setups run with their existing workload cycle counts; new or "
                "changed workloads are not estimated.",
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
            if result.note:
                _show_log(f"Built '{name}' ({result.note})", result.log, self)
            else:
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
