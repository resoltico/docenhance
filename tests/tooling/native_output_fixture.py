# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""One actual minimal CTest byte container shared by archive and supervisor controls."""

from __future__ import annotations


def journal_bytes(body: bytes) -> bytes:
    """Use actual minimal CTest outer framing without decoding the arbitrary child bytes."""
    return (
        b"Start testing: Oct 20 10:00 EEST\n"
        b"----------------------------------------------------------\n"
        b"1/1 Testing: fixed\n1/1 Test: fixed\nCommand: fixed\nDirectory: .\n"
        b'"fixed" start time: Oct 20 10:00 EEST\nOutput:\n'
        b"----------------------------------------------------------\n" + body + b"\n"
        b"<end of output>\nTest time = 0.01 sec\n"
        b"----------------------------------------------------------\nTest Passed.\n"
        b'"fixed" end time: Oct 20 10:00 EEST\n"fixed" time elapsed: 00:00:00\n'
        b"----------------------------------------------------------\n\n"
        b"End testing: Oct 20 10:00 EEST\n"
    )
