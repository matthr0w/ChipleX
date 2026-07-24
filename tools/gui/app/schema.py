"""CLI parameters (ber/seed/time) as ParamRefs, so sweeps treat them like
per-instance config parameters."""

from __future__ import annotations

from typing import List

from .system_model import ParamRef


def cli_parameters() -> List[ParamRef]:
    return [
        ParamRef(id="cli:ber", label="Bit error rate", scope="cli", value_type="float",
                 default=1e-12, special="ber"),
        ParamRef(id="cli:seed", label="RNG seed", scope="cli", value_type="int",
                 default=0xC0FFEE, special="seed"),
        ParamRef(id="cli:time", label="Simulated time (ns)", scope="cli", value_type="float",
                 default=0.0, special="time"),
    ]
