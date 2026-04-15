import os
import re
import uuid
import shutil
import subprocess
import threading
import redis
import json
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timedelta
from werkzeug.utils import secure_filename

from flask import Flask, request, jsonify, send_file, Response, stream_with_context
from flask_cors import CORS

from flask import send_from_directory

FRONTEND_DIR = os.path.join(os.path.dirname(__file__), "frontend")


# =====================================================
# CONFIG
# =====================================================

WORK_DIR = "/home/vspl007/Downloads/Management_switch_Package/ImagePacking"
SCRIPT_PATH = os.path.join(WORK_DIR, "run_packaging.sh")
JOBS_DIR = os.path.join(WORK_DIR, "jobs")
LOG_DIR = os.path.join(WORK_DIR, "logs")

# Dedicated directory for STK files uploaded via Validation tab (single-user).
VALIDATION_STK_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "validation_stk_uploads")

# img_upgrade: Validation tab runs playbooks/image_upgrade.yml (see roles/image_upgrade).
# Optional env for SCP source (switch copies STK from this host): IMG_UPGRADE_SCP_USER,
# IMG_UPGRADE_SCP_HOST, IMG_UPGRADE_SCP_PASSWORD — if unset, values stay as in roles/image_upgrade/vars/main.yml.
PACKAGE_DIR = os.path.dirname(os.path.abspath(__file__))
IMG_UPGRADE_DIR = os.path.join(PACKAGE_DIR, "img_upgrade")
IMG_UPGRADE_INVENTORY = os.path.join(IMG_UPGRADE_DIR, "inventory.ini")
IMG_UPGRADE_ROLE_VARS = os.path.join(IMG_UPGRADE_DIR, "roles", "image_upgrade", "vars", "main.yml")

os.makedirs(LOG_DIR, exist_ok=True)
os.makedirs(VALIDATION_STK_DIR, exist_ok=True)
os.makedirs(JOBS_DIR, exist_ok=True)

ALLOWED_PLATFORMS = {
    "M4350": "M4350",
    "M4300": "M4300",
    "M4250 IM": "M4250_IM",
    "M4250 LK": "M4250_LK"
}

# =====================================================
# APP
# =====================================================

app = Flask(__name__)
CORS(app)

# =====================================================
# REDIS CONNECTION
# =====================================================

redis_client = redis.Redis(
    host="localhost",
    port=6379,
    decode_responses=True
)

# =====================================================
# REDIS JOB HELPERS
# =====================================================

def set_job(job_id, data):
    redis_client.hset(f"job:{job_id}", mapping=data)

def update_job(job_id, key, value):
    redis_client.hset(f"job:{job_id}", key, value)

def get_job(job_id):
    return redis_client.hgetall(f"job:{job_id}")

def delete_job(job_id):
    redis_client.delete(f"job:{job_id}")

# Limit concurrent packaging jobs
executor = ThreadPoolExecutor(max_workers=4)

# =====================================================
# BACKGROUND TASK
# =====================================================

def run_packaging(job_id, job_meta):

    job_dir = job_meta["job_dir"]

    update_job(job_id, "status", "running")
    update_job(job_id, "progress", 0)

    cmd = [
        SCRIPT_PATH,
        job_meta["platform"],
        job_meta["agent_file"],
        job_meta["stk_file"],
        job_meta["new_version"],
        job_dir
    ]

    try:
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

        log_filename = f"{job_meta['platform']}_{job_meta['new_version']}_{timestamp}.log"
        log_path = os.path.join(LOG_DIR, log_filename)

        with open(log_path, "w") as logfile:

            process = subprocess.Popen(
                cmd,
                cwd=WORK_DIR,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True
            )

            error_detected = False

            for line in process.stdout:

                logfile.write(line)
                logfile.flush()

                print(f"[{job_id}] {line.strip()}")

                lower_line = line.lower()

                # detect failure patterns
                if "error:" in lower_line or "failed" in lower_line:
                    error_detected = True

                if line.startswith("[PROGRESS]"):
                    try:
                        pct = int(line.strip().split()[1])
                        update_job(job_id, "progress", pct)
                    except:
                        pass

        process.wait()

        if process.returncode != 0 or error_detected:

            shutil.rmtree(job_dir, ignore_errors=True)

            update_job(job_id, "status", "failed")
            update_job(job_id, "log", log_filename)

            return

        # ✅ Look inside outputs folder
        outputs_dir = os.path.join(job_dir, "output")

        if not os.path.isdir(outputs_dir):
            update_job(job_id, "status", "failed")
            update_job(job_id, "log", log_filename)
            return

        output_files = [
            f for f in os.listdir(outputs_dir)
            if f.lower().endswith(".stk")
        ]

        if not output_files:
            update_job(job_id, "status", "failed")
            update_job(job_id, "log", log_filename)
            return

        output_path = os.path.join(outputs_dir, output_files[0])

        update_job(job_id, "progress", 100)
        update_job(job_id, "status", "finished")
        update_job(job_id, "output", output_path)

    except Exception as e:
        print("Packaging error:", e)
        update_job(job_id, "status", "failed")
        update_job(job_id, "log", log_filename)


def cleanup_old_logs():

    now = datetime.now()

    for f in os.listdir(LOG_DIR):

        path = os.path.join(LOG_DIR, f)

        if not os.path.isfile(path):
            continue

        created = datetime.fromtimestamp(os.path.getmtime(path))

        if now - created > timedelta(days=10):
            os.remove(path)
# =====================================================
# ROUTES
# =====================================================

@app.route("/generate", methods=["POST"])
def generate():

    stk_file = request.files.get("stkFile")
    agent_file = request.files.get("agentFile")
    new_version = request.form.get("newVersion")
    platform_ui = request.form.get("platform")

    if not stk_file or not agent_file or not new_version or not platform_ui:
        return jsonify({"message": "Missing inputs"}), 400

    if platform_ui not in ALLOWED_PLATFORMS:
        return jsonify({"message": "Invalid platform"}), 400

    job_id = str(uuid.uuid4())
    job_dir = os.path.join(JOBS_DIR, job_id)
    os.makedirs(job_dir, exist_ok=True)

    # sanitize filenames
    stk_filename = secure_filename(stk_file.filename).replace(" ", "_")
    agent_filename = secure_filename(agent_file.filename).replace(" ", "_")

    stk_path = os.path.join(job_dir, stk_filename)
    agent_path = os.path.join(job_dir, agent_filename)

    stk_file.save(stk_path)
    agent_file.save(agent_path)

    set_job(job_id, {
        "status": "queued",
        "progress": 0,
        "output": ""
    })

    redis_client.expire(f"job:{job_id}", 3600)

    executor.submit(run_packaging, job_id, {
        "platform": ALLOWED_PLATFORMS[platform_ui],
        "stk_file": stk_filename,
        "agent_file": agent_filename,
        "new_version": new_version,
        "job_dir": job_dir
    })

    return jsonify({"job_id": job_id})


# -----------------------------------------------------

@app.route("/progress/<job_id>")
def get_progress(job_id):

    job = get_job(job_id)

    if not job:
        return jsonify({"status": "failed", "progress": 0})

    return jsonify({
    "status": job.get("status"),
    "progress": int(job.get("progress", 0)),
    "log": job.get("log")
})

# -----------------------------------------------------

@app.route("/download/<job_id>")
def download(job_id):

    job = get_job(job_id)

    if not job or job["status"] != "finished":
        return jsonify({"message": "Not ready"}), 400

    output_path = job["output"]

    if not os.path.exists(output_path):
        return jsonify({"message": "File missing"}), 404

    job_dir = os.path.dirname(os.path.dirname(output_path))

    # Read file into memory first (safe for single STK file)
    with open(output_path, "rb") as f:
        file_data = f.read()

    # Cleanup BEFORE returning response
    shutil.rmtree(job_dir, ignore_errors=True)

    delete_job(job_id)

    return Response(
        file_data,
        mimetype="application/octet-stream",
        headers={
            "Content-Disposition":
            f"attachment; filename={os.path.basename(output_path)}"
        }
    )

@app.route("/logs")
def list_logs():

    #clean up old logs
    cleanup_old_logs()
    
    logs = []

    for f in os.listdir(LOG_DIR):

        if not f.endswith(".log"):
            continue

        path = os.path.join(LOG_DIR, f)

        logs.append({
            "name": f,
            "size": os.path.getsize(path),
            "mtime": os.path.getmtime(path)
        })

    logs.sort(key=lambda x: x["mtime"], reverse=True)

    return jsonify(logs)

@app.route("/download-log/<filename>")
def download_log(filename):

    path = os.path.join(LOG_DIR, filename)

    if not os.path.exists(path):
        return jsonify({"error": "Log not found"}), 404

    return send_file(
        path,
        as_attachment=True,
        download_name=filename
    )

@app.route("/view-log/<filename>")
def view_log(filename):

    path = os.path.join(LOG_DIR, filename)

    if not os.path.exists(path):
        return "Log not found", 404

    with open(path, "r") as f:
        content = f.read()

    # parse metadata from filename
    name = filename.replace(".log", "")
    parts = name.split("_")

    if len(parts) >= 5 and parts[0] == "validation":
        # validation_<platform>_<version>_<date>_<time>.log
        platform = parts[1]
        version = parts[2]
        date = parts[3]
        time = parts[4]
    else:
        time = parts[-1]
        date = parts[-2]
        version = parts[-3]
        platform = "_".join(parts[:-3])

    formatted_date = f"{date[:4]}-{date[4:6]}-{date[6:8]}" if len(date) >= 8 else date
    formatted_time = f"{time[:2]}:{time[2:4]}:{time[4:6]}" if len(time) >= 6 else time

    html = f"""
    <html>
    <head>
    <title>STK Packaging Log</title>

    <style>

    body {{
        font-family: system-ui;
        margin:40px;
        background:#0f172a;
        color:#e2e8f0;
    }}

    h1 {{
        margin-bottom:10px;
    }}

    .details {{
        font-size:18px;
        margin-bottom:25px;
        line-height:1.6;
    }}

    .output {{
        font-family: monospace;
        font-size:14px;
        white-space: pre-wrap;
    }}

    </style>

    </head>

    <body>

    <h1>STK Packaging Log</h1>

    <div class="details">
    Platform: <b>{platform}</b><br>
    Version: <b>{version}</b><br>
    Generated: {formatted_date} {formatted_time}<br>
    Log File: {filename}
    </div>

    <div class="output">
{content}
    </div>

    </body>
    </html>
    """

    return html

# -----------------------------------------------------
# Validation tab: upload STK (legacy endpoint; Validate button uses /validate)
# -----------------------------------------------------
@app.route("/upload-validation-stk", methods=["POST"])
def upload_validation_stk():
    stk_file = request.files.get("stkFile")
    if not stk_file or not stk_file.filename or not stk_file.filename.lower().endswith(".stk"):
        return jsonify({"success": False, "message": "No valid STK file provided"}), 400
    filename = secure_filename(stk_file.filename).replace(" ", "_")
    dest_path = os.path.join(VALIDATION_STK_DIR, filename)
    stk_file.save(dest_path)
    return jsonify({"success": True, "message": "STK file uploaded", "filename": filename})


# -----------------------------------------------------
# img_upgrade: inventory + role vars for Validation tab
# -----------------------------------------------------
def _yaml_scalar_escape(val):
    return val.replace("\\", "\\\\").replace('"', '\\"')


def _read_role_var_scalar(content, key):
    m = re.search(r"^%s:\s*(.+)\s*$" % re.escape(key), content, re.MULTILINE)
    if not m:
        return ""
    v = m.group(1).strip()
    if (v.startswith('"') and v.endswith('"')) or (v.startswith("'") and v.endswith("'")):
        v = v[1:-1]
    return v


def _set_role_var_line(content, key, value, quote=True):
    """Replace or insert KEY: value line (quoted string if quote)."""
    line = '%s: "%s"' % (key, _yaml_scalar_escape(value)) if quote else "%s: %s" % (key, value)
    pat = r"^%s:\s*.+$" % re.escape(key)
    if re.search(pat, content, flags=re.MULTILINE):
        return re.sub(pat, line, content, flags=re.MULTILINE)
    if content.endswith("\n"):
        return content + line + "\n"
    return content + "\n" + line + "\n"


def _update_img_upgrade_inventory(switch_ip, switch_username, switch_password):
    """Write img_upgrade/inventory.ini [switches] group for the target switch."""
    pw_esc = switch_password.replace("\\", "\\\\").replace('"', '\\"')
    body = (
        "[switches]\n"
        "switch1 ansible_host=%s ansible_user=%s ansible_password=\"%s\"\n"
        % (switch_ip, switch_username, pw_esc)
    )
    with open(IMG_UPGRADE_INVENTORY, "w") as f:
        f.write(body)


def _update_img_upgrade_role_vars(
    image_file,
    image_dir_abs,
    expected_image_version,
    expected_app_mgr_version,
):
    """
    Update roles/image_upgrade/vars/main.yml for copy_image + verify_upgrade.
    SCP source host/user/password: env IMG_UPGRADE_SCP_USER, IMG_UPGRADE_SCP_HOST, IMG_UPGRADE_SCP_PASSWORD,
    or keep existing values from the file.
    """
    with open(IMG_UPGRADE_ROLE_VARS, "r") as f:
        content = f.read()

    img_user = os.environ.get("IMG_UPGRADE_SCP_USER", "").strip() or _read_role_var_scalar(content, "image_server_username")
    img_host = os.environ.get("IMG_UPGRADE_SCP_HOST", "").strip() or _read_role_var_scalar(content, "image_server_host")
    img_pw = os.environ.get("IMG_UPGRADE_SCP_PASSWORD", "")
    if img_pw == "":
        img_pw = _read_role_var_scalar(content, "image_server_password")

    content = _set_role_var_line(content, "image_file", image_file)
    content = _set_role_var_line(content, "image_path", image_dir_abs)
    content = _set_role_var_line(content, "expected_image_version", expected_image_version)
    content = _set_role_var_line(content, "expected_appmgr_version", expected_app_mgr_version or "0.0.0.0")
    content = _set_role_var_line(content, "image_server_username", img_user)
    content = _set_role_var_line(content, "image_server_host", img_host)
    content = _set_role_var_line(content, "image_server_password", img_pw)

    with open(IMG_UPGRADE_ROLE_VARS, "w") as f:
        f.write(content)


def _extract_stk_image_version_from_filename(filename):
    m = re.search(r"[-_]v(\d+\.\d+\.\d+\.\d+)\.stk$", filename, re.I)
    if m:
        return m.group(1)
    m2 = re.search(r"(\d+\.\d+\.\d+\.\d+)", filename)
    return m2.group(1) if m2 else ""


VALIDATION_STREAM_RESULT_MARKER = "\n---RESULT---\n"


def _validation_playbook_wait_seconds():
    """Seconds to wait for ansible after stdout closes (subprocess.wait). Env: VALIDATION_PLAYBOOK_MAX_WAIT_SEC."""
    try:
        v = int(os.environ.get("VALIDATION_PLAYBOOK_MAX_WAIT_SEC", "7200"))
    except (TypeError, ValueError):
        v = 7200
    return max(120, min(v, 86400))


def _clear_validation_stk_uploads():
    """Remove all files (and subdirs) under validation_stk_uploads after validation ends."""
    try:
        if not os.path.isdir(VALIDATION_STK_DIR):
            return
        for name in os.listdir(VALIDATION_STK_DIR):
            path = os.path.join(VALIDATION_STK_DIR, name)
            try:
                if os.path.isfile(path) or os.path.islink(path):
                    os.remove(path)
                elif os.path.isdir(path):
                    shutil.rmtree(path)
            except OSError as ex:
                print("[Validate] cleanup skip %s: %s" % (path, ex))
    except OSError as ex:
        print("[Validate] cleanup validation_stk_uploads failed: %s" % ex)


VALIDATION_LOG_HEADER = (
    "\n"
    "===============================================================================\n"
    "  VALIDATION TAB LOG  (Ansible image upgrade / application table)\n"
    "===============================================================================\n\n"
)


def _write_validation_log(full_output, platform_vars_name, appmgr_version):
    """Write full command output to a log file in LOG_DIR with validation tag. Filename includes platform and version for logs tab."""
    os.makedirs(LOG_DIR, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    version_safe = (appmgr_version or "unknown").strip()
    log_filename = "validation_%s_%s_%s.log" % (platform_vars_name, version_safe, timestamp)
    log_path = os.path.join(LOG_DIR, log_filename)
    with open(log_path, "w") as f:
        f.write(VALIDATION_LOG_HEADER)
        f.write(full_output)
    return log_filename


def _table_name_version_only(table_text):
    """Reduce application table to Name and Version columns only. Returns a simple two-column text block."""
    if not table_text or not table_text.strip():
        return ""
    lines = table_text.strip().split("\n")
    out = []
    for i, line in enumerate(lines):
        parts = line.split()
        if not parts:
            continue
        if i == 0 and "Name" in line and "Version" in line:
            out.append("Name                Version")
            out.append("-" * 40)
            continue
        if line.strip().startswith("-"):
            continue
        name = parts[0] if parts else ""
        version = ""
        for p in reversed(parts[1:]):
            if re.match(r"^\d+\.\d+\.\d+\.\d+$", p):
                version = p
                break
        out.append("%-18s %s" % (name, version or "-"))
    return "\n".join(out) if out else table_text


def _parse_application_table_and_version(full_output):
    """
    Parse ansible output for the 'show application' table and appmgr version.
    Returns (table_text, appmgr_version).
    Table format:
        Name             StartOnBoot AutoRestart ... Version
        ---------------- ----------- ----------- ...
        appmgr           Yes         Yes         ... 1.0.5.37
    """
    table_text = ""
    appmgr_version = ""
    lines = full_output.split("\n")
    for i, line in enumerate(lines):
        if "Name" in line and "StartOnBoot" in line and "Version" in line:
            # Collect this line and following lines until we hit a line that doesn't look like table
            block = [line]
            for j in range(i + 1, min(i + 25, len(lines))):
                next_line = lines[j]
                if next_line.strip() and (next_line.strip().startswith("-") or "appmgr" in next_line or "SecureDiagnostic" in next_line or re.match(r"^\s*\S+\s+", next_line)):
                    block.append(next_line)
                    if "appmgr" in next_line:
                        # Extract version: last column that looks like x.y.z.w
                        parts = next_line.split()
                        for p in reversed(parts):
                            if re.match(r"^\d+\.\d+\.\d+\.\d+$", p):
                                appmgr_version = p
                                break
                else:
                    break
            table_text = "\n".join(block)
            break
    return table_text.strip(), appmgr_version


def _parse_img_upgrade_ansible_output(output):
    """Detect image mismatch, revert, and versions from img_upgrade ansible -vv output.

    Version lines appear many times (initial show version vs post-upgrade). Use **last**
    Extracted/Debug/facts occurrence so we match verify_upgrade.yml, not the first CLI blob.
    """
    out = {
        "image_mismatch": False,
        "image_mismatch_snippet": "",
        "upgrade_reverted": False,
        "extracted_image_version": "",
        "extracted_appmgr_version": "",
        "upgrade_success_flag": None,
    }
    if not output:
        return out
    low = output.lower()
    # Only the fail: msg text (see check_platform.yml), not TASK names like "Fail if image does not match..."
    if "image mismatch!" in low:
        out["image_mismatch"] = True
        m = re.search(
            r"Image mismatch!\s*(.*?)(?=\n(?:TASK|PLAY|\[WARNING\]|fatal:)|\Z)",
            output,
            re.DOTALL | re.IGNORECASE,
        )
        out["image_mismatch_snippet"] = (m.group(1).strip() if m else "")[:1200]
    if "upgrade failed. device reverted" in low:
        out["upgrade_reverted"] = True

    def _last_capture(pattern, text, flags=0):
        ms = re.findall(pattern, text, flags)
        return ms[-1].strip() if ms else ""

    # Debug task msg lines (verify_upgrade.yml) — prefer last block
    ext_img = _last_capture(r"Extracted Image Version:\s*(?:\[')?([\d.]+)", output)
    ext_app = _last_capture(r"Extracted AppMgr Version:\s*(?:\[')?([\d.]+)", output)
    if ext_img:
        out["extracted_image_version"] = ext_img
    if ext_app:
        out["extracted_appmgr_version"] = ext_app

    # set_fact JSON in -vv (single-line ok: ... => {"ansible_facts": {...}})
    if not out["extracted_image_version"]:
        v = _last_capture(r'"extracted_version"\s*:\s*"([\d.]+)"', output)
        if v:
            out["extracted_image_version"] = v
    if not out["extracted_appmgr_version"]:
        v = _last_capture(r'"extracted_appmgr_version"\s*:\s*"([\d.]+)"', output)
        if v:
            out["extracted_appmgr_version"] = v

    # Last explicit upgrade_success fact (verify step)
    us = re.findall(r'"upgrade_success"\s*:\s*(true|false)', output, re.I)
    if us:
        out["upgrade_success_flag"] = us[-1].lower() == "true"

    succ = re.findall(r"Upgrade Success:\s*(True|False)", output)
    if succ:
        out["upgrade_success_flag"] = succ[-1] == "True"

    # Fallback: "Software Version........... X.Y.Z.W" inside switch output (use **last** — post-upgrade)
    if not out["extracted_image_version"]:
        v = _last_capture(r"Software Version\.+\s*([\d.]+)", output, re.I)
        if v:
            out["extracted_image_version"] = v

    return out


def _validation_stream_simplify_ansible_line(line):
    """Turn huge ansible -vv shell JSON lines into a short prefix + decoded stdout or msg list.

    Original line is still appended to full_lines for logging/parsing; this only affects
    what is yielded to the browser.
    """
    if not line:
        return [line]
    # Multiline debug: "foo.stdout": "spawn ssh... (duplicate of shell task stdout)
    if len(line) >= 800 and re.search(r'"\w+\.stdout"\s*:\s*"', line):
        return ["    [embedded .stdout omitted — see saved validation log]\n"]
    if len(line) < 400:
        return [line]
    if " => " not in line or "{" not in line:
        return [line]
    idx = line.find(" => ")
    prefix = line[: idx + 4].rstrip()
    rest = line[idx + 4 :].strip()
    if not rest.startswith("{"):
        return [line]
    try:
        data = json.loads(rest)
    except json.JSONDecodeError:
        return [line]

    out = [prefix + "\n"]
    stdout = data.get("stdout")
    if isinstance(stdout, str) and stdout.strip():
        out.append("--- switch CLI output ---\n")
        txt = stdout.replace("\r\n", "\n").replace("\r", "\n")
        if not txt.endswith("\n"):
            txt += "\n"
        out.append(txt)
        out.append("--- end ---\n")
        return out

    msg = data.get("msg")
    if isinstance(msg, list) and msg and all(isinstance(x, str) for x in msg):
        out.append("--- validation ---\n")
        for s in msg:
            out.append(s + "\n")
        out.append("--- end ---\n")
        return out
    if isinstance(msg, str) and msg.strip():
        out.append(msg.strip() + "\n")
        return out

    if len(rest) > 1500:
        out.append("[large ansible JSON — full copy in saved validation log]\n")
        return out
    out.append(rest + "\n")
    return out


def _extract_ssh_client_error_line(output):
    """Pick the ssh(1) client error line from shell task stdout (e.g. Connection refused)."""
    if not output:
        return ""
    keywords = (
        "connection refused",
        "connection timed out",
        "no route to host",
        "network is unreachable",
        "could not resolve hostname",
        "host key verification failed",
        "permission denied",
        "operation timed out",
    )

    # 1) Fast path: match ssh client line directly in raw output (works for escaped JSON payloads too).
    # Stop before quote/backslash/newline so we capture only the ssh line text.
    m_raw = re.search(r"(ssh:[^\\\n\r\"']+)", output, re.IGNORECASE)
    if m_raw:
        candidate = m_raw.group(1).strip()
        low = candidate.lower()
        if any(k in low for k in keywords):
            return candidate[:600]

    # 2) Decode common escaped newlines from ansible JSON and scan line-by-line.
    text = (
        output.replace("\\r\\n", "\n").replace("\\n", "\n").replace("\\r", "\n")
        .replace("\r\n", "\n").replace("\r", "\n")
    )
    for line in text.split("\n"):
        s = line.strip()
        if not s.lower().startswith("ssh:"):
            continue
        low = s.lower()
        if any(k in low for k in keywords):
            return s[:600]

    # 3) Any ssh: line as fallback
    m = re.search(r"(?m)^\s*(ssh:[^\n]+)", text, re.IGNORECASE)
    if m:
        return m.group(1).strip()[:600]
    return ""


def _extract_ansible_fatal_message(output, default=""):
    """Best-effort extract Ansible fail/fatal msg for UI (matches -vv JSON-ish lines)."""
    if not output:
        return (default or "").strip()
    idx = output.rfind("fatal:")
    chunk = output[idx : idx + 8000] if idx >= 0 else output[-8000:]
    m = re.search(r'"msg"\s*:\s*"((?:[^"\\]|\\.)*)"', chunk, re.DOTALL)
    if m:
        s = m.group(1).replace("\\n", "\n").replace("\\r", "\r").replace('\\"', '"')
        s = s.replace("\\\\", "\\").strip()
        return s[:2500] if s else (default or "").strip()
    m2 = re.search(r"msg:\s*>\s*\n((?:\s+[^\n]+\n)+)", chunk)
    if m2:
        return re.sub(r"^\s+", "", m2.group(1), flags=re.MULTILINE).strip()[:2500]
    return (default or "").strip()


def _write_validation_log_safe(full_output, platform_vars_name, appmgr_version):
    """Write log file; return filename or empty string on failure."""
    try:
        return _write_validation_log(full_output or "", platform_vars_name, appmgr_version or "")
    except Exception as ex:
        print("[Validate] Could not write validation log:", ex)
        return ""


def _validation_result_summary_text(parsed, application_table_name_version, application_table_raw):
    """Append to terminal before ---RESULT---."""
    lines = [
        "",
        "======================================================================",
        "  Validation summary",
        "======================================================================",
    ]
    if parsed.get("extracted_image_version"):
        lines.append("Software (image) version: %s" % parsed["extracted_image_version"])
    if parsed.get("extracted_appmgr_version"):
        lines.append("App-Mgr version: %s" % parsed["extracted_appmgr_version"])
    tbl = (application_table_name_version or "").strip() or (application_table_raw or "").strip()
    if tbl:
        lines.append("")
        lines.append("show application (summary):")
        lines.append(tbl)
    lines.append("======================================================================")
    return "\n".join(lines) + "\n"


# -----------------------------------------------------
# Validation tab: receive STK + credentials, save STK, update Ansible vars/inventory, run playbook
# -----------------------------------------------------
@app.route("/validate", methods=["POST"])
def validate():
    # Accept multipart/form-data: stkFile + switch_ip, switch_username, switch_password, expected_app_mgr_version
    stk_file = request.files.get("stkFile")
    switch_ip = (request.form.get("switch_ip") or "").strip()
    switch_username = (request.form.get("switch_username") or "").strip()
    switch_password = request.form.get("switch_password") or ""
    expected_app_mgr_version = (request.form.get("expected_app_mgr_version") or "").strip()
    platform_vars_name = (request.form.get("platform") or "").strip()
    if not stk_file or not stk_file.filename or not stk_file.filename.lower().endswith(".stk"):
        return jsonify({"success": False, "message": "Valid STK file is required"}), 400
    if not switch_ip or not switch_username:
        return jsonify({"success": False, "message": "Switch IP and username are required"}), 400

    filename = secure_filename(stk_file.filename).replace(" ", "_")
    dest_path = os.path.join(VALIDATION_STK_DIR, filename)
    stk_file.save(dest_path)

    expected_image_version = _extract_stk_image_version_from_filename(filename)
    if not expected_image_version:
        _clear_validation_stk_uploads()
        return jsonify({
            "success": False,
            "message": "Could not read firmware version (X.Y.Z.W) from the STK filename. Use a name like M4350-v14.0.6.19.stk.",
            "error_kind": "bad_filename",
        }), 400

    platform_log = (platform_vars_name or "unknown").strip() or "unknown"

    # Print received details to command line (for verification)
    print("-" * 50)
    print("[Validate] Received from frontend:")
    print("  STK file name :", filename)
    print("  Saved to      :", dest_path)
    print("  Switch IP     :", switch_ip)
    print("  Switch user   :", switch_username)
    print("  Switch pass   :", switch_password)
    print("  App-Mgr ver   :", expected_app_mgr_version or "(not set)")
    print("  Image ver     :", expected_image_version, "(from filename)")
    print("  Platform tag  :", platform_log)
    print("-" * 50)

    if not os.path.isdir(IMG_UPGRADE_DIR):
        _clear_validation_stk_uploads()
<<<<<<< HEAD
        return jsonify({"success": False, "message": "m4350_ansible directory not found"}), 500
    if not os.path.isfile(vars_path):
        return jsonify({
            "success": False,
            "message": "Ansible vars file not found for platform '%s' (expected: %s)" % (platform_vars_name, os.path.basename(vars_path)),
        }), 500
    if not os.path.isfile(M4350_INVENTORY_FILE):
        return jsonify({"success": False, "message": "Ansible inventory file not found: inventory/hosts.yml"}), 500
=======
        return jsonify({"success": False, "message": "img_upgrade directory not found", "error_kind": "config"}), 500
    if not os.path.isfile(IMG_UPGRADE_ROLE_VARS):
        _clear_validation_stk_uploads()
        return jsonify({"success": False, "message": "img_upgrade role vars (main.yml) not found", "error_kind": "config"}), 500
>>>>>>> e1be0f9 (Untested Validation UI page, merged Ansible)

    try:
        _update_img_upgrade_inventory(switch_ip, switch_username, switch_password)
        _update_img_upgrade_role_vars(
            filename,
            VALIDATION_STK_DIR,
            expected_image_version,
            expected_app_mgr_version or "0.0.0.0",
        )
    except Exception as e:
        print("[Validate] Failed to update Ansible files:", e)
        _clear_validation_stk_uploads()
        return jsonify({"success": False, "message": "Failed to update Ansible config: %s" % str(e), "error_kind": "config"}), 500

    # Stream playbook output line by line, then send result JSON
    def generate():
        playbook_cmd = [
            "ansible-playbook",
            "-i", "inventory.ini",
            "playbooks/image_upgrade.yml",
            "-vv",
        ]
        if shutil.which("stdbuf"):
            cmd = ["stdbuf", "-oL", "-eL"] + playbook_cmd
        else:
            cmd = playbook_cmd
        sub_env = os.environ.copy()
        sub_env["PYTHONUNBUFFERED"] = "1"

        full_lines = []
<<<<<<< HEAD
        env = os.environ.copy()
        env["ANSIBLE_FORCE_COLOR"] = "1"  # emit ANSI colors even when stdout is a pipe
        try:
            process = subprocess.Popen(
                cmd,
                cwd=M4350_ANSIBLE_DIR,
=======
        process = None
        disk_log_written = False
        max_wait = _validation_playbook_wait_seconds()

        def stop_ansible():
            nonlocal process
            if not process or process.poll() is not None:
                return
            try:
                process.terminate()
                try:
                    process.wait(timeout=12)
                except subprocess.TimeoutExpired:
                    process.kill()
                    try:
                        process.wait(timeout=5)
                    except Exception:
                        pass
            except Exception as ex:
                print("[Validate] Could not stop ansible subprocess:", ex)

        try:
            process = subprocess.Popen(
                cmd,
                cwd=IMG_UPGRADE_DIR,
>>>>>>> e1be0f9 (Untested Validation UI page, merged Ansible)
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
<<<<<<< HEAD
                env=env,
            )
            for line in iter(process.stdout.readline, ""):
                full_lines.append(line)
                yield line
            process.wait(timeout=600)
        except subprocess.TimeoutExpired:
            if process:
                process.kill()
            full_lines.append("\n[Ansible playbook timed out.]\n")
            yield "\n[Ansible playbook timed out.]\n"
            yield VALIDATION_STREAM_RESULT_MARKER + json.dumps({
                "success": False, "message": "Ansible playbook timed out.",
                "application_table": "", "application_table_name_version": "", "appmgr_version": None, "expected_version": expected_app_mgr_version.strip() or None, "version_match": False,
            })
            return
        except FileNotFoundError:
            msg = "ansible-playbook not found. Ensure Ansible is installed and on PATH."
            full_lines.append("\n" + msg + "\n")
            yield "\n" + msg + "\n"
            yield VALIDATION_STREAM_RESULT_MARKER + json.dumps({
                "success": False, "message": msg,
                "application_table": "", "application_table_name_version": "", "appmgr_version": None, "expected_version": expected_app_mgr_version.strip() or None, "version_match": False,
            })
            return
        except Exception as e:
            full_lines.append("\n" + str(e) + "\n")
            yield "\n" + str(e) + "\n"
            yield VALIDATION_STREAM_RESULT_MARKER + json.dumps({
                "success": False, "message": str(e),
                "application_table": "", "application_table_name_version": "", "appmgr_version": None, "expected_version": expected_app_mgr_version.strip() or None, "version_match": False,
            })
            return
=======
                env=sub_env,
            )
            for line in iter(process.stdout.readline, ""):
                full_lines.append(line)
                for vis in _validation_stream_simplify_ansible_line(line):
                    yield vis
            process.wait(timeout=max_wait)
>>>>>>> e1be0f9 (Untested Validation UI page, merged Ansible)

            output = "".join(full_lines)
            parsed = _parse_img_upgrade_ansible_output(output)
            application_table, appmgr_from_table = _parse_application_table_and_version(output)
            appmgr_version = (parsed.get("extracted_appmgr_version") or appmgr_from_table or "").strip() or None
            application_table_name_version = _table_name_version_only(application_table)

            yield _validation_result_summary_text(
                parsed,
                application_table_name_version,
                application_table,
            )

            log_filename = _write_validation_log(output, platform_log, appmgr_version or "")
            disk_log_written = True
            expected_ver = (expected_app_mgr_version or "").strip()
            rc_ok = process.returncode == 0

            if parsed["image_mismatch"]:
                success = False
                error_kind = "image_mismatch"
                detail = parsed.get("image_mismatch_snippet") or ""
                message = (
                    "Image mismatch: the STK does not match this switch model. "
                    "Use an image built for the connected hardware."
                )
                if detail:
                    message += "\n\n" + detail
                version_match = False
            elif not rc_ok:
                success = False
                fatal_detail = _extract_ansible_fatal_message(output)
                if parsed["upgrade_reverted"]:
                    error_kind = "upgrade_failed"
                    message = (
                        "Test image did not match expected versions; the switch was reverted to the prior image."
                    )
                else:
                    ssh_line = _extract_ssh_client_error_line(output)
                    if ssh_line:
                        error_kind = "ssh_unreachable"
                        message = (
                            ssh_line
                            + "\n\nSSH might be disabled on the switch. "
                            "If SSH is enabled, verify the IP address and that port 22 is reachable (firewall / VLAN)."
                        )
                    else:
                        error_kind = "ansible_failed"
                        message = fatal_detail or (
                            "Ansible reported failure (SSH, file copy, or playbook error). See output above."
                        )
                version_match = False
            else:
                success = True
                error_kind = None
                message = (
                    "Validation succeeded: software and App-Mgr versions match the expected values."
                )
                # Playbook verify_upgrade sets upgrade_success from extracted vs expected; trust it when present
                # so we do not mark mismatch from an earlier "Software Version" line in the log.
                if parsed.get("upgrade_success_flag") is True:
                    version_match = True
                elif parsed.get("upgrade_success_flag") is False:
                    version_match = False
                else:
                    version_match = True
                    if expected_ver and appmgr_version and expected_ver != appmgr_version:
                        version_match = False
                    if expected_image_version and parsed.get("extracted_image_version"):
                        if parsed["extracted_image_version"] != expected_image_version:
                            version_match = False

                if not version_match:
                    error_kind = "version_mismatch"
                    message = (
                        "Firmware or App-Mgr on the switch does not match your expected values."
                    )

            result = {
                "success": success,
                "error_kind": error_kind,
                "message": message,
                "stk_file_path": dest_path,
                "log_file": log_filename,
                "application_table": application_table,
                "application_table_name_version": application_table_name_version,
                "appmgr_version": appmgr_version,
                "expected_version": expected_ver or None,
                "expected_image_version": expected_image_version,
                "image_version": parsed.get("extracted_image_version") or None,
                "version_match": version_match,
            }
            if not success:
                result["details"] = output[:4000] if output else ""
                if error_kind == "ssh_unreachable":
                    sl = _extract_ssh_client_error_line(output)
                    if sl:
                        result["ansible_fatal_message"] = sl
                else:
                    fd = _extract_ansible_fatal_message(output)
                    if fd:
                        result["ansible_fatal_message"] = fd
            yield VALIDATION_STREAM_RESULT_MARKER + json.dumps(result)
        except subprocess.TimeoutExpired:
            stop_ansible()
            tail = "\n[Ansible did not exit within %ss after output ended.]\n" % max_wait
            full_lines.append(tail)
            yield tail
            output_so_far = "".join(full_lines)
            log_fn = _write_validation_log_safe(output_so_far, platform_log, "")
            disk_log_written = bool(log_fn)
            yield VALIDATION_STREAM_RESULT_MARKER + json.dumps({
                "success": False,
                "message": "Ansible playbook timed out.",
                "error_kind": "timeout",
                "application_table": "", "application_table_name_version": "", "appmgr_version": None,
                "expected_version": expected_app_mgr_version.strip() or None,
                "expected_image_version": expected_image_version,
                "version_match": False,
                "log_file": log_fn or None,
            })
        except FileNotFoundError:
            msg = "ansible-playbook not found. Ensure Ansible is installed and on PATH."
            full_lines.append("\n" + msg + "\n")
            yield "\n" + msg + "\n"
            output_so_far = "".join(full_lines)
            log_fn = _write_validation_log_safe(output_so_far, platform_log, "")
            disk_log_written = bool(log_fn)
            yield VALIDATION_STREAM_RESULT_MARKER + json.dumps({
                "success": False, "message": msg, "error_kind": "ansible_missing",
                "application_table": "", "application_table_name_version": "", "appmgr_version": None,
                "expected_version": expected_app_mgr_version.strip() or None,
                "expected_image_version": expected_image_version,
                "version_match": False,
                "log_file": log_fn or None,
            })
        except Exception as e:
            full_lines.append("\n" + str(e) + "\n")
            yield "\n" + str(e) + "\n"
            output_so_far = "".join(full_lines)
            log_fn = _write_validation_log_safe(output_so_far, platform_log, "")
            disk_log_written = bool(log_fn)
            yield VALIDATION_STREAM_RESULT_MARKER + json.dumps({
                "success": False, "message": str(e), "error_kind": "ansible_failed",
                "application_table": "", "application_table_name_version": "", "appmgr_version": None,
                "expected_version": expected_app_mgr_version.strip() or None,
                "expected_image_version": expected_image_version,
                "version_match": False,
                "log_file": log_fn or None,
            })
        finally:
            stop_ansible()
            if not disk_log_written:
                chunk = "".join(full_lines)
                if chunk.strip():
                    suffix = (
                        "\n\n[Incomplete: no full result JSON — often Gunicorn worker --timeout "
                        "(readme used 600s; image upgrades can exceed that). "
                        "Restart with: gunicorn --timeout 0 ... or --timeout 7200]\n"
                    )
                    fn = _write_validation_log_safe(chunk + suffix, platform_log, "")
                    if fn:
                        print("[Validate] Wrote partial validation log:", fn)
            _clear_validation_stk_uploads()

    return Response(
        stream_with_context(generate()),
        content_type="text/plain; charset=utf-8",
        headers={
            "X-Content-Type-Options": "nosniff",
            "Cache-Control": "no-store",
            "X-Accel-Buffering": "no",
        },
    )


@app.route("/delete-log/<filename>", methods=["DELETE"])
def delete_log(filename):

    path = os.path.join(LOG_DIR, filename)

    if not os.path.exists(path):
        return jsonify({"error": "Log not found"}), 404

    os.remove(path)

    return jsonify({"status": "deleted"})

@app.route("/")
def serve_index():
    return send_from_directory(FRONTEND_DIR, "index.html")

@app.route("/<path:filename>")
def serve_static(filename):
    return send_from_directory(FRONTEND_DIR, filename)
# =====================================================

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8000)
