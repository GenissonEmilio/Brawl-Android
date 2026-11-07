"""Stage the actual ARM64 build and its runtime resources, without copying disc files."""
import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

if not __debug__:
    raise RuntimeError('Run this validation tool without Python optimization (-O).')

ROOT = Path(__file__).resolve().parents[1]
SYSTEM = {'libandroid.so', 'liblog.so', 'libm.so', 'libdl.so', 'libc.so', 'libz.so',
          'libvulkan.so', 'libOpenSLES.so', 'libEGL.so', 'libGLESv2.so', 'libGLESv3.so',
          'libaaudio.so', 'libjnigraphics.so', 'libGLESv1_CM.so'}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir', type=Path, default=ROOT / 'wiicompiled/android-deps-build')
    p.add_argument('--ndk', type=Path, default=Path(os.environ.get('ANDROID_SDK_ROOT') or os.environ.get('ANDROID_HOME') or (ROOT.parent / '.tools/android-sdk' if os.name == 'nt' else Path.home() / '.local/share/android-sdk')) / 'ndk/30.0.16248370')
    args = p.parse_args()
    build = args.build_dir.resolve()
    if platform.system() not in ('Linux', 'Windows') or platform.machine().lower() not in ('x86_64', 'amd64'):
        p.error('supported hosts: Linux x86_64 and Windows x86_64')
    host = 'windows-x86_64' if os.name == 'nt' else 'linux-x86_64'
    suffix = '.exe' if os.name == 'nt' else ''
    prebuilt = args.ndk / 'toolchains/llvm/prebuilt' / host
    ndkbin = prebuilt / 'bin'
    strip, readelf = ndkbin / ('llvm-strip' + suffix), ndkbin / ('llvm-readelf' + suffix)
    destination = ROOT / 'wiicompiled/android/native'
    native = Path(tempfile.mkdtemp(prefix='native-stage-', dir=destination.parent))
    try:
        libraries = native / 'jniLibs/arm64-v8a'
        libraries.mkdir(parents=True, exist_ok=True)
        queue = [build / 'libmain.so']
        staged = {}
        while queue:
            src = queue.pop()
            if src.name in staged:
                continue
            assert src.is_file(), f'Missing real native library: {src}'
            assert src.name.endswith('.so'), f'Unsupported Android library name: {src.name}'
            for tool in (strip, readelf):
                assert tool.is_file() and (os.name == 'nt' or os.access(tool, os.X_OK)), f'Missing NDK tool: {tool}'
            dst = libraries / src.name
            subprocess.run([str(strip), '--strip-unneeded', '-o', str(dst), str(src)], check=True)
            dynamic = subprocess.check_output([str(readelf), '--dynamic', str(dst)], text=True)
            staged[src.name] = {'bytes': dst.stat().st_size, 'sha256': hashlib.sha256(dst.read_bytes()).hexdigest()}
            for name in re.findall(r'\(NEEDED\).*?\[([^\]]+)\]', dynamic):
                if name in SYSTEM or name in staged:
                    continue
                if name == 'libc++_shared.so':
                    queue.append(prebuilt / 'sysroot/usr/lib/aarch64-linux-android' / name)
                else:
                    candidates = list(build.rglob(name))
                    assert candidates, f'Unresolved native dependency: {name}'
                    hashes = {hashlib.sha256(c.read_bytes()).hexdigest() for c in candidates}
                    assert len(hashes) == 1, f'Ambiguous native dependency: {name}'
                    queue.append(candidates[0])
        exports = subprocess.check_output([str(readelf), '--dyn-syms', '--wide', str(libraries / 'libmain.so')], text=True)
        for symbol in ('SDL_main', 'Java_com_wither_nsmbw_GameActivity_nativeSetTouchState',
                       'Java_com_wither_nsmbw_GameActivity_nativeCancelTouchState',
                       'Java_com_wither_nsmbw_GameActivity_nativeTap',
                       'Java_com_wither_nsmbw_GameActivity_nativePointer',
                       'Java_com_wither_nsmbw_GameActivity_nativeTapScreen', 'BrawlAndroidDirectMenuVersion'):
            assert symbol in exports, f'Missing runtime entry: {symbol}'
        runtime = native / 'assets/runtime'
        runtime.mkdir(parents=True, exist_ok=True)
        for name in ('dsp_coef.bin', 'cacert.pem', 'initial_pipeline_cache.db'):
            assert (build / name).is_file(), f'Build post-step resource missing: {name}'
            shutil.copyfile(build / name, runtime / name)
        assert hashlib.sha256((runtime / 'dsp_coef.bin').read_bytes()).hexdigest() == 'd7741279c2e8ec5c5fb318f8fbdd6de6bf583520d288e836a5383233a4238179'
        assert (build / 'wii_bootstrap/shared2/wc24').is_dir()
        shutil.copytree(build / 'wii_bootstrap', runtime / 'wii_bootstrap', dirs_exist_ok=True)
        licenses = runtime / 'licenses'
        licenses.mkdir(exist_ok=True)
        for src, name in ((ROOT / 'wiicompiled/LICENSE', 'WiiCompiled-GPL-3.0.txt'),
                          (ROOT / 'wiicompiled/THIRD-PARTY-NOTICES.md', 'THIRD-PARTY-NOTICES.md'),
                          (ROOT / 'wiicompiled/aurora-main/LICENSE', 'Aurora-LICENSE.txt')):
            shutil.copyfile(src, licenses / name)
        assets = {p.relative_to(native / 'assets').as_posix():
                  {'bytes': p.stat().st_size, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
                  for p in (native / 'assets').rglob('*') if p.is_file()}
        manifest = {'libraries': staged, 'assets': assets}
        (native / 'staging-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
        # Publish a complete fresh tree; stale libraries/assets must never enter another APK.
        backup = destination.with_name('native-previous')
        if backup.exists():
            raise RuntimeError(f'Previous staging backup needs inspection: {backup}')
        if destination.exists():
            destination.rename(backup)
        try:
            native.rename(destination)
        except BaseException:
            if backup.exists():
                backup.rename(destination)
            raise
        if backup.exists():
            shutil.rmtree(backup)
        print(json.dumps(staged, indent=2))
        print('Staged real engine, resolved dependencies, runtime resources and public licenses.')
    finally:
        # Only the unpublished tree belongs to this attempt; retain any rollback backup.
        if native.exists():
            shutil.rmtree(native)


if __name__ == '__main__':
    main()
