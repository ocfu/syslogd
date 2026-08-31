/*
 * syslogd_web.c - HTTP web viewer for the syslog log file.
 *
 * Serves a dependency-free single-page web UI (HTML/CSS/JS from WEBROOT) plus a
 * JSON API (/api/log) and a Server-Sent Events live stream (/api/stream).
 *
 * Usage: ./syslogd_web [-w webroot]
 *   -w <dir>  Directory containing index.html/style.css/app.js (default: web)
 *
 * Accepts RFC 5424 log lines written by syslogd:
 *   <PRI>VERSION TIMESTAMP HOSTNAME APP-NAME PROCID MSGID STRUCTURED-DATA MSG
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>

#include "version.h"

#define PORT 8090
#define LOGFILE "/var/log/custom_syslog.log"
#define DEMO_LOGFILE "sample-500.log"
#define SSE_POLL_MS 500
#define SSE_INITIAL_LINES 200
#define DEMO_INITIAL_LINES 512
#define SYSLOGD_STATUS_FILE "/var/run/custom_syslog.status"

static const char *g_szWebRoot = "web";
static char g_szDemoLog[1024];
/* Which log file the API handlers read. Per-connection (server forks per
 * connection), so set at the top of handle_connection(). */
static const char *g_pLogFile = LOGFILE;

static const char *g_szFacilities[] = {
   "kern", "user", "mail", "daemon", "auth", "syslog", "lpr", "news",
   "uucp", "cron", "authpriv", "ftp", "ntp", "audit", "alert", "at",
   "local0", "local1", "local2", "local3",
   "local4", "local5", "local6", "local7"};
#define NUM_FACILITIES (sizeof(g_szFacilities) / sizeof(g_szFacilities[0]))

static const char *g_szSeverities[] = {
   "emergency", "alert", "critical", "error",
   "warning", "notice", "info", "debug"};
#define NUM_SEVERITIES (sizeof(g_szSeverities) / sizeof(g_szSeverities[0]))

/* ------------------------------------------------------------------ */
/* Small growable buffer helper                                       */
/* ------------------------------------------------------------------ */

typedef struct {
   char *pData;
   size_t nLen;
   size_t nCap;
} Buffer;

static void buff_init(Buffer *pB) {
   pB->pData = NULL;
   pB->nLen = 0;
   pB->nCap = 0;
}

static int buff_appendf(Buffer *pB, const char *szFmt, ...) {
   va_list ap;
   va_start(ap, szFmt);
   int nNeed = vsnprintf(NULL, 0, szFmt, ap);
   va_end(ap);
   if (nNeed < 0) return -1;

   if (pB->nLen + (size_t)nNeed + 1 > pB->nCap) {
      size_t nNewCap = pB->nLen + (size_t)nNeed + 1;
      char *pTmp = realloc(pB->pData, nNewCap);
      if (!pTmp) return -1;
      pB->pData = pTmp;
      pB->nCap = nNewCap;
   }
   va_start(ap, szFmt);
   vsnprintf(pB->pData + pB->nLen, pB->nCap - pB->nLen, szFmt, ap);
   va_end(ap);
   pB->nLen += (size_t)nNeed;
   return 0;
}

static void buff_free(Buffer *pB) {
   free(pB->pData);
   pB->pData = NULL;
   pB->nLen = pB->nCap = 0;
}

/* ------------------------------------------------------------------ */
/* HTTP helpers                                                        */
/* ------------------------------------------------------------------ */

static void send_head(int nFd, const char *szStatus, const char *szContentType,
                      const char *szExtra, long nContentLength) {
   dprintf(nFd,
      "HTTP/1.1 %s\r\n"
      "Content-Type: %s\r\n"
      "%s"
      "Content-Length: %ld\r\n"
      "Connection: close\r\n"
      "\r\n", szStatus, szContentType, szExtra ? szExtra : "", nContentLength);
}

/* Reads the raw request up to and including the header terminator or cap. */
static int read_request(int nFd, char *pBuf, size_t nSize) {
   size_t n = 0;
   char c;
   while (n + 1 < nSize && read(nFd, &c, 1) == 1) {
      pBuf[n++] = c;
      if (n >= 4 && strncmp(pBuf + n - 4, "\r\n\r\n", 4) == 0) break;
      if (n >= 4 && strncmp(pBuf + n - 4, "\n\n", 2) == 0) break;
   }
   pBuf[n] = '\0';
   return (int)n;
}

/* Extract request line fields: method, path (with query split out). */
static void parse_request(const char *pReq, char *pMethod, size_t nMethodSz,
                          char *pPath, size_t nPathSz, char *pQuery, size_t nQuerySz) {
   const char *p = pReq;
   size_t k = 0;
   while (*p && *p != ' ' && k + 1 < nMethodSz) pMethod[k++] = *p++;
   pMethod[k] = '\0';
   while (*p == ' ') p++;

   k = 0;
   while (*p && *p != ' ' && *p != '?' && k + 1 < nPathSz) pPath[k++] = *p++;
   pPath[k] = '\0';

   pQuery[0] = '\0';
   if (*p == '?') {
      p++;
      k = 0;
      while (*p && *p != ' ' && k + 1 < nQuerySz) pQuery[k++] = *p++;
      pQuery[k] = '\0';
   }
}

/* Extract integer query parameter value. Returns default if absent/invalid. */
static long query_int(const char *pQuery, const char *pKey, long nDefault) {
   size_t nKeyLen = strlen(pKey);
   const char *p = pQuery;
   while (p && *p) {
      const char *pAmp = strchr(p, '&');
      size_t nSeg = pAmp ? (size_t)(pAmp - p) : strlen(p);
      if (nSeg >= nKeyLen + 1 && strncmp(p, pKey, nKeyLen) == 0 && p[nKeyLen] == '=') {
         return atol(p + nKeyLen + 1);
      }
      if (!pAmp) break;
      p = pAmp + 1;
   }
   return nDefault;
}

/* JSON-escape a string into a growable buffer. */
static void json_append_escaped(Buffer *pB, const char *pS) {
   for (const unsigned char *p = (const unsigned char *)pS; *p; p++) {
      switch (*p) {
         case '"':  buff_appendf(pB, "\\\""); break;
         case '\\': buff_appendf(pB, "\\\\"); break;
         case '\n': buff_appendf(pB, "\\n"); break;
         case '\r': buff_appendf(pB, "\\r"); break;
         case '\t': buff_appendf(pB, "\\t"); break;
         default:
            if (*p < 0x20) buff_appendf(pB, "\\u%04x", *p);
            else buff_appendf(pB, "%c", *p);
            break;
      }
   }
}

/* ------------------------------------------------------------------ */
/* Log line parsing into an entry                                     */
/* ------------------------------------------------------------------ */

typedef struct {
   char szTs[40];
   char szHost[64];
   char szApp[64];
   char szProc[64];
   char szMsgid[64];
   char szFacility[32];
   char szSeverity[32];
   char szSd[256];
   char szMsg[1024];
   int bValid;
} LogEntry;

/* Parse an RFC 3339 timestamp (as used by RFC 5424) into an epoch (UTC)
 * and millisecond fraction. Returns 1 on success. */
static int rfc3339_to_epoch(const char *p, time_t *pEpoch, int *pnMs) {
   int y, mo, d, h, mi, s;
   if (sscanf(p, "%4d-%2d-%2dT%2d:%2d:%2d", &y, &mo, &d, &h, &mi, &s) < 6) return 0;
   if (!p[19]) return 0;
   const char *q = p + 19;
   int ms = 0;
   if (*q == '.') {
      q++;
      int nDig = 0;
      while (nDig < 6 && isdigit((unsigned char)q[nDig])) {
         if (nDig < 3) ms = ms * 10 + (q[nDig] - '0');
         nDig++;
      }
      q += nDig;
   }
   int off = 0;
   if (*q == 'Z' || *q == 'z') {
      off = 0;
   } else if (*q == '+' || *q == '-') {
      int sign = (*q == '+') ? 1 : -1;
      q++;
      int oh = 0, om = 0;
      if (isdigit((unsigned char)q[0]) && isdigit((unsigned char)q[1]))
         oh = (q[0] - '0') * 10 + (q[1] - '0');
      const char *r = q + 2;
      if (*r == ':') r++;
      if (isdigit((unsigned char)r[0]) && isdigit((unsigned char)r[1]))
         om = (r[0] - '0') * 10 + (r[1] - '0');
      off = sign * (oh * 3600 + om * 60);
   }
   struct tm tmUtc;
   memset(&tmUtc, 0, sizeof(tmUtc));
   tmUtc.tm_year = y - 1900;
   tmUtc.tm_mon = mo - 1;
   tmUtc.tm_mday = d;
   tmUtc.tm_hour = h;
   tmUtc.tm_min = mi;
   tmUtc.tm_sec = s;
   tmUtc.tm_isdst = 0;
   time_t t = timegm(&tmUtc);
   if (t == (time_t)-1) return 0;
   *pEpoch = t - off;
   *pnMs = ms;
   return 1;
}

/* Format an epoch as an UTC RFC 3339 timestamp with milliseconds (e.g.
 * 2026-08-30T21:17:47.874Z); browsers convert it to their local time. */
static void format_ts(time_t epoch, int ms, char *out, size_t cap) {
   struct tm *g = gmtime(&epoch);
   if (!g) { snprintf(out, cap, "-"); return; }
   strftime(out, cap, "%Y-%m-%dT%H:%M:%S", g);
   size_t n = strlen(out);
   snprintf(out + n, cap - n, ".%03dZ", ms);
}

/* Read the next whitespace-delimited token. Returns its length. */
static size_t next_token(const char **pp, char *out, size_t cap) {
   const char *p = *pp;
   while (isspace((unsigned char)*p)) p++;
   size_t n = 0;
   while (*p && !isspace((unsigned char)*p) && n + 1 < cap) out[n++] = *p++;
   out[n] = '\0';
   *pp = p;
   return n;
}

/* Read structured data (a '-' or one-or-more bounded [SD-ELEMENT]s) into out,
 * returning the pointer just past it. */
static const char *capture_structured_data(const char *p, char *out, size_t cap) {
   while (isspace((unsigned char)*p)) p++;
   size_t n = 0;
   if (*p == '-') {
      out[n++] = '-';
      out[n] = '\0';
      return p + 1;
   }
   while (*p == '[') {
      int inQuote = 0;
      if (n + 1 < cap) out[n++] = *p;
      p++;
      while (*p && !(*p == ']' && !inQuote)) {
         if (n + 1 < cap) out[n++] = *p;
         if (*p == '"') inQuote = !inQuote;
         p++;
      }
      if (*p == ']') { if (n + 1 < cap) out[n++] = *p; p++; }
      else break;
   }
   out[n] = '\0';
   return p;
}

static void parse_log_line(const char *pLine, LogEntry *pE) {
   memset(pE, 0, sizeof(*pE));
   pE->bValid = 0;
   const char *p = pLine;

   /* <PRI> */
   if (*p != '<') return;
   int pri = 0;
   p++;
   while (isdigit((unsigned char)*p)) { pri = pri * 10 + (*p - '0'); p++; }
   if (*p != '>') return;
   p++;
   int fac = pri / 8;
   int sev = pri % 8;
   if (fac >= 0 && fac < (int)NUM_FACILITIES) strcpy(pE->szFacility, g_szFacilities[fac]);
   else strcpy(pE->szFacility, "unknown");
   if (sev >= 0 && sev < (int)NUM_SEVERITIES) strcpy(pE->szSeverity, g_szSeverities[sev]);
   else strcpy(pE->szSeverity, "unknown");

   /* VERSION TIMESTAMP HOSTNAME APP-NAME PROCID MSGID */
   char tok[64];
   if (next_token(&p, tok, sizeof(tok)) == 0) return;
   if (next_token(&p, tok, sizeof(tok)) == 0) return;
   if (strcmp(tok, "-") == 0) {
      strcpy(pE->szTs, "-");
   } else {
      time_t epoch;
      int ms;
      if (rfc3339_to_epoch(tok, &epoch, &ms)) format_ts(epoch, ms, pE->szTs, sizeof(pE->szTs));
      else strncpy(pE->szTs, tok, sizeof(pE->szTs) - 1);
   }
   if (next_token(&p, pE->szHost, sizeof(pE->szHost)) == 0) return;
   if (next_token(&p, pE->szApp, sizeof(pE->szApp)) == 0) return;
   if (next_token(&p, pE->szProc, sizeof(pE->szProc)) == 0) return;
   if (next_token(&p, pE->szMsgid, sizeof(pE->szMsgid)) == 0) return;

   /* STRUCTURED-DATA then MSG (rest of line). */
   p = capture_structured_data(p, pE->szSd, sizeof(pE->szSd));
   while (isspace((unsigned char)*p)) p++;
   size_t nM = 0;
   while (*p && *p != '\r' && *p != '\n' && nM + 1 < sizeof(pE->szMsg)) pE->szMsg[nM++] = *p++;
   pE->szMsg[nM] = '\0';

   pE->bValid = 1;
}

/* Append a JSON object for an entry into a growable buffer. */
static void build_entry_json(Buffer *pB, LogEntry *pe) {
   buff_appendf(pB, "{\"ts\":\"");
   json_append_escaped(pB, pe->szTs);
   buff_appendf(pB, "\",\"host\":\"");
   json_append_escaped(pB, pe->szHost);
   buff_appendf(pB, "\",\"app\":\"");
   json_append_escaped(pB, pe->szApp);
   buff_appendf(pB, "\",\"proc\":\"");
   json_append_escaped(pB, pe->szProc);
   buff_appendf(pB, "\",\"msgid\":\"");
   json_append_escaped(pB, pe->szMsgid);
   buff_appendf(pB, "\",\"facility\":\"");
   json_append_escaped(pB, pe->szFacility);
   buff_appendf(pB, "\",\"severity\":\"");
   json_append_escaped(pB, pe->szSeverity);
   buff_appendf(pB, "\",\"sd\":\"");
   json_append_escaped(pB, pe->szSd);
   buff_appendf(pB, "\",\"msg\":\"");
   json_append_escaped(pB, pe->szMsg);
   buff_appendf(pB, "\"}");
}

/* Read newest count lines from the log as JSON array (newest first). */
static int api_log(int nFd, long nLimit) {
   if (nLimit <= 0) nLimit = 500;

   FILE *f = fopen(g_pLogFile, "r");
   if (!f) {
      send_head(nFd, "200 OK", "application/json", NULL, 2);
      dprintf(nFd, "[]");
      return 0;
   }

   char szLine[2048];
   LogEntry aEntries[512];
   int nCount = 0;

   /* Only keep the last nLimit lines: reuse a ring of entries. */
   while (fgets(szLine, sizeof(szLine), f)) {
      LogEntry e;
      parse_log_line(szLine, &e);
      if (!e.bValid) continue;
      if (nLimit > 0 && (long)nCount == nLimit) {
         /* shift ring: drop oldest */
         memmove(aEntries, aEntries + 1, sizeof(aEntries[0]) * (nLimit - 1));
         nCount = (int)nLimit - 1;
      }
      if (nCount < 512) aEntries[nCount++] = e;
   }
   fclose(f);

   Buffer body;
   buff_init(&body);
   buff_appendf(&body, "[");
   for (int i = nCount - 1; i >= 0; i--) {
      LogEntry *pe = &aEntries[i];
      if (i != nCount - 1) buff_appendf(&body, ",");
      build_entry_json(&body, pe);
   }
   buff_appendf(&body, "]");

   send_head(nFd, "200 OK", "application/json", NULL, (long)body.nLen);
   if (body.pData) dprintf(nFd, "%s", body.pData);
   buff_free(&body);
   return 0;
}

/* ---- Server-Sent Events live stream ---- */

static void get_file_size_offset(FILE *f, long *pnSize) {
   long nCur = ftell(f);
   fseek(f, 0L, SEEK_END);
   *pnSize = ftell(f);
   fseek(f, nCur, SEEK_SET);
}

static int api_stream(int nFd) {
   /* Initial snapshot: newest N lines as one event. */
   FILE *f = fopen(g_pLogFile, "r");
   long nStartPos = 0;
   Buffer body;
   buff_init(&body);

   if (f) {
      long nFileSize;
      get_file_size_offset(f, &nFileSize);
      nStartPos = nFileSize;

      /* collect last LINE_BUF bytes or whole file, whichever is smaller */
      long nWindow = nFileSize;
      if (nWindow > 256 * 1024) nWindow = 256 * 1024;
      fseek(f, nFileSize - nWindow, SEEK_SET);
      char *pChunk = malloc((size_t)nWindow + 2);
      if (pChunk) {
         size_t nGot = fread(pChunk, 1, (size_t)nWindow, f);
         pChunk[nGot] = '\0';
         buff_appendf(&body, "[");
         /* walk lines of the window, take the last nKeepMax lines */
         int nKeepMax = (g_pLogFile == g_szDemoLog) ? DEMO_INITIAL_LINES : SSE_INITIAL_LINES;
         char (*aKeep)[2048] = malloc(sizeof(char[2048]) * (size_t)nKeepMax);
         int nKeep = 0;
         if (aKeep) {
            char *pLine = strtok(pChunk, "\n");
            while (pLine) {
               if (nKeep == nKeepMax) {
                  memmove(aKeep, aKeep + 1, sizeof(aKeep[0]) * (size_t)(nKeep - 1));
                  nKeep--;
               }
               snprintf(aKeep[nKeep], sizeof(aKeep[0]), "%s", pLine);
               nKeep++;
               pLine = strtok(NULL, "\n");
            }
            for (int i = 0; i < nKeep; i++) {
               LogEntry e;
               parse_log_line(aKeep[i], &e);
               if (!e.bValid) continue;
               if (i != 0) buff_appendf(&body, ",");
               build_entry_json(&body, &e);
            }
            free(aKeep);
         }
         buff_appendf(&body, "]");
         free(pChunk);
      }
   }

   /* Send SSE headers and the initial event. */
   dprintf(nFd,
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/event-stream\r\n"
      "Cache-Control: no-cache\r\n"
      "Connection: keep-alive\r\n"
      "\r\n");
   if (body.nLen) {
      dprintf(nFd, "data: %s\n\n", body.pData);
   } else {
      dprintf(nFd, "data: []\n\n");
   }
   fflush(NULL);
   buff_free(&body);
   if (f) { fclose(f); f = NULL; }

   /* Poll the log for appended content. */
   for (;;) {
      usleep(SSE_POLL_MS * 1000);

      f = fopen(g_pLogFile, "r");
      if (!f) continue;
      long nFileSize;
      get_file_size_offset(f, &nFileSize);

      if (nFileSize < nStartPos) {
         /* File rotated: start over from the beginning. */
         nStartPos = 0;
      }
      if (nFileSize > nStartPos) {
         fseek(f, nStartPos, SEEK_SET);
         char szBlock[4096];
         long nLastNewline = nStartPos;
         long nDataEnd = nStartPos;
         while (nDataEnd < nFileSize) {
            size_t nWant = sizeof(szBlock) - 1;
            if ((long)nWant > nFileSize - nDataEnd) nWant = (size_t)(nFileSize - nDataEnd);
            size_t nGot = fread(szBlock, 1, nWant, f);
            if (nGot == 0) break;
            nDataEnd += (long)nGot;
            char *pB = szBlock;
            char *pEnd = szBlock + nGot;
            size_t nConsumed = 0;
            char *pNL;
            while ((pNL = memchr(pB, '\n', (size_t)(pEnd - pB))) != NULL) {
               size_t nLineLen = (size_t)(pNL - pB);
               char szLine[2048];
               if (nLineLen >= sizeof(szLine)) nLineLen = sizeof(szLine) - 1;
               memcpy(szLine, pB, nLineLen);
               szLine[nLineLen] = '\0';
               LogEntry e;
               parse_log_line(szLine, &e);
               if (e.bValid) {
                  Buffer j;
                  buff_init(&j);
                  build_entry_json(&j, &e);
                  dprintf(nFd, "data: %s\n\n", j.pData);
                  buff_free(&j);
               }
               size_t nSkip = (size_t)(pNL - pB) + 1;
               nConsumed += nSkip;
               pB = pNL + 1;
            }
            nLastNewline += nConsumed;
         }
         fflush(NULL);
         /* Back up to the last complete line so a trailing partial line is
          * re-read on the next poll. */
         nStartPos = nLastNewline;
      }
      fclose(f);
   }
   return 0;
}

/* ---- Static file serving ---- */

static const char *mime_for(const char *pPath) {
   const char *pDot = strrchr(pPath, '.');
   if (!pDot) return "application/octet-stream";
   if (strcmp(pDot, ".html") == 0) return "text/html";
   if (strcmp(pDot, ".css") == 0) return "text/css";
   if (strcmp(pDot, ".js") == 0) return "application/javascript";
   if (strcmp(pDot, ".ico") == 0) return "image/x-icon";
   if (strcmp(pDot, ".json") == 0) return "application/json";
   return "application/octet-stream";
}

static void serve_file(int nFd, const char *pUrlPath) {
   /* Safe to index.html. */
   char szRel[512];
   if (strcmp(pUrlPath, "/") == 0 || strcmp(pUrlPath, "/index.html") == 0) {
      snprintf(szRel, sizeof(szRel), "/index.html");
   } else {
      snprintf(szRel, sizeof(szRel), "%s", pUrlPath);
   }

   if (strstr(szRel, "..")) {
      send_head(nFd, "400 Bad Request", "text/plain", NULL, 15);
      dprintf(nFd, "Bad request");
      return;
   }

   char szFull[1024];
   snprintf(szFull, sizeof(szFull), "%s%s", g_szWebRoot, szRel);

   FILE *f = fopen(szFull, "rb");
   if (!f) {
      send_head(nFd, "404 Not Found", "text/plain", NULL, 11);
      dprintf(nFd, "Not found");
      return;
   }
   fseek(f, 0L, SEEK_END);
   long nSize = ftell(f);
   fseek(f, 0L, SEEK_SET);

   send_head(nFd, "200 OK", mime_for(szRel), NULL, nSize);
   char szBlock[8192];
   size_t nGot;
   while ((nGot = fread(szBlock, 1, sizeof(szBlock), f)) > 0) {
      if (write(nFd, szBlock, nGot) < 0) break;
   }
   fclose(f);
}

/* ------------------------------------------------------------------ */
/* Per-connection handling                                             */
/* ------------------------------------------------------------------ */

static void api_status(int nFd) {
   int online = 0;
   long pid = -1;
   char version[32] = "";
   FILE *f = fopen(SYSLOGD_STATUS_FILE, "r");
   if (f) {
      char line[160];
      while (fgets(line, sizeof(line), f)) {
         if (strncmp(line, "version=", 8) == 0) {
            snprintf(version, sizeof(version), "%s", line + 8);
            char *nl = strchr(version, '\n');
            if (nl) *nl = '\0';
         } else if (strncmp(line, "pid=", 4) == 0) {
            pid = atol(line + 4);
         }
      }
      fclose(f);
   }
   if (pid > 0) {
      if (kill((pid_t)pid, 0) == 0 || errno == EPERM) online = 1;
   }
   char szBody[256];
   snprintf(szBody, sizeof(szBody),
            "{\"name\":\"syslogd\",\"online\":%s,\"version\":\"%s\",\"pid\":%ld}",
            online ? "true" : "false",
            (version[0] ? version : "-"), pid);
   send_head(nFd, "200 OK", "application/json", NULL, (long)strlen(szBody));
   dprintf(nFd, "%s", szBody);
   shutdown(nFd, SHUT_WR);
   close(nFd);
}

static void handle_connection(int nFd) {
   char szReq[8192];
   int nLen = read_request(nFd, szReq, sizeof(szReq));
   if (nLen <= 0) {
      close(nFd);
      return;
   }

   char szMethod[16], szPath[1024], szQuery[512];
   parse_request(szReq, szMethod, sizeof(szMethod), szPath, sizeof(szPath), szQuery, sizeof(szQuery));

   /* Demo mode: /demo serves the same SPA but every API call reads the bundled
    * sample log (webroot/sample-500.log) instead of the live log file. The
    * prefix is stripped so the routing below is shared. */
   int nDemo = (strcmp(szPath, "/demo") == 0) || (strncmp(szPath, "/demo/", 6) == 0);
   if (nDemo) {
      snprintf(g_szDemoLog, sizeof(g_szDemoLog), "%s/%s", g_szWebRoot, DEMO_LOGFILE);
      g_pLogFile = g_szDemoLog;
      memmove(szPath, szPath + 5, strlen(szPath + 5) + 1);
      if (szPath[0] == '\0') strcpy(szPath, "/");
   } else {
      g_pLogFile = LOGFILE;
   }

   /* Only GET is supported. */
   if (strcmp(szMethod, "GET") != 0) {
      send_head(nFd, "405 Method Not Allowed", "text/plain", NULL, 18);
      dprintf(nFd, "Method not allowed");
      shutdown(nFd, SHUT_WR);
      close(nFd);
      return;
   }

   if (strcmp(szPath, "/api/log") == 0) {
      long nLimit = query_int(szQuery, "limit", 500);
      api_log(nFd, nLimit);
      shutdown(nFd, SHUT_WR);
      close(nFd);
      return;
   }

   if (strcmp(szPath, "/api/stream") == 0) {
      /* SSE: keeps the connection open (handle_connection returns after). */
      api_stream(nFd);
      shutdown(nFd, SHUT_WR);
      close(nFd);
      return;
   }

   if (strcmp(szPath, "/api/version") == 0) {
      char szBody[128];
      snprintf(szBody, sizeof(szBody),
               "{\"name\":\"syslogd_web\",\"version\":\"%s\"}", SYSLOGD_VERSION);
      send_head(nFd, "200 OK", "application/json", NULL, (long)strlen(szBody));
      dprintf(nFd, "%s", szBody);
      shutdown(nFd, SHUT_WR);
      close(nFd);
      return;
   }

   if (strcmp(szPath, "/api/status") == 0) {
      api_status(nFd);
      return;
   }

   if (strcmp(szPath, "/favicon.ico") == 0) {
      send_head(nFd, "204 No Content", "image/x-icon", NULL, 0);
      shutdown(nFd, SHUT_WR);
      close(nFd);
      return;
   }

   serve_file(nFd, szPath);
   shutdown(nFd, SHUT_WR);
   close(nFd);
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

static void print_usage(const char *pProg) {
   printf("Usage: %s [-w webroot]\n", pProg);
   printf("  -w <dir>  Directory with index.html/style.css/app.js (default: web)\n");
   printf("  -V        Print version and exit\n");
}

int main(int argc, char *argv[]) {
   int nOpt;
   while ((nOpt = getopt(argc, argv, "w:hV")) != -1) {
      switch (nOpt) {
         case 'w': g_szWebRoot = optarg; break;
         case 'V':
            printf("syslogd_web %s\n", SYSLOGD_VERSION);
            return 0;
         case 'h':
            print_usage(argv[0]);
            return 0;
         default:
            print_usage(argv[0]);
            return 1;
      }
   }

   int nOptVal = 1;
   int nDualStack = 0;

   int nServerFd = socket(AF_INET6, SOCK_STREAM, 0);
   if (nServerFd >= 0) {
      setsockopt(nServerFd, SOL_SOCKET, SO_REUSEADDR, &nOptVal, sizeof(nOptVal));
      setsockopt(nServerFd, IPPROTO_IPV6, IPV6_V6ONLY, &nDualStack, sizeof(nDualStack));

      struct sockaddr_in6 addr6;
      addr6.sin6_family = AF_INET6;
      addr6.sin6_addr = in6addr_any;
      addr6.sin6_port = htons(PORT);

      if (bind(nServerFd, (struct sockaddr *)&addr6, sizeof(addr6)) < 0) {
         perror("bind");
         exit(1);
      }
   } else {
      nServerFd = socket(AF_INET, SOCK_STREAM, 0);
      if (nServerFd < 0) {
         perror("socket");
         exit(1);
      }
      setsockopt(nServerFd, SOL_SOCKET, SO_REUSEADDR, &nOptVal, sizeof(nOptVal));

      struct sockaddr_in addr;
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = INADDR_ANY;
      addr.sin_port = htons(PORT);

      if (bind(nServerFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
         perror("bind");
         exit(1);
      }
   }

   if (listen(nServerFd, 10) < 0) {
      perror("listen");
      exit(1);
   }

   printf("Syslog web server running on http://localhost:%d/ (web root: %s)\n", PORT, g_szWebRoot);
   fflush(stdout);

   /* Fork per connection so long-lived SSE clients do not block the accept loop. */
   signal(SIGCHLD, SIG_IGN);

   for (;;) {
      int nClientFd = accept(nServerFd, NULL, NULL);
      if (nClientFd < 0) {
         if (errno == EINTR) continue;
         continue;
      }

      pid_t pid = fork();
      if (pid == 0) {
         close(nServerFd);
         handle_connection(nClientFd);
         _exit(0);
      }
      close(nClientFd);
   }

   close(nServerFd);
   return 0;
}
