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

```bash
make                       # debug-build via make debug
make debug                 # -O0 -g build
make clean
```

- Default `CC=clang`, `CFLAGS=-std=c17 -Wall -Wextra -pedantic -O2`.
- Produces three binaries in `build/`: `syslogd`, `syslogd_client`, `syslogd_web`.
- For a **glibc** build (e.g. the Docker image / Raspberry Pi) an explicit
  `_GNU_SOURCE` is required (`NI_NAMEREQD`, `getopt`/`optarg`,
  `struct timeval`):

  ```bash
  make CFLAGS="-std=c17 -D_GNU_SOURCE -Wall -Wextra -pedantic -O2"
  ```

## Run

- Server (needs `sudo` for the default log path):

  ```bash
  sudo ./build/syslogd -p 5514 -l /var/log/custom_syslog.log
  ```

  Options: `-p <port>` (default 514), `-l <logfile>`
  (default `/var/log/custom_syslog.log`), `-d`, `-V`. Env vars:
  `SYSLOGD_PORT`, `SYSLOGD_LOG_FILE`, `SYSLOGD_MAX_LOG_SIZE` (default 5 MB),
  `SYSLOGD_MAX_LOG_FILES` (default 5). Flags take precedence over env vars.

- Client:

  ```bash
  ./build/syslogd_client -p 5514 -f local0 -s info 127.0.0.1 "hello"
  ```

  Options: `-p <port>`, `-f <facility>`, `-s <severity>`, `-V`.

- Web viewer:

  ```bash
  ./build/syslogd_web -w ../web
  ```

  Options: `-w <dir>` (default `web`), `-V`. Env vars:
  `SYSLOGD_WEB_PORT` (default 8090); shares `SYSLOGD_LOG_FILE` and
  `SYSLOGD_MAX_LOG_FILES` with the server.

## Verify

- Version (single source of truth in `src/version.h`, `SYSLOGD_VERSION`):

  ```bash
  ./build/syslogd -V
  ./build/syslogd_client -V
  ./build/syslogd_web -V
  ```

- JS syntax check:

  ```bash
  node --check web/app.js
  ```

- Web viewer smoke test:

  ```bash
  curl -s http://127.0.0.1:8090/api/version
  curl -s http://127.0.0.1:8090/api/status
  curl -s "http://127.0.0.1:8090/api/log?limit=3"
  curl -s --max-time 3 -N http://127.0.0.1:8090/api/stream      # SSE
  ```

- Demo page (bundled sample log, see below):

  ```bash
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
- **Rotation**: `syslogd` rotates the log file when it reaches
  `SYSLOGD_MAX_LOG_SIZE` (default 5 MB). It uses a numbered scheme: the
  current file becomes `.1`, every `.i` is shifted to `.i+1`, and `.N`
  (`N = SYSLOGD_MAX_LOG_FILES`, default 5) is deleted. History is therefore
  `<logfile>.1` … `<logfile>.N` with `.N` oldest.
- **Web viewer history**: `syslogd_web` reads the rotated history
  (`<logfile>.N` … `<logfile>.1`) plus the current log in `/api/log`, using
  the same `SYSLOGD_MAX_LOG_FILES` as the server. The frontend loads this
  full history on startup via `/api/log` and uses the `/api/stream` SSE
  connection only for live updates. The demo (`/demo`) uses only the bundled
  sample log, no history.
- **Timestamps**: written/emitted as UTC RFC 3339 with milliseconds
  (`2026-08-30T21:17:47.874Z`). The JSON API (`/api/log`, `/api/stream`)
  sends this ISO UTC string; the browser converts it to local time for
  display and keeps the ISO string for sorting. The column header shows the
  browser time zone id.
- **Web server** (`syslogd_web.c`): dependency-free single-page UI served
  from the webroot (`index.html`/`style.css`/`app.js`, all loaded from the
  root with root-relative paths). It forks per connection, so a per-request
  global (e.g. the active log file `g_pLogFile`) is safe. API:

  - `GET /api/log?limit=N` — newest N lines as JSON (newest first, max 2000
    kept), across the rotated history and the current log. The ring buffer
    always keeps the newest N entries (the tail of the log), never the head;
    `limit` is capped at the 2000-entry array size. The ring is a circular
    buffer (O(1) append, no memmove), so reading a large log is fast.
  - `GET /api/stream` — Server-Sent Events: pushes appended lines as
    individual events every 500 ms. Handles log rotation. The initial snapshot
    (200 live / 512 demo) is sent but the frontend ignores it, loading history
    via `/api/log` instead.
  - `GET /api/config` — `{"maxRows":N}`; N is the viewer entry cap the
    browser keeps (from `SYSLOGD_MAX_ENTRIES`, default 4000).
  - `GET /api/version` — `{"name":"syslogd_web","version":"0.0.8"}`.
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
  prefixes API calls accordingly (`API_BASE`). On startup the viewer fetches
  `/api/config` then `/api/log` for the full history, and opens the SSE
  stream only for live updates.

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

## Verify with markdownlint

```bash
markdownlint AGENTS.md README.md docs/usage.md
```
