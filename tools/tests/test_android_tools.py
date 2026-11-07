"""Private-data-free regressions for profiling and failed native staging."""
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


def load(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), ROOT / 'tools' / (name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class ProfileTests(unittest.TestCase):
    def setUp(self):
        self.profile = load('profile-android')

    def test_old_ring_during_frozen_observation_is_zero(self):
        ring = '\n'.join(['8333333'] + [f'0 {1_000_000_000 + i * 16666667} 0' for i in range(120)])
        _, old = self.profile.parse_latency(ring)
        result = self.profile.summarize(old, max(old), 10)
        self.assertEqual(result['present_fps'], 0)
        self.assertEqual(result['present_count'], 0)
        self.assertEqual(result['observed_seconds'], 10)
        self.assertIsNone(result['interval_ms'])

    def test_tail_freeze_reduces_full_window_rate(self):
        baseline = 900_000_000_000  # Different epoch from host elapsed seconds.
        frames = {baseline + i * 16666667 for i in range(1, 121)}
        result = self.profile.summarize(frames | {baseline}, baseline, 10)
        self.assertEqual(result['present_fps'], 12)
        self.assertEqual(result['present_count'], 120)
        self.assertAlmostEqual(result['interval_ms']['mean'], 16.667)

    def test_full_command_records_frozen_window_without_reusing_ring(self):
        clock = [0.0]
        ring = '16666667\n0 1000000000000 0\n0 1000016666667 0'
        def shell(arguments, **kwargs):
            command = arguments[-1]
            if '--list' in command:
                return 'RequestedLayerState{SurfaceView[com.genisson.brawl.android/GameActivity](BLAST)#1 }'
            if command.startswith('pidof'):
                return '123'
            if '--latency' in command:
                return ring
            return 'TOTAL PSS: 1024'
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / 'profile.json'
            with patch.object(sys, 'argv', ['profile-android.py', '--adb', 'adb', '--seconds', '10', '--output', str(output)]), \
                 patch.object(self.profile.subprocess, 'check_output', side_effect=shell), \
                 patch.object(self.profile.time, 'monotonic', side_effect=lambda: clock[0]), \
                 patch.object(self.profile.time, 'sleep', side_effect=lambda seconds: clock.__setitem__(0, clock[0] + seconds)), \
                 patch('sys.stdout', new_callable=io.StringIO):
                self.profile.main()
            result = json.loads(output.read_text())
            self.assertEqual(result['present_fps'], 0)
            self.assertEqual(result['observed_seconds'], 10)
            self.assertEqual(result['present_count'], 0)

    def test_pending_and_invalid_timestamps_are_rejected(self):
        period, frames = self.profile.parse_latency('16666667\n0 0 0\n0 9223372036854775807 0\n0 42 0\ninvalid')
        self.assertEqual(period, 16666667)
        self.assertEqual(frames, {42})


class StagingTests(unittest.TestCase):
    def test_failure_after_library_copy_discards_only_the_new_tree(self):
        module = load('stage-android')
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            android = root / 'wiicompiled/android'
            build = root / 'engine'
            build.mkdir()
            (build / 'libmain.so').write_bytes(b'synthetic engine fixture')
            for name in ('native', 'native-previous'):
                (android / name).mkdir(parents=True)
                (android / name / 'keep').write_text(name)
            def strip(arguments, **kwargs):
                shutil.copyfile(arguments[-1], arguments[-2])
            def readelf(arguments, **kwargs):
                if '--dynamic' in arguments:
                    return ''
                return '\n'.join(('SDL_main', 'Java_com_wither_nsmbw_GameActivity_nativeSetTouchState',
                    'Java_com_wither_nsmbw_GameActivity_nativeCancelTouchState',
                    'Java_com_wither_nsmbw_GameActivity_nativeTap',
                    'Java_com_wither_nsmbw_GameActivity_nativePointer',
                    'Java_com_wither_nsmbw_GameActivity_nativeTapScreen', 'BrawlAndroidDirectMenuVersion'))
            with patch.object(module, 'ROOT', root), \
                 patch.object(sys, 'argv', ['stage-android.py', '--build-dir', str(build)]), \
                 patch.object(module.subprocess, 'run', side_effect=strip), \
                 patch.object(module.subprocess, 'check_output', side_effect=readelf):
                with self.assertRaisesRegex(AssertionError, 'Build post-step resource missing'):
                    module.main()
            self.assertEqual(list(android.glob('native-stage-*')), [])
            for name in ('native', 'native-previous'):
                self.assertEqual((android / name / 'keep').read_text(), name)

    def test_failed_attempt_cleans_temp_and_preserves_install_and_backup(self):
        module = load('stage-android')
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            android = root / 'wiicompiled/android'
            for name in ('native', 'native-previous'):
                (android / name).mkdir(parents=True)
                (android / name / 'keep').write_text(name)
            with patch.object(module, 'ROOT', root), patch.object(sys, 'argv', ['stage-android.py']):
                with self.assertRaisesRegex(AssertionError, 'Missing real native library'):
                    module.main()
            self.assertEqual(list(android.glob('native-stage-*')), [])
            for name in ('native', 'native-previous'):
                self.assertEqual((android / name / 'keep').read_text(), name)


@unittest.skipUnless(shutil.which('cmake'), 'CMake unavailable')
class ResourceTests(unittest.TestCase):
    def test_incremental_sync_updates_resources_and_removes_obsolete_bootstrap(self):
        # Execute the production synchronization block in a compiler-free CMake fixture.
        source = (ROOT / 'wiicompiled/runtime/cmake/PublicProducts.cmake').read_text()
        block = source[source.index('    # Resources must refresh'):source.index('\nendfunction()', source.index('    # Resources must refresh'))]
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / 'assets/wii').mkdir(parents=True)
            (root / 'assets/wii/old').write_text('old')
            for name in ('dsp', 'pipeline', 'cert'):
                (root / 'assets' / name).write_text('initial')
            fixture = '''cmake_minimum_required(VERSION 3.25)
project(resource_test NONE)
add_executable(product IMPORTED)
set_target_properties(product PROPERTIES IMPORTED_LOCATION "${CMAKE_BINARY_DIR}/product")
set(target product)
set(MKW_WII_BOOTSTRAP_SOURCE_DIR "${CMAKE_SOURCE_DIR}/assets/wii")
set(MKW_DSP_COEFFICIENT_ROM "${CMAKE_SOURCE_DIR}/assets/dsp")
set(MKW_INITIAL_PIPELINE_CACHE "${CMAKE_SOURCE_DIR}/assets/pipeline")
set(MKW_CA_CERTIFICATE_BUNDLE "${CMAKE_SOURCE_DIR}/assets/cert")
'''
            (root / 'CMakeLists.txt').write_text(fixture + block)
            build = root / 'build'
            subprocess.run(['cmake', '-S', str(root), '-B', str(build)], check=True, capture_output=True)
            command = ['cmake', '--build', str(build), '--target', 'mkw_runtime_resources', '--parallel', '1']
            subprocess.run(command, check=True, capture_output=True)
            (root / 'assets/wii/old').unlink()
            (root / 'assets/wii/new').write_text('new')
            (root / 'assets/cert').write_text('updated')
            subprocess.run(command, check=True, capture_output=True)
            self.assertFalse((build / 'wii_bootstrap/old').exists())
            self.assertEqual((build / 'wii_bootstrap/new').read_text(), 'new')
            self.assertEqual((build / 'cacert.pem').read_text(), 'updated')


if __name__ == '__main__':
    unittest.main()
