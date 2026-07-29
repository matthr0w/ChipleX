"""Dialog to create or edit a setup graphically."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Dict, Optional

import yaml
from PySide6.QtCore import QUrl
from PySide6.QtGui import QDesktopServices
from PySide6.QtWidgets import (QDialog, QDialogButtonBox, QHBoxLayout, QLabel,
                               QMessageBox, QVBoxLayout)

from .. import setup_writer
from ..project import Project
from .graph_editor import GraphEditor
from .markdown_viewer import show_markdown
from .theme import link

_LAYOUT_FILE = ".layout.json"


class SetupEditorDialog(QDialog):
    def __init__(self, project: Project, setup_name: str, is_new: bool = False, parent=None):
        super().__init__(parent)
        self.project = project
        self.name = setup_name
        self.is_new = is_new
        self.saved_name: Optional[str] = None
        self.setWindowTitle(f"{'' if is_new else 'Edit'} {setup_name}")
        self.resize(1080, 720)

        if is_new:
            doc = setup_writer.new_system_doc()
            layout: Dict = {}
        else:
            setup_dir = project.setup_dir(setup_name)
            doc = yaml.safe_load((setup_dir / "system.yaml").read_text()) or setup_writer.new_system_doc()
            layout = _load_layout(setup_dir)

        self._editor = GraphEditor(project, doc, layout)

        open_code = QDialogButtonBox(QDialogButtonBox.Open)
        open_code.button(QDialogButtonBox.Open).setText("Open program code")
        open_code.accepted.connect(self._open_code)

        docs_path = project.root / "docs" / "PROGRAM-CODE.md"
        hint = QLabel(link("#", "How to write program code"))
        hint.setToolTip("Open the program-code guide")
        hint.linkActivated.connect(lambda _=None, p=docs_path: show_markdown(self, p))

        buttons = QDialogButtonBox(QDialogButtonBox.Save | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self._save)
        buttons.rejected.connect(self.reject)

        footer = QHBoxLayout()
        footer.addWidget(open_code)
        footer.addWidget(hint)
        footer.addStretch(1)
        footer.addWidget(buttons)

        layout_box = QVBoxLayout(self)
        layout_box.addWidget(self._editor, 1)
        layout_box.addLayout(footer)

    def _persist(self) -> bool:
        """Write the setup to the workspace, scaffolding or syncing program code."""
        self._editor.commit()
        doc = self._editor.doc
        setups_dir = self.project.setups_dir

        if not (setups_dir / self.name).exists():
            setup_writer.create_setup(setups_dir, self.name, doc)
        else:
            setup_writer.write_system(setups_dir, self.name, doc)

        _save_layout(self.project.setup_dir(self.name), self._editor.layout_positions())
        return True

    def _save(self) -> None:
        self._persist()
        self.saved_name = self.name
        self.accept()

    def _open_code(self) -> None:
        self._persist()
        path = setup_writer.program_path(self.project.setups_dir, self.name)
        if path.is_file():
            QDesktopServices.openUrl(QUrl.fromLocalFile(str(path)))
        else:
            QMessageBox.warning(self, "Program code", "Could not create program.cpp.")


def _load_layout(setup_dir: Path) -> Dict:
    path = setup_dir / _LAYOUT_FILE
    if path.is_file():
        try:
            return json.loads(path.read_text())
        except (ValueError, OSError):
            return {}
    return {}


def _save_layout(setup_dir: Path, layout: Dict) -> None:
    try:
        (setup_dir / _LAYOUT_FILE).write_text(json.dumps(layout, indent=2))
    except OSError:
        pass
