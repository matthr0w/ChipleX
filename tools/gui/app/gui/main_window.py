"""Main application window: run list, sweep, and comparison tabs."""

from __future__ import annotations

import os
import tempfile
from pathlib import Path
from typing import List

from PySide6.QtWidgets import (QHBoxLayout, QLabel, QMainWindow, QMessageBox,
                               QProgressBar, QPushButton, QSpinBox, QTabWidget,
                               QVBoxLayout, QWidget)

from ..cycle_estimation import EstimationStatus
from ..project import Project
from ..runner import RunResult
from .results_tab import ResultsTab
from .runs_tab import RunsTab
from .setups_tab import SetupsTab
from .sweep_tab import SweepTab
from .worker import BatchController


class MainWindow(QMainWindow):
    def __init__(self, project: Project):
        super().__init__()
        self._project = project
        self.setWindowTitle("ChipleX")
        self.resize(1080, 720)

        setups = project.list_setups()

        self._setups_tab = SetupsTab(project)
        self._runs_tab = RunsTab(project, setups)
        self._sweep_tab = SweepTab(project, setups)
        self._results_tab = ResultsTab()

        self._tabs = QTabWidget()
        self._tabs.addTab(self._setups_tab, "Setups")
        self._tabs.addTab(self._runs_tab, "Runs")
        self._tabs.addTab(self._sweep_tab, "Sweep")
        self._tabs.addTab(self._results_tab, "Results")

        self._sweep_tab.runs_generated.connect(self._on_runs_generated)
        self._setups_tab.setups_changed.connect(self._on_setups_changed)
        self._setups_tab.status.connect(self._status_text)

        cpu = os.cpu_count() or 4
        self._cores = QSpinBox()
        self._cores.setRange(1, cpu)
        self._cores.setValue(max(1, cpu - 1))
        self._cores.setPrefix("Cores: ")

        self._timeout = QSpinBox()
        self._timeout.setRange(1, 24 * 3600)
        self._timeout.setValue(1800)
        self._timeout.setSuffix(" s timeout")

        self._run_btn = QPushButton("Run")
        self._run_btn.clicked.connect(self._run)
        self._cancel_btn = QPushButton("Cancel")
        self._cancel_btn.clicked.connect(self._cancel)
        self._cancel_btn.setEnabled(False)

        self._progress = QProgressBar()
        self._progress.setTextVisible(True)
        self._status = QLabel("Ready")

        controls = QHBoxLayout()
        controls.addWidget(self._cores)
        controls.addWidget(self._timeout)
        controls.addWidget(self._run_btn)
        controls.addWidget(self._cancel_btn)
        controls.addWidget(self._progress, 1)

        central = QWidget()
        outer = QVBoxLayout(central)
        outer.addWidget(self._tabs, 1)
        outer.addLayout(controls)
        outer.addWidget(self._status)
        self.setCentralWidget(central)

        # The controller re-emits on the GUI thread (its own signals are fed by
        # queued relays from the worker thread), so default connections resolve
        # to direct calls on the GUI thread here.
        self._controller = BatchController(self)
        self._controller.run_started.connect(self._on_run_started)
        self._controller.run_output.connect(self._on_run_output)
        self._controller.run_finished.connect(self._on_run_finished)
        self._controller.batch_finished.connect(self._on_batch_finished)

        self._workdir = Path(tempfile.gettempdir()) / "sysc_dse_runs"
        self._total = 0
        self._done = 0

        for problem in project.preflight():
            self._status.setText(problem)

    def _status_text(self, text: str) -> None:
        self._status.setText(text)

    def _on_setups_changed(self) -> None:
        setups = self._project.list_setups()
        self._runs_tab.set_setups(setups)
        self._sweep_tab.set_setups(setups)

    def _on_runs_generated(self, specs: List) -> None:
        self._runs_tab.add_specs(specs)
        self._tabs.setCurrentWidget(self._runs_tab)
        self._status.setText(f"Added {len(specs)} run(s) to the run list.")

    def _run(self) -> None:
        if self._controller.is_running():
            return
        problems = self._project.preflight()
        if problems:
            QMessageBox.critical(self, "Cannot run", "\n".join(problems))
            return
        specs = self._runs_tab.enabled_specs()
        if not specs:
            QMessageBox.information(self, "Run", "No enabled runs in the run list.")
            return

        self._runs_tab.reset_statuses("QUEUED")
        self._runs_tab.clear_output()
        self._total = len(specs)
        self._done = 0
        self._progress.setRange(0, self._total)
        self._progress.setValue(0)
        self._run_btn.setEnabled(False)
        self._cancel_btn.setEnabled(True)
        self._status.setText(f"Running {self._total} run(s) on {self._cores.value()} core(s)...")

        self._controller.start(
            self._project,
            specs,
            self._workdir,
            self._cores.value(),
            self._timeout.value(),
            self._runs_tab.log_level(),
        )

    def _cancel(self) -> None:
        self._controller.cancel()
        self._status.setText("Cancelling...")
        self._cancel_btn.setEnabled(False)

    def _on_run_started(self, label: str) -> None:
        self._runs_tab.set_status(label, "RUNNING")
        self._runs_tab.start_output(label)

    def _on_run_output(self, label: str, chunk: str) -> None:
        self._runs_tab.append_output_chunk(label, chunk)

    def _on_run_finished(self, result: RunResult) -> None:
        self._done += 1
        self._progress.setValue(self._done)
        if result.ok:
            status = "WARNING" if result.estimation_status != EstimationStatus.SUCCESS else "PASS"
        elif result.cancelled:
            status = "CANCELLED"
        else:
            status = "FAIL"
        self._runs_tab.set_status(result.spec.label, status)

    def _on_batch_finished(self, results: List[RunResult]) -> None:
        self._run_btn.setEnabled(True)
        self._cancel_btn.setEnabled(False)
        ok = sum(1 for r in results if r.ok)
        failed = len(results) - ok
        self._results_tab.set_results(results)
        # Only jump to the results automatically when every run is a clean PASS;
        # a WARNING (estimation skipped) or FAIL keeps the Runs tab in view so
        # the status column draws attention to it.
        all_pass = bool(results) and all(
            r.ok and r.estimation_status == EstimationStatus.SUCCESS for r in results
        )
        if all_pass:
            self._tabs.setCurrentWidget(self._results_tab)
        self._status.setText(f"Done: {ok} succeeded, {failed} failed.")
