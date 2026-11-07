"""Verify the Brawl package identity, real ARM64 engine and 16 KiB load alignment."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import zipfile

if not __debug__:
    raise RuntimeError('Run this validation tool without Python optimization (-O).')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('apk', type=Path)
    p.add_argument('--staging-manifest', type=Path, default=Path(__file__).resolve().parents[1] / 'wiicompiled/android/native/staging-manifest.json')
    default_sdk = Path(os.environ.get('ANDROID_SDK_ROOT') or os.environ.get('ANDROID_HOME') or (Path(__file__).resolve().parents[2] / '.tools/android-sdk' if os.name == 'nt' else Path.home() / '.local/share/android-sdk'))
    p.add_argument('--sdk', type=Path, default=default_sdk)
    p.add_argument('--aapt', type=Path, help='override SDK build-tools/35.0.0/aapt')
    args = p.parse_args()
    args.aapt = args.aapt or args.sdk / 'build-tools/35.0.0' / ('aapt.exe' if os.name == 'nt' else 'aapt')
    assert args.aapt.is_file() and (os.name == 'nt' or os.access(args.aapt, os.X_OK)), f'Missing Android SDK aapt: {args.aapt}'
    staging = json.loads(args.staging_manifest.read_text(encoding='utf-8'))
    badging = subprocess.check_output([str(args.aapt), 'dump', 'badging', str(args.apk)], text=True, encoding='utf-8')
    assert "name='com.genisson.brawl.android'" in badging
    assert "versionName='0.1.1-dev'" in badging and "sdkVersion:'31'" in badging
    manifest = subprocess.check_output([str(args.aapt), 'dump', 'xmltree', str(args.apk), 'AndroidManifest.xml'], text=True)
    assert 'android:debuggable' not in manifest, 'Unexpected debuggable release'
    icons = re.findall(r"application-icon-[^:]+:'([^']+)'", badging)
    assert icons, 'Missing application icon'
    with zipfile.ZipFile(args.apk) as z, args.apk.open('rb') as stream:
        names = set(z.namelist())
        assert z.testzip() is None
        assert 'lib/arm64-v8a/libmain.so' in names, 'No actual native engine'
        assert set(icons) <= names, 'Unresolved launcher icon'
        assert 'assets/runtime/dsp_coef.bin' in names and 'assets/runtime/initial_pipeline_cache.db' in names
        assert 'assets/runtime/licenses/WiiCompiled-GPL-3.0.txt' in names
        expected = {'assets/' + name: value for name, value in staging['assets'].items()}
        expected.update({'lib/arm64-v8a/' + name: value for name, value in staging['libraries'].items()})
        assert {n for n in names if n.startswith(('assets/', 'lib/')) and not n.endswith('/')} == set(expected), 'APK differs from the fresh staged library/asset allowlist'
        for name, metadata in expected.items():
            data = z.read(name)
            assert len(data) == metadata['bytes'] and hashlib.sha256(data).hexdigest() == metadata['sha256'], f'Unexpected packaged content: {name}'
        libraries = []
        for entry in z.infolist():
            path = PurePosixPath(entry.filename)
            assert not path.is_absolute() and '..' not in path.parts
            assert path.suffix.lower() not in {'.dol', '.rel', '.rvz', '.iso', '.wbfs', '.gcz', '.wia', '.ciso', '.sav', '.gci', '.jks', '.keystore', '.key', '.log'}
            if not entry.filename.startswith('lib/'):
                continue
            assert entry.filename.startswith('lib/arm64-v8a/')
            data = z.read(entry)
            assert data[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<H', data, 18)[0] == 183
            offset = struct.unpack_from('<Q', data, 32)[0]
            size, count = struct.unpack_from('<HH', data, 54)
            assert size == 56 and count
            for i in range(count):
                at = offset + i * size
                if struct.unpack_from('<I', data, at)[0] == 1:
                    fileoff, address = struct.unpack_from('<QQ', data, at + 8)
                    alignment = struct.unpack_from('<Q', data, at + 48)[0]
                    assert alignment >= 16384 and fileoff % 16384 == address % 16384
            assert entry.compress_type == zipfile.ZIP_STORED
            stream.seek(entry.header_offset + 26)
            filename, extra = struct.unpack('<HH', stream.read(4))
            assert (entry.header_offset + 30 + filename + extra) % 16384 == 0, 'APK native mapping lacks 16 KiB alignment'
            libraries.append({'name': path.name, 'bytes': len(data)})
    print('Verified Brawl release identity, native ARM64 engine, icon, licenses, runtime resources and 16 KiB ELF/ZIP alignment.')
    print(libraries)


if __name__ == '__main__':
    main()
