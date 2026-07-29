"""Application entry point."""

from __future__ import annotations

import stat
import sys

from PySide6.QtWidgets import QApplication, QMessageBox

from ..project import Project, is_frozen, user_data_dir
from .main_window import MainWindow


def _ensure_executable(path) -> None:
    """Restore the executable bit stripped by PyInstaller data extraction."""
    try:
        mode = path.stat().st_mode
        path.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    except OSError:
        pass


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("Chiplet Simulator")

    try:
        project = Project.discover()
    except FileNotFoundError as exc:
        QMessageBox.critical(None, "Chiplet Simulator", str(exc))
        return 1

    # Operate on a managed setups workspace seeded from the bundled originals.
    if is_frozen():
        _ensure_executable(project.sim_binary)
        workspace = user_data_dir() / "setups"
    else:
        workspace = project.root / "tools" / "gui" / "setups"
    project = project.seed_workspace(workspace)

    window = MainWindow(project)
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
