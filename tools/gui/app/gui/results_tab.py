"""Compare completed runs: a metrics table and a per-metric chart."""

from __future__ import annotations

import csv
from pathlib import Path
from typing import List

import pyqtgraph as pg
from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QAbstractItemView,
    QComboBox,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QSplitter,
    QTableWidget,
    QTableWidgetItem,
    QVBoxLayout,
    QWidget,
)

from .. import stats
from ..runner import RunResult

_HEADLINE_HINTS = (
    "simulation_time",
    "transaction_count",
    "hit_rate",
    "utilization.percentage",
)


class ResultsTab(QWidget):
    def __init__(self, parent=None):
        super().__init__(parent)
        self._results: List[RunResult] = []
        self._metric_names: List[str] = []

        self._filter = QLineEdit()
        self._filter.setPlaceholderText("Filter metrics (columns)...")
        self._filter.textChanged.connect(self._apply_filter)

        self._metric_combo = QComboBox()
        self._metric_combo.currentIndexChanged.connect(self._draw_plot)

        export_btn = QPushButton("Export CSV...")
        export_btn.clicked.connect(self._export_csv)

        top = QHBoxLayout()
        top.addWidget(QLabel("Metric:"))
        top.addWidget(self._metric_combo, 2)
        top.addWidget(self._filter, 3)
        top.addWidget(export_btn)

        self._table = QTableWidget(0, 0)
        self._table.setEditTriggers(QAbstractItemView.NoEditTriggers)
        self._table.setSelectionBehavior(QAbstractItemView.SelectRows)

        self._plot = pg.PlotWidget()
        self._plot.setBackground(None)
        self._plot.showGrid(x=False, y=True, alpha=0.3)

        splitter = QSplitter(Qt.Vertical)
        splitter.addWidget(self._table)
        splitter.addWidget(self._plot)
        splitter.setSizes([320, 260])

        layout = QVBoxLayout(self)
        layout.addLayout(top)
        layout.addWidget(splitter, 1)

    def set_results(self, results: List[RunResult]) -> None:
        self._results = [r for r in results if r.ok]
        metric_maps = [r.metrics for r in self._results]
        self._metric_names = stats.union_metric_names(metric_maps)
        self._rebuild_table()
        self._rebuild_metric_combo()
        self._draw_plot()

    def _ok_labels(self) -> List[str]:
        return [r.spec.label for r in self._results]

    def _rebuild_table(self) -> None:
        labels = self._ok_labels()
        self._table.clear()
        self._table.setColumnCount(1 + len(self._metric_names))
        self._table.setRowCount(len(labels))
        self._table.setHorizontalHeaderLabels(["Run"] + self._metric_names)

        for row, result in enumerate(self._results):
            self._table.setItem(row, 0, QTableWidgetItem(result.spec.label))
            for col, metric in enumerate(self._metric_names, start=1):
                value = result.metrics.get(metric)
                text = "" if value is None else f"{value:g}"
                self._table.setItem(row, col, QTableWidgetItem(text))
        self._table.resizeColumnsToContents()
        self._apply_filter(self._filter.text())

    def _rebuild_metric_combo(self) -> None:
        self._metric_combo.blockSignals(True)
        self._metric_combo.clear()
        self._metric_combo.addItems(self._metric_names)
        default = _default_metric(self._metric_names)
        if default is not None:
            self._metric_combo.setCurrentText(default)
        self._metric_combo.blockSignals(False)

    def _draw_plot(self) -> None:
        self._plot.clear()
        metric = self._metric_combo.currentText()
        if not metric or not self._results:
            return
        labels = self._ok_labels()
        heights = [float(r.metrics.get(metric, 0.0)) for r in self._results]
        xs = list(range(len(labels)))

        bar = pg.BarGraphItem(
            x=xs, height=heights, width=0.6, brush=pg.mkBrush(80, 130, 200)
        )
        self._plot.addItem(bar)
        self._plot.setTitle(metric)
        # Label the x-axis by run number (labels are shown in the table's # column);
        # full run labels would overlap on the axis.
        axis = self._plot.getAxis("bottom")
        axis.setTicks([list(zip(xs, [str(i + 1) for i in xs]))])
        axis.setLabel("run #")
        self._plot.getAxis("left").setLabel(metric)
        self._plot.enableAutoRange()

    def _apply_filter(self, text: str) -> None:
        needle = text.strip().lower()
        for col, metric in enumerate(self._metric_names, start=1):
            visible = needle in metric.lower()
            self._table.setColumnHidden(col, not visible)

    def _export_csv(self) -> None:
        if not self._results:
            return
        path, _ = QFileDialog.getSaveFileName(
            self, "Export results", "dse_results.csv", "CSV (*.csv)"
        )
        if not path:
            return
        with Path(path).open("w", newline="") as handle:
            writer = csv.writer(handle)
            writer.writerow(["#", "run"] + self._metric_names)
            for index, result in enumerate(self._results):
                row = [index + 1, result.spec.label]
                row += [result.metrics.get(m, "") for m in self._metric_names]
                writer.writerow(row)


def _default_metric(names: List[str]):
    for hint in _HEADLINE_HINTS:
        for name in names:
            if hint in name:
                return name
    return names[0] if names else None
