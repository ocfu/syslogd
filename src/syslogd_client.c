#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>

#define DEFAULT_PORT 514

const char *facility_names[] = {
    "kern", "user", "mail", "daemon", "auth", "syslog", "lpr", "news",
    "uucp", "cron", "authpriv", "ftp", "ntp", "audit", "alert", "at",
    "local0", "local1", "local2", "local3",
    "local4", "local5", "local6", "local7"
};

const char *severity_names[] = {
    "emergency", "alert", "critical", "error",
    "warning", "notice", "info", "debug"
};

int get_facility(const char *name) {
    for (int i = 0; i < (int)(sizeof(facility_names)/sizeof(facility_names[0])); ++i) {
        if (strcmp(name, facility_names[i]) == 0)
            return i;
    }
    return -1;
}

int get_severity(const char *name) {
    for (int i = 0; i < (int)(sizeof(severity_names)/sizeof(severity_names[0])); ++i) {
        if (strcmp(name, severity_names[i]) == 0)
            return i;
    }
    return -1;
}

int main(int argc, char *argv[]) {
    const char *server_ip = NULL;
    const char *message = NULL;
    int port = DEFAULT_PORT;
    int facility = 1; // user
    int severity = 6; // info

    int opt;
    while ((opt = getopt(argc, argv, "p:f:s:")) != -1) {
        switch (opt) {
            case 'p':
                port = atoi(optarg);
                break;
            case 'f': {
                int f = get_facility(optarg);
                if (f < 0) {
                    printf("Unknown facility: %s\n", optarg);
                    return 1;
                }
                facility = f;
                break;
            }
            case 's': {
                int s = get_severity(optarg);
                if (s < 0) {
                    printf("Unknown severity: %s\n", optarg);
                    return 1;
                }
                severity = s;
                break;
            }
            default:
                printf("Usage: %s [-p port] [-f facility] [-s severity] <server_ip> <message>\n", argv[0]);
                return 1;
        }
    }

    if (optind + 2 > argc) {
        printf("Usage: %s [-p port] [-f facility] [-s severity] <server_ip> <message>\n", argv[0]);
        return 1;
    }

    server_ip = argv[optind];
    message = argv[optind + 1];

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        return 1;
    }

    int pri = facility * 8 + severity;
    char syslog_msg[256];
    snprintf(syslog_msg, sizeof(syslog_msg), "<%d>%s", pri, message);

    ssize_t sent = sendto(sockfd, syslog_msg, strlen(syslog_msg), 0,
                          (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (sent < 0) {
        perror("sendto");
        close(sockfd);
        return 1;
    }

    printf("Sent syslog message to %s:%d (facility=%s, severity=%s)\n",
           server_ip, port, facility_names[facility], severity_names[severity]);
    close(sockfd);
    return 0;
}
