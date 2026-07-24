"""Expand a parameter sweep (Cartesian product of axes) into concrete runs.

An axis parameter may be a CLI knob or a per-instance config reference,
dispatched by scope.
"""

from __future__ import annotations

import itertools
from dataclasses import dataclass, field
from typing import Any, List

from .runspec import RunSpec
from .system_model import ParamRef


@dataclass
class SweepAxis:
    parameter: ParamRef
    values: List[Any]


@dataclass
class Sweep:
    setup: str
    axes: List[SweepAxis] = field(default_factory=list)
    base_time_ns: float = 0.0
    base_ber: float | None = None
    base_seed: int | None = None

    def count(self) -> int:
        total = 1
        for axis in self.axes:
            total *= max(1, len(axis.values))
        return total

    def expand(self) -> List[RunSpec]:
        if not self.axes:
            return [self._base_spec(self.setup)]

        value_lists = [axis.values for axis in self.axes]
        specs: List[RunSpec] = []
        for combo in itertools.product(*value_lists):
            spec = self._base_spec("")
            label_parts: List[str] = [self.setup]
            for axis, value in zip(self.axes, combo):
                _apply(spec, axis.parameter, value)
                label_parts.append(f"{axis.parameter.axis_name()}={value}")
            spec.label = "__".join(label_parts)
            specs.append(spec)
        return specs

    def _base_spec(self, label: str) -> RunSpec:
        return RunSpec(
            label=label or self.setup,
            setup=self.setup,
            time_ns=self.base_time_ns,
            ber=self.base_ber,
            seed=self.base_seed,
        )


def _apply(spec: RunSpec, parameter: ParamRef, value: Any) -> None:
    if parameter.scope == "cli":
        if parameter.special == "ber":
            spec.ber = float(value)
        elif parameter.special == "seed":
            spec.seed = int(value)
        elif parameter.special == "time":
            spec.time_ns = float(value)
    else:
        spec.overrides.append((parameter, value))
