// QR codes for the WiFi setup screen: Nayuki's qrcodegen (MIT), built inside the freestanding
// core with tiny stand-ins for the libc functions it calls.
#include "unicorn.h"
#include <limits.h>

namespace up_qr {
static void* q_memset(void* d, int c, unsigned long n) { unsigned char* p = (unsigned char*)d; while (n--) *p++ = (unsigned char)c; return d; }
static void* q_memmove(void* d, const void* s, unsigned long n) {
    unsigned char* a = (unsigned char*)d; const unsigned char* b = (const unsigned char*)s;
    if (a < b) while (n--) *a++ = *b++; else { a += n; b += n; while (n--) *--a = *--b; }
    return d;
}
static unsigned long q_strlen(const char* s) { unsigned long n = 0; while (s[n]) n++; return n; }
static int q_abs(int v) { return v < 0 ? -v : v; }
static void* q_memcpy(void* d, const void* s, unsigned long n) { return q_memmove(d, s, n); }
static const char* q_strchr(const char* s, int c) { for (; *s; s++) if (*s == (char)c) return s; return c ? 0 : s; }
static long q_labs(long v) { return v < 0 ? -v : v; }
#define memset q_memset
#define memmove q_memmove
#define memcpy q_memcpy
#define strchr q_strchr
#define strlen q_strlen
#define abs q_abs
#define labs q_labs
#define assert(x) ((void)0)
#include "third_party/qrcodegen.h"
#include "third_party/qrcodegen.c.inc"
#undef memset
#undef memmove
#undef memcpy
#undef strchr
#undef strlen
#undef abs
#undef labs
#undef assert
}  // namespace up_qr

namespace up {

int qr_encode(const char* text, u8* modules, int max_side) {
    static unsigned char buf[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_MAX_VERSION)], tmp[sizeof buf];
    if (!up_qr::qrcodegen_encodeText(text, tmp, buf, up_qr::qrcodegen_Ecc_MEDIUM, 1, QR_MAX_VERSION,
                                     up_qr::qrcodegen_Mask_AUTO, true))
        return 0;
    int n = up_qr::qrcodegen_getSize(buf);
    if (n > max_side) return 0;
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) modules[y * n + x] = up_qr::qrcodegen_getModule(buf, x, y) ? 1 : 0;
    return n;
}

}  // namespace up
