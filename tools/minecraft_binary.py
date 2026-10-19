#!/usr/bin/env python3
"""Local Xodus acquisition and PE inventory; no credentials pass through this tool."""

import argparse
import hashlib
import os
import re
import shutil
import struct
import subprocess
import uuid
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LOCAL = ROOT / ".local"
BINARIES = LOCAL / "minecraft-binaries"
XODUS_SOURCE = LOCAL / "xodus-source"
XODUS_BIN = XODUS_SOURCE / "target" / "release" / "xodus-cli"
XODUS_URL = "https://github.com/xodus-gaming/xodus.git"
XODUS_REV = "95f482fe37f83e6be31d74c642acd2f2f0503e9a"
MINECRAFT_PRODUCT = "9NBLGGH2JHXJ"
VERSION = re.compile(r"[0-9]+(?:\.[0-9]+){1,3}")
METADATA_NAMES = {"microsoftgame.config", "appxmanifest.xml"}


def fail(message):
    raise SystemExit(message)


def run(args, *, cwd=None, quiet=False):
    return subprocess.run(args, cwd=cwd, check=True,
                          stdout=subprocess.DEVNULL if quiet else None)


def check_git():
    if subprocess.run(["git", "check-ignore", "-q", str(BINARIES)],
                      cwd=ROOT).returncode != 0:
        fail("Binary directory is not ignored by Git.")
    tracked = subprocess.check_output(["git", "ls-files", "--cached", "--", ".local"],
                                      cwd=ROOT, text=True).splitlines()
    if tracked:
        fail("Proprietary local files are tracked or staged: " + ", ".join(tracked))
    staged = subprocess.check_output(["git", "diff", "--cached", "--name-only", "-z"],
                                     cwd=ROOT).decode().split("\0")
    binary_suffixes = (".exe", ".dll", ".msixvc", ".xsp", ".msix", ".appx")
    suspect = [name for name in staged if name.lower().endswith(binary_suffixes)]
    if suspect:
        fail("Staged executable or game package detected: " + ", ".join(suspect))
    print(f"Ignored: {BINARIES}")
    print("No .local files tracked or staged.")


def require_xodus():
    if not XODUS_BIN.is_file():
        fail("Xodus is not built. Run: python3 tools/minecraft_binary.py setup")
    return str(XODUS_BIN)


def setup():
    check_git()
    needed = [name for name in ("git", "cargo", "rustc", "pkg-config", "protoc", "cmake")
              if shutil.which(name) is None]
    if needed:
        fail("Missing build prerequisites: " + ", ".join(needed) + ". See docs/xodus.md.")
    rust = subprocess.check_output(["rustc", "--version"], text=True).split()[1]
    if tuple(map(int, rust.split(".")[:2])) < (1, 98):
        fail("Xodus requires Rust 1.98 or newer.")
    if subprocess.run(["pkg-config", "--exists", "webkit2gtk-4.1"]).returncode:
        fail("Missing WebKitGTK 4.1 development files; see docs/xodus.md.")
    LOCAL.mkdir(exist_ok=True)
    if not XODUS_SOURCE.exists():
        run(["git", "clone", XODUS_URL, str(XODUS_SOURCE)])
    elif not (XODUS_SOURCE / ".git").is_dir():
        fail(f"Existing path is not a Xodus checkout: {XODUS_SOURCE}")
    origin = subprocess.check_output(["git", "remote", "get-url", "origin"],
                                     cwd=XODUS_SOURCE, text=True).strip()
    if origin not in (XODUS_URL, "https://github.com/xodus-gaming/xodus"):
        fail("Existing Xodus checkout has an unexpected origin.")
    if subprocess.check_output(["git", "status", "--porcelain"],
                               cwd=XODUS_SOURCE, text=True).strip():
        fail("Existing Xodus checkout has local changes; resolve them manually.")
    run(["git", "checkout", "--detach", XODUS_REV], cwd=XODUS_SOURCE)
    run(["cargo", "build", "--release", "--locked", "-p", "xodus-cli"], cwd=XODUS_SOURCE)
    print(f"Built {XODUS_BIN} from {XODUS_REV}")


def xodus(command):
    env = os.environ.copy()
    env["XODUS_LOG"] = "off"
    return subprocess.run([require_xodus(), *command], env=env).returncode


def package_version(directory):
    for path in directory.rglob("*"):
        if path.name.lower() in METADATA_NAMES and path.is_file():
            try:
                root = ET.parse(path).getroot()
            except ET.ParseError:
                continue
            for element in root.iter():
                if element.tag.rsplit("}", 1)[-1].lower() in ("identity", "game"):
                    for key, value in element.attrib.items():
                        if key.lower() == "version" and VERSION.fullmatch(value):
                            return value
    return None


def pe_machine(path):
    try:
        with path.open("rb") as stream:
            if stream.read(2) != b"MZ":
                return None
            stream.seek(0x3C)
            offset = struct.unpack("<I", stream.read(4))[0]
            if offset > path.stat().st_size - 26:
                return None
            stream.seek(offset)
            header = stream.read(26)
            if header[:4] != b"PE\0\0":
                return None
            machine = struct.unpack_from("<H", header, 4)[0]
            magic = struct.unpack_from("<H", header, 24)[0]
            return "x86_64" if (machine, magic) == (0x8664, 0x20B) else "other"
    except (OSError, struct.error):
        return None


def candidates(directory):
    found = []
    for path in directory.rglob("*.exe"):
        if path.is_file() and pe_machine(path) == "x86_64":
            found.append(path)
    return sorted(found, key=lambda p: ("minecraft" not in p.name.lower(), len(p.parts), str(p)))


def declared_executables(directory):
    names = set()
    for path in directory.rglob("*"):
        if path.name.lower() in METADATA_NAMES and path.is_file():
            try:
                root = ET.parse(path).getroot()
            except ET.ParseError:
                continue
            for element in root.iter():
                tag = element.tag.rsplit("}", 1)[-1].lower()
                if tag in ("application", "executable", "pcbootstrapper"):
                    for key, value in element.attrib.items():
                        if key.lower() in ("executable", "name") and value.lower().endswith(".exe"):
                            names.add(Path(value.replace("\\", "/")).name.lower())
    return names


def digest(path):
    hasher = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            hasher.update(block)
    return hasher.hexdigest()


def inventory():
    if not BINARIES.exists():
        print("No local Minecraft downloads.")
        return
    for directory in sorted(BINARIES.glob("*/x86_64")):
        if directory.is_dir():
            print(f"{directory.parent.name}  x86_64  {directory}")


def identify(version):
    directory = BINARIES / version / "x86_64"
    if not directory.is_dir():
        fail(f"No local download for {version} at {directory}")
    recorded = package_version(directory)
    print(f"Requested directory version: {version}")
    print(f"Package version: {recorded or 'unavailable'}")
    found = candidates(directory)
    if not found:
        fail("No valid Windows x64 PE executable found. Xodus may have left the EXE encrypted; do not open it in REA.")
    declared = declared_executables(directory)
    found.sort(key=lambda path: (path.name.lower() not in declared,
                                 "minecraft" not in path.name.lower(), str(path)))
    for path in found:
        label = " (declared game executable)" if path.name.lower() in declared else ""
        print(f"PE x86_64{label}: {path}\nSHA-256: {digest(path)}")
    if len(found) > 1:
        print("Multiple PE files found; inspect package metadata to choose the game entry point.")


def download(expected_version):
    check_git()
    require_xodus()
    if expected_version and (BINARIES / expected_version / "x86_64").exists():
        fail(f"Version {expected_version} already exists; no files were overwritten.")
    BINARIES.mkdir(parents=True, exist_ok=True)
    staging = BINARIES / "_staging" / str(uuid.uuid4())
    staging.parent.mkdir(exist_ok=True)
    print("Xodus will request the current Minecraft for Windows catalog package using your signed-in account.")
    print(f"Local staging directory: {staging}")
    if xodus(["streaming", MINECRAFT_PRODUCT, str(staging)]):
        fail(f"Xodus failed. Inspect local staging files before retrying: {staging}")
    finalize(staging, expected_version)


def finalize(staging, expected_version=None):
    check_git()
    if not (staging / ".xodus-streaming.msixvc").is_file():
        fail(f"Xodus completion marker is missing. Staging retained at {staging}")
    version = package_version(staging)
    if not version:
        fail(f"Could not verify package version. Staging retained at {staging}")
    if expected_version and version != expected_version:
        fail(f"Catalog returned {version}, expected {expected_version}. Staging retained at {staging}")
    target = BINARIES / version / "x86_64"
    target.parent.mkdir(exist_ok=True)
    if target.exists():
        fail(f"Version {version} already exists. Staging retained at {staging}")
    staging.rename(target)
    print(f"Stored Minecraft {version} at {target}")
    identify(version)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest="command", required=True)
    for name in ("setup", "login", "versions", "download", "finalize", "local", "identify", "check-git"):
        sub = subs.add_parser(name)
        if name == "download":
            sub.add_argument("--expected-version", help="Reject a catalog package with a different version")
        if name == "identify":
            sub.add_argument("version")
        if name == "finalize":
            sub.add_argument("staging_id", help="UUID directory under .local/minecraft-binaries/_staging")
    args = parser.parse_args()
    if args.command == "setup":
        setup()
    elif args.command == "login":
        check_git()
        if xodus(["login"]):
            fail("Xodus login failed; check the webview and Secret Service session.")
    elif args.command == "versions":
        print("Upstream Xodus has no historical-version listing command. It resolves the current catalog package for a product ID.")
        inventory()
    elif args.command == "download":
        if args.expected_version and not VERSION.fullmatch(args.expected_version):
            fail("Expected version must contain 2 to 4 numeric components.")
        download(args.expected_version)
    elif args.command == "finalize":
        try:
            staging_id = str(uuid.UUID(args.staging_id))
        except ValueError:
            fail("Staging ID must be a UUID.")
        finalize(BINARIES / "_staging" / staging_id)
    elif args.command == "local":
        inventory()
    elif args.command == "identify":
        if not VERSION.fullmatch(args.version):
            fail("Version must contain 2 to 4 numeric components.")
        identify(args.version)
    else:
        check_git()


if __name__ == "__main__":
    main()
