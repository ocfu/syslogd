(function () {
  "use strict";

  var MAX_ROWS = 2000;
  var entries = [];
  var paused = false;
  var es = null;
  var retryTimer = null;

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
    tr.dataset.text = ((e.host || "") + " " + (e.msg || "")).toLowerCase();
    tr.innerHTML =
      "<td class='col-time'>" + esc(e.ts) + "</td>" +
      "<td class='col-host'>" + esc(e.host) + "</td>" +
      "<td class='col-msg'>" + esc(e.msg) + "</td>" +
      "<td class='col-severity'><span class='badge " + badgeClass("sev", e.severity) + "'>" + esc(e.severity || "unknown") + "</span></td>" +
      "<td class='col-facility'><span class='badge " + badgeClass("fac", e.facility) + "'>" + esc(e.facility || "unknown") + "</span></td>" +
      "<td class='col-port'>" + esc(e.port) + "</td>";
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
    if (q && (e.host || "").toLowerCase().indexOf(q) === -1 &&
               (e.msg || "").toLowerCase().indexOf(q) === -1) return false;
    return true;
  }

  function render() {
    rowsEl.innerHTML = "";
    var count = 0;
    for (var i = entries.length - 1; i >= 0; i--) {
      if (matches(entries[i])) {
        rowsEl.appendChild(buildRow(entries[i], i));
        count++;
      }
    }
    countEl.textContent = count + " entry" + (count === 1 ? "" : "s") + " (showing " + count + " of " + entries.length + ")";
    if (!count) {
      var tr = document.createElement("tr");
      tr.className = "empty";
      tr.innerHTML = "<td colspan='6'>No matching entries</td>";
      rowsEl.appendChild(tr);
    }
    pinToNewest();
  }

  function addEntry(e) {
    entries.push({ ts: e.ts, host: e.host, port: e.port, facility: e.facility, severity: e.severity, msg: e.msg });
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

  pauseBtn.addEventListener("click", function () {
    paused = !paused;
    pauseBtn.textContent = paused ? "Resume" : "Pause";
    if (!paused) render();
  });

  clearBtn.addEventListener("click", function () {
    entries = [];
    render();
  });

  populateFilters();
  openStream();
  render();
})();
