"""Execute a batch of runs concurrently across CPU cores.

Qt-independent; driven via callbacks. Each run executes in its own sandbox and
its statistics are parsed into a RunResult.
"""

from __future__ import annotations

import shutil
import signal
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from pathlib import Path
from threading import Event, Thread
from typing import Callable, Dict, List, Optional

from . import stats
from .project import Project
from .runspec import RunSpec

DEFAULT_TIMEOUT_S = 3600
_TAIL_CHARS = 20000


@dataclass
class RunResult:
    spec: RunSpec
    ok: bool = False
    returncode: Optional[int] = None
    duration_s: float = 0.0
    stdout_tail: str = ""
    stderr_tail: str = ""
    metrics: Dict[str, float] = field(default_factory=dict)
    stats_path: Optional[Path] = None
    error: Optional[str] = None
    cancelled: bool = False


class Runner:
    def __init__(
        self,
        project: Project,
        workdir_root: Path,
        max_workers: int,
        timeout_s: int = DEFAULT_TIMEOUT_S,
        keep_sandboxes: bool = False,
        log_level: str = "SILENT",
    ):
        self.project = project
        self.workdir_root = Path(workdir_root)
        self.max_workers = max(1, int(max_workers))
        self.timeout_s = timeout_s
        self.keep_sandboxes = keep_sandboxes
        self.log_level = log_level
        self._cancel = Event()

    def cancel(self) -> None:
        self._cancel.set()

    def run_batch(
        self,
        specs: List[RunSpec],
        on_start: Optional[Callable[[RunSpec], None]] = None,
        on_finish: Optional[Callable[[RunResult], None]] = None,
        on_output: Optional[Callable[[RunSpec, str], None]] = None,
    ) -> List[RunResult]:
        self._cancel.clear()
        self.workdir_root.mkdir(parents=True, exist_ok=True)
        results: List[RunResult] = []

        with ThreadPoolExecutor(max_workers=self.max_workers) as pool:
            future_to_spec = {}
            for spec in specs:
                future = pool.submit(self._run_one, spec, on_start, on_output)
                future_to_spec[future] = spec

            for future in as_completed(future_to_spec):
                result = future.result()
                results.append(result)
                if on_finish is not None:
                    on_finish(result)

        order = {id(s): i for i, s in enumerate(specs)}
        results.sort(key=lambda r: order.get(id(r.spec), 0))
        return results

    def _run_one(self, spec: RunSpec, on_start: Optional[Callable],
                 on_output: Optional[Callable] = None) -> RunResult:
        result = RunResult(spec=spec)
        if self._cancel.is_set():
            result.cancelled = True
            result.error = "cancelled before start"
            return result

        if on_start is not None:
            on_start(spec)

        safe_label = _slug(spec.label)
        sandbox = self.workdir_root / f"run_{safe_label}"
        stats_path = self.workdir_root / f"{safe_label}.stats.json"
        result.stats_path = stats_path
        if stats_path.exists():
            stats_path.unlink()

        started = time.monotonic()
        try:
            spec.build_sandbox(self.project, sandbox)
            argv = spec.argv(self.project.sim_binary, stats_path, self.log_level)
            proc = subprocess.Popen(
                argv,
                cwd=sandbox,
                env=self.project.child_env(),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
                start_new_session=True,
            )
            output = self._stream(proc, spec, on_output)
            result.returncode = proc.returncode
            result.stdout_tail = output[-_TAIL_CHARS:]
            result.cancelled = self._cancel.is_set()
        except Exception as exc:  # noqa: BLE001 - surface any launch failure to the UI
            result.error = str(exc)
            result.duration_s = time.monotonic() - started
            return result
        finally:
            if not self.keep_sandboxes:
                shutil.rmtree(sandbox, ignore_errors=True)

        result.duration_s = time.monotonic() - started

        if result.cancelled:
            result.error = "cancelled"
            return result

        if result.returncode != 0:
            result.error = f"simulator exited with code {result.returncode}"
            return result

        if not stats_path.is_file():
            result.error = "run produced no statistics file"
            return result

        try:
            result.metrics = stats.flatten_file(stats_path)
            result.ok = True
        except Exception as exc:  # noqa: BLE001
            result.error = f"failed to parse statistics: {exc}"
        return result

    def _stream(self, proc: subprocess.Popen, spec: RunSpec, on_output: Optional[Callable]) -> str:
        """Read merged stdout/stderr live, forwarding each line to on_output.

        A reader thread iterates the pipe so lines surface as the simulator
        emits them; the main thread enforces cancellation and the timeout by
        killing the process, which ends the reader at EOF.
        """
        chunks: List[str] = []

        def reader() -> None:
            for line in proc.stdout:
                chunks.append(line)
                if on_output is not None:
                    on_output(spec, line)

        thread = Thread(target=reader, daemon=True)
        thread.start()

        deadline = time.monotonic() + self.timeout_s
        while thread.is_alive():
            thread.join(timeout=0.2)
            if self._cancel.is_set() or time.monotonic() > deadline:
                _kill(proc)
                thread.join(timeout=5)
                break
        proc.wait()
        return "".join(chunks)


def _kill(proc: subprocess.Popen) -> None:
    try:
        import os

        os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
    except (ProcessLookupError, PermissionError):
        proc.terminate()


def _slug(text: str) -> str:
    keep = [c if c.isalnum() or c in ("-", "_") else "_" for c in text]
    return "".join(keep) or "run"
