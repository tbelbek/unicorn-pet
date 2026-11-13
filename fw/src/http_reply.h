#pragma once
// The listener's HTTP reply as it arrives (net.cpp reads it under one deadline, so it parses itself).
// Header-only and libc-only, so test/test_http.cpp runs it on the host.
#include <stdlib.h>
#include <string.h>
#include <strings.h>

// buf[0..n): what came so far; closed: the server ended the connection. Returns 1 when the reply is whole
// (*status, the body at buf + *body_at, *body_len bytes), 0 while more is to come, -1 if it is malformed
// (or chunked, which the listener never sends).
inline int http_reply(const char* buf, int n, bool closed, int* status, int* body_at, int* body_len) {
    int head = -1;
    for (int i = 0; i + 3 < n; i++)
        if (buf[i] == '\r' && buf[i + 1] == '\n' && buf[i + 2] == '\r' && buf[i + 3] == '\n') { head = i + 4; break; }
    if (head < 0) return closed ? -1 : 0;
    if (head < 12 || strncmp(buf, "HTTP/1.", 7) || buf[8] != ' ') return -1;
    int code = 0;
    for (int i = 9; i < 12; i++) {
        if (buf[i] < '0' || buf[i] > '9') return -1;
        code = code * 10 + buf[i] - '0';
    }
    long length = -1;
    for (int i = 0; i < head - 4;) {           // each header line
        int end = i;
        while (end < head - 4 && buf[end] != '\r') end++;
        const char* line = buf + i;
        int len = end - i;
        if (len >= 15 && !strncasecmp(line, "content-length:", 15)) {
            int k = 15;
            while (k < len && line[k] == ' ') k++;
            if (k == len) return -1;
            length = 0;
            for (; k < len; k++) {
                if (line[k] < '0' || line[k] > '9' || length > 100000000) return -1;
                length = length * 10 + line[k] - '0';
            }
        } else if (len > 18 && !strncasecmp(line, "transfer-encoding:", 18)) {
            return -1;
        }
        i = end + 2;
    }
    *status = code;
    *body_at = head;
    if (code < 200 || code == 204 || code == 304) { *body_len = 0; return 1; }
    if (length >= 0) {
        if (n - head < length) return closed ? -1 : 0;
        *body_len = (int)length;
        return 1;
    }
    if (!closed) return 0;
    *body_len = n - head;                       // no length: the body runs until the connection closes
    return 1;
}
