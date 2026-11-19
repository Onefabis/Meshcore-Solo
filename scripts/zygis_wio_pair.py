"""On-air tracking regression checks for two USB-connected Wio radios."""

from contextlib import ExitStack
import re
import secrets
import struct
import time


STATE = re.compile(r"Tracking:(on|off) GPS:(\w+) channel:(\d+) .* interval:(\d+) seconds")
STATUS = re.compile(
    r"Bat:(\d{1,3})% GPS:(fix|no fix; last known) https://maps\.google\.com/\?q="
    r"(-?\d+\.\d{6}),(-?\d+\.\d{6})|"
    r"Bat:(\d{1,3})% GPS:no fix; no last known position\. (Acquiring|Listening)\."
)
DISCOVERY_ROW = re.compile(
    r"Direct (\d{1,2}): .+ \[([0-9A-F]{16})\] "
    r"(?:RSSI:(-?\d+) SNR:([+-]?\d+\.\d)/([+-]?\d+\.\d)|"
    r"RX:(-?\d+)dBm SNR in/out:([+-]?\d+\.\d)/([+-]?\d+\.\d)dB)"
)
DISCOVERY_SUMMARY = re.compile(r"!triangulate: (\d{1,2}) direct repeater replies in 15s\. .+")
HELP = ("@[Handle] or all\n!kur\n!tikra [text]\n!power up\n!power down\n!power\n"
        "!interval <sec>\n!triangulate\n!help")
PUBLIC_KEY = bytes.fromhex("8b3387e9c5cdea6ac9e5edbaa115cd72")


def require(condition, detail):
    if not condition:
        raise RuntimeError(detail)


def status_report(body):
    match = STATUS.fullmatch(body)
    if not match:
        return False
    battery, fix, lat, lon, no_position_battery, _ = match.groups()
    return int(battery or no_position_battery) <= 100 and (
        fix is None or -90 <= float(lat) <= 90 and -180 <= float(lon) <= 180)


def route_ack(command, path, body):
    if path == 255 or path & 63 == 0:
        return body == f"ACK {command}; route: direct (no repeater hashes)."
    hashes = body.removeprefix(f"ACK {command}; route hashes: ")
    width = 2 * (1 + (path >> 6))
    return hashes != body and bool(re.fullmatch(
        rf"(?:[0-9A-F]{{{width}}}>)*[0-9A-F]{{{width}}}>?", hashes))


class Radio:
    def __init__(self, connection, query_frame):
        self.connection = connection
        self.query_frame = query_frame
        info = self.query(bytes([22, 14]), 13)
        own = self.query(bytes([1]) + bytes(7) + b"Zygis pair test", 5)
        self.name = own[58:].decode()
        self.version = info[60:80].split(b"\0")[0].decode()
        self.preset = own[48:58]
        self.max_channels = info[3]
        self.timestamp = 0
        self.original = self.state()
        self.slot = int(self.original[2])
        self.channel = self.get_channel(self.slot) if self.slot < self.max_channels else None

    def query(self, data, *replies):
        return self.query_frame(self.connection, data, *replies)

    def cli(self, text):
        return self.query(bytes([66]) + text.encode(), 29)[1:].decode()

    def state(self):
        match = STATE.fullmatch(self.cli("status show"))
        require(match is not None, "Unexpected tracker configuration reply")
        return match.groups()

    def get_channel(self, slot):
        return self.query(bytes([31, slot]), 18)

    def set_channel(self, slot, name, key):
        self.query(bytes([32, slot]) + name.ljust(32, b"\0") + key, 0)

    def send(self, body, slot=None, timestamp=None):
        if timestamp is None:
            self.timestamp = max(int(time.time()), self.timestamp + 1)
            timestamp = self.timestamp
        self.query(bytes([3, 0, self.slot if slot is None else slot]) +
                   struct.pack("<I", timestamp) + body.encode(), 0)
        return timestamp

    def messages(self):
        for _ in range(64):
            frame = self.query(bytes([10]), 10, 17, 16, 27)
            if frame[0] == 10:
                return
            if frame[0] == 17 and len(frame) >= 11:
                sender, sep, body = frame[11:].decode(errors="replace").partition(": ")
                if sep:
                    yield frame[4], frame[5], sender, body

    def restore(self):
        policy, _, slot, interval = self.original
        current = self.state()
        if current[3] != interval:
            require(self.cli(f"status interval {interval}").startswith("Status interval:"),
                    f"{self.name}: could not restore interval")
        if current[0] != policy or (policy == "off" and current[1] != "asleep"):
            require(self.cli(f"gps {policy}").startswith(f"Tracking {policy}"),
                    f"{self.name}: could not restore tracking policy")
        state = self.state()
        require((state[0], *state[2:]) == (policy, slot, interval) and
                self.cli("verify").startswith("READY:"), f"{self.name}: settings restoration failed")

    def reboot(self, reconnect):
        def uptime():
            return int.from_bytes(self.query(b"\x38\x00", 24)[4:8], "little")

        if uptime() < 10:
            time.sleep(10)
        before = uptime()
        self.connection.write(b"<\x07\x00\x13reboot")
        self.connection.flush()
        self.connection.close()
        time.sleep(4)
        self.connection = reconnect()
        self.query(bytes([22, 14]), 13)
        self.query(bytes([1]) + bytes(7) + b"Zygis pair test", 5)
        require(uptime() < before, f"{self.name}: USB reconnected without a confirmed reboot")


def wait_message(reader, sender, check, label, timeout=22, slot=None):
    deadline = time.monotonic() + timeout
    slot = reader.slot if slot is None else slot
    while time.monotonic() < deadline:
        found = None
        for received_slot, path, name, body in reader.messages():
            if received_slot == slot and name == sender.name and check(body):
                found = path
                # Keep any following tracker reply in the device's live queue.
                break
        if found is not None:
            return found
        time.sleep(0.2)
    raise RuntimeError(f"{reader.name}: timed out waiting for {label} from {sender.name}")


def no_reply(reader, sender, predicate, seconds=3):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        for slot, _, name, body in reader.messages():
            require(name != sender.name or not predicate(slot, body),
                    f"{reader.name}: unexpected tracking reply from {sender.name}")
        time.sleep(0.2)


def exchange(sender, target, command, expected, *, addressed=True, ack=False):
    message = f"@[{target.name}] {command}" if addressed else command
    sender.send(message)
    path = wait_message(target, sender, lambda body: body == message, f"{command} delivery")
    if ack:
        wait_message(sender, target, lambda body: route_ack(expected, path, body),
                     f"{expected} route acknowledgement")
    else:
        wait_message(sender, target, expected, f"{command} reply")


def exercise(sender, target, token, fresh_seconds):
    def command(text, expected, **kwargs):
        exchange(sender, target, text, expected, **kwargs)

    chat = f"Tracking test {token} Ž"
    sent = sender.send(chat)
    wait_message(target, sender, lambda body: body == chat, "exact UTF-8 channel text")
    sender.send(chat, timestamp=sent)
    no_reply(target, sender, lambda slot, body: slot == target.slot and body == chat, 2)
    wrong = f"@[NoSuch-{token}] !interval 57"
    sender.send(wrong)
    wait_message(target, sender, lambda body: body == wrong, "wrong-handle message")
    no_reply(sender, target, lambda _, body: body.startswith("Interval:57 seconds"))
    require(target.state()[3] == target.original[3], "Wrong handle changed tracker interval")
    print(f"PASS {sender.name} → {target.name}: exact channel text, duplicate suppression, wrong handle ignored", flush=True)

    command("!interval", lambda body: body.startswith(f"Interval:{target.original[3]} seconds"))
    command("!interval 0", lambda body: body.startswith("Interval:0 seconds"))
    command("!interval 86401", lambda body: body.startswith("Invalid interval."))
    command("!interval 4294967296", lambda body: body.startswith("Invalid interval."))
    require(target.state()[3] == "0", "Invalid interval changed tracker settings")
    command("!help", lambda body: body == HELP, addressed=False)
    command(f"!test-{token}", f"!test-{token}", ack=True)
    print(f"PASS {target.name}: interval query/set/rejection, broadcast help, unknown-command route ACK", flush=True)

    command("!power down", lambda body: body == "Power saving ON; GPS stopped. Listening/responding.")
    require(target.state()[:2] == ("off", "asleep"), "GPS still active after power down")
    command("!power", lambda body: body == "Power saving ON; tracking paused; listening/responding.")
    command("!kur", "!kur", ack=True)
    wait_message(sender, target, status_report, "one-shot position or no-fix report")
    require(target.state()[0] == "off", "!kur enabled periodic tracking")
    command("!power down", lambda body: body.startswith("Power saving ON; GPS stopped."))
    command("!power up", lambda body: body == "Power saving OFF; GPS tracking resumed.")
    wait_message(sender, target, status_report, "power-up status report")
    command("!power", lambda body: body == "Power saving OFF; tracking enabled (GPS sleeps between fixes).")
    require(target.state()[0] == "on", "Power up did not enable tracking")
    require("GPS:detected" in target.cli("tracker hardware"), "GPS hardware not detected")
    print(f"PASS {target.name}: pause, listening while paused, locate ACK and report, resume and GPS detected", flush=True)

    # Use a short interval for one scheduler cycle, then stop it before discovery.
    command("!interval 5", lambda body: body.startswith("Interval:5 seconds"))
    wait_message(sender, target, status_report, "scheduled status report", 20)
    command("!interval 0", lambda body: body.startswith("Interval:0 seconds"))
    require(target.cli("status send").startswith("Status queued;"), "Manual status request rejected")
    wait_message(sender, target, status_report, "manual status report")
    print(f"PASS {target.name}: scheduled and USB-requested reports", flush=True)

    for spelling in ("!triangulate", "!traingulate"):
        command(spelling, "!triangulate", ack=True)
        repeaters = set()
        count = None

        def discovery_message(body):
            nonlocal count
            if body.startswith("Direct "):
                row = DISCOVERY_ROW.fullmatch(body)
                require(row is not None, "Malformed repeater signal report")
                require(row[2] not in repeaters, "Repeater appeared twice in one scan")
                repeaters.add(row[2])
            summary = DISCOVERY_SUMMARY.fullmatch(body)
            if summary:
                count = int(summary[1])
            return summary is not None

        wait_message(sender, target, discovery_message, "discovery summary", 95)
        require(count == len(repeaters), "Repeater detail count differed from the summary")
        print(f"PASS {target.name}: {spelling} route ACK and discovery summary ({count} repeaters)",
              flush=True)

    if fresh_seconds:
        # A fresh fix depends on antenna placement and satellite visibility.
        command("!kur", "!kur", ack=True)
        try:
            wait_message(sender, target,
                         lambda body: status_report(body) and "% GPS:fix " in body,
                         "fresh GNSS report", fresh_seconds)
        except RuntimeError as error:
            if not str(error).startswith(f"{sender.name}: timed out waiting for fresh"):
                raise
            print(f"UNOBSERVED {target.name}: no fresh GNSS report in {fresh_seconds}s", flush=True)
        else:
            require(target.state()[1] == "asleep", "GPS remained awake after a fresh report")
            print(f"PASS {target.name}: fresh GNSS report and GPS sleep", flush=True)

    text = f"@[{target.name}] Tracking test message {token}"
    sender.send(text)
    wait_message(target, sender, lambda body: body == text, "addressed conversation")
    print(f"PASS {target.name}: addressed conversation delivered. Check screen and chirps on the device.", flush=True)


def configuration_checks(radios, reconnect):
    left, right = radios
    slots = []
    key = secrets.token_bytes(16)
    name = b"Zygis pair isolation"
    with ExitStack() as cleanup:
        for radio in radios:
            for slot in reversed(range(radio.max_channels)):
                channel = radio.get_channel(slot)
                if channel[2] == 0 and channel[34:] == bytes(16):
                    break
            else:
                raise RuntimeError(f"{radio.name}: no empty channel slot for isolation test")
            slots.append(slot)

            def remove_probe(device, index):
                device.set_channel(index, b"", bytes(32))
                channel = device.get_channel(index)
                require(channel[2] == 0 and channel[34:] == bytes(16),
                        f"{device.name}: temporary channel removal failed")

            cleanup.callback(remove_probe, radio, slot)
            radio.set_channel(slot, name, key)
            channel = radio.get_channel(slot)
            require(channel[2:34].split(b"\0")[0] == name and channel[34:] == key,
                    "Temporary isolation channel did not save")
        for source, target in ((0, 1), (1, 0)):
            sender, receiver = radios[source], radios[target]
            old = receiver.state()[3]
            unexpected = 319 if old != "319" else 320
            message = f"@[{receiver.name}] !interval {unexpected}"
            sender.send(message, slot=slots[source])
            wait_message(receiver, sender, lambda body: body == message,
                         "delivery on the unbound channel", slot=slots[target])
            no_reply(sender, receiver, lambda _, body: body.startswith(f"Interval:{unexpected} seconds"))
            require(receiver.state()[3] == old, "An unbound channel changed tracking settings")
            print(f"PASS {receiver.name}: command on a different private channel delivered as chat and ignored by tracking",
                  flush=True)
        for radio, slot in zip(radios, slots):
            radio.set_channel(slot, b"Renamed public probe", PUBLIC_KEY)
            listing = radio.cli(f"status channels {slot}")
            require(listing.endswith("(Public key)"), f"{radio.name}: Public key passed eligibility check")
            reply = radio.cli(f"status channel {slot}")
            if radio.state()[2] != radio.original[2]:
                radio.cli(f"status channel {radio.original[2]}")
            require(reply == "Error: Public key",
                    f"{radio.name}: Public key was not rejected by binding command")
            require(radio.state()[2] == radio.original[2] and radio.cli("verify").startswith("READY:"),
                    f"{radio.name}: private binding changed during Public key rejection")
            print(f"PASS {radio.name}: renamed Public key rejected for tracking", flush=True)

    for sender, target in ((left, right), (right, left)):
        interval = "73" if target.original[3] != "73" else "74"
        try:
            exchange(sender, target, f"!interval {interval}",
                     lambda body: body.startswith(f"Interval:{interval} seconds"))
            target.reboot(reconnect[target])
            observed = target.state()
            require((observed[0], *observed[2:]) == (target.original[0], target.original[2], interval)
                    and target.cli("verify").startswith("READY:"),
                    f"{target.name}: tracking configuration failed the reboot check")
            exchange(sender, target, "!interval", lambda body: body.startswith(f"Interval:{interval} seconds"))
            print(f"PASS {target.name}: interval, tracking policy and private binding survived reboot", flush=True)
        finally:
            if target.connection.is_open:
                target.restore()
                print(f"PASS {target.name}: original tracking configuration restored", flush=True)
            else:
                raise RuntimeError(f"{target.name}: USB did not return to restore interval {target.original[3]}")


def discovery_probe(sender, tracker, read_frame):
    command = f"@[{tracker.name}] !triangulate"
    sender.send(command)
    path = wait_message(tracker, sender, lambda body: body == command, "discovery command")
    deadline = time.monotonic() + 12
    while (frame := read_frame(sender.connection, deadline)) is not None:
        # Companion control push: SNR × 4, RSSI, path, request type/filter/tag.
        if len(frame) == 10 and frame[0] == 0x8E and frame[3] & 63 == 0 and frame[4:6] == b"\x81\x04":
            snr, tag = frame[1:2], frame[6:10]
            break
    else:
        raise RuntimeError(f"{sender.name}: did not receive the zero-hop repeater request")
    wait_message(sender, tracker, lambda body: route_ack("!triangulate", path, body),
                 "discovery route ACK", 7)
    prefix = secrets.token_bytes(8)
    response = b"\x37\x92" + snr + tag + prefix
    sender.query(response, 0)
    time.sleep(0.5)
    sender.query(response, 0)
    seen = set()
    summary_count = 0
    signal = None

    def inspect(body):
        nonlocal signal, summary_count
        row = DISCOVERY_ROW.fullmatch(body)
        if row:
            require(row[2] not in seen, "Duplicate repeater result")
            seen.add(row[2])
            if row[2] == prefix.hex().upper():
                rssi, snr_in, snr_out = row.groups()[2:5] if row[3] else row.groups()[5:8]
                require(float(snr_out) == round(int.from_bytes(snr, "little", signed=True) / 4, 1),
                        "Report did not preserve the remote SNR")
                require(-200 < int(rssi) < 0, "Receive RSSI is out of range")
                signal = (rssi, snr_in, snr_out)
        summary = DISCOVERY_SUMMARY.fullmatch(body)
        if summary:
            summary_count = int(summary[1])
        return summary is not None

    wait_message(sender, tracker, inspect, "controlled repeater discovery summary", 95)
    require(signal and summary_count == len(seen),
            f"{tracker.name}: controlled discovery probe row={signal is not None}, "
            f"summary count={summary_count}, received rows={len(seen)}")
    print(f"PASS {tracker.name}: controlled repeater via real LoRa, duplicate suppressed, "
          f"RX {signal[0]} dBm, SNR in/out {signal[1]}/{signal[2]} dB", flush=True)


def test_pair(args, port_for, query_frame, read_frame):
    import serial

    require(len(set(args.serials)) == 2, "Choose two distinct USB serials")
    require(args.fresh_fix_seconds >= 0, "Fresh-fix wait cannot be negative")
    with ExitStack() as stack:
        radios = []
        reconnect = {}
        for usb_serial in args.serials:
            def connect(number=usb_serial):
                deadline = time.monotonic() + 15
                while True:
                    try:
                        port = port_for(number)
                        return stack.enter_context(serial.Serial(
                            port.device, 115200, timeout=0.2, write_timeout=2))
                    except (OSError, RuntimeError):
                        if time.monotonic() >= deadline:
                            raise
                        time.sleep(0.5)

            radio = Radio(connect(), query_frame)
            reconnect[radio] = connect
            radios.append(radio)
            print(f"{usb_serial}: {radio.name} | {radio.version} | tracking slot {radio.slot} | "
                  f"{'ready' if radio.cli('verify').startswith('READY:') else 'blocked'}", flush=True)
        if args.identify:
            return
        left, right = radios
        require(left.name and right.name and left.name != right.name, "Distinct configured names are required")
        require(left.preset == right.preset, "The two radios have different LoRa presets")
        require(all(r.channel and r.cli("verify").startswith("READY:") for r in radios),
                "Both trackers need a valid private tracking binding")
        require(left.channel[34:] == right.channel[34:], "Tracking channel keys differ")
        for radio in radios:
            list(radio.messages())
        if args.configuration_only:
            try:
                configuration_checks(radios, reconnect)
            finally:
                for radio in radios:
                    if radio.connection.is_open:
                        radio.restore()
            return
        failures = []
        directions = [(sender, target) for sender, target in ((left, right), (right, left))
                      if args.target is None or target.name == args.target]
        require(directions, "The requested target name does not match either tracker")
        if args.smoke:
            for sender, target in directions:
                exchange(sender, target, "!power", lambda body: body.startswith("Power saving "))
                exchange(sender, target, "!kur", "!kur", ack=True)
                wait_message(sender, target, status_report, "one-shot position or no-fix report")
                print(f"PASS {sender.name} → {target.name}: private channel, !power, !kur ACK and status report",
                      flush=True)
            return
        if args.discovery_probe:
            for sender, target in directions:
                discovery_probe(sender, target, read_frame)
            return
        for sender, target in directions:
            try:
                exercise(sender, target, secrets.token_hex(3), args.fresh_fix_seconds)
            except (RuntimeError, OSError) as error:
                failures.append(f"{target.name}: {error}")
                print(f"FAIL {failures[-1]}", flush=True)
            finally:
                try:
                    target.restore()
                    print(f"PASS {target.name}: original tracking policy, binding and interval restored", flush=True)
                except (RuntimeError, OSError) as error:
                    failures.append(str(error))
        if args.alarms:
            for sender, target in directions:
                try:
                    exchange(sender, target, "!tikra Tracking test alarm", "!tikra", ack=True)
                    print(f"PASS {target.name}: test alarm delivered and ACKed. Dismiss with a physical button.",
                          flush=True)
                except (RuntimeError, OSError) as error:
                    failures.append(str(error))
        if failures:
            raise RuntimeError("Pair test failed:\n" + "\n".join(failures))
