"""Exercise the actual standalone EXE without requiring a purchased game in CI."""
import argparse
import ctypes
import hashlib
import os
from pathlib import Path
import shutil
import stat
import struct
import subprocess
import tempfile


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def pe_check(exe):
    data = exe.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    machine, sections = struct.unpack_from('<HH', data, pe + 4)
    optional_size = struct.unpack_from('<H', data, pe + 20)[0]
    optional = pe + 24
    check(machine == 0x14c and struct.unpack_from('<H', data, optional)[0] == 0x10b, 'Installer must be native x86')
    check(struct.unpack_from('<HH', data, optional + 48) == (6, 1), 'Minimum subsystem must be Windows 7')
    check(struct.unpack_from('<H', data, optional + 68)[0] == 2, 'Installer must use GUI subsystem')
    directories = optional + 96
    check(struct.unpack_from('<II', data, directories + 14 * 8) == (0, 0), 'Installer must not depend on the CLR')
    headers = optional + optional_size

    def offset(rva):
        for i in range(sections):
            at = headers + i * 40
            size, address, raw_size, raw = struct.unpack_from('<IIII', data, at + 8)
            if address <= rva < address + max(size, raw_size):
                return raw + rva - address
        raise ValueError('RVA outside PE sections')

    imports = []
    debug_rva, debug_size = struct.unpack_from('<II', data, directories + 6 * 8)
    if debug_size:
        debug = offset(debug_rva)
        for at in range(debug, debug + debug_size, 28):
            check(struct.unpack_from('<I', data, at + 12)[0] != 2, 'Installer must not embed CodeView/PDB paths')
    rva = struct.unpack_from('<I', data, directories + 8)[0]
    cursor = offset(rva)
    while any(data[cursor:cursor + 20]):
        name_rva = struct.unpack_from('<I', data, cursor + 12)[0]
        start = offset(name_rva)
        imports.append(data[start:data.index(0, start)].decode('ascii').lower())
        cursor += 20
    allowed = {'kernel32.dll', 'user32.dll', 'advapi32.dll', 'gdi32.dll', 'comctl32.dll', 'shell32.dll', 'ole32.dll', 'cabinet.dll'}
    check(set(imports) <= allowed, 'Unexpected installer runtime imports: ' + str(imports))
    print('PASS: native x86, Win7 subsystem, static runtime, no CLR/debug paths; imports:', ', '.join(imports))

    root = offset(struct.unpack_from('<I', data, directories + 2 * 8)[0])
    def entries(relative):
        at = root + relative
        named, ids = struct.unpack_from('<HH', data, at + 12)
        return [struct.unpack_from('<II', data, at + 16 + i * 8) for i in range(named + ids)]
    rcdata = next(child & 0x7fffffff for name, child in entries(0) if name == 10)
    payload = next(child & 0x7fffffff for name, child in entries(rcdata) if name == 101)
    languages = entries(payload)
    check(len(languages) == 1 and not languages[0][1] & 0x80000000, 'Invalid payload resource languages')
    rva, size = struct.unpack_from('<II', data, root + languages[0][1])
    return offset(rva), size


def snapshot(root):
    return {p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in root.rglob('*') if p.is_file()}


def run(exe, *args, success=True):
    result = subprocess.run([str(exe), *map(str, args)], capture_output=True, timeout=60)
    check((result.returncode == 0) == success,
          f'Unexpected exit {result.returncode}: {result.stdout.decode("utf-8", errors="replace")}')
    return result


def fixture(root):
    root.mkdir()
    (root / 'ASYLUM').mkdir()
    (root / 'DD_CD.EXE').write_bytes(b'retail exe fixture')
    (root / 'Dungeon.dll').write_bytes(b'retail DLL fixture')
    (root / 'DINPUT.dll').write_bytes(b'previous patch')
    (root / 'ASYLUM/keys.cfg').write_bytes(b'previous controls')
    (root / 'ASYLUM/config.dat').write_bytes(
        b'RESOLUTION 5RENDERING_PLATFORM 0\r\nD3D_ALLOW_MIPMAP 0\r\nRENDERING_PLATFORM 1\r\nKEEP 7\r\n')
    (root / 'deathtrap_native.ini').write_text(
        '[Display]\nWindowWidth=1600\nWindowHeight=900\n[Models]\nE3Heroine=1\n', encoding='ascii')
    (root / 'save.dat').write_bytes(b'preserve my save')


def main(exe):
    resource, resource_size = pe_check(exe)
    run(exe, '--verify-payload')
    with tempfile.TemporaryDirectory(prefix='native50-installer-test-') as directory:
        root = Path(directory)
        game = root / 'Тест игры with spaces'
        fixture(game)
        before = snapshot(game)
        run(exe, '--install', '--game-dir', game, success=False)
        check(snapshot(game) == before, 'Unknown game changed without consent')
        config_original = (game / 'ASYLUM/config.dat').read_bytes()
        (game / 'ASYLUM/config.dat').unlink()
        missing_before = snapshot(game)
        run(exe, '--install', '--accept-unknown', '--game-dir', game, success=False)
        check(snapshot(game) == missing_before, 'Missing-file preflight changed files')
        (game / 'ASYLUM/config.dat').write_bytes(config_original)
        os.chmod(game / 'DINPUT.dll', stat.S_IREAD)
        try:
            run(exe, '--install', '--accept-unknown', '--game-dir', game, success=False)
            check(snapshot(game) == before, 'Read-only preflight changed files')
        finally:
            os.chmod(game / 'DINPUT.dll', stat.S_IWRITE | stat.S_IREAD)
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.CreateFileW.restype = ctypes.c_void_p
        kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p,
                                      ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p]
        kernel.CloseHandle.argtypes = [ctypes.c_void_p]
        handle = kernel.CreateFileW(str(game / 'DINPUT.dll'), 0x80000000, 0, None, 3, 0, None)
        check(handle != ctypes.c_void_p(-1).value, 'Cannot create locked-file fixture')
        try:
            run(exe, '--install', '--accept-unknown', '--game-dir', game, success=False)
        finally:
            kernel.CloseHandle(handle)
        check(snapshot(game) == before, 'Locked-file preflight changed files')
        report = root / 'explicit setup report.txt'
        run(exe, '--install', '--accept-unknown', '--game-dir', game, '--log', report)
        check('100%' in report.read_text(encoding='utf-8'), 'Completion report missing')
        backups = list((game / 'back').iterdir())
        check(len(backups) == 1, 'Expected one unique rollback copy')
        for name in before:
            if name in ('DD_CD.EXE', 'Dungeon.dll', 'save.dat'):
                check(hashlib.sha256((game / name).read_bytes()).hexdigest() == before[name], 'Original or save changed: ' + name)
            else:
                check(hashlib.sha256((backups[0] / name).read_bytes()).hexdigest() == before[name], 'Incorrect rollback file: ' + name)
        ini = (game / 'deathtrap_native.ini').read_text()
        check('E3Heroine=1' in ini and 'WindowWidth=1600' in ini and 'WindowHeight=900' in ini, 'Settings not preserved')
        config = (game / 'ASYLUM/config.dat').read_bytes()
        check(b'RESOLUTION 5\r\nRENDERING_PLATFORM 13\r\n' in config and config.count(b'RENDERING_PLATFORM') == 1, 'Retail rendering profile invalid')
        check(b'\r\r\n' not in config and b'KEEP 7' in config, 'Retail config formatting damaged')
        check((game / 'native50-e3-model.n50p').stat().st_size > 1000000, 'Optional model missing')
        for name in ('README.txt', 'LICENSE.txt', 'THIRD_PARTY_NOTICES.txt', 'licenses/dxwrapper.txt'):
            check((game / 'Native50' / name).is_file(), 'Support/license file missing: ' + name)
        run(exe, '--install', '--accept-unknown', '--reset-settings', '--game-dir', game)
        check('E3Heroine=0' in (game / 'deathtrap_native.ini').read_text(), 'Reset settings option failed')
        check(len(list((game / 'back').iterdir())) == 2, 'Update overwrote previous rollback copy')
        check(not list(game.glob('.native50-setup-*')), 'Private staging left behind')
        print('PASS: missing/unknown/read-only/locked inputs, Unicode paths, update, backup, settings, saves, E3 payload and rendering config')
        original = exe.read_bytes()
        packed = original[resource:resource + resource_size]
        check(packed[:8] == b'N50CAB01', 'Expected compressed payload')
        raw_size, cab_size = struct.unpack_from('<II', packed, 40)
        check(cab_size == resource_size - 48 and packed[48:52] == b'MSCF', 'Invalid compressed resource')
        check(hashlib.sha256(packed[48:]).digest() == packed[8:40], 'Compressed payload hash mismatch')
        check(cab_size < raw_size * 0.6, 'Release installer payload is not effectively compressed')
        before = snapshot(game)
        for case in ('digest', 'compressed-data', 'oversized', 'wrong-length', 'compressed-length', 'cabinet-header', 'cabinet-file', 'cabinet-stream'):
            data = bytearray(original)
            if case == 'digest': data[resource + 10] ^= 1
            elif case == 'compressed-data': data[resource + resource_size - 20] ^= 1
            elif case == 'oversized': struct.pack_into('<I', data, resource + 40, 0xffffffff)
            elif case == 'wrong-length': struct.pack_into('<I', data, resource + 40, raw_size + 1)
            elif case == 'compressed-length': struct.pack_into('<I', data, resource + 44, cab_size + 1)
            else:
                # Rehash the changed CAB too, so the Windows decoder and its
                # memory/output bounds are tested beyond the outer digest.
                cabinet = resource + 48
                if case == 'cabinet-header': data[cabinet] ^= 1
                elif case == 'cabinet-stream':
                    first_block = struct.unpack_from('<I', data, cabinet + 36)[0]
                    struct.pack_into('<H', data, cabinet + first_block + 4, 0xffff)
                else:
                    file_table = struct.unpack_from('<I', data, cabinet + 16)[0]
                    data[cabinet + file_table + 16] = ord('x')
                data[resource + 8:resource + 40] = hashlib.sha256(data[resource + 48:resource + resource_size]).digest()
            damaged = root / ('damaged-' + case + '.exe')
            damaged.write_bytes(data)
            run(damaged, '--verify-payload', success=False)
            run(damaged, '--install', '--accept-unknown', '--game-dir', game, success=False)
            check(snapshot(game) == before, 'Damaged payload changed game files: ' + case)
            damaged.unlink()
        print('PASS: compressed payload hash, size bounds, invalid cabinet/header/name/stream rejected before game writes')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('exe', type=Path)
    main(parser.parse_args().exe.resolve())
