# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Independent simultaneous TV-L1 reference with an edge-scattered adjoint."""

from __future__ import annotations

import math
from dataclasses import dataclass

CHECKPOINT_START = 20
CHECKPOINT_PERIOD = 10
REQUIRED_PASSES = 2


@dataclass(frozen=True)
class TvSettings:
    """Independent recipe parameters."""

    fidelity: float = 1.5
    cap: int = 150
    tolerance: float = 1e-5


@dataclass(frozen=True)
class TvResult:
    """Reference iterate and measured stopping facts."""

    values: list[float]
    iterations: int
    stop: str
    primal_update: float
    dual_update: float
    objective_start: float
    objective_end: float


def objective(u: list[float], f: list[float], width: int, height: int, fidelity: float) -> float:
    """Enumerate actual forward edges and isotropic TV plus L1 fidelity."""
    return sum(
        math.hypot(
            u[i + 1] - u[i] if i % width + 1 < width else 0,
            u[i + width] - u[i] if i // width + 1 < height else 0,
        )
        + fidelity * abs(u[i] - f[i])
        for i in range(len(u))
    )


def solve(f: list[float], width: int, height: int, settings: TvSettings) -> TvResult:
    """Freeze old iterates, scatter edges, then apply independent scalar proximal updates."""
    fidelity, cap, tolerance = settings.fidelity, settings.cap, settings.tolerance
    step = 0.25
    u = f.copy()
    bar = f.copy()
    px = [0.0] * len(f)
    py = px.copy()
    passes = 0
    start = objective(u, f, width, height, fidelity)
    primal_update = dual_update = 0.0
    stop = "iteration_limit"
    for iteration in range(1, cap + 1):
        nx, ny = px.copy(), py.copy()
        for i in range(len(f)):
            dx = bar[i + 1] - bar[i] if i % width + 1 < width else 0
            dy = bar[i + width] - bar[i] if i // width + 1 < height else 0
            cx, cy = px[i] + step * dx, py[i] + step * dy
            norm = max(1.0, math.hypot(cx, cy))
            nx[i] = cx / norm if i % width + 1 < width else 0
            ny[i] = cy / norm if i // width + 1 < height else 0
        dual_update = max(
            math.hypot(a - b, c - d) for a, b, c, d in zip(nx, px, ny, py, strict=True)
        )
        adjoint = [0.0] * len(f)
        for i in range(len(f)):
            if i % width + 1 < width:
                adjoint[i] -= nx[i]
                adjoint[i + 1] += nx[i]
            if i // width + 1 < height:
                adjoint[i] -= ny[i]
                adjoint[i + width] += ny[i]
        next_u = []
        for old, original, dt in zip(u, f, adjoint, strict=True):
            value = old - step * dt
            delta = value - original
            sign = 1 if delta > 0 else -1 if delta < 0 else 0
            next_u.append(
                min(1.0, max(0.0, original + sign * max(abs(delta) - step * fidelity, 0)))
            )
        primal_update = max(abs(a - b) for a, b in zip(next_u, u, strict=True))
        bar = [a + (a - b) for a, b in zip(next_u, u, strict=True)]
        u, px, py = next_u, nx, ny
        if iteration >= CHECKPOINT_START and iteration % CHECKPOINT_PERIOD == 0:
            passes = passes + 1 if primal_update <= tolerance and dual_update <= tolerance else 0
            if passes == REQUIRED_PASSES:
                stop = "tolerance_met"
                break
    return TvResult(
        u,
        iteration,
        stop,
        primal_update,
        dual_update,
        start,
        objective(u, f, width, height, fidelity),
    )
