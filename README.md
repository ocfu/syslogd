# Configurable Syslog Server

A small, dependency-free syslog toolkit written in C17. It receives syslog
messages over UDP, writes them to a log file in **RFC 5424** format (with
automatic rotation and optional daemon mode), and ships with a test client
and a web viewer for the log.

The project builds three binaries, all sharing one version (`src/version.h`):

- `syslogd` — the syslog server (UDP listener, RFC 5424 log writer, rotation,
  daemon mode, status file).
- `syslogd_client` — sends a test syslog message over UDP.
- `syslogd_web` — web viewer on port 8090 (HTML table, JSON API, live
  Server-Sent-Events stream, demo page).

## Features

- UDP syslog receiver with configurable port and log file (`-p`, `-l`)
- RFC 5424 log format: `<PRI>1 TIMESTAMP HOSTNAME APP-NAME PROCID MSGID SD MSG`.
  Complete RFC 5424 messages are passed through unchanged; simple `<PRI>msg`
  messages are wrapped with the receive time and short hostname.
- PRI decoding to facility and severity names
- Automatic log rotation at 5 MB (rotated files keep a timestamp suffix)
- Daemon mode with PID file (`/var/run/custom_syslog.pid`) and a status file
  (`/var/run/custom_syslog.status`: version, pid, port, logfile)
- Graceful shutdown on SIGINT/SIGTERM
- Test client to inject messages, web viewer to inspect the log
- Shared version for all tools: `-V` prints `SYSLOGD_VERSION` from `src/version.h`

## Requirements

- C17 compiler (`clang` or `gcc`)
- `make`
- macOS or Linux

For a glibc build (e.g. the Docker image / Raspberry Pi) the source needs
`_GNU_SOURCE` (`NI_NAMEREQD`, `getopt`/`optarg`, `struct timeval`):
`make CFLAGS="-std=c17 -D_GNU_SOURCE -Wall -Wextra -pedantic -O2"`.

## Build

```
make          # release build into build/
make debug    # debug build (-g -O0)
make clean    # remove build/
```

## Usage

Syslog server (foreground, port 5514):

```
./build/syslogd -p 5514 -l /tmp/custom_syslog.log
```

Send a test message:

```
./build/syslogd_client -p 5514 -f local0 -s info 127.0.0.1 "hello world"
```

View the log in a browser:

```
./build/syslogd_web
# open http://localhost:8090/
```

All binaries print their option set via `-h` and their version via `-V`
(see [docs/usage.md](docs/usage.md) for the full reference). Environment note:
listening on the default port 514 requires root privileges; use a high port
such as 5514 for testing. The web viewer reads the compile-time default log
`/var/log/custom_syslog.log`, so start the server with
`-l /var/log/custom_syslog.log` to feed it.

### Web viewer

Served from the webroot (`web/`): a dependency-free single-page UI with
search, multi-select severity/facility filters, pagination, dark/light/system
theme, a "Pin to newest" live tail with transient highlight of fresh rows, a
live-connection indicator and a `syslogd` online/offline badge.

API (all `GET`, JSON except the stream):

- `/api/log?limit=N` — newest N lines, newest first.
- `/api/stream` — Server-Sent Events live tail (initial snapshot, then appends).
- `/api/version` — web viewer name/version.
- `/api/status` — syslogd version/pid/online from the server status file.
- `/demo` (and `/demo/*`) — the same viewer, but every API call reads the
  bundled sample log `web/sample-500.log` (500 RFC 5424 lines over 10 hours)
  instead of the live log file. Reached by URL only; there is no button.

Timestamps are written/emitted as UTC RFC 3339 with milliseconds
(`2026-08-30T21:17:47.874Z`) and converted to local time in the browser; the
Timestamp column header shows the browser's time-zone id.

## Sample output

Log line written after receiving `<134>hello world` from a host (wrapped form):

```
<134>1 2026-08-30T12:00:01.003Z hostname - - - - hello world
```

Foreground server also prints to stdout:

```
[127.0.0.1:63128] local0.info: hello world
```

## License

MIT — see [LICENSE](LICENSE). See [CONTRIBUTING.md](CONTRIBUTING.md) for how
to report bugs and contribute.