#!/usr/bin/env python3
"""Build and package a Žygis PlatformIO environment for release review."""

import argparse
from configparser import ConfigParser
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys

from zygis import ROOT, platformio


def targets():
    config = ConfigParser(interpolation=None)
    with (ROOT / "solo/zygis/platformio.ini").open(encoding="utf-8") as file:
        config.read_file(file)
    return [section.removeprefix("env:") for section in config.sections()
            if section.startswith("env:")]


def package(environment, output):
    project = [platformio(), "run", "-e", environment]
    subprocess.run(project, cwd=ROOT, check=True)
    build = ROOT / ".pio/build" / environment
    has_esp_image = (build / "bootloader.bin").is_file() and (build / "partitions.bin").is_file()
    if has_esp_image:
        subprocess.run(project + ["-t", "mergebin"], cwd=ROOT, check=True)
        sources = {"application.bin": "firmware.bin", "full-flash.bin": "firmware-merged.bin"}
    else:
        if not (build / "firmware.uf2").is_file():
            subprocess.run(
                [sys.executable, str(ROOT / "bin/uf2conv/uf2conv.py"), str(build / "firmware.hex"),
                 "-c", "-f", "0xADA52840", "-o", str(build / "firmware.uf2")],
                cwd=ROOT, check=True,
            )
        sources = {"dfu.zip": "firmware.zip", "uf2": "firmware.uf2"}
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    stem = f"{environment}-{commit[:12]}"
    output.mkdir(parents=True, exist_ok=True)
    files = []
    for suffix, source in sources.items():
        dest = output / f"{stem}.{suffix}"
        shutil.copyfile(build / source, dest)
        files.append(dest)
    manifest = output / f"{stem}.json"
    manifest.write_text(json.dumps({"environment": environment, "commit": commit,
                                    "files": [file.name for file in files]}, indent=2) + "\n")
    files.append(manifest)
    (output / f"{stem}.sha256").write_text("".join(
        f"{hashlib.sha256(file.read_bytes()).hexdigest()}  {file.name}\n" for file in files
    ))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    available = targets()
    parser.add_argument("environment", nargs="?", choices=available)
    parser.add_argument("--list", action="store_true", help="emit the build matrix as JSON")
    parser.add_argument("--output", type=Path, default=ROOT / "out/release")
    args = parser.parse_args()
    if args.list:
        print(json.dumps(available, separators=(",", ":")))
    elif args.environment:
        package(args.environment, args.output)
    else:
        parser.error("select an environment or use --list")


if __name__ == "__main__":
    main()
