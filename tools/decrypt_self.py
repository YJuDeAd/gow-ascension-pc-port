#!/usr/bin/env python3
# From sp00nznet/twistedmetal (MIT, https://github.com/sp00nznet/twistedmetal),
# tools/decrypt_self.py; it also reads newer scetool key files. Keys are not
# included: pass --keys or set PS3_KEYS.
"""Decrypt a retail PS3 SELF into a plain ELF.

Twisted Metal's EBOOT.BIN is a non-NPDRM disc SELF (key revision 0x16,
self_type APP). Decryption is the standard SCE chain:

    appldr erk/riv  --AES-256-CBC-->  metadata_info (per-file key + iv)
    metadata key/iv --AES-128-CTR-->  metadata headers, section table, keys
    section keys    --AES-128-CTR-->  each PHDR section, zlib if compressed

then the plain segments are pasted back at the offsets the SELF's own program
headers name.

Keys are NOT in this repo. Point --keys at a scetool-format key file (the
`[appldr] type=SELF revision=... self_type=APP erk=... riv=...` blocks);
default `data/keys`, which is gitignored.

Usage:
    python tools/decrypt_self.py input/EBOOT.BIN -o input/EBOOT.ELF
    python tools/decrypt_self.py --selftest
"""

import argparse
import os
import struct
import sys
import zlib

from Crypto.Cipher import AES

SELF_TYPES = {1: 'LV0', 2: 'LV1', 3: 'LV2', 4: 'APP', 5: 'ISO', 6: 'LDR', 8: 'NPDRM'}


def parse_keys(text):
    """Parse a scetool key file into a list of dicts, one per [section] block."""
    entries, cur = [], None
    for line in text.splitlines():
        line = line.strip()
        if line.startswith('['):
            cur = {'name': line[1:-1]}
            entries.append(cur)
        elif '=' in line and cur is not None:
            k, _, v = line.partition('=')
            cur[k.strip()] = v.strip()
    # Newer scetool key files say category= and program_type= where the
    # older ones say type= and self_type=.
    for e in entries:
        if 'category' in e:
            e.setdefault('type', e['category'])
        if 'program_type' in e:
            e.setdefault('self_type', e['program_type'])
    return entries


def find_other_key(entries, name):
    """A bare `key=` entry such as NP_klic_key / NP_klic_free."""
    for e in entries:
        if e.get('name') == name and e.get('key'):
            return bytes.fromhex(e['key'])
    raise KeyError(f'no [{name}] entry in the key file')


def find_self_key(entries, revision, self_type):
    """The erk/riv for a given key revision and SELF type. Duplicate blocks
    differ only by firmware `version`; the key material is the same."""
    for e in entries:
        if (e.get('type') == 'SELF'
                and e.get('self_type') == self_type
                and int(e.get('revision', '-1'), 16) == revision
                and e.get('erk')):
            return bytes.fromhex(e['erk']), bytes.fromhex(e['riv'])
    raise KeyError(f'no SELF key for revision {revision:#06x} self_type {self_type}')


def aes_ctr(key, iv, data):
    return AES.new(key, AES.MODE_CTR, nonce=b'', initial_value=iv).decrypt(data)


class Self:
    def __init__(self, data):
        self.d = data
        if data[:4] != b'SCE\0':
            raise ValueError('not an SCE file (no SCE\\0 magic)')
        (_, self.version, self.key_revision, self.header_type,
         self.metadata_offset, self.header_len, self.data_len) = struct.unpack('>4sIHHIQQ', data[:0x20])
        if self.header_type != 1:
            raise ValueError(f'header_type {self.header_type} is not SELF')

        (_, self.app_info_off, self.elf_off, self.phdr_off, self.shdr_off,
         self.sec_info_off, self.version_off, self.ctrl_off,
         self.ctrl_size, _) = struct.unpack('>10Q', data[0x20:0x70])

        self.auth_id, self.vendor_id, self.self_type, self.app_version = \
            struct.unpack('>QIIQ', data[self.app_info_off:self.app_info_off + 0x18])

        # The ELF header and program headers sit in the SELF in the clear.
        self.eh = data[self.elf_off:self.elf_off + 0x40]
        if self.eh[:4] != b'\x7fELF':
            raise ValueError('embedded ELF header is missing')
        (self.e_entry, self.e_phoff, self.e_shoff) = struct.unpack('>QQQ', self.eh[0x18:0x30])
        (self.e_phentsize, self.e_phnum, self.e_shentsize, self.e_shnum) = \
            struct.unpack('>HHHH', self.eh[0x36:0x3E])
        self.phdrs = [
            struct.unpack('>IIQQQQQQ', data[self.phdr_off + i * 0x38:self.phdr_off + (i + 1) * 0x38])
            for i in range(self.e_phnum)]

    def describe(self):
        return (f'SELF v{self.version} key_rev={self.key_revision:#06x} '
                f'type={SELF_TYPES.get(self.self_type, self.self_type)} '
                f'auth_id={self.auth_id:#018x} fw={self.app_version >> 48:x}.{self.app_version >> 32 & 0xffff:04x}\n'
                f'  ELF entry={self.e_entry:#x} phnum={self.e_phnum} shnum={self.e_shnum} '
                f'shoff={self.e_shoff:#x}')

    def npdrm_license(self):
        """License type from the NPD block in control info, or None if not NPDRM.
        1 = network, 2 = local (needs a RAP), 3 = free."""
        ctrl = self.d[self.ctrl_off:self.ctrl_off + self.ctrl_size]
        i = ctrl.find(b'NPD\x00')
        if i < 0:
            return None
        return struct.unpack('>I', ctrl[i + 8:i + 12])[0]

    def metadata(self, erk, riv, klic=None, klic_key=None):
        """Decrypt metadata_info and the metadata headers that follow it.

        An NPDRM SELF wraps metadata_info in one more layer: the content key
        (free titles use the published NP_klic_free) unwrapped with NP_klic_key,
        then AES-128-CBC with a zero IV."""
        info_off = self.metadata_offset + 0x20
        info = self.d[info_off:info_off + 0x40]
        if klic is not None:
            npdrm_key = AES.new(klic_key, AES.MODE_ECB).decrypt(klic)
            info = AES.new(npdrm_key, AES.MODE_CBC, bytes(16)).decrypt(info)
        info = AES.new(erk, AES.MODE_CBC, riv).decrypt(info)
        key, key_pad, iv, iv_pad = info[0:16], info[16:32], info[32:48], info[48:64]
        if key_pad != bytes(16) or iv_pad != bytes(16):
            raise ValueError('metadata_info padding is not zero — wrong key revision or key file')

        hdrs = aes_ctr(key, iv, self.d[info_off + 0x40:self.header_len])
        sig_len, _, sec_count, key_count, opt_size = struct.unpack('>QIIII', hdrs[:0x18])
        if not (0 < sec_count < 256 and 0 < key_count < 1024):
            raise ValueError(f'implausible metadata: {sec_count} sections, {key_count} keys')

        sections = [
            dict(zip(('data_off', 'data_size', 'type', 'prog_idx', 'hashed', 'sha1_idx',
                      'encrypted', 'key_idx', 'iv_idx', 'compressed'),
                     struct.unpack('>QQIIIIIIII', hdrs[0x20 + i * 0x30:0x20 + (i + 1) * 0x30])))
            for i in range(sec_count)]
        keys_off = 0x20 + sec_count * 0x30
        keys = [hdrs[keys_off + i * 0x10:keys_off + (i + 1) * 0x10] for i in range(key_count)]
        return sections, keys

    def to_elf(self, erk, riv, log=print, klic=None, klic_key=None):
        sections, keys = self.metadata(erk, riv, klic, klic_key)
        out = bytearray()

        def put(off, blob):
            nonlocal out
            if off + len(blob) > len(out):
                out.extend(b'\0' * (off + len(blob) - len(out)))
            out[off:off + len(blob)] = blob

        put(0, self.eh)
        put(self.e_phoff, self.d[self.phdr_off:self.phdr_off + self.e_phnum * self.e_phentsize])

        shdrs = self.d[self.shdr_off:self.shdr_off + self.e_shnum * self.e_shentsize]

        for s in sections:
            if s['type'] == 2:            # carried by a program header
                dest = self.phdrs[s['prog_idx']][2]
            elif s['type'] == 3:          # non-alloc section, placed by shdr
                dest = struct.unpack('>Q', shdrs[s['prog_idx'] * self.e_shentsize + 0x18:
                                                 s['prog_idx'] * self.e_shentsize + 0x20])[0]
            else:                         # 1 = the section header table itself
                continue
            blob = self.d[s['data_off']:s['data_off'] + s['data_size']]
            if s['encrypted'] == 3:
                blob = aes_ctr(keys[s['key_idx']], keys[s['iv_idx']], blob)
            if s['compressed'] == 2:
                blob = zlib.decompress(blob)
            log(f'  {"phdr" if s["type"] == 2 else "shdr"}[{s["prog_idx"]}] -> {dest:#x} '
                f'{len(blob):#x} bytes{" (inflated)" if s["compressed"] == 2 else ""}')
            put(dest, blob)

        # The section header table is plain in the SELF. Sections it names that
        # no metadata entry covers (.shstrtab here) stay zero — section *names*
        # are cosmetic; the lifter works off program headers and the OPD.
        if self.e_shoff and self.e_shnum:
            put(self.e_shoff, shdrs)
        return bytes(out)


def check_elf(elf, entry, log=print):
    """The entry point must land in a mapped, non-zero, sanely-aligned word.
    A wrong key still produces a file — it just produces noise here."""
    ok = elf[:4] == b'\x7fELF'
    phoff, phnum = struct.unpack('>Q', elf[0x20:0x28])[0], struct.unpack('>H', elf[0x38:0x3A])[0]
    file_off = None
    vaddr_end = 0
    for i in range(phnum):
        p_type, _, p_offset, p_vaddr, _, p_filesz, _, _ = struct.unpack(
            '>IIQQQQQQ', elf[phoff + i * 0x38:phoff + (i + 1) * 0x38])
        if p_type != 1:
            continue
        vaddr_end = max(vaddr_end, p_vaddr + p_filesz)
        if p_vaddr <= entry < p_vaddr + p_filesz:
            file_off = p_offset + (entry - p_vaddr)
    if file_off is None:
        log('  FAIL: entry point is not inside any PT_LOAD segment')
        return False
    # A PPU entry point is an OPD descriptor, not code: two big-endian 32-bit
    # words, the real function address and the TOC. Both must land inside the
    # image. Decrypting with the wrong key still yields a file — it yields
    # noise here.
    func, toc = struct.unpack('>II', elf[file_off:file_off + 8])
    log(f'  entry {entry:#x} -> OPD func={func:#x} toc={toc:#x}')
    # Bound by the image's own load extent, not a fixed size: a 40 MB EBOOT
    # (Sonic Unleashed) has a TOC well past any constant we could pick.
    return ok and 0 < func < vaddr_end and 0 < toc < vaddr_end


def selftest():
    # Made-up keys: the test checks the parser and the lookup, not real keys.
    keys = parse_keys('''
[appldr]
type=SELF
revision=0016
self_type=APP
erk=00112233445566778899AABBCCDDEEFF00112233445566778899AABBCCDDEEFF
riv=0123456789ABCDEF0123456789ABCDEF

[appldr]
type=SELF
revision=0016
self_type=NPDRM
erk=FFEEDDCCBBAA99887766554433221100FFEEDDCCBBAA99887766554433221100
riv=FEDCBA9876543210FEDCBA9876543210
''')
    assert len(keys) == 2
    erk, riv = find_self_key(keys, 0x16, 'APP')
    assert erk.hex().upper().startswith('00112233') and len(erk) == 32 and len(riv) == 16
    assert find_self_key(keys, 0x16, 'NPDRM')[0] != erk
    try:
        find_self_key(keys, 0x0A, 'APP')
        raise AssertionError('expected KeyError for a missing revision')
    except KeyError:
        pass
    # AES-128-CTR must round-trip; it is the same call for encrypt and decrypt.
    assert aes_ctr(bytes(16), bytes(16), aes_ctr(bytes(16), bytes(16), b'hello' * 8)) == b'hello' * 8
    print('selftest ok')


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('self_file', nargs='?', help='EBOOT.BIN or a .sprx')
    ap.add_argument('-o', '--output', help='default: <input>.ELF')
    ap.add_argument('--keys', default=os.environ.get('PS3_KEYS', 'data/keys'),
                    help='scetool-format key file (default: data/keys)')
    ap.add_argument('--info', action='store_true', help='describe the SELF, write nothing')
    ap.add_argument('--selftest', action='store_true')
    args = ap.parse_args()

    if args.selftest:
        selftest()
        return 0
    if not args.self_file:
        ap.error('a SELF file is required')

    sf = Self(open(args.self_file, 'rb').read())
    print(sf.describe())
    if args.info:
        return 0

    if not os.path.exists(args.keys):
        ap.error(f'key file {args.keys} not found; pass --keys or set PS3_KEYS')
    entries = parse_keys(open(args.keys).read())
    erk, riv = find_self_key(entries, sf.key_revision, SELF_TYPES.get(sf.self_type, ''))

    klic = klic_key = None
    license = sf.npdrm_license()
    if license is not None:
        names = {3: 'NP_klic_free'}
        if license not in names:
            ap.error(f'NPDRM license type {license} needs a RAP/klicensee this tool does not handle')
        print(f'  NPDRM, license type {license} ({names[license]})')
        klic = find_other_key(entries, names[license])
        klic_key = find_other_key(entries, 'NP_klic_key')

    elf = sf.to_elf(erk, riv, klic=klic, klic_key=klic_key)

    if not check_elf(elf, sf.e_entry):
        print('decryption produced a bad ELF — wrong keys?', file=sys.stderr)
        return 1

    out = args.output or os.path.splitext(args.self_file)[0] + '.ELF'
    open(out, 'wb').write(elf)
    print(f'{out}  {len(elf) / 1e6:.1f} MB')
    return 0


if __name__ == '__main__':
    sys.exit(main())
