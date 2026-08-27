"""Execute a batch of runs concurrently across CPU cores.

Qt-independent; driven via callbacks. Each run executes in its own sandbox and
its statistics are parsed into a RunResult.
"""

from __future__ import annotations

import os
import re
import shutil
import signal
import subprocess
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor, as_completed
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from threading import Event, Thread
from typing import Callable, Dict, List, Optional, TextIO

from . import stats
from .cycle_estimation import EstimationStatus, run_cycle_estimation
from .project import Project
from .runspec import RunSpec
from .setup_builder import build_setup_if_needed

DEFAULT_TIMEOUT_S = 3600
_TAIL_CHARS = 20000
# CSI sequences, which cover the SGR colour codes the simulator emits.
_ANSI_ESCAPE = re.compile("\x1b\\[[0-?]*[ -/]*[@-~]")


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
    estimation_status: EstimationStatus = EstimationStatus.SUCCESS


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

        # Build each distinct setup's plugin once up front (hash-gated), before
        # the run pool. The compiled libsetup.so is shared by all runs of that
        # setup regardless of their overrides, and building here avoids
        # concurrent CMake invocations racing on the shared build directory.
        self._builds = {}
        for setup in dict.fromkeys(spec.setup for spec in specs):
            if self._cancel.is_set():
                break
            self._builds[setup] = build_setup_if_needed(
                self.project, setup, self.timeout_s
            )

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

    def _run_one(
        self,
        spec: RunSpec,
        on_start: Optional[Callable],
        on_output: Optional[Callable] = None,
    ) -> RunResult:
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
        log_file = None
        try:
            log_file = _open_log(spec.log_path)
            self._emit(
                spec,
                on_output,
                log_file,
                f"Run '{spec.label}' of setup '{spec.setup}' started {_timestamp()}",
            )
            spec.build_sandbox(self.project, sandbox)

            # Plugin build: compiled once per setup in run_batch; replay its log
            # here and fail the run if that setup did not build.
            self._emit(spec, on_output, log_file, "Build")
            build = self._builds.get(spec.setup)
            if build is not None:
                self._emit_raw(spec, on_output, log_file, build.log)
                if not build.ok:
                    result.error = "Setup build failed"
                    result.duration_s = time.monotonic() - started
                    return result

            # Cycle estimation against this run's sandbox, so the workload cycle
            # counts match the exact system.yaml (with overrides) the sim loads.
            self._emit(spec, on_output, log_file, "Cycle Estimation")
            estimation = run_cycle_estimation(
                self.project,
                spec.setup,
                setups_dir=sandbox / "setups",
                build_dir=sandbox / "ce_build",
            )
            result.estimation_status = estimation.status
            self._emit_raw(spec, on_output, log_file, estimation.log)
            if estimation.status == EstimationStatus.SUCCESS:
                self._write_back_estimate(spec, sandbox)

            self._emit(spec, on_output, log_file, "Simulation")
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
            output = self._stream(proc, spec, on_output, log_file)
            result.returncode = proc.returncode
            result.stdout_tail = output[-_TAIL_CHARS:]
            result.cancelled = self._cancel.is_set()
            result.duration_s = time.monotonic() - started
            outcome = (
                "cancelled"
                if result.cancelled
                else f"exited with code {result.returncode}"
            )
            self._emit(
                spec,
                on_output,
                log_file,
                f"Run '{spec.label}' of setup '{spec.setup}' {outcome} after {result.duration_s:.1f} s",
            )
        except Exception as exc:  # noqa: BLE001 - surface any launch failure to the UI
            result.error = str(exc)
            result.duration_s = time.monotonic() - started
            self._emit(
                spec,
                on_output,
                log_file,
                f"Run '{spec.label}' of setup '{spec.setup}' failed: {exc}",
            )
            return result
        finally:
            if log_file is not None:
                log_file.close()
            if not self.keep_sandboxes:
                shutil.rmtree(sandbox, ignore_errors=True)

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

    def _write_back_estimate(self, spec: RunSpec, sandbox: Path) -> None:
        """Cache a fresh estimate into the workspace when it matches the default.

        Each run estimates in its own sandbox, so a default run's result would
        otherwise be discarded and gem5 would re-run for every run. The cycle
        counts depend only on gem5-relevant inputs, so a run that changed no
        gem5-relevant parameter produces the same input hash as the setup's
        default config; its sandbox workloads.yaml is therefore valid for the
        workspace and is copied back, letting later default runs (and the CLI)
        reuse it. A run that changed a gem5 parameter keeps its counts in the
        sandbox only, leaving the default cache intact.
        """
        if _has_gem5_override(spec):
            return
        src = sandbox / "setups" / spec.setup / "workloads.yaml"
        dst = self.project.setup_dir(spec.setup) / "workloads.yaml"
        if not src.is_file():
            return
        # Replace atomically: a sweep over a non-gem5 parameter runs many same
        # setup write-backs concurrently, and other runs read this file while
        # seeding their sandbox.
        fd, tmp_name = tempfile.mkstemp(dir=str(dst.parent), suffix=".tmp")
        os.close(fd)
        tmp = Path(tmp_name)
        try:
            shutil.copyfile(src, tmp)
            os.replace(tmp, dst)
        finally:
            tmp.unlink(missing_ok=True)

    def _stream(
        self,
        proc: subprocess.Popen,
        spec: RunSpec,
        on_output: Optional[Callable],
        log_file: Optional[TextIO] = None,
    ) -> str:
        """Read merged stdout/stderr live, forwarding each line to on_output.

        A reader thread iterates the pipe so lines surface as the simulator
        emits them; the main thread enforces cancellation and the timeout by
        killing the process, which ends the reader at EOF. Lines are flushed to
        the run's log file as they arrive so the file can be tailed while the
        batch is still running.
        """
        chunks: List[str] = []

        def reader() -> None:
            for line in proc.stdout:
                chunks.append(line)
                _write_log(log_file, line)
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

    def _emit(
        self,
        spec: RunSpec,
        on_output: Optional[Callable],
        log_file: Optional[TextIO],
        text: str,
    ) -> None:
        """Send a framework-generated marker line to the live output and the log."""
        line = f"\n=== {text} ===\n"
        _write_log(log_file, line)
        if on_output is not None:
            on_output(spec, line)

    def _emit_raw(
        self,
        spec: RunSpec,
        on_output: Optional[Callable],
        log_file: Optional[TextIO],
        text: str,
    ) -> None:
        """Send captured multi-line tool output verbatim to the live output and log."""
        if not text:
            return
        if not text.endswith("\n"):
            text += "\n"
        _write_log(log_file, text)
        if on_output is not None:
            on_output(spec, text)


def _open_log(log_path: Optional[Path]) -> Optional[TextIO]:
    """Open a run's log file for writing, creating missing parent directories.

    Raised errors are reported as run failures rather than silently dropping the
    log the user asked for.
    """
    if log_path is None:
        return None
    log_path = Path(log_path)
    log_path.parent.mkdir(parents=True, exist_ok=True)
    return log_path.open("w", encoding="utf-8")


def _write_log(log_file: Optional[TextIO], text: str) -> None:
    """Append to a run's log file, if one is set.

    Colour escapes are stripped because the file is read in editors that
    would otherwise show them as literal text.
    """
    if log_file is None:
        return
    log_file.write(_ANSI_ESCAPE.sub("", text))
    log_file.flush()


def _timestamp() -> str:
    return datetime.now().strftime("%Y-%m-%d %H:%M:%S")


def _kill(proc: subprocess.Popen) -> None:
    try:
        import os

        os.killpg(os.getpgid(proc.pid), signal.SIGTERM)
    except (ProcessLookupError, PermissionError):
        proc.terminate()


def _has_gem5_override(spec: RunSpec) -> bool:
    """True when the run overrides a gem5-relevant parameter.

    Such a run resolves to a different gem5 input hash than the setup default,
    so its estimate must not overwrite the default cache.
    """
    return any(getattr(ref, "gem5", False) for ref, _ in spec.overrides)


def _slug(text: str) -> str:
    keep = [c if c.isalnum() or c in ("-", "_") else "_" for c in text]
    return "".join(keep) or "run"
