"""Host regression: python tests/test_usb_in_pacer.py (requires g++ on PATH)."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class UsbInPacerTests(unittest.TestCase):
    def test_pacing_simulation(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / 'test.exe'
            subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra',
                            '-I', str(ROOT / 'src'),
                            str(ROOT / 'src' / 'usb_in_pacer.cpp'),
                            str(ROOT / 'tests' / 'usb_in_pacer_test.cpp'),
                            '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    unittest.main()
