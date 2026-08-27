"""Runs a batch on a QThread and reports progress via Qt signals.

The Runner's callbacks fire from its thread pool, so the controller re-emits on
the GUI thread (queued delivery) before slots run.
"""

from __future__ import annotations

from pathlib import Path
from typing import List

from PySide6.QtCore import QObject, QThread, Signal

from ..project import Project
from ..runner import Runner, RunResult
from ..runspec import RunSpec


class BatchWorker(QObject):
    run_started = Signal(str)  # label
    run_output = Signal(str, str)  # label, text chunk
    run_finished = Signal(object)  # RunResult
    batch_finished = Signal(list)  # List[RunResult]

    def __init__(
        self,
        project: Project,
        specs: List[RunSpec],
        workdir: Path,
        max_workers: int,
        timeout_s: int,
        log_level: str,
    ):
        super().__init__()
        self._project = project
        self._specs = specs
        self._runner = Runner(
            project, workdir, max_workers, timeout_s=timeout_s, log_level=log_level
        )

    def cancel(self) -> None:
        self._runner.cancel()

    def run(self) -> None:
        results = self._runner.run_batch(
            self._specs,
            on_start=lambda spec: self.run_started.emit(spec.label),
            on_finish=lambda result: self.run_finished.emit(result),
            on_output=lambda spec, chunk: self.run_output.emit(spec.label, chunk),
        )
        self.batch_finished.emit(results)


class BatchController(QObject):
    """Owns the worker thread and re-exposes its signals to the window."""

    run_started = Signal(str)
    run_output = Signal(str, str)
    run_finished = Signal(object)
    batch_finished = Signal(list)

    def __init__(self, parent: QObject | None = None):
        super().__init__(parent)
        self._thread: QThread | None = None
        self._worker: BatchWorker | None = None

    def is_running(self) -> bool:
        return self._thread is not None and self._thread.isRunning()

    def start(
        self,
        project: Project,
        specs: List[RunSpec],
        workdir: Path,
        max_workers: int,
        timeout_s: int,
        log_level: str = "SILENT",
    ) -> None:
        if self.is_running():
            return
        self._thread = QThread()
        self._worker = BatchWorker(
            project, specs, workdir, max_workers, timeout_s, log_level
        )
        self._worker.moveToThread(self._thread)

        self._worker.run_started.connect(self.run_started)
        self._worker.run_output.connect(self.run_output)
        self._worker.run_finished.connect(self.run_finished)
        self._worker.batch_finished.connect(self.batch_finished)
        self._worker.batch_finished.connect(self._cleanup)

        self._thread.started.connect(self._worker.run)
        self._thread.start()

    def cancel(self) -> None:
        if self._worker is not None:
            self._worker.cancel()

    def _cleanup(self, _results) -> None:
        if self._thread is not None:
            self._thread.quit()
            self._thread.wait()
        self._worker = None
        self._thread = None
