
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


/* ================= TAB SWITCHING ================= */

const tabButtons = document.querySelectorAll(".tab-btn");
const tabContents = document.querySelectorAll(".tab-content");

tabButtons.forEach(btn => {
  btn.addEventListener("click", () => {

    // Remove active from all buttons
    tabButtons.forEach(b => b.classList.remove("active"));
    btn.classList.add("active");

    // Hide all content
    tabContents.forEach(c => c.classList.remove("active"));

    // Show selected
    const target = document.getElementById(btn.dataset.tab);
    if (btn.dataset.tab === "tab2") {
    loadLogs();
    }
    if (target) {
      target.classList.add("active");
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

      // last two values are always timestamp
      const time = parts[parts.length - 1];
      const date = parts[parts.length - 2];

      // version always before timestamp
      const version = parts[parts.length - 3];

      // everything before version is platform
      const platform = parts.slice(0, parts.length - 3).join("_");

      const formattedDate =
        `${date.slice(0,4)}-${date.slice(4,6)}-${date.slice(6,8)}`;

      const formattedTime =
        `${time.slice(0,2)}:${time.slice(2,4)}:${time.slice(4,6)}`;

      const sizeKB = Math.round(log.size / 1024);

      const row = document.createElement("div");
      row.className = "log-item";

      row.dataset.time = log.mtime * 1000;

      row.innerHTML = `
      <div class="log-info">
          <div class="log-title">
              ${platform} • ${version}
          </div>
          <div class="log-meta">
              Generated: ${formattedDate} ${formattedTime}
              • ${sizeKB} KB
          </div>
      </div>
      <div class="log-actions">
        <button class="log-view" title="View log">👁</button>
        <button class="log-download" title="Download log">⬇</button>
        <button class="log-delete" title="Delete log">🗑</button>
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