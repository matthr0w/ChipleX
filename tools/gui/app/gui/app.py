"""Application entry point."""

from __future__ import annotations

import sys

from PySide6.QtWidgets import QApplication, QMessageBox

from ..project import Project
from .main_window import MainWindow


def main() -> int:
    app = QApplication(sys.argv)
    app.setApplicationName("Chiplet Simulator")

    try:
        project = Project.discover()
    except FileNotFoundError as exc:
        QMessageBox.critical(None, "Chiplet Simulator", str(exc))
        return 1

    # Operate on a managed setups workspace seeded from the repository.
    workspace = project.root / "tools" / "gui" / "setups"
    project = project.seed_workspace(workspace)

    window = MainWindow(project)
    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
