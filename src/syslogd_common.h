#ifndef SYSLOGD_COMMON_H
#define SYSLOGD_COMMON_H

/*
 * Shared constants for the syslogd toolkit (syslogd and syslogd_web).
 * This header is dependency-free and included by both binaries.
 */

/* Defaults (overridable via environment variables). */
#define DEFAULT_LOG_FILE "/var/log/custom_syslog.log"
#define DEFAULT_MAX_LOG_SIZE (5 * 1024 * 1024)  /* 5 MB */
#define DEFAULT_MAX_LOG_FILES 5
#define DEFAULT_SYSLOGD_PORT 514
#define DEFAULT_WEB_PORT 8090

/* syslogd (server) environment variable names. */
#define ENV_SYSLOGD_PORT "SYSLOGD_PORT"
#define ENV_SYSLOGD_LOG_FILE "SYSLOGD_LOG_FILE"
#define ENV_SYSLOGD_MAX_LOG_SIZE "SYSLOGD_MAX_LOG_SIZE"
#define ENV_SYSLOGD_MAX_LOG_FILES "SYSLOGD_MAX_LOG_FILES"

/* syslogd_web (viewer) environment variable names. */
#define ENV_WEB_PORT "SYSLOGD_WEB_PORT"

#endif
