#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define PORT 8090
#define LOGFILE "/var/log/custom_syslog.log"

void send_response(int client_fd) {
    FILE *f = fopen(LOGFILE, "r");
    char line[1024];

    // Build the full HTML body in memory first so we can send a correct
    // Content-Length header (required for browsers to accept the response).
    size_t body_cap = 8192;
    size_t body_len = 0;
    char *body = malloc(body_cap);
    if (!body) {
        close(client_fd);
        return;
    }

#define BODY_APPEND(...) \
    do { \
        int _need = snprintf(NULL, 0, __VA_ARGS__); \
        if (_need < 0) { \
            free(body); \
            close(client_fd); \
            return; \
        } \
        if (body_len + (size_t)_need + 1 > body_cap) { \
            size_t _new_cap = body_len + (size_t)_need + 1; \
            char *_tmp = realloc(body, _new_cap); \
            if (!_tmp) { \
                free(body); \
                close(client_fd); \
                return; \
            } \
            body = _tmp; \
            body_cap = _new_cap; \
        } \
        snprintf(body + body_len, body_cap - body_len, __VA_ARGS__); \
        body_len += (size_t)_need; \
    } while (0)

    BODY_APPEND(
        "<html><head><title>Syslog Viewer</title></head><body>"
        "<h2>Syslog Log File</h2>"
        "<table border='1' cellpadding='4' cellspacing='0'>"
        "<tr><th>Timestamp</th><th>Host</th><th>Port</th><th>Facility</th><th>Severity</th><th>Message</th></tr>"
    );

    if (f) {
        while (fgets(line, sizeof(line), f)) {
            // Expected log format:
            // YYYY-MM-DD HH:MM:SS [host:port] facility=... severity=... msg=...
            char ts[32], host[32], facility[32], severity[32], msg[900];
            int port;
            if (sscanf(line, "%31s %*s [%31[^]:]:%d] facility=%31s severity=%31s msg=%899[^\n]",
                       ts, host, &port, facility, severity, msg) == 6) {
                BODY_APPEND(
                    "<tr><td>%s</td><td>%s</td><td>%d</td><td>%s</td><td>%s</td><td>%s</td></tr>",
                    ts, host, port, facility, severity, msg);
            }
        }
        fclose(f);
    } else {
        BODY_APPEND("<tr><td colspan='6'>Log file not found.</td></tr>");
    }

    BODY_APPEND("</table></body></html>");

    dprintf(client_fd,
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n", body_len);
    dprintf(client_fd, "%s", body);

    free(body);
#undef BODY_APPEND
}

int main(void) {
    int opt = 1;
    int bDualStack = 0;

    // Try dual-stack first: an IPv6 socket with IPV6_V6ONLY=0 accepts both IPv6
    // and IPv4-mapped connections. Fall back to a plain IPv4 socket when IPv6
    // is unavailable on the platform.
    int server_fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (server_fd >= 0) {
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        setsockopt(server_fd, IPPROTO_IPV6, IPV6_V6ONLY, &bDualStack, sizeof(bDualStack));

        struct sockaddr_in6 addr6;
        addr6.sin6_family = AF_INET6;
        addr6.sin6_addr = in6addr_any;
        addr6.sin6_port = htons(PORT);

        if (bind(server_fd, (struct sockaddr *)&addr6, sizeof(addr6)) < 0) {
            perror("bind");
            exit(1);
        }
    } else {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            perror("socket");
            exit(1);
        }
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(PORT);

        if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("bind");
            exit(1);
        }
    }

    if (listen(server_fd, 10) < 0) {
        perror("listen");
        exit(1);
    }

    printf("Syslog web server running on http://localhost:%d/\n", PORT);
    fflush(stdout);

    while (1) {
        int client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) continue;

        send_response(client_fd);
        shutdown(client_fd, SHUT_WR);
        close(client_fd);
    }

    close(server_fd);
    return 0;
}