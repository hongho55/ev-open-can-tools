"""Execute the receive-only decoder and bounded incident-buffer fixtures."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class DiagnosticDetailsTests(unittest.TestCase):
    def test_decoder_and_recorder_fixtures(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler required')
        with tempfile.TemporaryDirectory(prefix='t2can-diagnostics-') as tmp:
            executable = str(Path(tmp) / 'diagnostics')
            unity_src = ROOT / '.pio/libdeps/native/Unity/src'
            unity_object = str(Path(tmp) / 'unity.o')
            subprocess.run(['cc', '-std=c99', '-I' + str(unity_src),
                            '-c', str(unity_src / 'unity.c'), '-o', unity_object],
                           check=True)
            subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                            '-I' + str(ROOT / 'include'),
                            '-I' + str(unity_src),
                            str(ROOT / 'test/test_native_diagnostic_details/test_diagnostic_details.cpp'),
                            unity_object,
                            '-o', executable], check=True)
            subprocess.run([executable], check=True)
