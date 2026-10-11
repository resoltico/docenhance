# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Owned subprocess groups, bounded waits and scoped termination unwinding."""

from __future__ import annotations

import contextlib
import os
import signal
import subprocess
import time
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from collections.abc import Callable, Iterator
    from pathlib import Path
    from types import FrameType
    from typing import IO

REAP_RACE_SECONDS = 0.1


def interrupted(_signum: int, _frame: FrameType | None) -> None:
    """Keep repeated termination ignored until the owning outer scope finishes cleanup."""
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
    msg = "Subprocess execution interrupted"
    raise InterruptedError(msg)


@contextlib.contextmanager
def termination_unwind() -> Iterator[None]:
    """Nested scopes reuse the owning POSIX handler through all outer cleanup."""
    if os.name == "posix" and signal.getsignal(signal.SIGTERM) is interrupted:
        yield
        return
    previous = signal.signal(signal.SIGTERM, interrupted) if os.name == "posix" else None
    try:
        yield
    finally:
        if previous is not None:
            signal.signal(signal.SIGTERM, previous)


@contextlib.contextmanager
def startup_interrupts() -> Iterator[None]:
    """Record callable soft signals until construction returns an owned child."""
    handlers: dict[int, Callable[[int, FrameType | None], object]] = {}
    pending: list[tuple[int, FrameType | None]] = []

    def record(signum: int, frame: FrameType | None) -> None:
        if not pending:
            pending.append((signum, frame))

    signals: list[int] = [signal.SIGINT]
    if os.name == "posix":
        signals.append(signal.SIGTERM)
    try:
        for signum in signals:
            previous = signal.getsignal(signum)
            if callable(previous):
                handlers[signum] = previous
                signal.signal(signum, record)
        yield
    finally:
        for signum, previous in handlers.items():
            signal.signal(signum, previous)
    if pending:
        signum, frame = pending[0]
        handlers[signum](signum, frame)


def kill_group(process: subprocess.Popen[bytes]) -> None:
    """Finish this owned POSIX group even when its direct child has already exited."""
    if os.name == "posix":
        signal_group(process, vars(signal)["SIGKILL"])
    elif process.poll() is None:
        process.kill()


def signal_group(process: subprocess.Popen[bytes], signum: int) -> bool:
    """Reap before group lookup; permission denial is never evidence of absence."""
    process.poll()
    try:
        vars(os)["killpg"](process.pid, signum)
    except ProcessLookupError:
        return False
    except PermissionError as denied:
        # Darwin can report EPERM for a group containing only an unreaped zombie.
        if process.poll() is None:
            try:
                process.wait(timeout=REAP_RACE_SECONDS)
            except subprocess.TimeoutExpired as incomplete:
                raise denied from incomplete
        try:
            vars(os)["killpg"](process.pid, 0)
        except ProcessLookupError:
            return False
        if signum:
            raise
    return True


def group_exists(process: subprocess.Popen[bytes]) -> bool:
    """Observe the owned group identity while nested runners finish their cleanup."""
    return signal_group(process, 0)


def stop_process(process: subprocess.Popen[bytes], *, graceful: bool) -> None:
    """Allow nested runners to unwind, then finish the owned group and reap the direct child."""
    if graceful and os.name == "posix":
        if not group_exists(process):
            process.wait()
            return
        signal_group(process, signal.SIGTERM)
        deadline = time.monotonic() + 10
        while group_exists(process) and time.monotonic() < deadline:
            time.sleep(min(0.05, max(0, deadline - time.monotonic())))
    kill_group(process)
    process.wait()


@contextlib.contextmanager
def owned_process(
    command: list[str],
    env: dict[str, str],
    output: IO[bytes] | IO[str] | None,
    *,
    cwd: Path | None = None,
    graceful: bool = False,
) -> Iterator[subprocess.Popen[bytes]]:
    """Own only the launched session; Windows cleanup covers its direct child."""
    with termination_unwind():
        process = None
        try:
            with startup_interrupts():
                process = subprocess.Popen(
                    command,
                    stdout=output,
                    stderr=subprocess.STDOUT,
                    env=env,
                    cwd=cwd,
                    start_new_session=os.name == "posix",
                )
            yield process
        finally:
            if process is not None:
                try:
                    stop_process(process, graceful=graceful)
                except OSError:
                    # Group cleanup remains unconfirmed; still join our unreaped direct child.
                    if process.poll() is None:
                        process.kill()
                    process.wait()
                    raise


def bounded_process(
    command: list[str],
    env: dict[str, str],
    log: Path | None,
    timeout: float,
    *,
    graceful: bool = False,
) -> int:
    """Wait with a deadline and retained log, or inherit the supervisor's output streams."""
    output = log.open("wb") if log is not None else contextlib.nullcontext(None)
    with output as stream, owned_process(command, env, stream, graceful=graceful) as process:
        return process.wait(timeout=timeout)
