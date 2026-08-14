/*
 * Minimal self-contained primitives needed by the Mobiclip decoder:
 *   - MSB-first bit reader (matches FFmpeg get_bits semantics)
 *   - Exp-Golomb codes
 *   - A tiny fixed-table VLC reader matching ff_init_vlc_from_lengths /
 *     get_vlc2 for codes whose length never exceeds the table bit width
 *     (true for every Mobiclip table: RL <= 12 bits, MV <= 6 bits).
 *
 * Reimplemented from scratch for the Wii homebrew port; no FFmpeg headers.
 */
#ifndef MOBI_BITS_H
#define MOBI_BITS_H

#include <stdint.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Bit reader (big-endian, MSB first)                                  */
/* ------------------------------------------------------------------ */

typedef struct BitReader {
    const uint8_t *buf;
    int size_bits;   /* total readable bits */
    int pos;         /* current bit index   */
} BitReader;

static inline void br_init(BitReader *b, const uint8_t *buf, int size_bytes)
{
    b->buf = buf;
    b->size_bits = size_bytes * 8;
    b->pos = 0;
}

static inline int br_left(const BitReader *b)
{
    return b->size_bits - b->pos;
}

/* peek up to 24 bits without advancing; reads zero past the end */
static inline uint32_t br_peek(const BitReader *b, int n)
{
    uint32_t v = 0;
    int p = b->pos;
    for (int i = 0; i < n; i++) {
        int bit = 0;
        if (p < b->size_bits)
            bit = (b->buf[p >> 3] >> (7 - (p & 7))) & 1;
        v = (v << 1) | bit;
        p++;
    }
    return v;
}

static inline uint32_t br_get(BitReader *b, int n)
{
    uint32_t v = br_peek(b, n);
    b->pos += n;
    return v;
}

static inline int br_get1(BitReader *b)
{
    int p = b->pos++;
    if (p >= b->size_bits) return 0;
    return (b->buf[p >> 3] >> (7 - (p & 7))) & 1;
}

static inline void br_skip(BitReader *b, int n) { b->pos += n; }

/* signed, sign-extended n-bit value */
static inline int br_get_signed(BitReader *b, int n)
{
    uint32_t v = br_get(b, n);
    if (n == 0) return 0;
    if (v & (1u << (n - 1)))
        v |= ~((1u << n) - 1);
    return (int)v;
}

/* ------------------------------------------------------------------ */
/* Exp-Golomb                                                          */
/* ------------------------------------------------------------------ */

static inline int br_get_ue(BitReader *b)
{
    int lz = 0;
    while (br_get1(b) == 0) {
        lz++;
        if (lz > 31) return 0;
    }
    if (lz == 0) return 0;
    return ((1 << lz) - 1) + (int)br_get(b, lz);
}

static inline int br_get_se(BitReader *b)
{
    int ue = br_get_ue(b);
    if (ue & 1) return (ue + 1) >> 1;     /* 1,3,5 -> 1,2,3 */
    return -(ue >> 1);                     /* 0,2,4 -> 0,-1,-2 */
}

/* ------------------------------------------------------------------ */
/* Fixed-table VLC                                                     */
/* ------------------------------------------------------------------ */

typedef struct VlcEntry {
    uint8_t len;       /* code length in bits (0 = unused) */
    uint16_t sym;      /* decoded symbol                   */
} VlcEntry;

typedef struct MiniVlc {
    int bits;          /* table index width */
    VlcEntry *table;   /* size 1<<bits      */
} MiniVlc;

/*
 * Build a flat decode table from per-entry lengths and symbols, exactly
 * matching FFmpeg ff_init_vlc_from_lengths code assignment:
 *   code starts at 0 (aligned to bit 32); each present entry records the
 *   current code, then code += 1<<(32-len).
 * All lengths here are <= table bits, so no subtables are needed.
 *
 * lens:  int8 length per code (lens_wrap stride in bytes)
 * syms:  symbol array, sym_size bytes each (1 or 2)
 */
static inline void minivlc_build(MiniVlc *v, VlcEntry *storage, int table_bits,
                                 int nb_codes,
                                 const uint8_t *lens, int lens_wrap,
                                 const void *syms, int sym_wrap, int sym_size)
{
    v->bits = table_bits;
    v->table = storage;
    memset(storage, 0, sizeof(VlcEntry) << table_bits);

    uint32_t code = 0;
    const uint8_t *lp = lens;
    const uint8_t *sp = (const uint8_t *)syms;
    for (int i = 0; i < nb_codes; i++, lp += lens_wrap, sp += sym_wrap) {
        int len = (int8_t)*lp;
        if (len <= 0) {
            if (len < 0) { /* shouldn't occur in Mobiclip tables */ }
            continue;
        }
        unsigned sym = (sym_size == 2) ? *(const uint16_t *)sp : *sp;

        int idx = code >> (32 - table_bits);
        int nb = 1 << (table_bits - len);
        for (int k = 0; k < nb; k++) {
            storage[idx + k].len = len;
            storage[idx + k].sym = sym;
        }
        code += 1u << (32 - len);
    }
}

/* read one VLC symbol */
static inline int minivlc_get(BitReader *b, const MiniVlc *v)
{
    uint32_t idx = br_peek(b, v->bits);
    const VlcEntry *e = &v->table[idx];
    b->pos += e->len;   /* len 0 => no advance (corrupt stream guard) */
    return e->sym;
}

#endif /* MOBI_BITS_H */
