#!/usr/bin/env python3
"""Cross-build the real Brawl ARM64 engine using already provisioned Android tools.

Run heavy operations through codex-build on the workspace host. Dependencies are
never downloaded unless --provision-dependencies is explicitly supplied.
"""
import argparse
import datetime
from collections import deque
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
WC = ROOT / 'wiicompiled'
PINNED_NDK = '30.0.16248370'


def workers(value):
    count = int(value)
    if count not in (1, 2):
        raise argparse.ArgumentTypeError('use one or two workers to preserve desktop memory')
    return count


def executable(path, description):
    if not path.is_file() or (os.name != 'nt' and not os.access(path, os.X_OK)):
        raise RuntimeError(f'{description} is missing or not executable: {path}')
    return path.resolve()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk', type=Path, default=Path(os.environ.get('ANDROID_SDK_ROOT') or os.environ.get('ANDROID_HOME') or (ROOT.parent / '.tools/android-sdk' if os.name == 'nt' else Path.home() / '.local/share/android-sdk')))
    parser.add_argument('--ndk', type=Path)
    parser.add_argument('--deps', type=Path, default=ROOT.parent / '.tools/brawl-deps')
    parser.add_argument('--build-dir', type=Path, default=WC / 'android-deps-build')
    parser.add_argument('--cmake', type=Path)
    parser.add_argument('--ninja', type=Path)
    parser.add_argument('--jobs', type=workers, default=2)
    parser.add_argument('--translated-jobs', type=workers, default=1)
    parser.add_argument('--allow-ndk-version-mismatch', action='store_true', help='explicitly accept a different NDK than the Dawn-compatible pin; use an isolated build directory')
    parser.add_argument('--dependencies-only', action='store_true')
    parser.add_argument('--configure-only', action='store_true')
    provision = parser.add_mutually_exclusive_group()
    provision.add_argument('--provision-dependencies', action='store_true')
    provision.add_argument('--skip-dependencies', action='store_true', help='use existing sources.cmake (the default)')
    args = parser.parse_args()
    if platform.system() not in ('Linux', 'Windows') or platform.machine().lower() not in ('x86_64', 'amd64'):
        parser.error('this builder supports Linux x86_64 and Windows x86_64 hosts')
    sdk, deps, build = args.sdk.resolve(), args.deps.resolve(), args.build_dir.resolve()
    ndk = (args.ndk or sdk / 'ndk' / PINNED_NDK).resolve()
    suffix = '.exe' if os.name == 'nt' else ''
    cmake = executable(args.cmake or sdk / 'cmake/3.31.6/bin' / ('cmake' + suffix), 'CMake')
    bundled_ninja = sdk / 'cmake/3.31.6/bin' / ('ninja' + suffix)
    ninja = executable(args.ninja or (bundled_ninja if bundled_ninja.is_file() else Path(shutil.which('ninja') or bundled_ninja)), 'Ninja')
    toolchain = ndk / 'build/cmake/android.toolchain.cmake'
    properties = ndk / 'source.properties'
    if not toolchain.is_file() or not properties.is_file():
        parser.error(f'incomplete NDK installation: {ndk}')
    match = re.search(r'^Pkg\.Revision\s*=\s*(\S+)', properties.read_text(), re.MULTILINE)
    if not match:
        parser.error('NDK source.properties has no package revision')
    if match[1] != PINNED_NDK and not args.allow_ndk_version_mismatch:
        parser.error(f'Dawn package expects NDK {PINNED_NDK}, found {match[1]}; accepting another toolchain requires --allow-ndk-version-mismatch')
    host = 'windows-x86_64' if os.name == 'nt' else 'linux-x86_64'
    executable(ndk / 'toolchains/llvm/prebuilt' / host / 'bin' / ('clang' + suffix), 'NDK Clang')
    version = subprocess.check_output([str(cmake), '--version'], text=True).splitlines()[0]
    parsed = re.search(r'(\d+)\.(\d+)', version)
    if not parsed or tuple(map(int, parsed.groups())) < (3, 31):
        parser.error('CMake 3.31 or newer is required')
    # Never reuse a cache configured for another source tree or toolchain.
    cache = build / 'CMakeCache.txt'
    if cache.is_file():
        values = dict(re.findall(r'^([^/#][^:=]*):[^=]*=(.*)$', cache.read_text(), re.MULTILINE))
        for key, wanted in (('CMAKE_HOME_DIRECTORY', WC / 'runtime'), ('CMAKE_TOOLCHAIN_FILE', toolchain), ('CMAKE_ANDROID_NDK', ndk)):
            if values.get(key) and Path(values[key]).resolve() != wanted.resolve():
                parser.error(f'{key} differs in {cache}; choose another --build-dir')
    preload = deps / 'sources.cmake'
    if not args.provision_dependencies and not preload.is_file():
        parser.error(f'verified dependency preload missing: {preload}; provision it separately or use --provision-dependencies')
    if not args.dependencies_only and not (WC / 'generated/build_shards/shards.cmake').is_file():
        parser.error('translate private Brawl inputs first; generated/build_shards/shards.cmake is missing')
    build.mkdir(parents=True, exist_ok=True)
    logs = build / 'logs'
    logs.mkdir(exist_ok=True)
    stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f')

    def run(name, command):
        log = logs / f'{name}-{stamp}.log'
        print(f'{name}: {log}', flush=True)
        with log.open('w', encoding='utf-8') as output:
            output.write(' '.join(map(str, command)) + '\n'); output.flush()
            result = subprocess.run(list(map(str, command)), stdout=output, stderr=subprocess.STDOUT)
        if result.returncode:
            with log.open(encoding='utf-8', errors='replace') as output:
                print(''.join(deque(output, maxlen=45)), file=sys.stderr)
            raise RuntimeError(f'{name} failed ({result.returncode}); see {log}')

    if args.provision_dependencies:
        run('dependencies', [sys.executable, ROOT / 'tools/provision-brawl-deps.py', '--destination', deps, '--jobs', args.jobs])
    if not preload.is_file():
        raise RuntimeError(f'dependency preload missing: {preload}')
    configure = [cmake, '-S', WC / 'runtime', '-B', build, '-G', 'Ninja', '-C', preload,
                 f'-DCMAKE_MAKE_PROGRAM={ninja.as_posix()}', f'-DCMAKE_TOOLCHAIN_FILE={toolchain.as_posix()}',
                 '-DANDROID_ABI=arm64-v8a', '-DANDROID_PLATFORM=android-30', '-DANDROID_STL=c++_static',
                 '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_POSITION_INDEPENDENT_CODE=ON',
                 '-DCMAKE_POLICY_DEFAULT_CMP0168=NEW', '-DBUILD_SHARED_LIBS=OFF',
                 '-DAURORA_DAWN_PROVIDER=package', '-DAURORA_DAWN_LINKAGE=static',
                 '-DAURORA_SDL3_PROVIDER=vendor', '-DAURORA_SDL3_LINKAGE=static',
                 f'-DMKW_TRANSLATED_COMPILE_JOBS={args.translated_jobs}',
                 f'-DMKW_BUILD_PRODUCTS={"OFF" if args.dependencies_only else "ON"}']
    run('configure', configure)
    if args.configure_only:
        print(f'Configured Android ARM64 build: {build}')
        return
    targets = ['aurora_gx', 'aurora_pad', 'aurora_si', 'aurora_vi', 'aurora_mtx', 'mkw_cryptopp', 'mbedtls', 'mkw_platform'] if args.dependencies_only else ['WiiCompiled']
    run('compile', [cmake, '--build', build, '--parallel', args.jobs, '--target', *targets])
    if not args.dependencies_only and not (build / 'libmain.so').is_file():
        raise RuntimeError(f'engine library missing after build: {build / "libmain.so"}')
    print(f'Built Android ARM64 {"native dependencies" if args.dependencies_only else "engine"}: {build}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
