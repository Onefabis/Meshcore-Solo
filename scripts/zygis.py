#!/usr/bin/env python3
"""Common Žygis build, test, and Wio USB commands."""

import argparse
import hashlib
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]


def run(command, project):
    subprocess.run(command, cwd=project, check=True)


def platformio():
    return os.environ.get("PIO") or shutil.which("pio") or str(Path.home() / ".local/bin/pio")


def wio_ports():
    from serial.tools.list_ports import comports

    return [port for port in comports() if (port.vid, port.pid) == (0x2886, 0x1667)]


def wio_port(serial_number):
    ports = [port for port in wio_ports() if port.serial_number == serial_number]
    if len(ports) != 1:
        raise RuntimeError(f"Expected one application port for Wio USB serial {serial_number}")
    return ports[0]


def read_frame(connection, deadline):
    while time.monotonic() < deadline:
        if connection.read(1) != b">":
            continue
        header = connection.read(2)
        if len(header) != 2:
            continue
        size = int.from_bytes(header, "little")
        if not 0 < size <= 2048:
            continue
        data = bytearray()
        while len(data) < size and time.monotonic() < deadline:
            data.extend(connection.read(size - len(data)))
        if len(data) == size:
            return bytes(data)
    return None


def query_frame(connection, payload, *replies):
    connection.write(b"<" + struct.pack("<H", len(payload)) + payload)
    deadline = time.monotonic() + 5
    while (data := read_frame(connection, deadline)) is not None:
        if data[0] in replies:
            return data
        if data[0] == 1:
            raise RuntimeError(f"Device rejected command {payload[0]}")
    raise TimeoutError(f"No USB reply for command {payload[0]}")


def snapshot(serial, port):
    with serial.Serial(port.device, 115200, timeout=0.2, write_timeout=2) as connection:
        def query(payload, reply):
            return query_frame(connection, payload, reply)

        info = query(bytes([22, 14]), 13)
        if len(info) < 82 or info[20:60].split(b"\0")[0] != b"Seeed Wio Tracker L1":
            raise RuntimeError("Unexpected Wio device response")
        own = query(bytes([1]) + bytes(7) + b"Zygis USB check", 5)
        tuning = query(bytes([43]), 23)
        channels = [query(bytes([31, slot]), 18) for slot in range(info[3])]
        tracking = query(bytes([66]) + b"status show", 29)
        ready = query(bytes([66]) + b"verify", 29)[1:].startswith(b"READY:")
        tracking = tracking.replace(b"GPS:acquiring", b"GPS:runtime").replace(
            b"GPS:asleep", b"GPS:runtime"
        )
        settings = hashlib.sha256(own[2:36] + own[44:] + tuning + info[4:8] + info[80:])
        for channel in channels:
            # Ignore unused name padding in companion replies.
            settings.update(channel[1:2] + channel[2:34].split(b"\0")[0] + b"\0" + channel[34:])
        settings.update(tracking)
        return info[60:80].split(b"\0")[0].decode(), settings.digest(), ready


def flash(args):
    import serial

    port = wio_port(args.serial)
    package = args.package.resolve(strict=True)
    core = Path(os.environ.get("PLATFORMIO_CORE_DIR", Path.home() / ".platformio"))
    uploader = core / "packages/tool-adafruit-nrfutil/adafruit-nrfutil.py"
    if not uploader.is_file():
        raise RuntimeError(f"PlatformIO Wio uploader missing at {uploader}")
    version, settings, ready = snapshot(serial, port)
    print(f"Flashing {port.device} ({version}) with {package.name}", flush=True)
    command = [sys.executable, str(uploader), "dfu", "serial",
               "--package", str(package), "--baudrate", "115200"]
    with serial.Serial(port.device, 1200, timeout=0.2) as boot_request:
        boot_request.dtr = False
    deadline = time.monotonic() + 15
    while True:
        boot = next((candidate for candidate in wio_ports()
                     if candidate.serial_number == args.serial
                     and candidate.description == "TRACKER L1"), None)
        if boot:
            break
        if time.monotonic() >= deadline:
            raise RuntimeError(f"Wio USB serial {args.serial} did not enter its bootloader")
        time.sleep(0.3)
    with tempfile.TemporaryFile(mode="w+") as log:
        result = subprocess.run(command + ["--port", boot.device],
                                stdout=log, stderr=subprocess.STDOUT, timeout=180)
        log.seek(0)
        output = log.read()
    if result.returncode or "Device programmed." not in output:
        raise RuntimeError("DFU did not report success.\n" + output[-4000:])
    deadline = time.monotonic() + 30
    while True:
        try:
            port = next(port for port in wio_ports() if port.serial_number == args.serial)
            installed, after, after_ready = snapshot(serial, port)
            break
        except (StopIteration, serial.SerialException, TimeoutError):
            if time.monotonic() >= deadline:
                raise RuntimeError("DFU completed but the Wio application did not respond over USB")
            time.sleep(0.5)
    if installed != args.expect_version:
        raise RuntimeError(f"DFU completed, but the Wio reports {installed!r}")
    if args.provision and (after, after_ready) != (settings, ready):
        print(f"DFU complete. Booted {installed}. Settings are ready for provisioning.")
        return
    if (after, after_ready) != (settings, ready):
        raise RuntimeError("DFU completed, but the USB settings comparison found a difference")
    print(f"DFU complete. Booted {installed}. Identity, channels, radio and tracking settings verified.")
    print(f"Private tracking binding: {'ready' if after_ready else 'not ready'}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build", help="build a PlatformIO environment")
    build.add_argument("environment")
    build.add_argument("--project", type=Path, default=ROOT)
    build.add_argument("--clean", action="store_true")
    commands.add_parser("devices", help="list connected Wio application ports")
    status = commands.add_parser("status-wio", help="read the installed firmware and tracking binding")
    status.add_argument("--serial", required=True, help="USB serial from devices")
    tests = commands.add_parser("test", help="run common host checks")
    tests.add_argument("suite", choices=("tracking", "native", "docs"))
    upload = commands.add_parser("flash-wio", help="install a Wio application DFU ZIP")
    upload.add_argument("package", type=Path)
    upload.add_argument("--serial", required=True, help="USB serial from devices")
    upload.add_argument("--expect-version", required=True, help="firmware name reported after reboot")
    upload.add_argument("--provision", action="store_true", help="allow settings to change during a storage migration")
    e2e = commands.add_parser("test-wio-pair", help="exercise tracking over LoRa on two USB Wios")
    e2e.add_argument("serials", nargs=2)
    e2e.add_argument("--identify", action="store_true", help="only read names and tracking configuration")
    e2e.add_argument("--smoke", action="store_true", help="check channel delivery, power query and locate report")
    e2e.add_argument("--alarms", action="store_true", help="send test alarms that need physical dismissal")
    e2e.add_argument("--fresh-fix-seconds", type=int, default=20)
    e2e.add_argument("--target", help="only exercise the tracker with this configured name")
    e2e.add_argument("--configuration-only", action="store_true",
                     help="test isolation using a temporary channel, then check settings through reboot")
    e2e.add_argument("--discovery-probe", action="store_true",
                     help="use each USB peer as a controlled direct discovery responder")
    args = parser.parse_args()

    if args.command == "build":
        command = [platformio(), "run", "-e", args.environment]
        if args.clean:
            run(command + ["-t", "clean"], args.project)
        run(command, args.project)
        print(f"Build output: {args.project / '.pio/build' / args.environment}")
    elif args.command == "devices":
        ports = wio_ports()
        for port in ports:
            print(f"{port.device}  {port.serial_number}  {port.description}")
        if not ports:
            print("No Wio application port detected.")
    elif args.command == "status-wio":
        import serial

        version, _, ready = snapshot(serial, wio_port(args.serial))
        print(f"Firmware: {version}. Private tracking binding: {'ready' if ready else 'not ready'}.")
    elif args.command == "flash-wio":
        flash(args)
    elif args.command == "test-wio-pair":
        from zygis_wio_pair import test_pair

        test_pair(args, wio_port, query_frame, read_frame)
    elif args.suite == "tracking":
        for script in sorted((ROOT / "test/tracking").glob("test_*.py")):
            run([sys.executable, str(script)], ROOT)
    elif args.suite == "native":
        run([platformio(), "test", "-e", "native", "-e", "native_kiss_modem", "-vv"], ROOT)
    else:
        run([sys.executable, "scripts/check_project_docs.py"], ROOT)


if __name__ == "__main__":
    try:
        main()
    except ModuleNotFoundError as error:
        if error.name != "serial":
            raise
        sys.exit("USB commands require pyserial. Run with PlatformIO's Python or install pyserial.")
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        sys.exit(str(error))
