// The listener reply parser the firmware runs under its request deadline (fw/src/http_reply.h).
//   make -C test http
#include <cassert>
#include <cstdio>
#include <cstring>
#include "../fw/src/http_reply.h"

static int parse(const char* s, bool closed, int* st = 0, int* at = 0, int* len = 0) {
    int a, b, c;
    return http_reply(s, (int)strlen(s), closed, st ? st : &a, at ? at : &b, len ? len : &c);
}

int main() {
    const char* ok = "HTTP/1.0 200 OK\r\nServer: unicorn-listener\r\nContent-Type: application/json; charset=utf-8\r\n"
                     "Content-Length: 11\r\n\r\n{\"ok\":true}";
    int st, at, len;
    assert(parse(ok, false, &st, &at, &len) == 1);  // whole as soon as the length is in, no need to wait for close
    assert(st == 200 && len == 11 && !strncmp(ok + at, "{\"ok\":true}", 11));
    char part[200];
    for (size_t cut = 0; cut < strlen(ok); cut++) {  // any prefix: more to come, or malformed once closed
        memcpy(part, ok, cut); part[cut] = 0;
        assert(parse(part, false) == 0);
        assert(parse(part, true) == -1);
    }
    assert(parse("HTTP/1.0 304 Not Modified\r\nServer: x\r\n\r\n", false, &st, &at, &len) == 1 && st == 304 && len == 0);
    assert(parse("HTTP/1.0 401 Unauthorized\r\ncontent-length: 2\r\n\r\n{}", false, &st) == 1 && st == 401);
    assert(parse("HTTP/1.0 200 OK\r\n\r\nabc", false) == 0);        // no length: until the server closes
    assert(parse("HTTP/1.0 200 OK\r\n\r\nabc", true, &st, &at, &len) == 1 && len == 3);
    assert(parse("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n0\r\n\r\n", true) == -1);
    assert(parse("HTTP/1.0 2x0 OK\r\n\r\n", true) == -1);
    assert(parse("SSH-2.0-OpenSSH\r\n\r\n", true) == -1);
    assert(parse("HTTP/1.0 200 OK\r\nContent-Length: -5\r\n\r\n", true) == -1);
    assert(parse("HTTP/1.0 200 OK\r\nContent-Length: 99999999999\r\n\r\n", true) == -1);
    assert(parse("HTTP/1.0 200 OK\r\nContent-Length:\r\n\r\n", true) == -1);
    std::printf("http reply parser: ok\n");
    return 0;
}
