"""Create a deterministic, allowlisted native installer resource from release staging."""
import argparse
import hashlib
from pathlib import Path
import struct

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
    output.write_bytes(b'N50SET01' + hashlib.sha256(body).digest() + body)
    print(f'Native installer payload: {len(FILES)} reviewed files, {len(body) + 40} bytes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build(args.stage, args.output)
