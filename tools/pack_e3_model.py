"""Pack reconstructed resources as copies/differences against the user's BOX.

The output contains no complete retail archive. Identical retail resources and
the Original model are copied from the installed game when the cache is built.
"""
import argparse
import hashlib
from pathlib import Path
import struct


def archive(path):
    data = path.read_bytes()
    index, = struct.unpack_from('<I', data)
    count, = struct.unpack_from('<I', data, index)
    assert 0 < count < 16384 and index + 4 + 12 * (count - 1) == len(data)
    entries = [None] + [struct.unpack_from('<II4s', data, index + 4 + 12*i)
                        for i in range(count-1)]
    assert all(o >= 4 and o+s <= index for o, s, _ in entries[1:])
    return data, entries


def spans(before, after):
    assert len(before) == len(after)
    if before == after:
        return []
    changes = []
    start = None
    # Mesh fields are 32-bit, so grouping by words keeps the pack compact.
    for block in range(0,len(after),1024):
        end = min(block+1024,len(after))
        if before[block:end] == after[block:end]:
            if start is not None:
                changes.append((start,after[start:block]));start=None
            continue
        for offset in range(block,end,4):
            different = before[offset:offset+4] != after[offset:offset+4]
            if different and start is None:
                start = offset
            if not different and start is not None:
                changes.append((start, after[start:offset])); start = None
    if start is not None:
        changes.append((start, after[start:]))
    return changes


def deduplicate(data):
    """Store repeated mesh blocks once, including across different-size parts."""
    output=bytearray(struct.pack('<4sII',b'N50Z',1,len(data)))
    seen={};position=start=0
    while position+32<=len(data):
        key=data[position:position+32];previous=seen.get(key);seen[key]=position
        if previous is None:
            position+=4;continue
        size=32
        while (position+size+1024<=len(data) and
               data[previous+size:previous+size+1024]==data[position+size:position+size+1024]):
            size+=1024
        while position+size+4<=len(data) and data[previous+size:previous+size+4]==data[position+size:position+size+4]:
            size+=4
        literal=data[start:position]
        output.extend(struct.pack('<I',len(literal)));output.extend(literal)
        output.extend(struct.pack('<II',position-previous,size))
        for probe in range(position+4,position+size,4):
            if probe+32<=len(data):seen[data[probe:probe+32]]=probe
        position+=size;start=position
    if start<len(data):
        output.extend(struct.pack('<I',len(data)-start));output.extend(data[start:])
    return output


def build(base, target, fixture, output):
    original, old = archive(base)
    rebuilt, new = archive(target)
    metadata = (fixture/'dual-models.bin').read_bytes()
    assert len(metadata) == 32 and metadata[:4] == b'N50D'
    _, version, resources, first_mesh, mesh_count, first_texture, texture_count, _ = struct.unpack('<4s7I', metadata)
    assert version in (1, 2, 3, 4) and resources == len(new) and mesh_count == 1200
    assert resources == first_texture + texture_count + (2 if version >= 2 else 0)
    original_ids = [*range(3885,3905), *range(4070,4290), *range(8139,9099)]
    def get(data, entries, i):
        o, s, _ = entries[i]; return data[o:o+s]
    old_hashes = {hashlib.sha256(get(original,old,i)).digest():i for i in range(1,len(old))}
    candidates = {}
    records = bytearray(); modes = [0,0,0]
    for i in range(1,len(new)):
        raw = get(rebuilt,new,i); kind = new[i][2]
        candidates_for_part = []
        if i < len(old) and old[i][1] == len(raw):
            candidates_for_part.append((0,i,get(original,old,i)))
        exact = old_hashes.get(hashlib.sha256(raw).digest())
        if exact:
            candidates_for_part.append((0,exact,raw))
        if first_mesh <= i < first_mesh+mesh_count:
            source = original_ids[i-first_mesh]
            candidates_for_part.append((0,source,get(original,old,source)))
        # Reuse prior geometry for the same part across injury/team variants.
        key = (kind,len(raw))
        for previous in candidates.get(key,[])[:24]:
            candidates_for_part.append((1,previous,get(rebuilt,new,previous)))
        best = (len(raw),2,0,[])
        for mode, source, before in candidates_for_part:
            changes = spans(before,raw)
            cost = sum(8+len(pixels) for _,pixels in changes)
            if cost < best[0]:
                best = cost,mode,source,changes
                if cost == 0: break
        _,mode,source,changes = best; modes[mode] += 1
        records.extend(struct.pack('<III4sI',mode,source,len(raw),kind,len(changes)))
        if mode == 2:
            records.extend(raw)
        for offset,pixels in changes:
            records.extend(struct.pack('<II',offset,len(pixels))); records.extend(pixels)
        candidates.setdefault(key,[]).append(i)
    lists = ('HEROINE.RES','L1.RES','L8.RES','L10.RES','NETGAME.RES')
    preloads = bytearray()
    for name in lists:
        data = (fixture/'patched-pst'/name).read_bytes()
        assert len(data) >= 8 and len(data) == 4+4*struct.unpack_from('<I',data)[0]
        preloads.extend(struct.pack('<I',len(data))); preloads.extend(data)
    packed = (struct.pack('<4sIII',b'N50P',1,len(old),len(new)) +
              hashlib.sha256(original).digest() + metadata + records + preloads)
    compact=deduplicate(packed)
    output.parent.mkdir(parents=True,exist_ok=True); output.write_bytes(compact)
    print(f'Model delta: {len(compact)} bytes ({len(packed)} before block sharing); base copies={modes[0]}, '
          f'reconstructed copies={modes[1]}, new resources={modes[2]}')
    print('SHA256:',hashlib.sha256(compact).hexdigest())


if __name__ == '__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--base',required=True,type=Path)
    p.add_argument('--target',required=True,type=Path)
    p.add_argument('--fixture',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path)
    a=p.parse_args(); build(a.base,a.target,a.fixture,a.output)
