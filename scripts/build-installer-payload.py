"""Create a deterministic, allowlisted native installer resource from release staging."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile

FILES = {
    'payload/DINPUT.dll': 'DINPUT.dll',
    'payload/native50-e3-model.n50p': 'native50-e3-model.n50p',
    'payload/deathtrap_native.ini': 'deathtrap_native.ini',
    'payload/keys.cfg': 'ASYLUM/keys.cfg',
    'payload/dxwrapper/DDraw.dll': 'DDraw.dll',
    'payload/dxwrapper/dxwrapper.dll': 'dxwrapper.dll',
    'payload/dxwrapper.ini': 'dxwrapper.ini',
    'payload/dgVoodoo/D3D9.dll': 'D3D9.dll',
    'payload/dgVoodoo/D3DImm.dll': 'D3DImm.dll',
    'payload/dgVoodoo.conf': 'dgVoodoo.conf',
    **{name: 'Native50/' + name for name in (
        'README.txt', 'CHANGELOG.txt', 'LICENSE.txt', 'THIRD_PARTY_NOTICES.txt',
        'licenses/dxwrapper.txt', 'source/dxwrapper/README.md',
        'source/dxwrapper/deathtrap-dxwrapper.patch',
        'source/dxwrapper/deathtrap-page-restore-present.patch')},
}
PINNED = {
    'DDraw.dll': '8bae794eb7506711f57b690cfb8660a5f008b0185e764f8cb56d5972e01a9f33',
    'dxwrapper.dll': '304c3a528ad1ab4b25bbee48da0371a9db435df1cdf733cd1be13c2ff27bcc8e',
    'D3D9.dll': 'd8d2e15bf5d0e01c89317a733492997de8f7f562a972fe564fd17d194ee5d1f3',
    'D3DImm.dll': '8b2850d0af5f07cf2928ac9666192c3adcb0290f10ed8f942f1594f3a4f51c73',
}


def compress(payload, directory):
    # Use the Windows CAB codec: LZX compresses the complete verified payload,
    # and the Win7-compatible FDI decoder needs no redistributed archive DLL.
    if not 44 <= len(payload) <= 64 * 1024 * 1024:
        raise ValueError('Invalid total installer payload size')
    with tempfile.TemporaryDirectory(prefix='native50-cab-', dir=directory) as temporary:
        root = Path(temporary)
        raw = root / 'payload.bin'
        raw.write_bytes(payload)
        os.utime(raw, (946684800, 946684800))
        makecab = Path(os.environ['SystemRoot']) / 'System32/makecab.exe'
        result = subprocess.run([str(makecab), '/D', 'CompressionType=LZX', '/D',
                                 'CompressionMemory=21', 'payload.bin', 'payload.cab'],
                                cwd=root, capture_output=True)
        if result.returncode:
            raise RuntimeError('Windows CAB compression failed: ' +
                               result.stdout.decode(errors='replace'))
        cabinet = (root / 'payload.cab').read_bytes()
        if cabinet[:4] != b'MSCF' or struct.unpack_from('<I', cabinet, 8)[0] != len(cabinet):
            raise ValueError('Invalid compressed cabinet')
    return (b'N50CAB01' + hashlib.sha256(cabinet).digest() +
            struct.pack('<II', len(payload), len(cabinet)) + cabinet)


def build(stage, output):
    manifest = {}
    for line in (stage / 'SHA256SUMS.txt').read_text(encoding='utf-8').splitlines():
        digest, name = line.split('  ', 1)
        if name in manifest:
            raise ValueError('Duplicate checksum entry')
        manifest[name] = digest.lower()
    body = bytearray(struct.pack('<I', len(FILES)))
    for source, destination in sorted(FILES.items()):
        path = stage / source
        if path.is_symlink() or not path.resolve().is_relative_to(stage.resolve()):
            raise ValueError('Payload cannot contain links')
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if manifest.get(source) != digest or (destination in PINNED and PINNED[destination] != digest):
            raise ValueError('Payload checksum mismatch: ' + source)
        if not data or len(data) > 64 * 1024 * 1024:
            raise ValueError('Invalid payload size: ' + source)
        if destination.endswith('.dll') and not data.startswith(b'MZ'):
            raise ValueError('Invalid runtime: ' + source)
        name = destination.encode('ascii')
        body += struct.pack('<HI', len(name), len(data)) + bytes.fromhex(digest) + name + data
    output.parent.mkdir(parents=True, exist_ok=True)
    payload = b'N50SET01' + hashlib.sha256(body).digest() + body
    packed = compress(payload, output.parent)
    output.write_bytes(packed)
    print(f'Native installer payload: {len(FILES)} reviewed files, '
          f'{len(payload)} -> {len(packed)} bytes (LZX)')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.stage, args.output)
