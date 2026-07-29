/* Public interface for the bundled NanoJPEG baseline decoder (nanojpeg.c). */
#ifndef NANOJPEG_H
#define NANOJPEG_H

typedef enum {
    NJ_OK = 0,
    NJ_NO_JPEG,
    NJ_UNSUPPORTED,
    NJ_OUT_OF_MEM,
    NJ_INTERNAL_ERR,
    NJ_SYNTAX_ERROR,
    __NJ_FINISHED,
} nj_result_t;

void njInit(void);
nj_result_t njDecode(const void *jpeg, const int size);
int njGetWidth(void);
int njGetHeight(void);
int njIsColor(void);
unsigned char *njGetImage(void);
int njGetImageSize(void);
void njDone(void);

/* Enable THP unescaped-scan mode (raw 0xFF in entropy data). Persists across
 * njDecode() calls; call once with 1 for THP, it is off by default. */
void njSetUnescaped(int v);

#endif
