"""Move identical E3 metalwork out of all injury/team parts without changing it.

The two shared native meshes retain the exact existing vertices, normals,
materials and triangles. Native resource IDs and wound surfaces stay intact.
Only local purchased-game fixtures are consumed; no game archive is shipped.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import shutil
import struct

from pack_e3_model import archive


def mesh(data):
    nf, fo, nr, ro, nv, vo = struct.unpack_from('<6I', data, 24)
    assert fo + nf * 52 <= len(data) and vo + nv * 32 <= len(data)
    vertices = [data[vo + 32*i:vo + 32*(i+1)] for i in range(nv)]
    faces = []
    for i in range(nf):
        raw = data[fo + 52*i:fo + 52*(i+1)]
        count, refs = struct.unpack_from('<II', raw, 40)
        assert 3 <= count <= 16 and refs + count * 16 <= len(data)
        corners = [data[refs + 16*j:refs + 16*(j+1)] for j in range(count)]
        indices = [(struct.unpack_from('<I', corner, 8)[0] - vo) // 32 for corner in corners]
        assert all(0 <= index < nv for index in indices)
        assert all(struct.unpack_from('<I', c, 8)[0] == vo + 32*j for c, j in zip(corners, indices))
        faces.append((raw, corners, indices))
    return vertices, faces


def face_key(vertices, face):
    raw, corners, indices = face
    # Flat metal faces do not sample the inherited texture ID at +36.
    return (raw[:36], raw[48:52], tuple(
        # +0 and +28 are mutable native projection/lighting caches, not shape.
        (vertices[i][4:28], c[:8], c[12:16]) for c, i in zip(corners, indices)))


def write_mesh(original, vertices, faces):
    used = sorted({i for _, _, indices in faces for i in indices})
    mapping, unique, compact = {}, {}, []
    for old in used:
        key = vertices[old][4:28]
        if key not in unique:
            unique[key] = len(compact)
            # Native PRT initializer 8B0D0 resets both cache words and calls
            # 8C170 to recompute bounds. Keep coordinates/normals exact.
            compact.append(bytes(4) + key + bytes(4))
        mapping[old] = unique[key]
    nf, nr, nv = len(faces), sum(len(f[2]) for f in faces), len(compact)
    fo, ro = 52, 52 + 52*nf
    vo = ro + 16*nr
    out = bytearray(original[:52])
    assert struct.unpack_from('<I', original)[0] & 5 == 0
    out[8:24] = bytes(16)
    struct.pack_into('<6I', out, 24, nf, fo, nr, ro, nv, vo)
    out.extend(bytes(vo - 52))
    out.extend(b''.join(compact))
    cursor = ro
    for i, (raw, corners, indices) in enumerate(faces):
        pos = fo + 52*i
        out[pos:pos+52] = raw
        struct.pack_into('<II', out, pos+40, len(indices), cursor)
        for corner, index in zip(corners, indices):
            out[cursor:cursor+16] = corner
            struct.pack_into('<I', out, cursor+8, vo + 32*mapping[index])
            cursor += 16
    assert cursor == vo
    return bytes(out)


def chain_kind(rid):
    if rid == 3903 or (4081 <= rid <= 4281 and (rid-4081) % 20 == 0) or (
            8139 <= rid <= 9079 and (rid-8139) % 20 == 0):
        return 0
    if rid == 3890 or (4070 <= rid <= 4270 and (rid-4070) % 20 == 0) or (
            8147 <= rid <= 9087 and (rid-8147) % 20 == 0):
        return 1
    return None


def build(source, output, base=None):
    assert not output.exists(), 'Use a new fixture directory'
    data, entries = archive(source/'DUNGEON-E3-RECONSTRUCTION.BOX')
    original_data, original_entries = archive(base) if base else (None, None)
    metadata = struct.unpack('<4s7I', (source/'dual-models.bin').read_bytes())
    assert metadata == (b'N50D', 1, 15345, 14135, 1200, 15335, 10, 14125)
    def resource(rid):
        offset, size, _ = entries[rid]
        return data[offset:offset+size]
    canonical, chains = [], []
    for rid in (3903, 3890):
        original = resource(rid)
        vertices, faces = mesh(original)
        metal = [f for f in faces if struct.unpack_from('<I', f[0], 24)[0] & 15 == 4]
        assert metal
        if rid == 3903:
            assert min(struct.unpack_from('<i', vertices[i], 8)[0]
                       for _, _, indices in metal for i in indices) >= -28
        canonical.append(collections.Counter(face_key(vertices, f) for f in metal))
        chains.append(write_mesh(original, vertices, metal))
    replacements = {}
    removed_bytes = 0
    for rid in [*range(3885, 3905), *range(4070, 4290), *range(8139, 9099)]:
        kind = chain_kind(rid)
        original = resource(rid)
        if kind is None and original_data is not None:
            offset, size, _ = original_entries[rid]
            if original == original_data[offset:offset+size]:
                continue
        vertices, faces = mesh(original)
        if kind is None:
            kept = faces
        else:
            selected = [f for f in faces if face_key(vertices, f) in canonical[kind]]
            assert collections.Counter(face_key(vertices, f) for f in selected) == canonical[kind], rid
            kept = [f for f in faces if face_key(vertices, f) not in canonical[kind]]
        replacement = write_mesh(original, vertices, kept)
        vv, ff = mesh(replacement)
        # Compare complete drawable attributes, not just counts or silhouettes.
        assert [face_key(vv, f) for f in ff] == [face_key(vertices, f) for f in kept]
        if kind is not None:
            cv, cf = mesh(chains[kind])
            assert collections.Counter(face_key(cv, f) for f in cf) == canonical[kind]
        replacements[rid] = replacement
        removed_bytes += len(original) - len(replacement)
    assert sum(chain_kind(rid) is not None for rid in replacements) == 120
    output.mkdir(parents=True)
    unique = {}
    new_entries = []
    with (output/'DUNGEON-E3-RECONSTRUCTION.BOX').open('wb') as file:
        file.write(bytes(4))
        for rid in range(1, len(entries)+2):
            raw = replacements.get(rid)
            if raw is None:
                raw = resource(rid) if rid < len(entries) else chains[rid-len(entries)]
            kind = entries[rid][2] if rid < len(entries) else b'.prt'
            key = (kind, raw)
            if key not in unique:
                unique[key] = file.tell()
                file.write(raw)
            new_entries.append((unique[key], len(raw), kind))
        index = file.tell()
        file.write(struct.pack('<I', len(new_entries)+1))
        for entry in new_entries:
            file.write(struct.pack('<II4s', *entry))
        file.seek(0)
        file.write(struct.pack('<I', index))
    (output/'dual-models.bin').write_bytes(struct.pack(
        '<4s7I', b'N50D', 2, 15347, 14135, 1200, 15335, 10, 14125))
    shutil.copytree(source/'patched-pst', output/'patched-pst')
    result = output/'DUNGEON-E3-RECONSTRUCTION.BOX'
    report = {'changed_parts': len(replacements), 'shared_chain_meshes': 2,
              'removed_repeated_mesh_bytes': removed_bytes,
              'shared_mesh_bytes': sum(map(len, chains)), 'previous_bytes': len(data),
              'optimized_bytes': result.stat().st_size,
              'sha256': hashlib.sha256(result.read_bytes()).hexdigest(),
              'drawable_attributes_preserved': True}
    (output/'chain-sharing.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--base', type=Path)
    args = parser.parse_args()
    build(args.source, args.output, args.base)
