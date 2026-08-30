(function () {
  "use strict";

  var MAX_ROWS = 2000;
  var entries = [];
  var paused = false;
  var es = null;
  var retryTimer = null;
  var sortKey = null;
  var sortAsc = true;

  var rowsEl = document.getElementById("logRows");
  var searchEl = document.getElementById("searchText");
  var facilityEl = document.getElementById("filterFacility");
  var severityEl = document.getElementById("filterSeverity");
  var countEl = document.getElementById("entryCount");
  var connEl = document.getElementById("connStatus");
  var pauseBtn = document.getElementById("btnPause");
  var clearBtn = document.getElementById("btnClear");
  var pinEl = document.getElementById("chkPin");

  var FACILITIES = ["kern","user","mail","daemon","auth","syslog","lpr","news","uucp","cron","authpriv","ftp","ntp","audit","alert","at","local0","local1","local2","local3","local4","local5","local6","local7"];
  var SEVERITIES = ["emergency","alert","critical","error","warning","notice","info","debug"];

  function badgeClass(prefix, name) {
    var base = prefix + "-" + name;
    return name ? base : "unknown";
  }

  function esc(s) {
    return String(s == null ? "" : s).replace(/[&<>"']/g, function (c) {
      return { "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" }[c];
    });
  }

  function buildRow(e, id) {
    var tr = document.createElement("tr");
    tr.dataset.facility = e.facility || "";
    tr.dataset.severity = e.severity || "";
    tr.dataset.text = ((e.host || "") + " " + (e.app || "") + " " + (e.proc || "") + " " + (e.msgid || "") + " " + (e.msg || "")).toLowerCase();
    var body = "";
    if (e.sd && e.sd !== "-") {
      body = "<span class='sd'>" + esc(e.sd) + "</span> ";
    }
    body += esc(e.msg);
    tr.innerHTML =
      "<td class='col-time'>" + esc(e.ts) + "</td>" +
      "<td class='col-host'>" + esc(e.host) + "</td>" +
      "<td class='col-facility'><span class='badge " + badgeClass("fac", e.facility) + "'>" + esc(e.facility || "unknown") + "</span></td>" +
      "<td class='col-severity'><span class='badge " + badgeClass("sev", e.severity) + "'>" + esc(e.severity || "unknown") + "</span></td>" +
      "<td class='col-msgid'>" + esc(e.msgid) + "</td>" +
      "<td class='col-app'>" + esc(e.app) + "</td>" +
      "<td class='col-proc'>" + esc(e.proc) + "</td>" +
      "<td class='col-msg'>" + body + "</td>";
    return tr;
  }

  function pinToNewest() {
    if (pinEl.checked && rowsEl.parentElement) {
      rowsEl.parentElement.scrollTop = rowsEl.parentElement.scrollHeight;
    }
  }

  function matches(e) {
    if (facilityEl.value && e.facility !== facilityEl.value) return false;
    if (severityEl.value && e.severity !== severityEl.value) return false;
    var q = searchEl.value.trim().toLowerCase();
    if (q) {
      var hay = ((e.host || "") + " " + (e.app || "") + " " + (e.proc || "") + " " + (e.msgid || "") + " " + (e.msg || "")).toLowerCase();
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

  function render() {
    rowsEl.innerHTML = "";
    var shown = entries.slice();
    if (sortKey) shown.sort(compareEntries);
    else shown.reverse();
    var count = 0;
    for (var i = 0; i < shown.length; i++) {
      if (matches(shown[i])) {
        rowsEl.appendChild(buildRow(shown[i], i));
        count++;
      }
    }
    countEl.textContent = count + " entry" + (count === 1 ? "" : "s") + " (showing " + count + " of " + entries.length + ")";
    if (!count) {
      var tr = document.createElement("tr");
      tr.className = "empty";
      tr.innerHTML = "<td colspan='8'>No matching entries</td>";
      rowsEl.appendChild(tr);
    }
    pinToNewest();
  }

  function addEntry(e) {
    entries.push({ ts: e.ts, host: e.host, app: e.app, proc: e.proc, msgid: e.msgid, facility: e.facility, severity: e.severity, sd: e.sd, msg: e.msg });
    if (entries.length > MAX_ROWS) {
      entries.splice(0, entries.length - MAX_ROWS);
    }
    if (!paused) render();
    else updateCount();
  }

  function updateCount() {
    var c = 0;
    for (var i = entries.length - 1; i >= 0; i--) if (matches(entries[i])) c++;
    countEl.textContent = c + " entry" + (c === 1 ? "" : "s") + " (showing " + c + " of " + entries.length + ")";
  }

  function setConn(state) {
    if (state === "live") { connEl.textContent = "● live"; connEl.className = "status live"; }
    else { connEl.textContent = "● disconnected"; connEl.className = "status off"; }
  }

  function openStream() {
    if (es) es.close();
    es = new EventSource("/api/stream");
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

  function addAllOption(sel) {
    var o = document.createElement("option");
    o.value = ""; o.textContent = "All";
    sel.appendChild(o);
  }

  function populateFilters() {
    addAllOption(facilityEl);
    FACILITIES.forEach(function (f) {
      var o = document.createElement("option");
      o.value = f; o.textContent = f;
      facilityEl.appendChild(o);
    });
    addAllOption(severityEl);
    SEVERITIES.forEach(function (s) {
      var o = document.createElement("option");
      o.value = s; o.textContent = s;
      severityEl.appendChild(o);
    });
  }

  searchEl.addEventListener("input", render);
  facilityEl.addEventListener("change", render);
  severityEl.addEventListener("change", render);

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
      updateHeaderIndicators();
      render();
    });
  }

  pinEl.addEventListener("change", function () {
    if (pinEl.checked) {
      sortKey = null;
      sortAsc = true;
      updateHeaderIndicators();
      render();
    }
  });

  pauseBtn.addEventListener("click", function () {
    paused = !paused;
    pauseBtn.textContent = paused ? "Resume" : "Pause";
    if (!paused) render();
  });

  clearBtn.addEventListener("click", function () {
    entries = [];
    sortKey = null;
    sortAsc = true;
    if (pinEl) pinEl.checked = true;
    updateHeaderIndicators();
    render();
  });

  populateFilters();
  updateHeaderIndicators();
  openStream();
  render();
})();
