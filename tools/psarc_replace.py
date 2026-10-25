"""Rewrite a PSARC with some of its files replaced, every other block copied as is.

    python tools/psarc_replace.py IN.PSARC OUT.PSARC /PATH/IN/ARCHIVE=local_file [...]
    python tools/psarc_replace.py IN.PSARC --cat /PATH/IN/ARCHIVE      (print one file)
    python tools/psarc_replace.py IN.PSARC --list

The PSARC layout (version 1.4, zlib): a 32-byte header (magic, version,
compression, TOC length, TOC entry size, entry count, block size, flags), the
entries (MD5 of the path, first block index, 40-bit size, 40-bit offset), a
table of compressed block sizes (0 meaning a full stored block), then the data.
Entry 0 is the manifest, the paths of the others one per line. Used for the
PT-BR mod: GAME.PSARC's LANGS_BCES01741.TXT with the US release's
"17 pt-br BR_PORTUGUESE" line added.
"""
import struct
import sys
import zlib


class Psarc:
    def __init__(self, path):
        self.f = open(path, 'rb')
        (self.magic, self.version, self.comp, self.toc_len, self.ent_size, self.n,
         self.block, self.flags) = struct.unpack('>4sI4sIIIII', self.f.read(32))
        if self.magic != b'PSAR' or self.comp != b'zlib':
            raise SystemExit(f'{path}: not a zlib PSARC')
        toc = self.f.read(self.toc_len - 32)
        self.entries = []
        for i in range(self.n):
            e = toc[i * self.ent_size:(i + 1) * self.ent_size]
            self.entries.append((e[:16], struct.unpack('>I', e[16:20])[0],
                                 int.from_bytes(e[20:25], 'big'), int.from_bytes(e[25:30], 'big')))
        self.zbytes = 2 if self.block <= 0x10000 else 3 if self.block <= 0x1000000 else 4
        table = toc[self.n * self.ent_size:]
        self.zsizes = [int.from_bytes(table[i:i + self.zbytes], 'big')
                       for i in range(0, len(table) - self.zbytes + 1, self.zbytes)]
        self.names = self.read(0).decode('utf-8').split('\n')

    def nblocks(self, idx):
        size = self.entries[idx][2]
        return max(1, -(-size // self.block)) if size else 0

    def raw_blocks(self, idx):
        """The entry's blocks as stored: (bytes, stored size field)."""
        _, b0, size, off = self.entries[idx]
        self.f.seek(off)
        out = []
        for b in range(b0, b0 + self.nblocks(idx)):
            z = self.zsizes[b]
            out.append((self.f.read(z or self.block), z))
        return out

    def read(self, idx):
        size = self.entries[idx][2]
        data = b''
        for raw, z in self.raw_blocks(idx):
            remaining = size - len(data)
            if z == 0 or len(raw) == min(self.block, remaining) and raw[:1] != b'\x78':
                data += raw
            else:
                try:
                    data += zlib.decompress(raw)
                except zlib.error:
                    data += raw
        return data[:size]

    def index_of(self, path):
        return self.names.index(path) + 1


def encode_blocks(data, block):
    out = []
    for i in range(0, len(data), block):
        chunk = data[i:i + block]
        z = zlib.compress(chunk, 9)
        if len(z) < len(chunk):
            out.append((z, len(z)))
        else:
            out.append((chunk, 0 if len(chunk) == block else len(chunk)))
    return out


def write(src, out_path, replace):
    blocks_per_entry = []
    for i in range(src.n):
        name = src.names[i - 1] if i else None
        if name in replace:
            blocks_per_entry.append((len(replace[name]), encode_blocks(replace[name], src.block)))
        else:
            blocks_per_entry.append((src.entries[i][2], src.raw_blocks(i)))
    nblocks = sum(len(b) for _, b in blocks_per_entry)
    toc_len = 32 + src.n * src.ent_size + nblocks * src.zbytes
    entries, table, data = b'', b'', bytearray()
    bidx = 0
    for i, (size, blocks) in enumerate(blocks_per_entry):
        entries += (src.entries[i][0] + struct.pack('>I', bidx) + size.to_bytes(5, 'big')
                    + (toc_len + len(data)).to_bytes(5, 'big'))
        for raw, z in blocks:
            table += z.to_bytes(src.zbytes, 'big')
            data += raw
        bidx += len(blocks)
    header = struct.pack('>4sI4sIIIII', b'PSAR', src.version, b'zlib', toc_len, src.ent_size,
                         src.n, src.block, src.flags)
    with open(out_path, 'wb') as f:
        f.write(header + entries + table + bytes(data))


def main(argv):
    src = Psarc(argv[1])
    if argv[2] == '--list':
        for i, name in enumerate(src.names, 1):
            print(name, src.entries[i][2])
        return 0
    if argv[2] == '--cat':
        sys.stdout.buffer.write(src.read(src.index_of(argv[3])))
        return 0
    replace = {}
    for spec in argv[3:]:
        path, local = spec.split('=', 1)
        src.index_of(path)                      # must exist
        replace[path] = open(local, 'rb').read()
    write(src, argv[2], replace)
    check = Psarc(argv[2])
    for i in range(1, src.n):
        name = src.names[i - 1]
        want = replace[name] if name in replace else src.read(i)
        if check.read(i) != want:
            raise SystemExit(f'verify failed: {name}')
    print(f'{argv[2]}: {src.n - 1} files, {len(replace)} replaced, verified')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
