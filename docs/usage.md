# Usage

The project ships three binaries, all built into `build/`.

## syslogd — the server

Listen for UDP syslog messages and append them to a log file.

```
Usage: ./build/syslogd [-p port] [-l logfile] [-d]
```

| Option | Description | Default |
| ------ | ----------- | ------- |
| `-p <port>` | UDP port to listen on | `514` |
| `-l <logfile>` | Path to the log file | `/var/log/custom_syslog.log` |
| `-d` | Run as a daemon (foreground by default) | off |

Notes:

- Listening on the default port `514` requires root (`sudo`). Use a high port
  such as `5514` for testing.
- In daemon mode the server writes its PID to
  `/var/run/custom_syslog.pid` and removes it on shutdown.
- The log file rotates automatically once it reaches 5 MB: the current file is
  renamed to `<logfile>.<YYYYMMDD_HHMMSS>` and a new one is started.
- SIGINT and SIGTERM trigger a clean shutdown (remove the PID file, close the
  socket, close syslog).

### Output format

Each message is appended to the log file as one line:

```
YYYY-MM-DD HH:MM:SS [host:port] facility=<facility> severity=<severity> msg=<message>
```

- `host` is the sender's short hostname when it resolves, otherwise the IP
  address, left-aligned in a 15-character column.
- `facility` and `severity` are decoded from the message's PRI prefix (`<N>`),
  e.g. `<134>` → facility `local0`, severity `info`. Unknown values become
  `unknown`.
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
Usage: ./build/syslogd_client [-p port] [-f facility] [-s severity] <server_ip> <message>
```

| Option | Description | Default |
| ------ | ----------- | ------- |
| `-p <port>` | Destination UDP port | `514` |
| `-f <facility>` | Facility name (`kern`, `user`, `mail`, `daemon`, `auth`, `syslog`, `lpr`, `news`, `uucp`, `cron`, `authpriv`, `ftp`, `ntp`, `audit`, `alert`, `at`, `local0`…`local7`) | `user` |
| `-s <severity>` | Severity name (`emergency`…`debug`) | `info` |

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
./build/syslogd_web
# open http://localhost:8090/
```

The viewer reads `/var/log/custom_syslog.log` (compile-time default). Start
the server with `-l /var/log/custom_syslog.log` so the viewer finds its data.
Columns: Timestamp, Host, Port, Facility, Severity, Message.

### Exit codes

| Code | Meaning |
| ---- | ------- |
| 1    | Socket, bind, or listen failure |

## Manual test

```
make debug
./build/syslogd -p 5514 -l /tmp/custom_syslog.log &
./build/syslogd_client -p 5514 127.0.0.1 "hello world from client"
./build/syslogd_web &
# check http://localhost:8090/ and /tmp/custom_syslog.log
```

`sample.txt` contains two ready-made syslog messages to reuse as the client's
message argument.