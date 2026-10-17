/*
 * src/compat/gow_inflate.cpp — Host-side RFC 1951 DEFLATE / zlib inflater
 * for God of War: Ascension Edge Zlib SPU decompression requests.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace {

struct Bits {
    const uint8_t* src;
    uint32_t len, pos;
    uint32_t bitbuf, bitcnt;
    bool bad;

    int bit()
    {
        if (!bitcnt) {
            if (pos >= len) { bad = true; return 0; }
            bitbuf = src[pos++];
            bitcnt = 8;
        }
        const int b = bitbuf & 1;
        bitbuf >>= 1;
        bitcnt--;
        return b;
    }
    uint32_t bits(int n)
    {
        uint32_t v = 0;
        for (int i = 0; i < n; i++) v |= (uint32_t)bit() << i;
        return v;
    }
};

/* Canonical Huffman decoding table: counts per length, plus symbols in order. */
struct Huff {
    int16_t count[16];
    int16_t sym[288];
};

void huff_build(Huff* h, const uint8_t* lengths, int n)
{
    memset(h->count, 0, sizeof h->count);
    for (int i = 0; i < n; i++) h->count[lengths[i]]++;
    h->count[0] = 0;
    int16_t offs[16];
    offs[1] = 0;
    for (int l = 1; l < 15; l++) offs[l + 1] = (int16_t)(offs[l] + h->count[l]);
    for (int i = 0; i < n; i++)
        if (lengths[i]) h->sym[offs[lengths[i]]++] = (int16_t)i;
}

int huff_decode(Bits& b, const Huff* h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len <= 15; len++) {
        code |= b.bit();
        const int count = h->count[len];
        if (code - first < count) return h->sym[index + (code - first)];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
        if (b.bad) break;
    }
    return -1;
}

const uint16_t LEN_BASE[29] = {
    3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
const uint8_t LEN_EXTRA[29] = {
    0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
const uint16_t DIST_BASE[30] = {
    1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,
    4097,6145,8193,12289,16385,24577 };
const uint8_t DIST_EXTRA[30] = {
    0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

/* One compressed block into `out`; returns bytes written or -1. */
int inflate_block(Bits& b, uint8_t* out, uint32_t cap, uint32_t written,
                  const Huff* lit, const Huff* dist)
{
    for (;;) {
        const int sym = huff_decode(b, lit);
        if (sym < 0 || b.bad) return -1;
        if (sym < 256) {
            if (written >= cap) return -1;
            out[written++] = (uint8_t)sym;
        } else if (sym == 256) {
            return (int)written;
        } else {
            const int s = sym - 257;
            if (s >= 29) return -1;
            const uint32_t length = LEN_BASE[s] + b.bits(LEN_EXTRA[s]);
            const int ds = huff_decode(b, dist);
            if (ds < 0 || ds >= 30) return -1;
            const uint32_t d = DIST_BASE[ds] + b.bits(DIST_EXTRA[ds]);
            if (d > written || written + length > cap) return -1;
            for (uint32_t i = 0; i < length; i++, written++)
                out[written] = out[written - d];
        }
    }
}

}  // namespace

/* Inflate a raw DEFLATE or zlib stream. Returns bytes written, or -1.
 * A leading 0x78 (CMF with CM=8) is treated as a zlib wrapper and skipped. */
extern "C" int gow_inflate(uint8_t* out, uint32_t out_cap, const uint8_t* src, uint32_t src_len)
{
    if (!out || !src || src_len < 1) return -1;

    uint32_t start = 0;
    if (src_len >= 2 && (src[0] & 0x0F) == 8 && ((src[0] << 8) | src[1]) % 31 == 0)
        start = 2;   /* zlib header (RFC 1950) */

    Bits b{src, src_len, start, 0, 0, false};
    uint32_t written = 0;

    for (;;) {
        const int final = b.bit();
        const int type = (int)b.bits(2);
        if (b.bad) return -1;

        if (type == 0) {                        /* stored */
            b.bitcnt = 0;
            if (b.pos + 4 > b.len) return -1;
            const uint32_t n = (uint32_t)src[b.pos] | ((uint32_t)src[b.pos + 1] << 8);
            b.pos += 4;
            if (b.pos + n > b.len || written + n > out_cap) return -1;
            memcpy(out + written, src + b.pos, n);
            b.pos += n;
            written += n;
        } else if (type == 1 || type == 2) {
            Huff lit, dist;
            if (type == 1) {                    /* fixed Huffman */
                uint8_t l[288], d[30];
                for (int i = 0; i < 144; i++) l[i] = 8;
                for (int i = 144; i < 256; i++) l[i] = 9;
                for (int i = 256; i < 280; i++) l[i] = 7;
                for (int i = 280; i < 288; i++) l[i] = 8;
                for (int i = 0; i < 30; i++) d[i] = 5;
                huff_build(&lit, l, 288);
                huff_build(&dist, d, 30);
            } else {                            /* dynamic Huffman */
                static const uint8_t ORDER[19] = {
                    16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
                const int hlit  = (int)b.bits(5) + 257;
                const int hdist = (int)b.bits(5) + 1;
                const int hclen = (int)b.bits(4) + 4;
                if (hlit > 286 || hdist > 30) return -1;

                uint8_t cl[19] = {0};
                for (int i = 0; i < hclen; i++) cl[ORDER[i]] = (uint8_t)b.bits(3);
                Huff clh;
                huff_build(&clh, cl, 19);

                uint8_t lengths[288 + 30] = {0};
                int i = 0;
                while (i < hlit + hdist) {
                    const int sym = huff_decode(b, &clh);
                    if (sym < 0 || b.bad) return -1;
                    if (sym < 16) {
                        lengths[i++] = (uint8_t)sym;
                    } else if (sym == 16) {
                        if (!i) return -1;
                        const uint8_t prev = lengths[i - 1];
                        for (int r = 3 + (int)b.bits(2); r && i < hlit + hdist; r--)
                            lengths[i++] = prev;
                    } else if (sym == 17) {
                        for (int r = 3 + (int)b.bits(3); r && i < hlit + hdist; r--)
                            lengths[i++] = 0;
                    } else {
                        for (int r = 11 + (int)b.bits(7); r && i < hlit + hdist; r--)
                            lengths[i++] = 0;
                    }
                }
                huff_build(&lit, lengths, hlit);
                huff_build(&dist, lengths + hlit, hdist);
            }
            const int n = inflate_block(b, out, out_cap, written, &lit, &dist);
            if (n < 0) return -1;
            written = (uint32_t)n;
        } else {
            return -1;                          /* type 3 is reserved */
        }
        if (final) break;
        if (b.bad) return -1;
    }
    return (int)written;
}
