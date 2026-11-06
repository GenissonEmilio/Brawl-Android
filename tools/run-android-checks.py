#!/usr/bin/env python3
"""Build isolated Android regression executables; never operates adb or a phone.

Produces bin/run-checks.sh for the device operator to copy and run with the
executables. No SDL, Dawn, commercial inputs or translated code is required.
"""
import argparse
from pathlib import Path
import subprocess
import sys

CHECKS = ("host_context", "context_abi", "save_state_jump", "psq_helpers", "nand_save",
          "nand_settings", "sc_serial", "runtime_config", "input_expr")


def run(arguments):
    subprocess.run([str(argument) for argument in arguments], check=True)


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ndk", required=True, type=Path, help="NDK 30 directory")
    parser.add_argument("--cmake", default="cmake")
    parser.add_argument("--ninja", default="ninja")
    parser.add_argument("--build-dir", type=Path, default=repo / "build/android-checks")
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    try:
        toolchain = args.ndk.resolve() / "build/cmake/android.toolchain.cmake"
        if not toolchain.is_file():
            raise ValueError("NDK Android CMake toolchain does not exist")
        properties = (args.ndk / "source.properties").read_text()
        if not any(line.strip().startswith("Pkg.Revision = 30.") for line in properties.splitlines()):
            raise ValueError("these checks require NDK 30")
        if args.jobs < 1:
            raise ValueError("--jobs must be positive")
        build = args.build_dir.resolve()
        run((args.cmake, "-S", repo / "cmake/android_checks", "-B", build, "-G", "Ninja",
             f"-DCMAKE_MAKE_PROGRAM={args.ninja}", f"-DCMAKE_TOOLCHAIN_FILE={toolchain.as_posix()}",
             "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-30",
             "-DANDROID_STL=c++_static", "-DCMAKE_BUILD_TYPE=Release"))
        run((args.cmake, "--build", build, "--parallel", args.jobs))
        script = """#!/system/bin/sh
set -eu
cd "$(dirname "$0")"
export TMPDIR="$PWD/tmp"
mkdir -p "$TMPDIR"
"""
        for check in CHECKS:
            script += f'echo "CHECK {check}"\n./{check}\n'
        script += """# Same 24 reserved-type rejection cases as runtime/tests/psq_reserved_tests.cmake.
for operation in load store; do
  for type in 1 2 3; do
    for lanes in 0 1; do
      for route in resolved null; do
        result=0
        ./psq_helpers "$operation" "$type" "$lanes" "$route" > "$TMPDIR/reserved.log" 2>&1 || result=$?
        if [ "$result" -ne 86 ]; then
          echo "FAIL reserved PSQ: $operation/$type/$lanes/$route exit $result"
          cat "$TMPDIR/reserved.log"
          exit 1
        fi
      done
    done
  done
done
echo "PASS: 9 native checks and 24 reserved PSQ cases"
"""
        (build / "bin/run-checks.sh").write_text(script, encoding="utf-8", newline="\n")
        print(f"Built standalone Android checks: {build / 'bin'}")
        print("Device operator: copy bin contents to a writable executable directory; chmod 755 the binaries; run sh run-checks.sh.")
        print("These checks do not validate ART/JNI fiber-stack integration or a playable Brawl match.")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Android checks build FAILED: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
