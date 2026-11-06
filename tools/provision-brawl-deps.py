#!/usr/bin/env python3
"""Fetch Brawl's Android dependency pins outside the checkout (Python 3.12+).

Archives come from the corresponding upstream release/tag and are SHA-256 checked
before extraction. Game files and toolchains are deliberately outside this tool's
scope. A CMake preload file makes configuration entirely offline afterwards.
"""
from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import tarfile
import urllib.request
import zipfile


# Match wiicompiled/aurora-main/{CMakeLists.txt,extern/CMakeLists.txt}, its
# Dawn/SDL providers and runtime/cmake/MbedTLSPin.cmake. SDL and Dawn digests are
# also published in their GitHub release asset metadata. Abseil's digest is for
# the official GitHub tag archive (the upstream CMake previously omitted it).
PACKAGES = (
    ("SDL", "SDL3-3.4.4.tar.gz", "ee712dbe6a89bb140bbfc2ce72358fb5ee5cc2240abeabd54855012db30b3864",
     "https://github.com/libsdl-org/SDL/releases/download/release-3.4.4/SDL3-3.4.4.tar.gz"),
    ("abseil-cpp", "abseil-cpp-20240722.0.tar.gz", "f50e5ac311a81382da7fa75b97310e4b9006474f9560ac46f54a9967f07d4ae3",
     "https://github.com/abseil/abseil-cpp/archive/refs/tags/20240722.0.tar.gz"),
    ("dawn_prebuilt", "dawn-android-aarch64-v20260603.191052.tar.gz", "0e63e8cbf53551f703f582d1306f4257c0380353f66b53369d96952ce6d9f934",
     "https://github.com/theofficialgman/dawn-build/releases/download/v20260603.191052/dawn-android-aarch64.tar.gz"),
    ("fmt", "fmt-11.1.4.tar.gz", "ac366b7b4c2e9f0dde63a59b3feb5ee59b67974b14ee5dc9ea8ad78aa2c1ee1e",
     "https://github.com/fmtlib/fmt/archive/refs/tags/11.1.4.tar.gz"),
    ("freetype", "freetype-2.14.3.tar.gz", "e61b31ab26358b946e767ed7eb7f4bb2e507da1cfefeb7a8861ace7fd5c899a1",
     "https://download-mirror.savannah.gnu.org/releases/freetype/freetype-2.14.3.tar.gz"),
    ("imgui", "imgui-1.91.9b-docking.tar.gz", "466fdef9b18de15f0bb6e288e3d00ffa3d82200ec458ce5e4f724a161d9528a5",
     "https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b-docking.tar.gz"),
    ("png", "libpng-1.6.58.tar.gz", "a9d4df463d36a6e5f9c29bd6f4967312d17e996c1854f3511f833924eb1993cf",
     "https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.58.tar.gz"),
    ("sqlite3", "sqlite-amalgamation-3510300.zip", "acb1e6f5d832484bf6d32b681e858c38add8b2acdfd42ac5df24b8afb46552b4",
     "https://sqlite.org/2026/sqlite-amalgamation-3510300.zip"),
    ("tracy", "tracy-a64b9a20294d59421a2f57aeca3c6383d8c48169.tar.gz", "24d342b5127d7f659dc3cf94f24347b348cc736f25b17a691fd7f69541937658",
     "https://github.com/wolfpld/tracy/archive/a64b9a20294d59421a2f57aeca3c6383d8c48169.tar.gz"),
    ("xxhash", "xxHash-0.8.4.tar.gz", "5738270935e7c3d38a79b3adf7c9692566ce7895a25f67de43ad52ab504acd32",
     "https://github.com/Cyan4973/xxHash/archive/refs/tags/v0.8.4.tar.gz"),
    ("zstd", "zstd-1.5.7.tar.gz", "eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3",
     "https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz"),
    ("mkw_mbedtls_upstream", "mbedtls-3.6.7.tar.bz2", "a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6",
     "https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2"),
)


def digest(path: Path) -> str:
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(block)
    return hasher.hexdigest()


def safe_member(name: str) -> None:
    path = PurePosixPath(name.replace("\\", "/"))
    if path.is_absolute() or ".." in path.parts or ":" in name:
        raise ValueError(f"Unsafe archive member: {name}")


def prepare(root: Path, package: tuple[str, str, str, str]) -> dict[str, str]:
    name, filename, sha256, url = package
    archive = root / "archives" / filename
    if not archive.exists():
        partial = archive.with_suffix(archive.suffix + ".partial")
        print(f"Downloading {name}", flush=True)
        request = urllib.request.Request(url, headers={"User-Agent": "Brawl-Android-dependency-provisioner"})
        with urllib.request.urlopen(request, timeout=90) as response, partial.open("wb") as output:
            shutil.copyfileobj(response, output)
        if digest(partial) != sha256:
            raise ValueError(f"SHA-256 mismatch: {partial}; expected {sha256}")
        partial.replace(archive)
    if digest(archive) != sha256:
        raise ValueError(f"SHA-256 mismatch: {archive}; expected {sha256}")
    target = root / "sources" / name
    marker = target / ".brawl-archive-sha256"
    if not marker.exists() or marker.read_text().strip() != sha256:
        if target.exists():
            raise ValueError(f"Unrecognized dependency tree: {target}; move it aside before provisioning")
        temporary = root / "sources" / (".extract-" + name)
        if temporary.exists():
            # Resolve both operands before deleting an interrupted extraction.
            if temporary.resolve().parent != (root / "sources").resolve():
                raise ValueError(f"Extraction path escaped dependency directory: {temporary}")
            shutil.rmtree(temporary)
        temporary.mkdir()
        if zipfile.is_zipfile(archive):
            with zipfile.ZipFile(archive) as source:
                for member in source.infolist():
                    safe_member(member.filename)
                source.extractall(temporary)
        else:
            with tarfile.open(archive) as source:
                members = []
                for member in source.getmembers():
                    safe_member(member.name)
                    # Only zstd test aliases are symlinks; none are build inputs.
                    if member.isfile() or member.isdir():
                        members.append(member)
                source.extractall(temporary, members=members, filter="data")
        entries = list(temporary.iterdir())
        source_root = entries[0] if len(entries) == 1 and entries[0].is_dir() else temporary
        source_root.rename(target)
        if temporary.exists():
            temporary.rmdir()
        marker.write_text(sha256 + "\n", encoding="utf-8")
    print(f"Verified {name}", flush=True)
    return {"name": name, "url": url, "sha256": sha256, "source": target.as_posix()}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--destination", type=Path,
                        default=Path(__file__).resolve().parents[2] / ".tools" / "brawl-deps")
    parser.add_argument("--jobs", type=int, default=4)
    arguments = parser.parse_args()
    root = arguments.destination.resolve()
    checkout = Path(__file__).resolve().parents[1]
    if root == checkout or checkout in root.parents:
        parser.error("Dependencies must be outside the repository")
    (root / "archives").mkdir(parents=True, exist_ok=True)
    (root / "sources").mkdir(exist_ok=True)
    with ThreadPoolExecutor(max_workers=max(1, arguments.jobs)) as executor:
        packages = list(executor.map(lambda package: prepare(root, package), PACKAGES))
    (root / "manifest.json").write_text(json.dumps(packages, indent=2) + "\n", encoding="utf-8")
    lines = ["# Generated by provision-brawl-deps.py; local paths, never commit."]
    for package in packages:
        lines.append(f'set(FETCHCONTENT_SOURCE_DIR_{package["name"].upper()} "{package["source"]}" CACHE PATH "" FORCE)')
    lines.append('set(FETCHCONTENT_FULLY_DISCONNECTED ON CACHE BOOL "" FORCE)')
    (root / "sources.cmake").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Offline CMake dependencies ready: {root / 'sources.cmake'}")


if __name__ == "__main__":
    main()
