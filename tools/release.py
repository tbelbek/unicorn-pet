#!/usr/bin/env python3
"""Cut a signed firmware release for the toy's over-the-air updates (fw/src/ota.cpp).

  uv run --with cryptography python tools/release.py             bump FW_VERSION, build, sign -> dist/
  uv run --with cryptography python tools/release.py --keygen    only create the key and fw/src/release_key.h
  uv run --with cryptography python tools/release.py --selftest  sign and check a dummy image (make -C test release)

The signing key lives at ~/.config/unicorn-pet/release-key.pem (mode 600), never in the repo; only its
public half goes into fw/src/release_key.h. Nothing is published: the script prints the
`gh release create` command, and running it is your decision.
"""
import argparse, base64, hashlib, json, os, pathlib, re, shutil, subprocess, sys

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

ROOT = pathlib.Path(__file__).resolve().parent.parent
KEY = pathlib.Path.home() / ".config/unicorn-pet/release-key.pem"
HEADER = ROOT / "fw/src/release_key.h"
VERSION_H = ROOT / "fw/src/version.h"
BUILT = ROOT / "fw/.pio/build/amoled18/firmware.bin"
DIST = ROOT / "dist"
REPO = "tbelbek/unicorn-pet"
SLOT = 0x640000                    # one app slot in default_16MB.csv
MANIFEST_MAX = 1023                # the toy reads at most this much (fw/src/ota.cpp)


def message(version, size, sha):   # byte for byte what ota_message() in fw/src/manifest.h builds
    return f"unicorn-pet|{version}|{size}|{sha}".encode()


def sign(key, version, data):
    sha = hashlib.sha256(data).hexdigest()
    sig = key.sign(message(version, len(data), sha), ec.ECDSA(hashes.SHA256()))   # DER
    return {"version": version, "size": len(data), "sha256": sha, "sig": base64.b64encode(sig).decode()}


def verify(pub, m, data):
    """What the toy checks: the signature over the manifest, then the image's size and hash."""
    try:
        pub.verify(base64.b64decode(m["sig"], validate=True), message(m["version"], m["size"], m["sha256"]),
                   ec.ECDSA(hashes.SHA256()))
    except (InvalidSignature, ValueError, KeyError, TypeError):
        return False
    return len(data) == m["size"] and hashlib.sha256(data).hexdigest() == m["sha256"]


def der(pub):
    return pub.public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)


def header_text(pub):
    pem = pub.public_bytes(serialization.Encoding.PEM, serialization.PublicFormat.SubjectPublicKeyInfo).decode()
    body = "\n".join(f'    "{line}\\n"' for line in pem.strip().splitlines())
    return ("#pragma once\n"
            "// The public half of the release signing key, written by tools/release.py (the private half stays at\n"
            "// ~/.config/unicorn-pet/release-key.pem, outside the repo). Only images it signed are installed.\n"
            f"static const char RELEASE_PUBKEY[] =\n{body};\n")


def header_key(text):
    """The key back out of release_key.h, as the firmware embeds it (None: no key yet)."""
    after = text.split("RELEASE_PUBKEY", 1)[1]
    pem = "".join(re.findall(r'"((?:[^"\\]|\\.)*)"', after)).replace("\\n", "\n")
    return serialization.load_pem_public_key(pem.encode()) if pem.strip() else None


def load_key(create):
    in_header = header_key(HEADER.read_text())
    if KEY.exists():
        if KEY.stat().st_mode & 0o077:
            print(f"warning: {KEY} is readable by others; chmod 600 it")
        key = serialization.load_pem_private_key(KEY.read_bytes(), None)
        if in_header and der(in_header) != der(key.public_key()):
            sys.exit(f"{HEADER.relative_to(ROOT)} holds a different key than {KEY}. Toys in the field trust the "
                     "header's key: restore the matching private key instead of signing with this one.")
        return key
    if in_header:
        sys.exit(f"{KEY} is missing, but {HEADER.relative_to(ROOT)} already holds a key. Restore the private key "
                 "from your backup: a new key would make every toy refuse every update until it is reflashed "
                 "over USB.")
    if not create:
        sys.exit("no release key yet")
    KEY.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    key = ec.generate_private_key(ec.SECP256R1())
    pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                            serialization.NoEncryption())
    with os.fdopen(os.open(KEY, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600), "wb") as f:
        f.write(pem)
    print(f"created the release key {KEY} (mode 600).\n"
          "BACK IT UP somewhere safe: without it no toy accepts another update until it is reflashed over USB.")
    return key


def write_header(pub):
    text = header_text(pub)
    if HEADER.read_text() != text:
        HEADER.write_text(text)
        print(f"wrote the public key into {HEADER.relative_to(ROOT)}: commit it")


def release():
    key = load_key(create=True)
    write_header(key.public_key())
    old = VERSION_H.read_text()
    found = re.search(r"#define FW_VERSION (\d+)", old)
    version = int(found.group(1)) + 1
    VERSION_H.write_text(old.replace(found.group(0), f"#define FW_VERSION {version}"))
    pio = shutil.which("pio") or str(pathlib.Path.home() / ".local/bin/pio")
    try:
        subprocess.run([pio, "run", "-d", str(ROOT / "fw")], check=True)
    except BaseException:
        VERSION_H.write_text(old)          # a failed build does not use up a version number
        raise
    data = BUILT.read_bytes()
    if not data or data[0] != 0xE9 or len(data) > SLOT:
        sys.exit(f"{BUILT} is not an app image that fits a {SLOT:#x} slot")
    manifest = sign(key, version, data)
    text = json.dumps(manifest) + "\n"
    # self-check against the key the image itself carries, the way the toy will check it
    if len(text) > MANIFEST_MAX or not verify(header_key(HEADER.read_text()), json.loads(text), data):
        sys.exit("self-check failed: nothing written")
    DIST.mkdir(exist_ok=True)
    (DIST / "firmware.bin").write_bytes(data)
    (DIST / "manifest.json").write_text(text)
    print(f"\nrelease v{version}: dist/firmware.bin ({len(data)} bytes), dist/manifest.json, signature verified.\n"
          f"Commit fw/src/version.h (and fw/src/release_key.h if it changed) and push, then publish with:\n\n"
          f"  gh release create v{version} dist/firmware.bin dist/manifest.json --repo {REPO} --latest "
          f"--title \"Unicorn Pet v{version}\" --notes \"Firmware {version}\"\n")


def selftest():
    """A throwaway key (never the real one): a good image passes, every kind of tampering fails."""
    key = ec.generate_private_key(ec.SECP256R1())
    pub = key.public_key()
    image = bytes([0xE9]) + os.urandom(8191)
    m = json.loads(json.dumps(sign(key, 7, image)))         # through JSON, as the toy gets it
    assert verify(pub, m, image), "a signed image must pass"
    assert der(header_key(header_text(pub))) == der(pub), "release_key.h must carry the key unchanged"
    bad = bytearray(image)
    bad[1000] ^= 1
    assert not verify(pub, m, bytes(bad)), "a changed image must fail"
    assert not verify(pub, m, image + b"\0"), "a longer image must fail"
    assert not verify(pub, {**m, "version": 8}, image), "a changed version (replay as newer) must fail"
    assert not verify(pub, {**m, "size": m["size"] + 1}, image + b"\0"), "a changed size must fail"
    assert not verify(pub, {**m, "sig": "AAAA"}, image), "a broken signature must fail"
    assert not verify(pub, {k: v for k, v in m.items() if k != "sig"}, image), "an unsigned manifest must fail"
    other = ec.generate_private_key(ec.SECP256R1()).public_key()
    assert not verify(other, m, image), "someone else's key must fail"
    # the limits fw/src/manifest.h parses within
    assert re.fullmatch(r"[0-9a-f]{64}", m["sha256"]) and len(m["sig"]) <= 96
    assert len(json.dumps(m)) + 1 <= MANIFEST_MAX
    print("release selftest ok: a signed image passes; tampered image, version, size, signature and key fail")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--selftest", action="store_true", help="sign and verify a dummy image with a throwaway key")
    ap.add_argument("--keygen", action="store_true", help="only create the key and fw/src/release_key.h")
    a = ap.parse_args()
    if a.selftest:
        selftest()
    elif a.keygen:
        write_header(load_key(create=True).public_key())
    else:
        release()
