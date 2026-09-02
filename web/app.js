(function () {
  "use strict";

  /* When the viewer is served under /demo every API call is prefixed with it,
   * so the server routes to the bundled sample log instead of the live file. */
  var API_BASE = (location.pathname.indexOf("/demo") === 0) ? "/demo" : "";

  var MAX_ROWS = 4000;
  var HIGHLIGHT_MS = 1000;
  var BAR_HIGHLIGHT_MS = 5000;
  var entries = [];
  var paused = false;
  var es = null;
  var retryTimer = null;
  var sortKey = null;
  var sortAsc = true;

  var rowsEl = document.getElementById("logRows");
  var searchEl = document.getElementById("searchText");
  var facilityEl = document.getElementById("mselFacility");
  var severityEl = document.getElementById("mselSeverity");
  var countEl = document.getElementById("entryCount");
  var connEl = document.getElementById("connStatus");
  var pauseBtn = document.getElementById("btnPause");
  var clearBtn = document.getElementById("btnClear");
  var reloadBtn = document.getElementById("btnReload");
  var pinEl = document.getElementById("chkPin");
  var pageSizeEl = document.getElementById("pageSize");
  var pagerTop = document.getElementById("pagerTop");
  var pagerBottom = document.getElementById("pagerBottom");
  var themeSelect = document.getElementById("themeSelect");
  var versionBadge = document.getElementById("versionBadge");
  var syslogdBadge = document.getElementById("syslogdBadge");
  var tsTzLabel = document.getElementById("tsTzLabel");

  var FACILITIES = ["kern","user","mail","daemon","auth","syslog","lpr","news","uucp","cron","authpriv","ftp","ntp","audit","alert","at","local0","local1","local2","local3","local4","local5","local6","local7"];
  var SEVERITIES = ["emergency","alert","critical","error","warning","notice","info","debug"];

  /* Warning and more severe: color the row text like the severity badge.
   * Debug is the least severe: dim the text. */
  var SEV_TEXT = {
    emergency: "sev-text-emergency",
    alert: "sev-text-alert",
    critical: "sev-text-critical",
    error: "sev-text-error",
    warning: "sev-text-warning"
  };

  /* Multi-select widget. Selection lives in a Set; an empty set = show all. */
  var apiMultiselect = [];
  var facilitySet = new Set();
  var severitySet = new Set();

  var pageSize = 25;    // 0 = "All"
  var currentPage = 1;
  var totalPages = 1;
  var totalShown = 0;

  function closeMultiSelects(except) {
    for (var i = 0; i < apiMultiselect.length; i++) {
      if (apiMultiselect[i] !== except) apiMultiselect[i].style.display = "none";
    }
  }

  function buildMultiSelect(wrap, label, options, selSet, onChange) {
    var btn = document.createElement("button");
    btn.type = "button";
    btn.className = "msel-btn";
    wrap.appendChild(btn);

    var dd = document.createElement("div");
    dd.className = "msel-dd";
    dd.style.display = "none";
    wrap.appendChild(dd);

    function labelText() {
      var all = (selSet.size === 0 || selSet.size === options.length);
      if (all) btn.textContent = label + ": All";
      else btn.textContent = label + ": " + Array.from(selSet).join(", ");
      if (all) btn.classList.remove("filtered");
      else btn.classList.add("filtered");
    }

    function refreshChecks() {
      var cbs = dd.querySelectorAll("input[type=checkbox]");
      for (var i = 0; i < cbs.length; i++) {
        var key = cbs[i].getAttribute("data-key");
        cbs[i].checked = selSet.has(key);
      }
    }

    options.forEach(function (v) {
      var lab = document.createElement("label");
      lab.className = "msel-item";
      var cb = document.createElement("input");
      cb.type = "checkbox";
      cb.setAttribute("data-key", v);
      cb.checked = false;
      cb.addEventListener("change", function () {
        if (cb.checked) selSet.add(v);
        else selSet.delete(v);
        labelText();
        onChange();
      });
      var sp = document.createElement("span");
      sp.textContent = v;
      lab.appendChild(cb);
      lab.appendChild(sp);
      dd.appendChild(lab);
    });

    var clear = document.createElement("button");
    clear.type = "button";
    clear.className = "msel-clear";
    clear.textContent = "Clear";
    clear.addEventListener("click", function (e) {
      e.stopPropagation();
      selSet.clear();
      refreshChecks();
      labelText();
      onChange();
      closeMultiSelects(dd);
    });
    dd.appendChild(clear);

    btn.addEventListener("click", function (e) {
      e.stopPropagation();
      closeMultiSelects(dd);
      dd.style.display = (dd.style.display === "none") ? "block" : "none";
    });

    labelText();
    apiMultiselect.push(dd);
  }

  function badgeClass(prefix, name) {
    var base = prefix + "-" + name;
    return name ? base : "unknown";
  }

  function esc(s) {
    return String(s == null ? "" : s).replace(/[&<>"']/g, function (c) {
      return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c];
    });
  }

  function greyRgb() {
    var th = document.body.getAttribute("data-theme");
    var light = th === "light" ||
      (th === "system" && window.matchMedia("(prefers-color-scheme: light)").matches);
    return light ? "100,116,139" : "71,85,105";
  }

  function buildRow(e, id) {
    var tr = document.createElement("tr");
    tr.dataset.facility = e.facility || "";
    tr.dataset.severity = e.severity || "";
    tr.dataset.text = ((e.host || "") + " " + (e.app || "") + " " + (e.proc || "") + " " + (e.msgid || "") + " " + (e.msg || "")).toLowerCase();
    var rowClass = SEV_TEXT[e.severity] || (e.severity === "debug" ? "sev-text-dim" : "");
    if (rowClass) tr.className = rowClass;
    var sd = (e.sd && e.sd !== "-") ? "<span class='sd'>" + esc(e.sd) + "</span>" : "";
    tr.innerHTML =
      "<td class='col-time'>" + esc(e.tsLocal != null ? e.tsLocal : e.ts) + "</td>" +
      "<td class='col-host'>" + esc(e.host) + "</td>" +
      "<td class='col-severity'><span class='badge " + badgeClass("sev", e.severity) + "'>" + esc(e.severity || "unknown") + "</span></td>" +
      "<td class='col-msgid'>" + esc(e.msgid) + "</td>" +
      "<td class='col-msg'>" + esc(e.msg) + "</td>" +
      "<td class='col-data'>" + sd + "</td>" +
      "<td class='col-app'>" + esc(e.app) + "</td>" +
      "<td class='col-facility'><span class='badge " + badgeClass("fac", e.facility) + "'>" + esc(e.facility || "unknown") + "</span></td>" +
      "<td class='col-proc'>" + esc(e.proc) + "</td>";
    if (pinEl.checked && e.arrive != null) {
      var age = Date.now() - e.arrive;
      if (age >= 0 && age < HIGHLIGHT_MS) {
        var lineAlpha = Math.max(0, 1 - age / HIGHLIGHT_MS).toFixed(3);
        tr.style.backgroundColor = "rgba(" + greyRgb() + "," + lineAlpha + ")";
      }
      if (age >= 0 && age < BAR_HIGHLIGHT_MS) {
        tr.firstElementChild.style.boxShadow = "inset 3px 0 0 rgba(" + greyRgb() + ",1)";
      }
    }
    return tr;
  }

  function pinToNewest() {
    if (pinEl.checked && rowsEl.parentElement) {
      rowsEl.parentElement.scrollTop = rowsEl.parentElement.scrollHeight;
    }
  }

  function matches(e) {
    if (facilitySet.size && !facilitySet.has(e.facility)) return false;
    if (severitySet.size && !severitySet.has(e.severity)) return false;
    var q = searchEl.value.trim().toLowerCase();
    if (q) {
      var hay = ((e.host || "") + " " + (e.app || "") + " " + (e.proc || "") + " " + (e.msgid || "") + " " + (e.sd || "") + " " + (e.msg || "")).toLowerCase();
      if (hay.indexOf(q) === -1) return false;
    }
    return true;
  }

  function compareEntries(a, b) {
    var v = 0;
    if (sortKey === "ts") {
      v = a.ts < b.ts ? -1 : (a.ts > b.ts ? 1 : 0);
    } else {
      var ka = String(a[sortKey] || "").toLowerCase();
      var kb = String(b[sortKey] || "").toLowerCase();
      v = ka < kb ? -1 : (ka > kb ? 1 : 0);
    }
    return sortAsc ? v : -v;
  }

  function goToPage(n) {
    if (n < 1) n = 1;
    if (n > totalPages) n = totalPages;
    if (n !== currentPage) {
      currentPage = n;
      if (pinEl) {
        pinEl.checked = (n === 1 && !sortKey);
      }
      render();
    }
  }

  function filterChanged() { currentPage = 1; render(); }

  function middleItems(P, c) {
    var S = 4;
    var out = [];
    if (P <= S) {
      for (var p = 1; p <= P; p++) out.push(p);
      return out;
    }
    var showLeft = (c - 1) > 2;
    var showRight = (P - c) > 2;
    var budget = S - (showLeft ? 1 : 0) - (showRight ? 1 : 0);
    var lo = c - Math.floor((budget - 1) / 2);
    var hi = c + Math.ceil((budget - 1) / 2);
    if (lo < 1) { hi += 1 - lo; lo = 1; }
    if (hi > P) { lo -= hi - P; hi = P; }
    if (lo < 1) lo = 1;
    if (showLeft) out.push("...");
    for (var p = lo; p <= hi; p++) out.push(p);
    if (showRight) out.push("...");
    return out;
  }

  function renderPagerInto(container) {
    container.innerHTML = "";
    container.style.display = "";

    var label = document.createElement("span");
    label.className = "page-current";
    label.textContent = "Page " + currentPage + " of " + totalPages;
    container.appendChild(label);

    function mkBtn(label, title, cb, disabled, cls) {
      var b = document.createElement("button");
      b.type = "button";
      b.className = "page-btn" + (cls ? " " + cls : "");
      b.textContent = label;
      b.title = title;
      if (disabled) { b.disabled = true; b.className += " disabled"; }
      else b.addEventListener("click", cb);
      return b;
    }

    var group = document.createElement("div");
    group.className = "pager-group";
    container.appendChild(group);

    group.appendChild(mkBtn("|<", "First page", function () { goToPage(1); }, currentPage === 1));
    group.appendChild(mkBtn("<", "Previous", function () { goToPage(currentPage - 1); }, currentPage === 1));

    var mid = document.createElement("div");
    mid.className = "pager-mid";
    group.appendChild(mid);

    var items = middleItems(totalPages, currentPage);
    for (var i = 0; i < items.length; i++) {
      var it = items[i];
      if (it === "...") {
        var s = document.createElement("span");
        s.className = "page-btn page-ellipsis";
        s.textContent = "…";
        mid.appendChild(s);
      } else {
        var pn = Number(it);
        mid.appendChild(mkBtn(String(pn), "Page " + pn,
          (function (p) { return function () { goToPage(p); }; })(pn),
          pn === currentPage, pn === currentPage ? "current" : ""));
      }
    }
    for (var k = items.length; k < 4; k++) {
      var spacer = document.createElement("span");
      spacer.className = "page-btn page-slot";
      mid.appendChild(spacer);
    }

    group.appendChild(mkBtn(">", "Next", function () { goToPage(currentPage + 1); }, currentPage === totalPages));
    group.appendChild(mkBtn(">|", "Last page", function () { goToPage(totalPages); }, currentPage === totalPages));
  }

  function render() {
    var shown = entries.slice();
    if (sortKey) shown.sort(compareEntries);
    else shown.reverse();
    var filtered = [];
    for (var i = 0; i < shown.length; i++) {
      if (matches(shown[i])) filtered.push(shown[i]);
    }
    totalShown = filtered.length;
    totalPages = (pageSize === 0) ? 1 : Math.max(1, Math.ceil(totalShown / pageSize));
    if (currentPage > totalPages) currentPage = totalPages;
    var startIdx = (pageSize === 0) ? 0 : (currentPage - 1) * pageSize;
    var endIdx = (pageSize === 0) ? totalShown : Math.min(totalShown, startIdx + pageSize);

    rowsEl.innerHTML = "";
    var count = 0;
    for (var i = startIdx; i < endIdx; i++) {
      rowsEl.appendChild(buildRow(filtered[i], i));
      count++;
    }
    countEl.textContent = totalShown + " entry" + (totalShown === 1 ? "" : "s") + " (page " + currentPage + " of " + totalPages + ")";
    if (!count) {
      var tr = document.createElement("tr");
      tr.className = "empty";
      tr.innerHTML = "<td colspan='9'>No matching entries</td>";
      rowsEl.appendChild(tr);
    }
    renderPagerInto(pagerTop);
    renderPagerInto(pagerBottom);
    pinToNewest();
  }

  function normalizeEntry(e) {
    var tsLocal = e.ts;
    var d = new Date(e.ts);
    if (!isNaN(d.getTime())) {
      var p = function (n) { return (n < 10 ? "0" : "") + n; };
      var ms = d.getMilliseconds();
      tsLocal = d.getFullYear() + "-" + p(d.getMonth() + 1) + "-" + p(d.getDate()) +
        " " + p(d.getHours()) + ":" + p(d.getMinutes()) + ":" + p(d.getSeconds()) +
        "." + (ms < 10 ? "00" : ms < 100 ? "0" : "") + ms;
    }
    return { ts: e.ts, tsLocal: tsLocal, host: e.host, app: e.app, proc: e.proc, msgid: e.msgid, facility: e.facility, severity: e.severity, sd: e.sd, msg: e.msg };
  }

  function addEntry(e) {
    var en = normalizeEntry(e);
    en.arrive = Date.now();
    entries.push(en);
    if (entries.length > MAX_ROWS) {
      entries.splice(0, entries.length - MAX_ROWS);
    }
    if (!paused) render();
    else updateCount();
  }

  function updateCount() {
    var c = 0;
    for (var i = entries.length - 1; i >= 0; i--) if (matches(entries[i])) c++;
    totalShown = c;
    totalPages = (pageSize === 0) ? 1 : Math.max(1, Math.ceil(c / pageSize));
    if (currentPage > totalPages) currentPage = totalPages;
    countEl.textContent = c + " entry" + (c === 1 ? "" : "s") + " (page " + currentPage + " of " + totalPages + ")";
    renderPagerInto(pagerTop);
    renderPagerInto(pagerBottom);
  }

  function setConn(state) {
    if (state === "live") { connEl.textContent = "● live"; connEl.className = "status live"; }
    else { connEl.textContent = "● disconnected"; connEl.className = "status off"; }
  }

  function setPaused(p) {
    if (p) { connEl.textContent = "● paused"; connEl.className = "status paused"; }
    else if (es) setConn("live");
    else setConn("off");
  }

  function openStream() {
    if (es) es.close();
    es = new EventSource(API_BASE + "/api/stream");
    es.onopen = function () { setConn("live"); };
    es.onerror = function () {
      setConn("off");
      es.close();
      es = null;
      retryTimer = setTimeout(openStream, 3000);
    };
    es.onmessage = function (ev) {
      try {
        var e = JSON.parse(ev.data);
        if (Array.isArray(e)) { e.forEach(addEntry); render(); }
        else addEntry(e);
      } catch (err) { /* ignore malformed */ }
    };
  }

  searchEl.addEventListener("input", filterChanged);

  function updateHeaderIndicators() {
    var ths = document.querySelectorAll("thead th.sortable");
    for (var i = 0; i < ths.length; i++) {
      var key = ths[i].getAttribute("data-key");
      ths[i].className = ths[i].className.replace(/\bsorted\b|\basc\b|\bdesc\b/g, "").trim();
      if (key === sortKey) ths[i].className += " sorted " + (sortAsc ? "asc" : "desc");
    }
  }

  var ths = document.querySelectorAll("thead th.sortable");
  for (var i = 0; i < ths.length; i++) {
    ths[i].addEventListener("click", function () {
      var key = this.getAttribute("data-key");
      if (sortKey === key) sortAsc = !sortAsc;
      else { sortKey = key; sortAsc = true; }
      if (pinEl) pinEl.checked = false;
      currentPage = 1;
      updateHeaderIndicators();
      render();
    });
  }

  pinEl.addEventListener("change", function () {
    if (pinEl.checked) {
      sortKey = null;
      sortAsc = true;
      currentPage = 1;
      updateHeaderIndicators();
      render();
    }
  });

  pauseBtn.addEventListener("click", function () {
    paused = !paused;
    pauseBtn.textContent = paused ? "Resume" : "Pause";
    setPaused(paused);
    if (!paused) render();
  });

  reloadBtn.addEventListener("click", function () {
    fetch(API_BASE + "/api/log?limit=" + MAX_ROWS)
      .then(function (r) {
        if (!r.ok) throw new Error("reload failed");
        return r.json();
      })
      .then(function (data) {
        entries = [];
        if (Array.isArray(data)) {
          /* /api/log returns newest-first; push reversed so entries are
           * oldest->newest, matching the live/SSE path and pin-to-newest. */
          for (var i = data.length - 1; i >= 0; i--) {
            entries.push(normalizeEntry(data[i]));
          }
          if (entries.length > MAX_ROWS) {
            entries.splice(0, entries.length - MAX_ROWS);
          }
        }
        sortKey = null;
        sortAsc = true;
        currentPage = 1;
        if (pinEl) pinEl.checked = true;
        updateHeaderIndicators();
        render();
      })
      .catch(function () { /* ignore */ });
  });

  clearBtn.addEventListener("click", function () {
    entries = [];
    sortKey = null;
    sortAsc = true;
    currentPage = 1;
    if (pinEl) pinEl.checked = true;
    updateHeaderIndicators();
    render();
  });

  buildMultiSelect(severityEl, "Severity", SEVERITIES, severitySet, filterChanged);
  buildMultiSelect(facilityEl, "Facility", FACILITIES, facilitySet, filterChanged);

  pageSizeEl.addEventListener("change", function () {
    pageSize = (pageSizeEl.value === "all") ? 0 : (parseInt(pageSizeEl.value, 10) || 100);
    currentPage = 1;
    render();
  });

  function applyTheme(v) {
    document.body.setAttribute("data-theme", v);
    try { localStorage.setItem("syslog-theme", v); } catch (e) { /* ignore */ }
  }
  var savedTheme = "system";
  try { savedTheme = localStorage.getItem("syslog-theme") || "system"; } catch (e) { /* ignore */ }
  themeSelect.value = savedTheme;
  applyTheme(savedTheme);
  themeSelect.addEventListener("change", function () { applyTheme(themeSelect.value); });

  /* Show the web viewer version next to the title. */
  fetch(API_BASE + "/api/version")
    .then(function (r) { return r.json(); })
    .then(function (v) {
      if (v && v.version) versionBadge.textContent = "v" + v.version;
    })
    .catch(function () { /* ignore */ });

  /* Apply the server-configured entry cap (SYSLOGD_MAX_ENTRIES). */
  fetch(API_BASE + "/api/config")
    .then(function (r) { return r.json(); })
    .then(function (c) {
      if (c && c.maxRows > 0) MAX_ROWS = c.maxRows;
    })
    .catch(function () { /* ignore */ });

  /* Show the browser time zone id next to the Timestamp column header. */
  var tz = "local";
  try { tz = Intl.DateTimeFormat().resolvedOptions().timeZone || "local"; } catch (e) { /* ignore */ }
  if (tsTzLabel) tsTzLabel.textContent = "(" + tz + ")";

  /* Show the syslogd server status + version. */
  function refreshSyslogdStatus() {
    fetch(API_BASE + "/api/status")
      .then(function (r) { return r.json(); })
      .then(function (s) {
        if (s && s.online) {
          syslogdBadge.textContent = "● syslogd online";
          syslogdBadge.className = "syslogd-badge online";
        } else {
          syslogdBadge.textContent = "● syslogd offline";
          syslogdBadge.className = "syslogd-badge offline";
        }
      })
      .catch(function () {
        syslogdBadge.textContent = "● syslogd ?";
        syslogdBadge.className = "syslogd-badge offline";
      });
  }
  refreshSyslogdStatus();
  setInterval(refreshSyslogdStatus, 5000);

  /* Drive the continuous fade of freshly arrived rows while pinned. */
  function hasRecent() {
    if (!pinEl.checked) return false;
    var now = Date.now();
    for (var i = 0; i < entries.length; i++) {
      var e = entries[i];
      if (e.arrive != null && now - e.arrive < BAR_HIGHLIGHT_MS && matches(e)) return true;
    }
    return false;
  }
  setInterval(function () { if (hasRecent()) render(); }, 250);

  document.addEventListener("click", function () { closeMultiSelects(null); });

  updateHeaderIndicators();
  openStream();
  render();
})();
