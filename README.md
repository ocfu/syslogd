# Configurable Syslog Server

A small, dependency-free syslog toolkit written in C17. It receives syslog
messages over UDP, writes them to a log file (with automatic rotation and
optional daemon mode), and ships with a test client and a web viewer for the
log.

The project builds three binaries:

- `syslogd` — the syslog server (UDP listener, log writer, rotation, daemon).
- `syslogd_client` — sends a test syslog message over UDP.
- `syslogd_web` — serves the log file as an HTML table on port 8090.

## Features

- UDP syslog receiver with configurable port and log file
- RFC 3164-style PRI decoding (facility and severity names)
- Automatic log rotation at 5 MB (rotated files keep a timestamp suffix)
- Daemon mode with PID file (`/var/run/custom_syslog.pid`)
- Graceful shutdown on SIGINT/SIGTERM
- Test client to inject messages, web viewer to inspect the log

## Requirements

- C17 compiler (`clang` or `gcc`)
- `make`
- macOS or Linux

## Build

```
make          # release build into build/
make debug    # debug build (-g -O0)
make run ARGS="-p 5514 -l /tmp/custom_syslog.log"   # debug build + run server
make clean    # remove build/
```

## Usage

Syslog server (foreground, port 5514):

```
./build/syslogd -p 5514 -l /tmp/custom_syslog.log
```

Send a test message:

```
./build/syslogd_client -p 5514 127.0.0.1 "hello world"
```

View the log in a browser:

```
./build/syslogd_web
# open http://localhost:8090/
```

Both binaries print their option set via `-h` (see [docs/usage.md](docs/usage.md)
for the full reference). Environment note: listening on the default port 514
requires root privileges; use a high port such as 5514 for testing.

### Sample output

Foreground server line after receiving `<134>hello world` from localhost:

```
[127.0.0.1:63128] local0.info: hello world
2026-08-30 12:00:01 [127.0.0.1                     :63128] facility=local0 severity=info msg=hello world
```

## License

MIT — see [LICENSE](LICENSE). See [CONTRIBUTING.md](CONTRIBUTING.md) for how
to report bugs and contribute.