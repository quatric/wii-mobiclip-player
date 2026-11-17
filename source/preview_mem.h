/*
 * Single-serving preview: one file carried inside the .dol.
 *
 * mobipeg writes a copy of the preview build with the file added as a DOL
 * data section loaded at PREVIEW_ADDR in MEM2, behind a small header:
 *
 *   0x00  "MOBIPEG-PREVIEW\0"   magic, 16 bytes
 *   0x10  u32 size              big-endian, bytes of file that follow
 *   0x14  char ext[12]          the file's extension, e.g. ".mo", NUL-ended
 *   0x20  file data
 *
 * preview_mount() exposes it read-only as "mem:/preview<ext>", so the
 * player opens it through the same fopen() path as a file on the SD card.
 */
#ifndef PREVIEW_MEM_H
#define PREVIEW_MEM_H

#define PREVIEW_ADDR       0x91000000u
#define PREVIEW_HEADER     0x20
/* IOS and libogc's IPC heap sit at the top of MEM2; stay below them. */
#define PREVIEW_LIMIT      0x93000000u

/* Returns the path of the carried file, or NULL when the .dol has none. */
const char *preview_mount(void);

#endif
