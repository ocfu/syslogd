# Usage

The project ships three binaries, all built into `build/`. Their version comes
from the single `SYSLOGD_VERSION` in `src/version.h`; each binary prints it
with `-V`.

## syslogd — the server

Listen for UDP syslog messages and append them to a log file in RFC 5424
format.

```
Usage: ./build/syslogd [-p port] [-l logfile] [-d] [-V]
```

| Option | Description | Default |
| ------ | ----------- | ------- |
| `-p <port>` | UDP port to listen on | `514` |
| `-l <logfile>` | Path to the log file | `/var/log/custom_syslog.log` |
| `-d` | Run as a daemon (foreground by default) | off |
| `-V` | Print version and exit | — |

Notes:

- Listening on the default port `514` requires root (`sudo`). Use a high port
  such as `5514` for testing.
- In daemon mode the server writes its PID to
  `/var/run/custom_syslog.pid` and removes it on shutdown.
- On startup the server unconditionally writes a status file
  `/var/run/custom_syslog.status` (`version=`, `pid=`, `port=`, `logfile=`)
  and removes it on shutdown. The web viewer reads it for `/api/status`.
- The log file rotates automatically once it reaches `MAX_LOG_SIZE` (default
  5 MB). Rotation uses a numbered scheme: the current file becomes `.1`, each
  existing `.i` is shifted to `.i+1`, and `.N` (oldest, `N = MAX_LOG_FILES`,
  default 5) is deleted — so the history is `<logfile>.1` … `<logfile>.N` with
  `.N` the oldest.
- SIGINT and SIGTERM trigger a clean shutdown (remove the PID/status file,
  close the socket, close syslog).

### Environment variables (syslogd)

The server can be configured with environment variables instead of flags.
Flags take precedence where both are set. `SYSLOGD_*` variables for the server:

| Variable | Description | Default |
| -------- | ----------- | ------- |
| `SYSLOGD_PORT` | UDP port to listen on | `514` |
| `SYSLOGD_LOG_FILE` | Path to the log file | `/var/log/custom_syslog.log` |
| `SYSLOGD_MAX_LOG_SIZE` | Rotation threshold in bytes | `5242880` (5 MB) |
| `SYSLOGD_MAX_LOG_FILES` | Number of rotated history files kept | `5` |

Example:

```
SYSLOGD_PORT=5514 SYSLOGD_LOG_FILE=/tmp/custom.log \
SYSLOGD_MAX_LOG_SIZE=1048576 SYSLOGD_MAX_LOG_FILES=3 ./build/syslogd
```

### Log format (RFC 5424)

Each message is appended to the log file as one RFC 5424 line:

```
<PRI>VERSION TIMESTAMP HOSTNAME APP-NAME PROCID MSGID STRUCTURED-DATA MSG
```

Example (wrapped form of `<134>hello world`):

```
<134>1 2026-08-30T12:00:01.003Z myhost - - - - hello world
```

- `TIMESTAMP` is UTC RFC 3339 with milliseconds (`.003Z`).
- Messages that already are a complete RFC 5424 message (a `<PRI>1` header)
  are written through **unchanged**, preserving their own
  TIMESTAMP/HOSTNAME/APP-NAME/PROCID/MSGID/SD/MSG. Simple `<PRI>msg` messages
  are wrapped: the receive time, the sender's short hostname, and `- - - -`
  for the RFC 5424 fields.
- `facility` and `severity` are decoded from the message's PRI prefix
  (`<N>`), e.g. `<134>` → facility `local0`, severity `info`. Unknown values
  become `unknown`.
- In foreground mode each received message is also printed to stdout in the
  form `[ip:port] facility.severity: message`.

### Exit codes

| Code | Meaning |
| ---- | ------- |
| 0    | Clean shutdown (SIGINT/SIGTERM) |
| 1    | Usage error or runtime failure (socket, bind, daemonize) |

## syslogd_client — the test client

Send a single syslog message over UDP.

```
Usage: ./build/syslogd_client [-p port] [-f facility] [-s severity] <server_ip> <message> [-V]
```

| Option | Description | Default |
| ------ | ----------- | ------- |
| `-p <port>` | Destination UDP port | `514` |
| `-f <facility>` | Facility name (`kern`, `user`, `mail`, `daemon`, `auth`, `syslog`, `lpr`, `news`, `uucp`, `cron`, `authpriv`, `ftp`, `ntp`, `audit`, `alert`, `at`, `local0`…`local7`) | `user` |
| `-s <severity>` | Severity name (`emergency`…`debug`) | `info` |
| `-V` | Print version and exit | — |

Example:

```
./build/syslogd_client -p 5514 -f local0 -s info 127.0.0.1 "hello world"
```

The client builds a PRI from facility and severity (PRI = facility × 8 +
severity) and prints the effective PRI value as part of its confirmation.

### Exit codes

| Code | Meaning |
| ---- | ------- |
| 0    | Message sent |
| 1    | Usage error, unknown facility/severity, socket or send failure |

## syslogd_web — the web viewer

Serve the log file as an HTML table over HTTP on port 8090.

```
Usage: ./build/syslogd_web [-w webroot] [-V]
```

| Option | Description | Default |
| ------ | ----------- | ------- |
| `-w <dir>` | Directory with `index.html`/`style.css`/`app.js` | `web` |
| `-V` | Print version and exit | — |

```
./build/syslogd_web -w ../web
# open http://localhost:8090/
```

The viewer reads `/var/log/custom_syslog.log` by default (the same file the
server writes), including the `/var/log/custom_syslog.log.N` rotated history.
Start the server with `-l /var/log/custom_syslog.log` (or `SYSLOGD_LOG_FILE`)
so the viewer finds its data.
Table columns: Timestamp (browser-local, with the time-zone id in the header),
Host, Severity, MsgID, Message, Data (structured data), App-Name, Facility,
ProcessID.

Features: search box, multi-select severity/facility filters, pagination
(Lines/page, "All"), dark/light/system theme, Reload/Clear/Pause, and a "Pin
to newest" live tail that highlights freshly arrived rows (grey line glow for
1 s plus a left bar for 5 s). A live-connection indicator and a `syslogd`
online/offline badge are shown in the header.

### Environment variables (syslogd_web)

The viewer reads its port from `SYSLOGD_WEB_PORT` and shares the log file and
history settings with the server (`SYSLOGD_LOG_FILE` and
`SYSLOGD_MAX_LOG_FILES`):

| Variable | Description | Default |
| -------- | ----------- | ------- |
| `SYSLOGD_WEB_PORT` | HTTP port to bind | `8090` |
| `SYSLOGD_LOG_FILE` | Log file (plus its rotated history) | `/var/log/custom_syslog.log` |
| `SYSLOGD_MAX_LOG_FILES` | Number of rotated history files to include | `5` |
| `SYSLOGD_MAX_ENTRIES` | Max entries the web viewer keeps in memory (delivered to the browser via `/api/config`) | `4000` |

The viewer reads the same `SYSLOGD_LOG_FILE` and `SYSLOGD_MAX_LOG_FILES` the
server uses, so they stay in sync automatically and the viewer shows the full
history. Example:

```bash
SYSLOGD_WEB_PORT=8090 SYSLOGD_LOG_FILE=/tmp/custom.log \
SYSLOGD_MAX_LOG_FILES=3 ./build/syslogd_web -w web
```

### API (all GET)

| Endpoint | Response |
| -------- | -------- |
| `/api/log?limit=N` | JSON array of the newest N lines (newest first; default 500, max 2000 kept), spanning the rotated history and the current log. Always returns the tail (newest) entries, never the head. Each entry: timestamp (UTC RFC 3339 with ms), host, app, proc, msgid, facility, severity, sd, msg. |
| `/api/stream` | Server-Sent Events. Pushes newly written lines as individual events every 500 ms. Handles log rotation. An initial snapshot (200 live / 512 demo) is sent but the frontend ignores it, loading history via `/api/log` instead. |
| `/api/config` | `{"maxRows":N}` — the viewer entry cap the browser should apply (from `SYSLOGD_MAX_ENTRIES`; default `4000`). |
| `/api/version` | `{"name":"syslogd_web","version":"0.0.7"}` |
| `/api/status` | `{"name":"syslogd","online":true\|false,"version":"…","pid":…}` from the status file; liveness via `kill(pid,0)` (`EPERM` counts as online). |
| `/demo`, `/demo/*` | The same viewer and API, but every API call reads `web/sample-500.log` (the bundled demo log) instead of `LOGFILE`. No button in the UI; reached by URL only. |

### Exit codes

| Code | Meaning |
| ---- | ------- |
| 1    | Socket, bind, or listen failure |

## Demo log

`web/sample-500.log` (also mirrored at the repo root) contains 500 RFC 5424
lines: severity distribution info=375, debug=50, alert=10, critical=15,
warning=20, notice=15, error=12, emergency=3, with timestamps spread over 10
hours and typical clients (web/db/dns/mail/backup servers, router/switch/NAS,
Raspberry Pis, sensors). It is shown by the `/demo` page of `syslogd_web`.

## Manual test

```
make debug
./build/syslogd -p 5514 -l /tmp/custom_syslog.log &
./build/syslogd_client -p 5514 -f local0 -s info 127.0.0.1 "hello world from client"
./build/syslogd_web -w web &
# check http://localhost:8090/ and /tmp/custom_syslog.log
```

Web viewer checks:

```
curl -s http://127.0.0.1:8090/api/version
curl -s http://127.0.0.1:8090/api/status
curl -s "http://127.0.0.1:8090/api/log?limit=3"
curl -s --max-time 3 -N http://127.0.0.1:8090/api/stream   # SSE
curl -s http://127.0.0.1:8090/demo                          # demo page
```

`sample.txt` contains two ready-made syslog messages to reuse as the client's
message argument.