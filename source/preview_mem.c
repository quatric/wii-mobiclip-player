/* Read-only "mem:" device over the file a preview .dol carries. */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/iosupport.h>
#include <sys/stat.h>
#include <gccore.h>

#include "preview_mem.h"

static const uint8_t *blob;
static uint32_t blob_size;
static char blob_path[32];

typedef struct {
    uint32_t pos;
} MemFile;

static int mem_open(struct _reent *r, void *fs, const char *path, int flags, int mode)
{
    const char *name = strchr(path, ':');
    name = name ? name + 1 : path;
    if ((flags & O_ACCMODE) != O_RDONLY) {
        r->_errno = EROFS;
        return -1;
    }
    if (strcmp(name, blob_path + 4)) {   /* blob_path is "mem:/preview..." */
        r->_errno = ENOENT;
        return -1;
    }
    ((MemFile *)fs)->pos = 0;
    return (int)fs;
}

static int mem_close(struct _reent *r, void *fd)
{
    return 0;
}

static ssize_t mem_read(struct _reent *r, void *fd, char *ptr, size_t len)
{
    MemFile *f = fd;
    if (f->pos >= blob_size)
        return 0;
    if (len > blob_size - f->pos)
        len = blob_size - f->pos;
    memcpy(ptr, blob + f->pos, len);
    f->pos += len;
    return len;
}

static off_t mem_seek(struct _reent *r, void *fd, off_t pos, int dir)
{
    MemFile *f = fd;
    off_t base = dir == SEEK_SET ? 0 : dir == SEEK_CUR ? (off_t)f->pos : (off_t)blob_size;
    off_t to = base + pos;
    if (to < 0) {
        r->_errno = EINVAL;
        return -1;
    }
    f->pos = to > (off_t)blob_size ? blob_size : (uint32_t)to;
    return to;
}

static int mem_fstat(struct _reent *r, void *fd, struct stat *st)
{
    memset(st, 0, sizeof(*st));
    st->st_size = blob_size;
    st->st_mode = S_IFREG | S_IRUSR;
    return 0;
}

static const devoptab_t mem_devoptab = {
    .name       = "mem",
    .structSize = sizeof(MemFile),
    .open_r     = mem_open,
    .close_r    = mem_close,
    .read_r     = mem_read,
    .seek_r     = mem_seek,
    .fstat_r    = mem_fstat,
};

const char *preview_mount(void)
{
    const uint8_t *h = (const uint8_t *)PREVIEW_ADDR;
    uint32_t size, end;
    char ext[12];

    if (memcmp(h, "MOBIPEG-PREVIEW", 16))
        return NULL;
    size = (uint32_t)h[16] << 24 | h[17] << 16 | h[18] << 8 | h[19];
    end  = PREVIEW_ADDR + PREVIEW_HEADER + size;
    if (!size || end > PREVIEW_LIMIT || end < PREVIEW_ADDR)
        return NULL;
    memcpy(ext, h + 20, sizeof(ext));
    ext[sizeof(ext) - 1] = 0;

    /* Keep anything taking MEM2 after this point clear of the file. */
    if ((uint32_t)SYS_GetArena2Lo() < ((end + 31) & ~31u))
        SYS_SetArena2Lo((void *)((end + 31) & ~31u));

    blob      = h + PREVIEW_HEADER;
    blob_size = size;
    snprintf(blob_path, sizeof(blob_path), "mem:/preview%s", ext);
    if (FindDevice("mem:") < 0 && AddDevice(&mem_devoptab) < 0)
        return NULL;
    return blob_path;
}
