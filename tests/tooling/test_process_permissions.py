# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Permission denial remains failure; reaping resolves only independently confirmed absence."""

from __future__ import annotations

import contextlib
import json
import os
import signal
import socket
import subprocess
import sys
import unittest
from typing import TYPE_CHECKING
from unittest.mock import Mock, patch

from tools_path import ROOT

import process_execution

if TYPE_CHECKING:
    from collections.abc import Callable
    from types import FrameType

STARTUP_CHILD = """
import json,os,signal,socket,sys
connection=socket.create_connection((sys.argv[1],int(sys.argv[2])),timeout=10)
connection.settimeout(None)
connection.sendall(json.dumps({'pid':os.getpid(),
    'term_default':signal.getsignal(signal.SIGTERM)==signal.SIG_DFL,
    'int_ignored':signal.getsignal(signal.SIGINT)==signal.SIG_IGN}).encode()+b'\\n')
connection.recv(1)
"""

REPEATED_TERMINATION = """
import json,os,signal,socket,sys
sys.path.insert(0,sys.argv[1])
import process_execution
connection=socket.socket(fileno=int(sys.argv[2]))
try:
    with process_execution.termination_unwind():
        try:
            with process_execution.owned_process(
                [sys.executable,'-c','import threading; threading.Event().wait()'],
                dict(os.environ),None,graceful=True) as child:
                connection.sendall(json.dumps({'pid':child.pid}).encode())
                child.wait()
        finally:
            ignored=signal.getsignal(signal.SIGTERM)==signal.SIG_IGN
            connection.sendall(b'i' if ignored else b'h')
            if connection.recv(1)!=b'q': raise RuntimeError('Cleanup checkpoint lost')
            connection.sendall(b'a')
            if connection.recv(1)!=b'r': raise RuntimeError('Cleanup release lost')
            connection.sendall(b'd')
except InterruptedError:
    raise SystemExit(1)
"""


class GroupPermissionTests(unittest.TestCase):
    """Independent syscall failures cannot be converted into successful group cleanup."""

    def test_constructor_signals_cannot_escape_child_ownership(self) -> None:
        """Real child readiness precedes interruption, before Popen can return its identity."""
        cases = ["interrupt", "custom", "ignored"]
        if os.name == "posix":
            cases.append("terminate")
        for mode in cases:
            with self.subTest(mode=mode):
                self.exercise_startup(mode)

    def exercise_startup(self, mode: str) -> None:
        """Interrupt the real constructor at an explicit post-spawn readiness checkpoint."""
        original_handler = signal.getsignal(signal.SIGINT)
        constructor = vars(subprocess.Popen)["_execute_child"]
        created: list[subprocess.Popen[bytes]] = []
        connections: list[socket.socket] = []
        callbacks: list[int] = []

        def custom(signum: int, _frame: FrameType | None) -> None:
            callbacks.append(signum)
            message = "Custom startup interruption"
            raise ValueError(message)

        with socket.socket() as listener:
            listener.bind(("127.0.0.1", 0))
            listener.listen(1)
            listener.settimeout(10)
            host, port = listener.getsockname()
            facts: dict[str, object] = {}

            def execute_child(
                child: subprocess.Popen[bytes], *args: object, **kwargs: object
            ) -> None:
                constructor(child, *args, **kwargs)
                created.append(child)
                connection, _address = listener.accept()
                connection.settimeout(10)
                connections.append(connection)
                with connection.makefile("rb") as stream:
                    facts.update(json.loads(stream.readline()))
                signum = signal.SIGTERM if mode == "terminate" else signal.SIGINT
                signal.raise_signal(signum)

            try:
                handlers: dict[str, signal.Handlers | Callable[[int, FrameType | None], object]] = {
                    "custom": custom,
                    "ignored": signal.SIG_IGN,
                }
                selected_handler = handlers.get(mode, signal.default_int_handler)
                signal.signal(signal.SIGINT, selected_handler)
                expected = {
                    "terminate": InterruptedError,
                    "interrupt": KeyboardInterrupt,
                    "custom": ValueError,
                }
                with patch("subprocess.Popen._execute_child", new=execute_child):
                    scope = process_execution.owned_process(
                        [sys.executable, "-c", STARTUP_CHILD, host, str(port)],
                        dict(os.environ),
                        None,
                        graceful=True,
                    )
                    if mode == "ignored":
                        with scope as child:
                            self.assertIsNone(child.poll())
                    else:
                        with self.assertRaises(expected[mode]), scope:
                            self.fail("Pending interruption was not delivered")
                self.assertEqual(len(created), 1)
                self.assert_startup_child(mode, facts, created[0], connections[0])
                self.assertEqual(signal.getsignal(signal.SIGINT), selected_handler)
                self.assertEqual(callbacks, [signal.SIGINT] if mode == "custom" else [])
            finally:
                signal.signal(signal.SIGINT, original_handler)
                for child in created:
                    if child.poll() is None:
                        child.kill()
                    child.wait()
                for connection in connections:
                    connection.close()

    def assert_startup_child(
        self,
        mode: str,
        facts: dict[str, object],
        child: subprocess.Popen[bytes],
        connection: socket.socket,
    ) -> None:
        """Descriptor closure and native exit establish cleanup, independently of helper calls."""
        self.assertEqual(facts["pid"], child.pid)
        self.assertTrue(facts["term_default"])
        self.assertEqual(facts["int_ignored"], mode == "ignored")
        self.assertIsNotNone(child.poll(), "Startup interruption leaked a live child")
        self.assertEqual(connection.recv(1), b"")
        if os.name == "posix":
            self.assertEqual(child.returncode, -signal.SIGTERM)

    def test_repeated_termination_cannot_interrupt_outer_reconciliation(self) -> None:
        """Real second TERM arrives after inner cleanup, while the outer cleanup handshake waits."""
        if os.name != "posix":
            self.assertNotEqual(os.name, "posix")
            return
        connection, peer = socket.socketpair()
        with connection, peer:
            connection.settimeout(10)
            process = subprocess.Popen(
                [
                    sys.executable,
                    "-c",
                    REPEATED_TERMINATION,
                    str(ROOT / "tools"),
                    str(peer.fileno()),
                ],
                pass_fds=(peer.fileno(),),
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                start_new_session=True,
            )
            peer.close()
            child_pid = None
            child_finished = False
            try:
                child_pid = json.loads(connection.recv(4096))["pid"]
                process.send_signal(signal.SIGTERM)
                checkpoint = connection.recv(1)
                child_finished = True
                self.assertEqual(
                    checkpoint, b"i", "Inner scope restored a cancellable outer handler"
                )
                process.send_signal(signal.SIGTERM)
                connection.sendall(b"q")
                self.assertEqual(connection.recv(1), b"a")
                connection.sendall(b"r")
                self.assertEqual(connection.recv(1), b"d")
                output = process.communicate(timeout=10)[0]
                self.assertEqual(process.returncode, 1, output.decode())
                self.assertEqual(output, b"")
            finally:
                if child_pid is not None and not child_finished:
                    with contextlib.suppress(ProcessLookupError):
                        vars(os)["killpg"](child_pid, vars(signal)["SIGKILL"])
                if process.poll() is None:
                    process.kill()
                process.communicate()

    def test_zombie_transition_needs_reap_and_native_absence(self) -> None:
        """Only ESRCH after own child exits resolves a raced EPERM into group absence."""
        process = Mock(spec=subprocess.Popen)
        process.pid = 12345
        process.poll.side_effect = [None, 143]
        with patch(
            "process_execution.os.killpg",
            create=True,
            side_effect=[PermissionError(1, "denied"), ProcessLookupError(3, "absent")],
        ) as kill:
            self.assertFalse(process_execution.signal_group(process, signal.SIGTERM))
        self.assertEqual(process.poll.call_count, 2)
        self.assertEqual(
            [call.args for call in kill.call_args_list], [(12345, signal.SIGTERM), (12345, 0)]
        )

    def test_live_child_permission_denial_is_not_absence(self) -> None:
        """A live child with unknown group permissions does not permit an absence claim."""
        process = Mock(spec=subprocess.Popen)
        process.pid = 12345
        process.poll.return_value = None
        process.wait.side_effect = subprocess.TimeoutExpired("fixture", 0.1)
        with (
            patch(
                "process_execution.os.killpg", side_effect=PermissionError(1, "denied"), create=True
            ),
            self.assertRaises(PermissionError),
        ):
            process_execution.group_exists(process)

    def test_exiting_child_must_be_joined_before_absence_retry(self) -> None:
        """A child not yet waitable gets a bounded join; only a native ESRCH establishes absence."""
        process = Mock(spec=subprocess.Popen)
        process.pid = 12345
        process.poll.return_value = None
        process.wait.return_value = 143
        with patch(
            "process_execution.os.killpg",
            create=True,
            side_effect=[PermissionError(1, "denied"), ProcessLookupError(3, "absent")],
        ):
            self.assertFalse(process_execution.group_exists(process))
        process.wait.assert_called_once_with(timeout=0.1)

    def test_reaped_child_does_not_excuse_unknown_group_permission(self) -> None:
        """Even an exited leader cannot establish absence of its surviving group."""
        process = Mock(spec=subprocess.Popen)
        process.pid = 12345
        process.poll.return_value = 0
        with (
            patch(
                "process_execution.os.killpg", side_effect=PermissionError(1, "denied"), create=True
            ),
            self.assertRaises(PermissionError),
        ):
            process_execution.group_exists(process)

    def test_nonzero_signal_denial_stays_failure_when_group_still_exists(self) -> None:
        """A successful existence probe cannot hide a denied termination signal."""
        process = Mock(spec=subprocess.Popen)
        process.pid = 12345
        process.poll.return_value = 0
        with (
            patch(
                "process_execution.os.killpg",
                side_effect=[PermissionError(1, "denied"), None],
                create=True,
            ),
            self.assertRaises(PermissionError),
        ):
            process_execution.signal_group(process, signal.SIGTERM)

    def test_real_zombie_child_is_reaped_before_group_lookup(self) -> None:
        """Native waitid observes exit without reaping; cleanup then confirms group absence."""
        if os.name != "posix" or not hasattr(os, "waitid") or not hasattr(os, "WNOWAIT"):
            self.assertTrue(
                os.name != "posix" or not hasattr(os, "waitid") or not hasattr(os, "WNOWAIT")
            )
            return
        process = subprocess.Popen(
            [sys.executable, "-c", "raise SystemExit(17)"], cwd=ROOT, start_new_session=True
        )
        try:
            waitid = vars(os)["waitid"]
            waitid(vars(os)["P_PID"], process.pid, vars(os)["WEXITED"] | vars(os)["WNOWAIT"])
            self.assertIsNone(process.returncode)
            self.assertFalse(process_execution.group_exists(process))
            self.assertEqual(process.returncode, 17)
        finally:
            if process.poll() is None:
                process.kill()
            process.wait()

    def test_non_posix_direct_child_cleanup_preserves_signal_handler(self) -> None:
        """Run direct-child cleanup with a real child while preserving the signal handler."""
        original = signal.getsignal(signal.SIGTERM)
        with (
            patch("process_execution.os.name", "nt"),
            patch("process_execution.os.killpg", create=True) as groups,
            process_execution.owned_process(
                [sys.executable, "-c", "import threading; threading.Event().wait()"],
                dict(os.environ),
                None,
            ) as child,
        ):
            self.assertIsNone(child.poll())
            self.assertEqual(signal.getsignal(signal.SIGTERM), original)
        self.assertIsNotNone(child.returncode)
        self.assertNotEqual(child.returncode, 0)
        groups.assert_not_called()
        self.assertEqual(signal.getsignal(signal.SIGTERM), original)

    def test_group_denial_still_joins_direct_child_without_claiming_cleanup(self) -> None:
        """A real launched child is killed/reaped, while the denied group result still fails."""
        if os.name != "posix":
            self.assertNotEqual(os.name, "posix")
            return
        process = None
        try:
            with (
                patch(
                    "process_execution.os.killpg",
                    side_effect=PermissionError(1, "denied"),
                    create=True,
                ),
                self.assertRaises(PermissionError),
                process_execution.owned_process(
                    [sys.executable, "-c", "import threading; threading.Event().wait()"],
                    dict(os.environ),
                    None,
                    cwd=ROOT,
                    graceful=True,
                ) as process,
            ):
                self.assertIsNone(process.poll())
            self.assertIsNotNone(process.returncode)
            self.assertNotEqual(process.returncode, 0)
        finally:
            if process is not None:
                if process.poll() is None:
                    process.kill()
                process.wait()
