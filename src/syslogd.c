// filepath: /syslogd/src/syslogd.c
/*
 * Configurable Syslog Server in C
 *
 * Options:
 *   -p <port>     UDP port to listen on (default 514)
 *   -l <logfile>  Path to log file (default /var/log/custom_syslog.log)
 *   -d            Run as daemon (default: run in foreground)
 *
 * Example:
 *   sudo ./syslogd_custom -p 5514 -l /tmp/test_syslog.log -d
 */

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define BUFFER_SIZE 2048
#define MAX_LOG_SIZE (5 * 1024 * 1024)  // 5 MB
#define PID_FILE "/var/run/custom_syslog.pid"

const char *facility_names[] = {
    "kern", "user", "mail", "daemon", "auth", "syslog", "lpr", "news",
    "uucp", "cron", "authpriv", "ftp", "ntp", "audit", "alert", "at",
    "local0", "local1", "local2", "local3",
    "local4", "local5", "local6", "local7"};

const char *severity_names[] = {
    "emergency", "alert", "critical", "error",
    "warning", "notice", "info", "debug"};

volatile sig_atomic_t running = 1;
int syslog_port = 514;
char log_file[256] = "/var/log/custom_syslog.log";
int run_as_daemon = 0;

void handle_signal(int sig) {
   (void)sig;
   running = 0;
}

void write_pidfile(void) {
   FILE *f = fopen(PID_FILE, "w");
   if (f) {
      fprintf(f, "%d\n", getpid());
      fclose(f);
   } else {
      syslog(LOG_ERR, "Failed to write PID file: %s", strerror(errno));
   }
}

void rotate_log_if_needed(const char *filename) {
   struct stat st;
   if (stat(filename, &st) == 0 && st.st_size >= MAX_LOG_SIZE) {
      time_t now = time(NULL);
      struct tm *tm_info = localtime(&now);

      char backup_name[512];
      strftime(backup_name, sizeof(backup_name),
               "%Y%m%d_%H%M%S", tm_info);

      char rotated[600];
      snprintf(rotated, sizeof(rotated), "%s.%s", filename, backup_name);

      if (rename(filename, rotated) == 0) {
         syslog(LOG_INFO, "Rotated log file -> %s", rotated);
      } else {
         syslog(LOG_ERR, "Failed to rotate log file: %s", strerror(errno));
      }
   }
}

void write_log(const char *fac, const char *sev, const char *msg,
               const struct sockaddr_in *client) {
   FILE *f;

   rotate_log_if_needed(log_file);

   f = fopen(log_file, "a");
   if (!f) {
      syslog(LOG_ERR, "Failed to open log file: %s", strerror(errno));
      return;
   }

   time_t now = time(NULL);
   char timestr[64];
   strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", localtime(&now));

   // Resolve hostname
   char host[NI_MAXHOST];
   int port = ntohs(client->sin_port);
   int res = getnameinfo((struct sockaddr *)client, sizeof(*client),
                         host, sizeof(host), NULL, 0, NI_NAMEREQD);
   if (res != 0) {
      // Fallback to IP address if hostname cannot be resolved
      strncpy(host, inet_ntoa(client->sin_addr), sizeof(host));
      host[sizeof(host) - 1] = '\0';
   } else {
      // Only use the first part of the domain, limit to 15 chars
      char short_host[16] = {0};
      char *dot = strchr(host, '.');
      if (dot) {
         size_t len = dot - host;
         if (len > 15) len = 15;
         strncpy(short_host, host, len);
         short_host[len] = '\0';
      } else {
         strncpy(short_host, host, 15);
         short_host[15] = '\0';
      }
      strncpy(host, short_host, sizeof(host));
      host[sizeof(host) - 1] = '\0';
   }

   // Ensure fixed column width (max 15 chars for host)
   char host_col[16];
   snprintf(host_col, sizeof(host_col), "%-15s", host);

   fprintf(f, "%s [%s:%d] facility=%s severity=%s msg=%s\n",
           timestr,
           host_col,
           port,
           fac, sev, msg);

   fclose(f);
}

void parse_syslog_message(const char *msg, char *out_fac, char *out_sev, char *out_text) {
   int pri = -1;
   const char *p = msg;

   if (*p == '<') {
      pri = atoi(p + 1);
      const char *end = strchr(p, '>');
      if (end) {
         p = end + 1;

         int facility = pri / 8;
         int severity = pri % 8;

         if (facility >= 0 && facility < (int)(sizeof(facility_names) / sizeof(facility_names[0]))) {
            strcpy(out_fac, facility_names[facility]);
         } else {
            strcpy(out_fac, "unknown");
         }

         if (severity >= 0 && severity < (int)(sizeof(severity_names) / sizeof(severity_names[0]))) {
            strcpy(out_sev, severity_names[severity]);
         } else {
            strcpy(out_sev, "unknown");
         }

         strncpy(out_text, p, BUFFER_SIZE - 1);
         out_text[BUFFER_SIZE - 1] = '\0';
         return;
      }
   }

   strcpy(out_fac, "unknown");
   strcpy(out_sev, "unknown");
   strncpy(out_text, msg, BUFFER_SIZE - 1);
   out_text[BUFFER_SIZE - 1] = '\0';
}

void daemonize(void) {
   pid_t pid = fork();
   if (pid < 0) exit(EXIT_FAILURE);
   if (pid > 0) exit(EXIT_SUCCESS);

   if (setsid() < 0) exit(EXIT_FAILURE);

   signal(SIGCHLD, SIG_IGN);
   signal(SIGHUP, SIG_IGN);

   pid = fork();
   if (pid < 0) exit(EXIT_FAILURE);
   if (pid > 0) exit(EXIT_SUCCESS);

   umask(0);
   chdir("/");

   for (int fd = sysconf(_SC_OPEN_MAX); fd >= 0; fd--) {
      close(fd);
   }

   int fd0 = open("/dev/null", O_RDWR);
   dup2(fd0, STDIN_FILENO);
   dup2(fd0, STDOUT_FILENO);
   dup2(fd0, STDERR_FILENO);
}

void usage(const char *prog) {
   fprintf(stderr, "Usage: %s [-p port] [-l logfile] [-d]\n", prog);
   exit(EXIT_FAILURE);
}

int main(int argc, char *argv[]) {
   int opt;
   while ((opt = getopt(argc, argv, "p:l:d")) != -1) {
      switch (opt) {
         case 'p':
            syslog_port = atoi(optarg);
            break;
         case 'l':
            strncpy(log_file, optarg, sizeof(log_file) - 1);
            log_file[sizeof(log_file) - 1] = '\0';
            break;
         case 'd':
            run_as_daemon = 1;
            break;
         default:
            usage(argv[0]);
      }
   }

   openlog("custom_syslogd", LOG_PID, LOG_DAEMON);

   if (run_as_daemon) {
      daemonize();
      write_pidfile();
   }

   signal(SIGTERM, handle_signal);
   signal(SIGINT, handle_signal);

   syslog(LOG_INFO, "Custom syslog server started (port=%d, logfile=%s, daemon=%s)",
          syslog_port, log_file, run_as_daemon ? "yes" : "no");

   int sockfd;
   struct sockaddr_in server_addr, client_addr;
   char buffer[BUFFER_SIZE];
   socklen_t addr_len = sizeof(client_addr);

   if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
      syslog(LOG_ERR, "Socket creation failed: %s", strerror(errno));
      exit(EXIT_FAILURE);
   }

   // Add this block to set a 1-second timeout
   struct timeval tv;
   tv.tv_sec = 1;
   tv.tv_usec = 0;
   setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

   memset(&server_addr, 0, sizeof(server_addr));
   server_addr.sin_family = AF_INET;
   server_addr.sin_addr.s_addr = INADDR_ANY;
   server_addr.sin_port = htons(syslog_port);

   if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
      syslog(LOG_ERR, "Bind failed: %s", strerror(errno));
      close(sockfd);
      exit(EXIT_FAILURE);
   }

   while (running) {
      ssize_t len = recvfrom(sockfd, buffer, BUFFER_SIZE - 1, 0,
                             (struct sockaddr *)&client_addr, &addr_len);
      if (len < 0) {
         if (errno == EINTR) continue;
         syslog(LOG_ERR, "recvfrom failed: %s", strerror(errno));
         continue;
      }

      buffer[len] = '\0';

      char fac[32], sev[32], text[BUFFER_SIZE];
      parse_syslog_message(buffer, fac, sev, text);

      write_log(fac, sev, text, &client_addr);

      if (!run_as_daemon) {  // Print to stdout in foreground mode
         printf("[%s:%d] %s.%s: %s\n",
                inet_ntoa(client_addr.sin_addr),
                ntohs(client_addr.sin_port),
                fac, sev, text);
         fflush(stdout);
      }
   }

   syslog(LOG_INFO, "Custom syslog server shutting down");
   close(sockfd);
   if (run_as_daemon) unlink(PID_FILE);
   closelog();
   return 0;
}
