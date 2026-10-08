# SPDX-FileCopyrightText: 2026 Ervins Strauhmanis
# SPDX-License-Identifier: MPL-2.0
"""Actual FFT source identity and compilation admission, independent of feature cache flags."""

from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from tools_path import ROOT

import audit_build


class OpencvRecipeTests(unittest.TestCase):
    """The private corrections must be the ones compiled from unchanged locked input."""

    def test_fft_source_identity_and_actual_compilation(self) -> None:
        """Reject absent, stock, altered and duplicated native FFT build inputs."""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            original = root / "source/modules/core/src" / "dxt.cpp"
            owned = root / "deps/opencv/owned-source/dxt.cpp"
            original.parent.mkdir(parents=True)
            owned.parent.mkdir(parents=True)
            original.write_bytes(b"locked original")
            digest = hashlib.sha256(original.read_bytes()).hexdigest()

            def check() -> list[str]:
                return audit_build.opencv_dft_failures(
                    root / "deps/opencv", root / "source", digest
                )

            self.assertTrue(check())
            owned.write_bytes(b"reviewed ownership and typed dispatch corrections\n")
            expected = hashlib.sha256(owned.read_bytes()).hexdigest()
            database = root / "deps/opencv/compile_commands.json"
            database.write_text(json.dumps([{"file": str(owned)}]))
            with patch("audit_build.OPENCV_OWNED_DXT_SHA256", expected):
                self.assertFalse(check())
                canonical = owned.read_bytes()
                owned.write_bytes(canonical.replace(b"\n", b"\r\n"))
                self.assertTrue(check())
                owned.write_bytes(canonical)
                database.write_text(json.dumps([{"file": str(original)}]))
                self.assertTrue(check())
                database.write_text(json.dumps([{"file": str(owned)}, {"file": str(original)}]))
                self.assertTrue(check())
                database.write_text(json.dumps([{"file": str(owned)}]))
                original.write_bytes(b"changed source cache")
                self.assertTrue(check())
                original.write_bytes(b"locked original")
                owned.write_bytes(b"stock raw pointer factory")
                self.assertTrue(check())

    def test_required_fft_source_binding_is_complete(self) -> None:
        """The CPU recipe requires ownership, typed dispatch and a complete source SHA."""
        features = json.loads((ROOT / "deps/features.json").read_text())["dependencies"]["opencv"]
        self.assertTrue(features["DOCENHANCE_OPENCV_OWNED_DFT_CONTEXTS"] is True)
        self.assertTrue(features["DOCENHANCE_OPENCV_TYPED_DFT_DISPATCH"] is True)
        self.assertRegex(features["DOCENHANCE_OPENCV_DXT_SHA256"], r"^[0-9a-f]{64}$")


class OpencvGenerationTests(unittest.TestCase):
    """Actual platform writers must produce the same reviewed LF bytes."""

    def test_fft_generation_is_canonical_without_changing_the_source(self) -> None:
        """Windows text output must not change the compiled source's byte identity."""
        cmake = shutil.which("cmake")
        self.assertIsNotNone(cmake, "CMake is required for native source generation controls")
        before = b"""ReplacementDFT1D *impl = new ReplacementDFT1D();
OcvDftBasicImpl *impl = new OcvDftBasicImpl();
ReplacementDFT2D *impl = new ReplacementDFT2D();
OcvDftImpl *impl = new OcvDftImpl();
return Ptr<DFT1D>(impl);
        }
        delete impl;
return Ptr<DFT2D>(impl);
        }
        delete impl;
static void CCSIDFT_64f(const OcvDftOptions & c, const double* src, double* dst)
{
    CCSIDFT(c, src, dst);
}
(DFTFunc)DFT_32f,
(DFTFunc)RealDFT_32f,
(DFTFunc)CCSIDFT_32f,
(DFTFunc)DFT_64f,
(DFTFunc)RealDFT_64f,
(DFTFunc)CCSIDFT_64f
"""
        expected = b"""Ptr<ReplacementDFT1D> impl = makePtr<ReplacementDFT1D>();
Ptr<OcvDftBasicImpl> impl = makePtr<OcvDftBasicImpl>();
Ptr<ReplacementDFT2D> impl = makePtr<ReplacementDFT2D>();
Ptr<OcvDftImpl> impl = makePtr<OcvDftImpl>();
return Ptr<DFT1D>(impl);
        }
return Ptr<DFT2D>(impl);
        }
static void CCSIDFT_64f(const OcvDftOptions & c, const double* src, double* dst)
{
    CCSIDFT(c, src, dst);
}

template<typename Sample, void (*Function)(const OcvDftOptions&, const Sample*, Sample*)>
static void DFT_dispatch(const OcvDftOptions& c, const void* src, void* dst)
{
    Function(c, static_cast<const Sample*>(src), static_cast<Sample*>(dst));
}
DFT_dispatch<Complexf, DFT_32f>,
DFT_dispatch<float, RealDFT_32f>,
DFT_dispatch<float, CCSIDFT_32f>,
DFT_dispatch<Complexd, DFT_64f>,
DFT_dispatch<double, RealDFT_64f>,
DFT_dispatch<double, CCSIDFT_64f>
"""
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source/modules/core/src/dxt.cpp"
            source.parent.mkdir(parents=True)
            source.write_bytes(before)
            binary = root / "build"
            script = root / "generate.cmake"
            script.write_text(f"""set(CMAKE_SOURCE_DIR "{(root / "source").as_posix()}")
set(CMAKE_BINARY_DIR "{binary.as_posix()}")
set(DOCENHANCE_OPENCV_OWNED_DFT_CONTEXTS ON)
set(DOCENHANCE_OPENCV_TYPED_DFT_DISPATCH ON)
set(DOCENHANCE_OPENCV_DXT_SHA256 "{hashlib.sha256(before).hexdigest()}")
function(get_target_property output target property)
  set(${{output}} "{source.as_posix()}" PARENT_SCOPE)
endfunction()
function(set_property)
endfunction()
include("{(ROOT / "cmake/opencv-hooks/fft-ownership.cmake").as_posix()}")
""")
            result = subprocess.run(
                [str(cmake), "-P", str(script)], capture_output=True, text=True, check=False
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(source.read_bytes(), before)
            self.assertEqual((binary / "owned-source/dxt.cpp").read_bytes(), expected)
