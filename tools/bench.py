"""Drive the toy on the desk over USB serial (the firmware's bench console, fw/src/main.cpp).

  python tools/bench.py info                 one command, its reply
  python tools/bench.py shot out.png         the framebuffer as a PNG (what the panel is sent)
  python tools/bench.py mic 2 out.wav        a recording (0 yes/no, 1 open, 2 level, 3 five raw seconds) as a WAV
  python tools/bench.py run script.txt DIR   one command per line; every "shot" lands in DIR as NN.png

Needs pyserial (PlatformIO's Python has it: ~/.platformio/penv/bin/python).
"""
import base64, pathlib, struct, sys, time, zlib
import serial

PORT = "/dev/cu.usbmodem101"
B64 = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=")


def open_port():
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = PORT, 115200, 0.2
    s.dtr = s.rts = True                      # as macOS raises them on open: no RTS-without-DTR edge, no reset
    s.open()
    # macOS toggles the lines on open anyway, which can reset the board: wait out its boot
    end = time.time() + 1.5
    while time.time() < end:
        if s.read(4096):
            end = max(end, time.time() + 1.0)   # still talking (booting): give it a little longer
    return s


def reply(s, prefixes, timeout=10.0):
    """Lines until one starts with a prefix; a block (SHOT/WAV) is read through its END."""
    end = time.time() + timeout
    while time.time() < end:
        line = s.readline().decode("utf-8", "replace").strip()
        if not line:
            continue
        if line.startswith(("SHOT ", "WAV")) and ("SHOT" in prefixes or "WAV" in prefixes):
            head, body = line, []
            while True:
                b = s.readline().decode("ascii", "replace").strip()
                if b == "END":
                    return head, base64.b64decode("".join(body))
                if b and len(b) <= 76 and all(c in B64 for c in b):
                    body.append(b)
                elif b:                           # another task's log line landed inside the dump
                    print("  |", b)
        if line.startswith(prefixes) or line[1:].startswith(prefixes):   # (the first byte after open can be lost)
            return line, None
        print("  |", line)                       # the firmware's own log, passed through
    raise TimeoutError(f"no reply starting with {prefixes}")


def png(path, w, h, rgb565):
    rows = b""
    for y in range(h):
        row = bytearray(b"\0")
        for (v,) in struct.iter_unpack("<H", rgb565[y * w * 2:(y + 1) * w * 2]):
            row += bytes(((v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31))
        rows += row
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    pathlib.Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                                   + chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


def command(s, cmd, out=None):
    if cmd.startswith("wait"):                # the bench's own pause, not sent
        time.sleep(float(cmd.split()[1]))
        return cmd
    s.write((cmd + "\n").encode())
    if cmd == "shot":
        for attempt in range(3):              # a log line cut into a base64 line breaks it: take it again
            head, data = reply(s, ("SHOT", "ERR"), 20)
            _, w, h = head.split()
            if len(data) == int(w) * int(h) * 2:
                png(out, int(w), int(h), data)
                return f"shot -> {out}"
            s.write(b"shot\n")
        raise RuntimeError("shot: the frame never arrived whole")
    if cmd.startswith("mic"):
        line, _ = reply(s, ("OK", "ERR"))
        if line.startswith("ERR"):
            return line
        head, _ = reply(s, ("HEARD",), 15)
        try:
            _, wav = reply(s, ("WAV",), 3)
            if out and wav:
                pathlib.Path(out).write_bytes(wav)
        except TimeoutError:
            pass
        return head
    return reply(s, ("OK", "ERR", "INFO", "I2C", "NAPS", "RAILS", "CODEC", "BATLOG"))[0]


def main():
    s = open_port()
    s.reset_input_buffer()
    a = sys.argv[1:]
    if a[0] == "run":
        out = pathlib.Path(a[2]); out.mkdir(parents=True, exist_ok=True)
        k = 0
        for cmd in pathlib.Path(a[1]).read_text().splitlines():
            cmd = cmd.split("#")[0].strip()
            if not cmd:
                continue
            name = None
            if cmd.startswith("shot"):
                k += 1
                label = cmd[4:].strip() or "shot"
                name, cmd = out / f"{k:02d}-{label}.png", "shot"
            print(command(s, cmd, name))
    else:
        print(command(s, " ".join(x for x in a if not x.endswith((".png", ".wav"))),
                      next((x for x in a if x.endswith((".png", ".wav"))), None)))


if __name__ == "__main__":
    main()
