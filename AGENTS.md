# syslogd - Agent Instructions

## Goal
`syslogd` is a configurable, dependency-free **C17** syslog suite: a UDP syslog
server, a test client, and a web viewer. All three share the same source
version.

- `syslogd` — UDP listener (default port 514) that accepts RFC 5424 syslog
  messages and appends them to a log file.
- `syslogd_client` — sends a test message to the server.
- `syslogd_web` — HTTP viewer on port 8090 serving the log as a table, with a
  JSON API and a live Server-Sent-Events stream.

## Repository / git workflow
- Its origin is the public repo `https://github.com/ocfu/syslogd.git`.
- The commit-style here is short lowercase headings, e.g.
  `Add versioning and status API to syslogd client and web interface`.

## Build
```
make                       # debug-build via make debug
make debug                 # -O0 -g build
make clean
```
- Default `CC=clang`, `CFLAGS=-std=c17 -Wall -Wextra -pedantic -O2`.
- Produces three binaries in `build/`: `syslogd`, `syslogd_client`, `syslogd_web`.
- For a **glibc** build (e.g. the Docker image / Raspberry Pi) an explicit
  `_GNU_SOURCE` is required (`NI_NAMEREQD`, `getopt`/`optarg`,
  `struct timeval`):
  ```
  make CFLAGS="-std=c17 -D_GNU_SOURCE -Wall -Wextra -pedantic -O2"
  ```

## Run
- Server (needs `sudo` for the default log path):
  ```
  sudo ./build/syslogd -p 5514 -l /var/log/custom_syslog.log
  ```
  Options: `-p <port>` (default 514), `-l <logfile>`
  (default `/var/log/custom_syslog.log`), `-d`, `-V`.
- Client:
  ```
  ./build/syslogd_client -p 5514 -f local0 -s info 127.0.0.1 "hello from a syslog client"
  ```
  Options: `-p <port>`, `-f <facility>`, `-s <severity>`, `-V`.
- Web viewer:
  ```
  ./build/syslogd_web -w ../web
  ```
  Options: `-w <dir>` (default `web`), `-V`. Listens on port 8090.

## Verify
- Version (single source of truth in `src/version.h`, `SYSLOGD_VERSION`):
  ```
  ./build/syslogd -V
  ./build/syslogd_client -V
  ./build/syslogd_web -V
  ```
- JS syntax check:
  ```
  node --check web/app.js
  ```
- Web viewer smoke test:
  ```
  curl -s http://127.0.0.1:8090/api/version
  curl -s http://127.0.0.1:8090/api/status
  curl -s "http://127.0.0.1:8090/api/log?limit=3"
  curl -s --max-time 3 -N http://127.0.0.1:8090/api/stream      # SSE
  ```
- Demo page (bundled sample log, see below):
  ```
  curl -s http://127.0.0.1:8090/demo
  curl -s "http://127.0.0.1:8090/demo/api/log?limit=3"
  ```

## Demos / sample data
- `web/sample-500.log` (also mirrored at the repo root) is the bundled demo
  log: exactly 500 RFC 5424 lines, severity counts info=375, debug=50,
  alert=10, critical=15, warning=20, notice=15, error=12, emergency=3,
  timestamps distributed over 10 hours.
- `/demo` on the web server serves the same SPA but every API call reads
  `web/sample-500.log` instead of the live log file. There is deliberately no
  button in the viewer UI; `/demo` is only reached by URL.

## Architecture notes
- **Log format / RFC 5424 passthrough**: each line written by `syslogd` is
  `<PRI>1 TIMESTAMP HOSTNAME APP-NAME PROCID MSGID SD MSG`. Messages that
  already are a complete RFC 5424 message (`is_rfc5424_raw`) are written
  through unchanged, preserving their own header fields. Simple `<PRI>msg`
  messages are wrapped with the receive time, the short hostname, and
  `- - - -` for the RFC 5424 fields.
- **Timestamps**: written/emitted as UTC RFC 3339 with milliseconds
  (`2026-08-30T21:17:47.874Z`). The JSON API (`/api/log`, `/api/stream`)
  sends this ISO UTC string; the browser converts it to local time for
  display and keeps the ISO string for sorting. The column header shows the
  browser time zone id.
- **Web server** (`syslogd_web.c`): dependency-free single-page UI served
  from the webroot (`index.html`/`style.css`/`app.js`, all loaded from the
  root with root-relative paths). It forks per connection, so a per-request
  global (e.g. the active log file `g_pLogFile`) is safe. API:
  - `GET /api/log?limit=N` — newest N lines as JSON (newest first, max 512 kept).
  - `GET /api/stream` — Server-Sent Events: initial snapshot of the newest
    lines, then polls the log file and pushes appended lines. Handles log
    rotation. The initial-window size is 200 for the live log and 512 for the
    demo so all sample entries show.
  - `GET /api/version` — `{"name":"syslogd_web","version":"0.0.1"}`.
  - `GET /api/status` — reads the server status file and reports
    `online`/`offline` (liveness via `kill(pid,0)`; `EPERM` counts as online).
  - `/demo*` — same SPA/API with the demo logfile.
- **Status file**: `syslogd` writes `/var/run/custom_syslog.status`
  (`version=`, `pid=`, `port=`, `logfile=`) on startup (unconditionally) and
  removes it on shutdown. The web viewer polls `/api/status` every 5 s.
- **Versioning**: single `SYSLOGD_VERSION` in `src/version.h`; the Makefile
  makes every target depend on it so a version bump rebuilds all tools.
  Versions start at `0.0.1`.
- **Viewer features** in `web/app.js`: search, multi-select severity/facility
  filters, pagination, dark/light/system theme, Reload/Clear/Pause, "Pin to
  newest" with a grey highlight for freshly arrived rows (line glow 1 s, left
  bar 5 s), live-connection indicator, and a `syslogd` online/offline badge.
  `app.js` detects the site base (`/` vs `/demo`) via `location.pathname` and
  prefixes API calls accordingly (`API_BASE`).

## Coding conventions
Follow the full guideline at `~/Dev/Guidelines/GUIDELINE.md`; the key rules
for this codebase:
- **C17**, English names and comments only.
- Classes PascalCase, methods camelCase.
- Variables use fixed-width types (`int8_t`, `int32_t`) and scope/type
  prefixes: `_` + type prefix for private members, `__` for protected,
  `UPPER_SNAKE_CASE` for constants, `g_` for globals.
- 3-space indentation, `{` on the same line. No public class members.
- Avoid dynamic allocation where reasonable; keep dependencies at zero.
- Web UI is a single dependency-free JS file (`web/app.js`); keep it that way
  (no build step for the UI, no external libraries).