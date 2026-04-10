
/* ---------- ELEMENT REFERENCES ---------- */
const stkInput = document.getElementById("stkInput");
const agentInput = document.getElementById("agentInput");

const stkBtn = document.getElementById("stkBtn");
const agentBtn = document.getElementById("agentBtn");

const stkBtnText = document.getElementById("stkBtnText");
const agentBtnText = document.getElementById("agentBtnText");

const stkIcon = stkBtn.querySelector(".upload-icon");
const agentIcon = agentBtn.querySelector(".upload-icon");

const versionBox = document.getElementById("currentVersionBox");
const versionValue = document.getElementById("currentVersionValue");

const newVersion = document.getElementById("newVersion");
const platform = document.getElementById("platform");
const generateBtn = document.getElementById("generateBtn");

const btnLabel = generateBtn.querySelector(".btn-label");
const btnProgress = generateBtn.querySelector(".btn-progress");

const btnText = document.querySelector(".btn-text");
const btnPercent = document.querySelector(".btn-percent");

let jobRunning = false;

/* ---------- VALIDATION TAB ELEMENTS (may be null if tab not in DOM) ---------- */
const validationStkInput = document.getElementById("validationStkInput");
const validationStkBtn = document.getElementById("validationStkBtn");
const validationStkBtnText = document.getElementById("validationStkBtnText");
const clearValidationStkBtn = document.getElementById("clearValidationStkBtn");
const validateBtn = document.getElementById("validateBtn");
const switchIp = document.getElementById("switchIp");
const switchUsername = document.getElementById("switchUsername");
const switchPassword = document.getElementById("switchPassword");
const switchPasswordToggle = document.getElementById("switchPasswordToggle");
const expectedAppMgrVersion = document.getElementById("expectedAppMgrVersion");
const validationResult = document.getElementById("validationResult");
const validationPlatformValue = document.getElementById("validationPlatformValue");
const validationImageVersionValue = document.getElementById("validationImageVersionValue");
const validationFormCard = document.getElementById("validationFormCard");
const appContainer = document.querySelector(".app");
const validationTerminalWrap = document.getElementById("validationTerminalWrap");
const validationTerminal = document.getElementById("validationTerminal");
const validationBackBtn = document.getElementById("validationBackBtn");

let validationDetectedPlatform = "";  // set from STK filename when file selected (e.g. M4350)

const VALIDATION_PREVIEW_EMPTY = "Select an .stk file to preview";
const VALIDATION_PLATFORM_UNKNOWN = "Not detected from this filename";
const VALIDATION_VERSION_UNKNOWN = "No version found in filename";

const VALIDATION_RESULT_MARKER = "\n---RESULT---\n";

/**
 * Convert ANSI escape codes (e.g. from Ansible) to HTML for colored terminal output.
 * Escapes HTML first, then replaces SGR codes with <span> styles.
 */
function ansiToHtml(text) {
  if (text == null || text === "") return "";
  const esc = (s) => String(s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;")
    .replace(/"/g, "&quot;");
  const s = esc(text);
  const sgr = (codes) => {
    const n = (codes || "0").split(";").map((c) => parseInt(c, 10) || 0);
    const styles = [];
    for (const c of n) {
      if (c === 0) return "color:inherit;font-weight:normal;";
      if (c === 1) styles.push("font-weight:bold");
      else if (c === 22) styles.push("font-weight:normal");
      else if (c === 31) styles.push("color:#f44336");
      else if (c === 32) styles.push("color:#4caf50");
      else if (c === 33) styles.push("color:#ffeb3b");
      else if (c === 34) styles.push("color:#2196f3");
      else if (c === 35) styles.push("color:#e040fb");
      else if (c === 36) styles.push("color:#00bcd4");
      else if (c === 37) styles.push("color:#e0e0e0");
      else if (c === 39) styles.push("color:inherit");
      else if (c === 90) styles.push("color:#757575");
      else if (c === 91) styles.push("color:#ff5252");
      else if (c === 92) styles.push("color:#69f0ae");
      else if (c === 93) styles.push("color:#ffff00");
      else if (c === 94) styles.push("color:#448aff");
      else if (c === 95) styles.push("color:#ff80ab");
      else if (c === 96) styles.push("color:#84ffff");
      else if (c === 97) styles.push("color:#ffffff");
    }
    return styles.length ? styles.join(";") + ";" : "";
  };
  const open = (style) => style ? `<span style="${style}">` : "<span>";
  let out = "";
  let i = 0;
  const re = /\x1b\[([\d;]*)m/g;
  let match;
  let lastIndex = 0;
  while ((match = re.exec(s)) !== null) {
    out += s.slice(lastIndex, match.index);
    const style = sgr(match[1]);
    out += "</span>" + (style ? open(style) : "<span>");
    lastIndex = re.lastIndex;
  }
  out += s.slice(lastIndex);
  return "<span>" + out + "</span>";
}

// Display platform in lowercase for UI (m4350, m4300, m4250H, m4250L)
function platformDisplayLowercase(platform) {
  if (!platform) return "";
  const map = { "M4350": "m4350", "M4300": "m4300", "M4250 IM": "m4250H", "M4250 LK": "m4250L" };
  return map[platform] || platform.toLowerCase();
}

/* ================= TAB SWITCHING ================= */

const tabButtons = document.querySelectorAll(".tab-btn");
const tabContents = document.querySelectorAll(".tab-content");

tabButtons.forEach(btn => {
  btn.addEventListener("click", () => {
    const tabId = btn.getAttribute("data-tab");
    if (!tabId) return;

    // Remove active from all buttons
    tabButtons.forEach(b => b.classList.remove("active"));
    btn.classList.add("active");

    // Hide all content
    tabContents.forEach(c => c.classList.remove("active"));

    // Show selected tab content
    const target = document.getElementById(tabId);
    if (target) {
      target.classList.add("active");
    }
    if (appContainer) {
      if (tabId === "tab3") appContainer.classList.add("app--validation-wide");
      else appContainer.classList.remove("app--validation-wide");
    }
    if (tabId === "tab2") {
      loadLogs();
    }
  });
});


/* ---------- BUTTON CLICK HANDLERS ---------- */
stkBtn.onclick = () => {
  if (stkInput.files.length) {
    stkInput.value = "";
    stkBtnText.textContent = "Select STK File";
    stkIcon.textContent = "⬆";
    stkBtn.classList.remove("has-file");
    versionBox.style.display = "none";
    versionValue.textContent = "—";
    updateButtonState();
  } else {
    stkInput.click();
  }
};

agentBtn.onclick = () => {
  if (agentInput.files.length) {
    agentInput.value = "";
    agentBtnText.textContent = "Select Agent File";
    agentIcon.textContent = "⬆";
    agentBtn.classList.remove("has-file");
    updateButtonState();
  } else {
    agentInput.click();
  }
};


/* ---------- ENABLE / DISABLE GENERATE BUTTON ---------- */
function updateButtonState() {

  const versionOk = validateVersionInput();
  const platformOk = validatePlatformMatch();

  if (
    stkInput.files.length &&
    agentInput.files.length &&
    newVersion.value.trim() &&
    platform.value &&
    versionOk &&
    platformOk
  ) {
    generateBtn.classList.add("enabled");
    generateBtn.disabled = false;
  } else {
    generateBtn.classList.remove("enabled");
    generateBtn.disabled = true;
  }
}

/* ---------- STK FILE SELECTION ---------- */
stkInput.addEventListener("change", () => {
  const file = stkInput.files[0];
  if (!file) return;

  // Button text → filename
  stkBtnText.textContent = file.name;
  stkBtn.classList.add("has-file");

  // ICON → X
  stkIcon.textContent = "✕";

  // Extract version
  const versionMatch = file.name.match(
    /[-_]?v(\d+\.\d+\.\d+\.\d+)\.stk$/i
  );
  versionValue.textContent = versionMatch ? versionMatch[1] : "UNKNOWN";
  versionBox.style.display = "block";

  const detectedPlatform = detectPlatformFromFilename(file.name);
  if (detectedPlatform) platform.value = detectedPlatform;

  updateButtonState();
});



/* ---------- AGENT FILE SELECTION ---------- */
agentInput.addEventListener("change", () => {

  const file = agentInput.files[0];
  if (!file) return;

  agentBtnText.textContent = file.name;
  agentBtn.classList.add("has-file");

  agentIcon.textContent = "✕";

  validatePlatformMatch();
  updateButtonState();
});

function detectRawPlatform(filename) {

  const name = filename.toUpperCase();

  if (name.match(/^M4350/)) return "M4350";
  if (name.match(/^M4300/)) return "M4300";

  if (name.match(/^M4250[\-_]?H/) || name.includes("M4250_IM"))
    return "M4250H";

  if (name.match(/^M4250[\-_]?L/) || name.includes("M4250_LK"))
    return "M4250L";

  return "";
}

function detectPlatformFromFilename(filename) {
  const name = filename.toUpperCase();

  if (name.startsWith("M4350")) return "M4350";
  if (name.startsWith("M4300")) return "M4300";

  // Handle M4250 variants (heuristic / extendable)
  if (name.startsWith("M4250H")) {
    return "M4250 IM"; // default
  }

  if (name.startsWith("M4250L")) {
    return "M4250 LK";
  }
  
  return "";
}

function detectStkImageVersionFromFilename(filename) {
  const v = filename.match(/[-_]?v(\d+\.\d+\.\d+\.\d+)\.stk$/i);
  if (v) return v[1];
  const any = filename.match(/(\d+\.\d+\.\d+\.\d+)/);
  return any ? any[1] : "";
}

function validatePlatformMatch() {

  const errorEl = document.getElementById("platformError");
  errorEl.style.display = "none";

  const stkFile = stkInput.files[0];
  const agentFile = agentInput.files[0];

  if (!stkFile || !agentFile) return true;

  const stkRaw = detectRawPlatform(stkFile.name);
  const agentRaw = detectRawPlatform(agentFile.name);

  if (!stkRaw || !agentRaw) {
    errorEl.textContent = "Unable to detect platform from filenames";
    errorEl.style.display = "block";
    return false;
  }

  if (stkRaw !== agentRaw) {

    errorEl.textContent =
      `Platform mismatch: STK (${stkRaw}) ≠ Agent (${agentRaw})`;

    errorEl.style.display = "block";

    return false;
  }

  return true;
}

function showToast(message, type = "error") {
    const container = document.getElementById("toastContainer");
    const toast = document.createElement("div");
    toast.className = `toast ${type}`;
    // NO BUTTONS for general toasts
    toast.innerHTML = `<div>${message}</div>`;

    // Click to close
    toast.onclick = () => {
        toast.remove();
    };

    container.appendChild(toast);

    // Auto remove after 15 seconds (changed from 5000)
    setTimeout(() => {
        if (toast.parentNode) toast.remove();
    }, 15000);  // Changed to 15 seconds
}

function showDownloadLogButton(logFilename) {

  const container = document.getElementById("toastContainer");

  const logToast = document.createElement("div");
  logToast.className = "toast error";

  logToast.innerHTML = `
    <div style="margin-bottom:10px;">
      Script failed.
    </div>
    <div style="display:flex; gap:10px; justify-content:flex-end;">
      <button class="toast-download">Download Log</button>
      <button class="toast-cancel">Cancel</button>
    </div>
  `;

  container.appendChild(logToast);

  logToast.querySelector(".toast-download").onclick = () => {
    window.location =
      "/download-log/" + logFilename;

    logToast.remove();   // ✅ CLOSE AFTER DOWNLOAD CLICK
  };

  logToast.querySelector(".toast-cancel").onclick = () => {
    logToast.remove();
  };
}


function extractFilename(disposition) {
  if (!disposition) return null;

  //RFC 5987 (preferred): filename*=
  const utf8Match = disposition.match(/filename\*\=UTF-8''([^;]+)/i);
  if (utf8Match) {
    return decodeURIComponent(utf8Match[1]);
  }

  //Standard: filename=
  const asciiMatch = disposition.match(/filename="([^"]+)"/i);
  if (asciiMatch) {
    return asciiMatch[1];
  }

  return null;
}

function parseVersion(v) {
  const parts = v.split(".").map(n => Number(n));
  if (parts.length !== 4 || parts.some(isNaN)) return null;
  return parts;
}

function comparePrefix(a, b) {
  return a[0] === b[0] && a[1] === b[1] && a[2] === b[2];
}


function validateVersionInput() {
    const inputEl = document.getElementById("newVersion");
    const errorEl = document.getElementById("versionError");
    const warnEl = document.getElementById("versionWarning");
    const validEl = document.getElementById("versionCorrect");
    
    // Clear ALL states first - more thorough cleanup
    errorEl.style.display = "none";
    warnEl.style.display = "none";
    validEl.style.display = "none";
    inputEl.classList.remove("input-error", "input-warning", "input-valid");

    const current = versionValue.textContent;
    const input = newVersion.value.trim();

    if (!current || current === "UNKNOWN" || !input) {
        inputEl.classList.remove("input-error", "input-warning", "input-valid");
        return false;
    }

    const curr = parseVersion(current);
    const next = parseVersion(input);

    if (!curr || !next) {
        errorEl.textContent = "Invalid version format (use X.Y.Z.N)";
        errorEl.style.display = "inline";
        inputEl.classList.add("input-error");
        return false;
    }

    /* ---------- NUMERIC comparison ---------- */
    for (let i = 0; i < 4; i++) {
        if (next[i] > curr[i]) break;
        if (next[i] < curr[i]) {
            errorEl.textContent = `New version must be greater than ${current}`;
            errorEl.style.display = "inline";
            inputEl.classList.add("input-error");
            return false;
        }
    }

    /* ---------- Same major.minor.patch ---------- */
    if (comparePrefix(curr, next)) {
        validEl.textContent = `Valid version (expected ${curr[3]+1})`;
        validEl.style.display = "inline";
        inputEl.classList.add("input-valid");
        return true;
    }

    /* ---------- Prefix changed → warning ---------- */
    warnEl.textContent = `Warning: Release/Version/Maintenance changed (${current} → ${input})`;
    warnEl.style.display = "inline";
    inputEl.classList.add("input-warning");
    return true;
}
function startBackendProgress(jobId) {

  generateBtn.classList.add("running");
  btnText.textContent = "Generating...";
  btnPercent.textContent = "0%";
  btnProgress.style.width = "0%";

  const interval = setInterval(async () => {

    try {
      const response = await fetch(`/progress/${jobId}`);
      const data = await response.json();

      const pct = data.progress || 0;
      const status = data.status;

      btnProgress.style.width = pct + "%";
      btnPercent.textContent = pct + "%";

      if (status === "finished") {

        clearInterval(interval);

        const fileResponse = await fetch(`/download/${jobId}`);
        const blob = await fileResponse.blob();

        const url = window.URL.createObjectURL(blob);
        const a = document.createElement("a");
        a.href = url;

        const disposition = fileResponse.headers.get("Content-Disposition");
        let filename = "SIGNED_IMAGE.stk";

        if (disposition && disposition.includes("filename=")) {
          filename = disposition.split("filename=")[1];
        }

        a.download = filename;
        document.body.appendChild(a);
        a.click();
        a.remove();
        window.URL.revokeObjectURL(url);
        jobRunning = false;
        resetFrontend();
        resetGenerateButton();
        showToast("Package generated successfully", "success");
      }

      if (status === "failed") {
        clearInterval(interval);
        if (data.log) {
            showDownloadLogButton(data.log);
        } else {
            showToast("Packaging failed", "error");
        }
        jobRunning = false;
        resetGenerateButton();
      }

    } catch (err) {
      console.error(err);
      clearInterval(interval);
      showToast("Connection lost", "error");
      resetGenerateButton();
    }

  }, 2000);
}

function resetGenerateButton() {
  generateBtn.classList.remove("running");
  btnProgress.style.width = "0%";
  btnText.textContent = "Generate Signed Package";
  btnPercent.textContent = "";
}

/*================== Reset Frontend ================= */ 
function resetFrontend() {

  /* ---------- Clear File Inputs ---------- */
  stkInput.value = "";
  agentInput.value = "";

  /* ---------- Reset Upload Buttons ---------- */
  stkBtnText.textContent = "Select STK File";
  agentBtnText.textContent = "Select Agent File";

  stkIcon.textContent = "⬆";
  agentIcon.textContent = "⬆";

  stkBtn.classList.remove("has-file");
  agentBtn.classList.remove("has-file");

  /* ---------- Reset Version Box ---------- */
  versionBox.style.display = "none";
  versionValue.textContent = "—";

  /* ---------- Reset Version Input ---------- */
  newVersion.value = "";

  /* ---------- Reset Platform ---------- */
  platform.value = "";

  /* ---------- Clear Validation Messages ---------- */
  document.getElementById("versionError").style.display = "none";
  document.getElementById("versionWarning").style.display = "none";
  document.getElementById("versionCorrect").style.display = "none";

  /* ---------- Reset Generate Button ---------- */
  resetGenerateButton();
  generateBtn.classList.remove("enabled");
  generateBtn.disabled = true;
}


/* ---------- INPUT LISTENERS ---------- */
newVersion.addEventListener("input", () => {
  validateVersionInput();
  updateButtonState();
});

platform.addEventListener("change", updateButtonState);

/* ---------- GENERATE BUTTON ---------- */
generateBtn.onclick = async () => {

    if (jobRunning) {
    return;
  }

  jobRunning = true;
  const stkFile = stkInput.files[0];
  const agentFile = agentInput.files[0];
  const version = newVersion.value.trim();
  const plat = platform.value;

  if (!stkFile || !agentFile || !version || !plat) {
    showToast("All inputs are required", "error");
    jobRunning = false;
    return;
  }

  const formData = new FormData();
  formData.append("stkFile", stkFile);
  formData.append("agentFile", agentFile);
  formData.append("newVersion", version);
  formData.append("platform", plat);

  try {
    const response = await fetch("/generate", {
      method: "POST",
      body: formData
    });

    if (!response.ok) {
      jobRunning = false;
      showToast("Backend error", "error");
      return;
    }

    const result = await response.json();

    if (!result.job_id) {
      showToast("Invalid backend response", "error");
      return;
    }

    // Start progress bar
    startBackendProgress(result.job_id);

  } catch (err) {
    console.error(err);
    showToast("Failed to connect to backend", "error");
  }
};

async function loadLogs(){

  const list = document.getElementById("logList");

  try{

    const res = await fetch("/logs");
    const logs = await res.json();

    if(!logs.length){
      list.innerHTML = "No logs found";
      return;
    }

    list.innerHTML = "";

    logs.forEach(log => {

      const parts = log.name.replace(".log","").split("_");
      let platform, version, date, time;

      if (parts[0] === "validation" && parts.length >= 5) {
        // validation_<platform>_<version>_<date>_<time>.log
        platform = parts[1];
        version = parts[2];
        date = parts[3];
        time = parts[4];
      } else {
        // packaging log: last two are timestamp, version before that, rest is platform
        time = parts[parts.length - 1];
        date = parts[parts.length - 2];
        version = parts[parts.length - 3];
        platform = parts.slice(0, parts.length - 3).join("_");
      }

      const formattedDate =
        date.length >= 8 ? `${date.slice(0,4)}-${date.slice(4,6)}-${date.slice(6,8)}` : date;
      const formattedTime =
        time.length >= 6 ? `${time.slice(0,2)}:${time.slice(2,4)}:${time.slice(4,6)}` : time;

      const isValidationLog = parts[0] === "validation" && parts.length >= 5;
      const versionLabel = (isValidationLog && version === "unknown") ? "Version not detected" : version;
      const logTitle = isValidationLog ? `validation • ${platform} • ${versionLabel}` : `${platform} • ${version}`;

      const sizeKB = Math.round(log.size / 1024);

      const row = document.createElement("div");
      row.className = "log-item";

      row.dataset.time = log.mtime * 1000;

      row.innerHTML = `
      <div class="log-info">
          <div class="log-title">
              ${logTitle}
          </div>
          <div class="log-meta">
              Generated: ${formattedDate} ${formattedTime}
              • ${sizeKB} KB
          </div>
      </div>
      <div class="log-actions">
        <button class="log-view" title="View log">
          <svg viewBox="0 0 24 24" width="18" height="18" fill="none" stroke="white" stroke-width="2">
            <path d="M1 12s4-7 11-7 11 7 11 7-4 7-11 7-11-7-11-7z"/>
            <circle cx="12" cy="12" r="3"/>
          </svg>
        </button>
        <button class="log-download" title="Download log">
        <svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="white" stroke-width="2.5">
        <path d="M12 5v14"/>
        <path d="M19 12l-7 7-7-7"/>
        </svg>
        </button>

        <button class="log-delete" title="Delete log">
        <svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="white" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round">
          <polyline points="3 6 5 6 21 6"/>
          <path d="M19 6l-1 14H6L5 6"/>
          <path d="M10 11v6"/>
          <path d="M14 11v6"/>
          <path d="M9 6V4h6v2"/>
        </svg>
      </button>
    </div>
    `;

      row.querySelector("button").onclick = () => {
        window.location = "/download-log/" + log.name;
      };

      row.querySelector(".log-view").onclick = () => {
        window.open("/view-log/" + log.name, "_blank");
      };

      row.querySelector(".log-download").onclick = () => {
        window.location = "/download-log/" + log.name;
      };

      row.querySelector(".log-delete").onclick = () => {
        openDeleteModal(log, platform, version, formattedDate, formattedTime);
      };

      list.appendChild(row);

    });

  }catch(err){
    list.innerHTML = "Failed to load logs";
  }
}
function filterLogs(type){

  const now = new Date();

  const logs = document.querySelectorAll(".log-item");

  logs.forEach(log => {

    const time = new Date(Number(log.dataset.time));

    let show = false;

    switch(type){

      case "1hr":
        show = (now - time) <= 3600000;
        break;

      case "today":
        show = now.toDateString() === time.toDateString();
        break;

      case "yesterday":
        const y = new Date();
        y.setDate(now.getDate()-1);
        show = y.toDateString() === time.toDateString();
        break;

      case "week":
        show = (now - time) <= 7*86400000;
        break;

      default:
        show = true;
    }

    log.style.display = show ? "flex" : "none";

  });
}

document.addEventListener("click",(e)=>{

  if(!e.target.classList.contains("sort-btn")) return;

  document.querySelectorAll(".sort-btn")
    .forEach(b=>b.classList.remove("active"));

  e.target.classList.add("active");

  filterLogs(e.target.dataset.sort);

});

let logToDelete = null;

function openDeleteModal(log, platform, version, date, time){

  logToDelete = log.name;

  document.getElementById("modalLogName").textContent =
    `${platform} • ${version}`;

  document.getElementById("modalLogMeta").textContent =
    `Generated: ${date} ${time}`;

  document.getElementById("deleteModal").classList.add("show");
}

document.getElementById("cancelDelete").onclick = () => {
  document.getElementById("deleteModal").classList.remove("show");
};

document.getElementById("confirmDelete").onclick = async () => {

  if(!logToDelete) return;

  await fetch("/delete-log/" + logToDelete,{
    method:"DELETE"
  });

  document.getElementById("deleteModal").classList.remove("show");

  loadLogs();
};

/* ================= VALIDATION TAB ================= */

function validateSwitchIpInput() {
  const errEl = document.getElementById("switchIpError");
  if (!switchIp || !errEl) return false;
  const raw = switchIp.value.trim();
  switchIp.classList.remove("input-error");
  errEl.textContent = "";
  errEl.style.display = "none";

  if (!raw) return false;

  if (/[^0-9.]/.test(raw)) {
    errEl.textContent = "Only digits and dots are allowed.";
    errEl.style.display = "block";
    switchIp.classList.add("input-error");
    return false;
  }

  const dotCount = (raw.match(/\./g) || []).length;
  if (dotCount > 3) {
    errEl.textContent = "An IPv4 address uses exactly three dots (four numbers, e.g. 192.168.1.1).";
    errEl.style.display = "block";
    switchIp.classList.add("input-error");
    return false;
  }

  const parts = raw.split(".");
  for (let i = 0; i < parts.length; i++) {
    const p = parts[i];
    if (p === "") continue;
    if (!/^\d+$/.test(p)) {
      errEl.textContent = "Each octet must be a whole number.";
      errEl.style.display = "block";
      switchIp.classList.add("input-error");
      return false;
    }
    const n = parseInt(p, 10);
    if (n > 255) {
      errEl.textContent = "Each octet must be between 0 and 255.";
      errEl.style.display = "block";
      switchIp.classList.add("input-error");
      return false;
    }
  }

  if (dotCount < 3) return false;

  if (parts.length !== 4 || parts.some(p => p === "")) {
    errEl.textContent = "Enter four numbers separated by dots (e.g. 192.168.1.1).";
    errEl.style.display = "block";
    switchIp.classList.add("input-error");
    return false;
  }

  for (const p of parts) {
    const n = parseInt(p, 10);
    if (n < 0 || n > 255) {
      errEl.textContent = "Each octet must be between 0 and 255.";
      errEl.style.display = "block";
      switchIp.classList.add("input-error");
      return false;
    }
  }

  return true;
}

function validateExpectedAppMgrInput() {
  const errEl = document.getElementById("expectedAppMgrVersionError");
  if (!expectedAppMgrVersion || !errEl) return true;
  const v = expectedAppMgrVersion.value.trim();
  expectedAppMgrVersion.classList.remove("input-error");
  errEl.textContent = "";
  errEl.style.display = "none";

  if (!v) return true;

  const parsed = parseVersion(v);
  if (!parsed) {
    errEl.textContent = "Invalid version format (use X.Y.Z.N)";
    errEl.style.display = "block";
    expectedAppMgrVersion.classList.add("input-error");
    return false;
  }

  return true;
}

function updateValidationButtonStates() {
  if (!validationStkInput || !validateBtn || !switchIp || !switchUsername || !switchPassword) return;
  const hasFile = validationStkInput.files.length > 0;
  const ipOk = validateSwitchIpInput();
  const appMgrOk = validateExpectedAppMgrInput();
  const canValidate = hasFile &&
    validationDetectedPlatform &&
    ipOk &&
    switchUsername.value.trim() &&
    switchPassword.value.trim() &&
    appMgrOk;
  validateBtn.disabled = !canValidate;
  if (canValidate) validateBtn.classList.add("enabled");
  else validateBtn.classList.remove("enabled");
}

function initValidationTab() {
  if (!validationStkBtn || !validationStkInput || !validateBtn ||
      !validationResult || !switchIp || !switchUsername || !switchPassword || !expectedAppMgrVersion ||
      !validationPlatformValue || !validationImageVersionValue) return;

  function setValidationPreviewEmpty() {
    validationPlatformValue.textContent = VALIDATION_PREVIEW_EMPTY;
    validationPlatformValue.classList.add("is-empty");
    validationImageVersionValue.textContent = VALIDATION_PREVIEW_EMPTY;
    validationImageVersionValue.classList.add("is-empty");
  }

  function clearValidationStkUi() {
    validationStkInput.value = "";
    if (validationStkBtnText) validationStkBtnText.textContent = "Select STK File";
    const icon = validationStkBtn.querySelector(".upload-icon");
    if (icon) icon.textContent = "⬆";
    validationStkBtn.classList.remove("has-file");
    setValidationPreviewEmpty();
    validationDetectedPlatform = "";
    updateValidationButtonStates();
  }

  validationStkBtn.onclick = () => {
    if (validationStkInput.files.length) {
      clearValidationStkUi();
    } else {
      validationStkInput.click();
    }
  };

  if (clearValidationStkBtn) {
    clearValidationStkBtn.onclick = (e) => {
      e.stopPropagation();
      clearValidationStkUi();
    };
  }

  validationStkInput.addEventListener("change", () => {
    const file = validationStkInput.files[0];
    if (!file) return;
    if (validationStkBtnText) {
      validationStkBtnText.textContent = file.name.length > 48 ? file.name.slice(0, 45) + "…" : file.name;
    }
    validationStkBtn.classList.add("has-file");
    const icon = validationStkBtn.querySelector(".upload-icon");
    if (icon) icon.textContent = "✕";
    validationDetectedPlatform = detectPlatformFromFilename(file.name) || "";
    if (validationDetectedPlatform) {
      validationPlatformValue.textContent = validationDetectedPlatform;
      validationPlatformValue.classList.remove("is-empty");
    } else {
      validationPlatformValue.textContent = VALIDATION_PLATFORM_UNKNOWN;
      validationPlatformValue.classList.add("is-empty");
    }
    const imgVer = detectStkImageVersionFromFilename(file.name);
    if (imgVer) {
      validationImageVersionValue.textContent = imgVer;
      validationImageVersionValue.classList.remove("is-empty");
    } else {
      validationImageVersionValue.textContent = VALIDATION_VERSION_UNKNOWN;
      validationImageVersionValue.classList.add("is-empty");
    }
    updateValidationButtonStates();
  });

  switchIp.addEventListener("input", updateValidationButtonStates);
  switchUsername.addEventListener("input", updateValidationButtonStates);
  switchPassword.addEventListener("input", updateValidationButtonStates);
  if (expectedAppMgrVersion) expectedAppMgrVersion.addEventListener("input", updateValidationButtonStates);

  if (switchPasswordToggle && switchPassword) {
    switchPasswordToggle.addEventListener("click", () => {
      const isPassword = switchPassword.type === "password";
      switchPassword.type = isPassword ? "text" : "password";
      switchPasswordToggle.classList.toggle("show-password", isPassword);
      switchPasswordToggle.setAttribute("title", isPassword ? "Hide password" : "Show password");
      switchPasswordToggle.setAttribute("aria-label", isPassword ? "Hide password" : "Show password");
    });
  }

  validateBtn.onclick = async () => {
    const file = validationStkInput.files[0];
    if (!file || !switchUsername.value.trim() || !switchPassword.value.trim()) {
      showToast("Select STK file and fill switch credentials", "error");
      return;
    }
    if (!validateSwitchIpInput() || !validateExpectedAppMgrInput()) {
      showToast("Fix the highlighted fields before validating.", "error");
      return;
    }
    if (validateBtn.classList.contains("running")) return;
    validateBtn.classList.add("running");
    validateBtn.disabled = true;

    if (validationFormCard) validationFormCard.classList.add("validation-running");
    if (validationTerminalWrap) validationTerminalWrap.style.display = "block";
    if (validationTerminal) validationTerminal.innerHTML = ansiToHtml("Running ansible-playbook...\n\n");

    const formData = new FormData();
    formData.append("stkFile", file);
    formData.append("switch_ip", switchIp.value.trim());
    formData.append("switch_username", switchUsername.value.trim());
    formData.append("switch_password", switchPassword.value.trim());
    formData.append("expected_app_mgr_version", (expectedAppMgrVersion && expectedAppMgrVersion.value) ? expectedAppMgrVersion.value.trim() : "");
    formData.append("platform", platformDisplayLowercase(validationDetectedPlatform) || "");

    try {
      const response = await fetch("/validate", { method: "POST", body: formData });
      const contentType = response.headers.get("content-type") || "";
      if (contentType.includes("application/json")) {
        const data = await response.json().catch(() => ({}));
        if (validationFormCard) validationFormCard.classList.remove("validation-running");
        if (validationTerminalWrap) validationTerminalWrap.style.display = "none";
        showToast(data.message || "Validation failed", "error");
        if (data.application_table !== undefined) showValidationResultModal(data);
        validateBtn.classList.remove("running");
        updateValidationButtonStates();
        return;
      }
      if (!response.body) {
        throw new Error("No response body");
      }
      const reader = response.body.getReader();
      const decoder = new TextDecoder();
      let buffer = "";
      while (true) {
        const { value, done } = await reader.read();
        if (done) break;
        buffer += decoder.decode(value, { stream: true });
        if (validationTerminal) {
          validationTerminal.innerHTML = ansiToHtml(buffer);
          validationTerminal.scrollTop = validationTerminal.scrollHeight;
        }
      }
      const idx = buffer.indexOf(VALIDATION_RESULT_MARKER);
      if (idx !== -1) {
        if (validationTerminal) validationTerminal.innerHTML = ansiToHtml(buffer.slice(0, idx));
        const jsonStr = buffer.slice(idx + VALIDATION_RESULT_MARKER.length).trim();
        try {
          const data = JSON.parse(jsonStr);
          if (validationResult) {
            validationResult.textContent = data.message || "";
            validationResult.className = data.success ? "validation-result validation-result-success" : "validation-result validation-result-error";
          }
          if (data.success) showToast(data.message || "Validation completed", "success");
          else showToast(data.message || "Validation failed", "error");
          showValidationResultModal(data);
        } catch (e) {
          console.error(e);
          showToast("Invalid result from server", "error");
        }
      } else if (validationTerminal) {
        validationTerminal.innerHTML = ansiToHtml(buffer || "(No output)");
      }
    } catch (err) {
      console.error(err);
      if (validationTerminal) validationTerminal.innerHTML = ansiToHtml((validationTerminal.innerText || "") + "\n\nConnection error: " + err.message);
      showToast("Failed to connect to backend", "error");
    }
    if (validationFormCard) validationFormCard.classList.remove("validation-running");
    validateBtn.classList.remove("running");
    updateValidationButtonStates();
  };

  if (validationBackBtn) {
    validationBackBtn.onclick = () => {
      if (validationTerminalWrap) validationTerminalWrap.style.display = "none";
      if (validationFormCard) validationFormCard.style.display = "block";
      if (validationTerminal) validationTerminal.innerHTML = "";
      if (validationResult) { validationResult.textContent = ""; validationResult.className = "validation-result"; }
    };
  }

  updateValidationButtonStates();
}

function showValidationResultModal(data) {
  const modal = document.getElementById("validationResultModal");
  const titleEl = document.getElementById("validationModalTitle");
  const messageEl = document.getElementById("validationModalMessage");
  const tableEl = document.getElementById("validationModalTable");
  if (!modal || !titleEl || !messageEl || !tableEl) return;
  titleEl.classList.remove("validation-modal-success", "validation-modal-mismatch", "validation-modal-failed");
  modal.classList.remove("validation-modal-success", "validation-modal-mismatch", "validation-modal-failed");
  const success = data && data.success === true;
  const versionMatch = data && data.version_match === true;
  if (success && versionMatch) {
    titleEl.textContent = "Success";
    titleEl.classList.add("validation-modal-success");
    modal.classList.add("validation-modal-success");
    messageEl.textContent = data.expected_version
      ? "Expected App-Mgr version matches: " + (data.appmgr_version || data.expected_version)
      : "Validation completed successfully.";
  } else if (success && !versionMatch) {
    titleEl.textContent = "Version mismatch";
    titleEl.classList.add("validation-modal-mismatch");
    modal.classList.add("validation-modal-mismatch");
    messageEl.textContent = data.expected_version && data.appmgr_version
      ? "Expected " + data.expected_version + ", switch has " + data.appmgr_version
      : "Validation completed but version could not be verified.";
  } else {
    titleEl.textContent = "Validation failed";
    titleEl.classList.add("validation-modal-failed");
    modal.classList.add("validation-modal-failed");
    messageEl.textContent = data.message || "Ansible playbook failed.";
  }
  tableEl.textContent = (data.application_table_name_version && data.application_table_name_version.trim())
    ? data.application_table_name_version.trim()
    : (data.application_table && data.application_table.trim())
      ? data.application_table.trim()
      : "(No application table in output)";
  modal.classList.add("show");
}

document.getElementById("validationModalClose") && document.getElementById("validationModalClose").addEventListener("click", () => {
  const modal = document.getElementById("validationResultModal");
  if (modal) modal.classList.remove("show");
});

initValidationTab();