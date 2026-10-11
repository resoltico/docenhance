# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Real handshakes prove owned child cleanup without claiming native or fuzz coverage."""

from __future__ import annotations

import argparse
import contextlib
import json
import os
import signal
import socket
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

import process_execution
import run_native_suite
from test_evidence import EvidenceError

NATIVE_BUDGET = 1740

CHILD = """
import json, os, signal, socket, subprocess, sys
role, host, port = sys.argv[1:4]
if role in ('grandchild', 'sentinel'):
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
else:
    subprocess.Popen([sys.executable, __file__, 'grandchild', host, port])
connection = socket.create_connection((host, int(port)), timeout=10)
connection.sendall((json.dumps({'role':role,'pid':os.getpid(),'group':os.getpgrp()})+'\\n').encode())
connection.settimeout(None)
while True:
    message = connection.recv(1)
    if not message or message == b'x':
        break
"""

NESTED = """
import json, os, signal, socket, subprocess, sys
role, host, port, tools = sys.argv[1:]
sys.path.insert(0, tools)
import process_execution
command = [sys.executable, __file__, 'engine', host, port, tools]
if role in ('engine', 'sentinel'):
    signal.signal(signal.SIGTERM, signal.SIG_IGN)
if role == 'child':
    subprocess.Popen([sys.executable, __file__, 'grandchild', host, port, tools])
connection = socket.create_connection((host, int(port)), timeout=10)
connection.sendall((json.dumps({'role':role,'pid':os.getpid(),'group':os.getpgrp()})+'\\n').encode())
connection.settimeout(None)
if role == 'grandchild':
    original_stop = process_execution.stop_process
    def cleanup(process, *, graceful):
        connection.sendall(b'c')
        if connection.recv(1) != b'r':
            raise RuntimeError('Nested cleanup handshake was lost')
        original_stop(process, graceful=graceful)
    process_execution.stop_process = cleanup
    try:
        with process_execution.owned_process(command, dict(os.environ), None) as process:
            process.wait()
    except InterruptedError:
        raise SystemExit(1)
else:
    connection.recv(1)
"""

SUPERVISOR = """
import argparse, json, os, pathlib, socket, subprocess, sys
from unittest.mock import patch
sys.path.insert(0, sys.argv[1])
import check_linux, process_execution, run_native_suite, run_fuzz_campaign
root = pathlib.Path(sys.argv[2])
mode, host, port = sys.argv[3:]
command = [sys.executable, str(root/'child.py'), 'child', host, port, sys.argv[1]]
result = 1
if mode.startswith('nested'):
    observer = socket.create_connection((host, int(port)), timeout=10)
    identity = {'role':'observer','pid':os.getpid(),'group':os.getpgrp()}
    observer.sendall((json.dumps(identity)+'\\n').encode())
    observer.settimeout(None)
    original_group_exists = process_execution.group_exists
    observed = False
    term_sent = False
    original_killpg = process_execution.os.killpg
    def observe_termination(group, signum):
        global term_sent
        result = original_killpg(group, signum)
        if signum == process_execution.signal.SIGTERM:
            term_sent = True
        return result
    process_execution.os.killpg = observe_termination
    def observe_group(process):
        global observed
        if term_sent and process.poll() is not None and not observed:
            observed = True
            observer.sendall(b'g')
            if observer.recv(1) != b'r':
                raise RuntimeError('Group grace observation handshake was lost')
        return original_group_exists(process)
    process_execution.group_exists = observe_group

def boundary(*args, **kwargs):
    assert kwargs['graceful'] is True
    if mode == 'native':
        assert args[2] is None and 0 < args[3] <= 1740
    return process_execution.bounded_process(
        command, dict(os.environ), root/'engine.log', 10, graceful=True)
try:
    if mode == 'native':
        with patch('run_native_suite.discovery', return_value=[{'name':'fixture'}]), \\
             patch('run_native_suite.compilation'), \\
             patch('run_native_suite.registrations', return_value=set()), \\
             patch('run_native_suite.complete_junit'), \\
             patch('run_native_suite.bounded_process', side_effect=boundary):
            result = run_native_suite.run(argparse.Namespace(build=root,ctest='fixture',jobs=1))
    elif mode == 'campaign':
        with patch('run_fuzz_campaign.registration', return_value={'fixture'}), \\
             patch('run_fuzz_campaign.client_violations', return_value=[]), \\
             patch('run_fuzz_campaign.inspect_archives', return_value={}), \\
             patch('run_fuzz_campaign.bounded_process', side_effect=boundary):
            args = argparse.Namespace(build=root,ctest='fixture',seconds=1,jobs=1)
            result = run_fuzz_campaign.campaign(args)
    elif mode == 'invoke':
        result = 0 if check_linux.invoke(command, root/'engine.log') else 1
    elif mode == 'nested-exited':
        with process_execution.owned_process(command, dict(os.environ), None,
                                              graceful=True) as process:
            exit_code = process.wait()
            observer.sendall(b'e')
            observer.recv(1)
        result = exit_code
    else:
        result = process_execution.bounded_process(
            command, dict(os.environ), root/'engine.log',
            5 if mode == 'timeout' else 10, graceful=True)
except (OSError, ValueError, subprocess.SubprocessError) as error:
    (root/'error.txt').write_text(str(error))
(root/'result.json').write_text(json.dumps({'exit':result}))
raise SystemExit(result)
"""


def receive_children(
    listener: socket.socket,
    connections: dict[str, socket.socket],
    identities: dict[str, dict[str, int | str]],
    count: int,
) -> None:
    """Wait for explicit process readiness messages with bounded socket deadlines."""
    for _ in range(count):
        connection, _address = listener.accept()
        connection.settimeout(10)
        data = b""
        while not data.endswith(b"\n"):
            part = connection.recv(4096)
            if not part:
                message = "Child closed before readiness handshake"
                raise AssertionError(message)
            data += part
        identity = json.loads(data)
        role = identity["role"]
        connections[role] = connection
        identities[role] = identity


def cleanup_children(
    supervisor: subprocess.Popen[bytes],
    sentinel: subprocess.Popen[bytes],
    connections: dict[str, socket.socket],
    identities: dict[str, dict[str, int | str]],
    finished: set[str],
) -> None:
    """Clean only owned fixture processes, avoiding already-observed finished identities."""
    if supervisor.poll() is None:
        supervisor.kill()
    supervisor.wait()
    for role in identities:
        if role not in {"sentinel", "observer"} and role not in finished:
            with contextlib.suppress(ProcessLookupError):
                os.kill(int(identities[role]["pid"]), vars(signal)["SIGKILL"])
    sentinel.kill()
    sentinel.wait()
    for connection in connections.values():
        connection.close()


class ProcessExecutionTests(unittest.TestCase):
    """Owned POSIX groups end on success, timeout and interruption; unrelated processes survive."""

    def exercise(self, mode: str) -> None:
        """Block only after explicit socket readiness, then observe real descriptor closure."""
        if os.name != "posix":
            self.assertNotEqual(
                os.name, "posix", "POSIX group guarantees are not claimed on Windows"
            )
            return
        with tempfile.TemporaryDirectory(prefix="process-ownership-") as directory:
            root = Path(directory)
            (root / "child.py").write_text(NESTED if mode.startswith("nested") else CHILD)
            (root / "supervisor.py").write_text(SUPERVISOR)
            with socket.socket() as listener:
                listener.bind(("127.0.0.1", 0))
                listener.listen(5)
                listener.settimeout(10)
                host, port = listener.getsockname()
                command = [
                    sys.executable,
                    str(root / "supervisor.py"),
                    str(ROOT / "tools"),
                    str(root),
                    mode,
                    host,
                    str(port),
                ]
                finished: set[str] = set()
                connections: dict[str, socket.socket] = {}
                identities: dict[str, dict[str, int | str]] = {}
                with (root / "supervisor.log").open("wb") as log:
                    supervisor = subprocess.Popen(
                        command, stdout=log, stderr=log, start_new_session=True
                    )
                    sentinel = subprocess.Popen(
                        [
                            sys.executable,
                            str(root / "child.py"),
                            "sentinel",
                            host,
                            str(port),
                            str(ROOT / "tools"),
                        ],
                        stdout=log,
                        stderr=log,
                        start_new_session=True,
                    )
                    try:
                        receive_children(
                            listener, connections, identities, 5 if mode.startswith("nested") else 3
                        )
                        expected_roles = {"child", "grandchild", "sentinel"}
                        if mode.startswith("nested"):
                            expected_roles.update({"engine", "observer"})
                        self.assertEqual(set(connections), expected_roles)
                        self.assertEqual(
                            identities["child"]["group"], identities["grandchild"]["group"]
                        )
                        self.trigger_cleanup(mode, connections, identities, supervisor)
                        expected_exit = 0 if mode == "normal" else 1
                        self.assertEqual(
                            supervisor.wait(timeout=25),
                            expected_exit,
                            (root / "supervisor.log").read_text(),
                        )
                        for role in expected_roles - {"sentinel"}:
                            self.assertEqual(
                                connections[role].recv(1), b"", f"{role} survived {mode}"
                            )
                            finished.add(role)
                        self.assertIsNone(sentinel.poll(), "Unrelated process was terminated")
                        result = json.loads((root / "result.json").read_text())
                        self.assertEqual(result["exit"], expected_exit)
                        if mode == "campaign":
                            reports = list((root / "fuzz-work").glob("campaign-*/campaign.json"))
                            self.assertEqual(len(reports), 1)
                            self.assertFalse(json.loads(reports[0].read_text())["passed"])
                    finally:
                        cleanup_children(supervisor, sentinel, connections, identities, finished)

    def trigger_cleanup(
        self,
        mode: str,
        connections: dict[str, socket.socket],
        identities: dict[str, dict[str, int | str]],
        supervisor: subprocess.Popen[bytes],
    ) -> None:
        """Release leader or cancel only after readiness, preserving the nested cleanup barriers."""
        if mode == "normal":
            connections["child"].sendall(b"x")
        elif mode != "timeout":
            if mode == "nested-exited":
                connections["child"].sendall(b"x")
                self.assertEqual(connections["observer"].recv(1), b"e")
            supervisor.send_signal(signal.SIGTERM)
        if mode.startswith("nested"):
            self.assertNotEqual(identities["engine"]["group"], identities["child"]["group"])
            self.assertEqual(connections["grandchild"].recv(1), b"c")
            self.assertEqual(connections["observer"].recv(1), b"g")
            connections["grandchild"].sendall(b"r")
            connections["observer"].sendall(b"r")

    def test_normal_direct_child_exit_cleans_remaining_grandchild(self) -> None:
        """An otherwise successful leader cannot leave its SIGTERM-ignoring descendant."""
        self.exercise("normal")

    def test_timeout_cleans_remaining_grandchild_and_cannot_pass(self) -> None:
        """A watchdog refusal leaves no child and reports failure rather than success."""
        self.exercise("timeout")

    def test_sigterm_unwinds_owned_group_and_preserves_unrelated_process(self) -> None:
        """Supervisor cancellation kills only its owned session and remains a failure."""
        self.exercise("signal")

    def test_native_supervisor_uses_termination_cleanup(self) -> None:
        """The real native orchestration function inherits owned subprocess behavior."""
        self.exercise("native")

    def test_campaign_supervisor_retains_failed_interruption_evidence(self) -> None:
        """The real campaign function records failure after its child group is cleaned."""
        self.exercise("campaign")

    def test_grace_allows_nested_runner_to_clean_detached_engine(self) -> None:
        """A leader exiting first cannot kill a runner before its detached engine is cleaned."""
        self.exercise("nested")

    def test_grace_handles_leader_reaped_before_cleanup_starts(self) -> None:
        """An already-exited leader still leaves nested runners entitled to group cleanup."""
        self.exercise("nested-exited")

    def test_image_preparation_cli_uses_owned_interrupt_cleanup(self) -> None:
        """The real Linux image-build invocation joins its CLI group on supervisor termination."""
        self.exercise("invoke")

    def test_scoped_handler_restores_prior_signal_handler(self) -> None:
        """Normal scope exit and exceptional unwind restore the process's prior policy."""
        if os.name != "posix":
            self.assertNotEqual(os.name, "posix")
            return
        original = signal.getsignal(signal.SIGTERM)
        with process_execution.termination_unwind():
            self.assertNotEqual(signal.getsignal(signal.SIGTERM), original)
        self.assertEqual(signal.getsignal(signal.SIGTERM), original)

        with self.assertRaises(InterruptedError), process_execution.termination_unwind():
            os.kill(os.getpid(), signal.SIGTERM)
        self.assertEqual(signal.getsignal(signal.SIGTERM), original)

    def test_native_admission_time_reduces_one_absolute_execution_budget(self) -> None:
        """Discovery cost consumes the same deadline; exhausted admission cannot launch tests."""
        for admitted_at in (9, 1741):
            with self.subTest(admitted_at=admitted_at), tempfile.TemporaryDirectory() as directory:
                args = argparse.Namespace(build=Path(directory), ctest="fixture", jobs=1)
                with (
                    patch("run_native_suite.time.monotonic", side_effect=[0, admitted_at]),
                    patch("run_native_suite.discovery", return_value=[{"name": "fixture"}]),
                    patch("run_native_suite.compilation"),
                    patch("run_native_suite.registrations", return_value=set()),
                    patch("run_native_suite.complete_junit"),
                    patch("run_native_suite.bounded_process", return_value=7) as execution,
                ):
                    if admitted_at > NATIVE_BUDGET:
                        with self.assertRaisesRegex(EvidenceError, "exhausted"):
                            run_native_suite.run(args)
                        execution.assert_not_called()
                    else:
                        self.assertEqual(run_native_suite.run(args), 7)
                        self.assertEqual(execution.call_args.args[3], 1731)
