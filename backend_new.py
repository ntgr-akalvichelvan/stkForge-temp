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

# m4350_ansible: update vars + inventory from Validation tab, then run image upgrade playbook
PACKAGE_DIR = os.path.dirname(os.path.abspath(__file__))
M4350_ANSIBLE_DIR = os.path.join(PACKAGE_DIR, "m4350_ansible")
M4350_INVENTORY_FILE = os.path.join(M4350_ANSIBLE_DIR, "inventory", "hosts.yml")

# Allowed Ansible vars filenames (frontend sends lowercase: m4350, m4300, m4250H, m4250L)
VALIDATION_PLATFORM_VARS = ("m4350", "m4300", "m4250H", "m4250L")

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

    time = parts[-1]
    date = parts[-2]
    version = parts[-3]
    platform = "_".join(parts[:-3])

    formatted_date = f"{date[:4]}-{date[4:6]}-{date[6:8]}"
    formatted_time = f"{time[:2]}:{time[2:4]}:{time[4:6]}"

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
# Ansible: update vars and inventory from Validation inputs, run playbook
# -----------------------------------------------------
def _get_vars_path(platform_vars_name):
    """Path to vars file e.g. m4350.yml, m4300.yml, m4250H.yml."""
    return os.path.join(M4350_ANSIBLE_DIR, "vars", platform_vars_name + ".yml")


def _update_platform_vars(vars_path, image_file, image_path, expected_app_mgr_version):
    """Update given vars file with image_file, image_path, expected_version."""
    with open(vars_path, "r") as f:
        content = f.read()
    content = re.sub(r"^image_file:\s*.+$", "image_file: %s" % image_file, content, flags=re.MULTILINE)
    content = re.sub(r"^image_path:\s*.+$", "image_path: %s" % image_path, content, flags=re.MULTILINE)
    content = re.sub(
        r"^(\s*expected_version:\s*)[\"'].*?[\"']",
        r'\g<1>"%s"' % expected_app_mgr_version.replace("\\", "\\\\").replace('"', '\\"'),
        content,
        flags=re.MULTILINE,
    )
    with open(vars_path, "w") as f:
        f.write(content)


def _update_m4350_inventory(switch_ip, switch_username, switch_password):
    """Update m4350_ansible/inventory/hosts.yml with switch connection details."""
    with open(M4350_INVENTORY_FILE, "r") as f:
        content = f.read()
    content = re.sub(r"^(\s*ansible_host:\s*).+$", r"\g<1>%s" % switch_ip, content, flags=re.MULTILINE)
    content = re.sub(r"^(\s*ansible_user:\s*).+$", r"\g<1>%s" % switch_username, content, flags=re.MULTILINE)
    # Quote password for YAML in case of special chars
    pw_escaped = switch_password.replace("\\", "\\\\").replace('"', '\\"')
    content = re.sub(r"^(\s*ansible_password:\s*).+$", r'\g<1>"%s"' % pw_escaped, content, flags=re.MULTILINE)
    with open(M4350_INVENTORY_FILE, "w") as f:
        f.write(content)


def _run_ansible_playbook(platform_extra_var):
    """Run ansible-playbook from m4350_ansible dir. platform_extra_var e.g. m4350 for vars/{{ platform }}.yml."""
    cmd = [
        "ansible-playbook",
        "-i", "inventory/hosts.yml",
        "playbooks/03_image_upgrade.yml",
        "-e", "platform=%s" % platform_extra_var,
        "-vv",
    ]
    try:
        result = subprocess.run(
            cmd,
            cwd=M4350_ANSIBLE_DIR,
            capture_output=True,
            text=True,
            timeout=600,
        )
        out = (result.stdout or "") + (result.stderr or "")
        if result.returncode != 0:
            return False, out
        return True, out
    except subprocess.TimeoutExpired:
        return False, "Ansible playbook timed out."
    except FileNotFoundError:
        return False, "ansible-playbook not found. Ensure Ansible is installed and on PATH."
    except Exception as e:
        return False, str(e)


VALIDATION_STREAM_RESULT_MARKER = "\n---RESULT---\n"


VALIDATION_LOG_HEADER = (
    "\n"
    "===============================================================================\n"
    "  VALIDATION TAB LOG  (Ansible image upgrade / application table)\n"
    "===============================================================================\n\n"
)


def _write_validation_log(full_output):
    """Write full command output to a log file in LOG_DIR with validation tag. Returns log filename."""
    os.makedirs(LOG_DIR, exist_ok=True)
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    log_filename = "validation_%s.log" % timestamp
    log_path = os.path.join(LOG_DIR, log_filename)
    with open(log_path, "w") as f:
        f.write(VALIDATION_LOG_HEADER)
        f.write(full_output)
    return log_filename


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
    if platform_vars_name not in VALIDATION_PLATFORM_VARS:
        return jsonify({
            "success": False,
            "message": "Invalid platform. Expected one of: m4350, m4300, m4250H, m4250L.",
        }), 400

    filename = secure_filename(stk_file.filename).replace(" ", "_")
    dest_path = os.path.join(VALIDATION_STK_DIR, filename)
    stk_file.save(dest_path)

    # Print received details to command line (for verification)
    print("-" * 50)
    print("[Validate] Received from frontend:")
    print("  STK file name :", filename)
    print("  Saved to      :", dest_path)
    print("  Switch IP     :", switch_ip)
    print("  Switch user   :", switch_username)
    print("  Switch pass   :", switch_password)
    print("  App-Mgr ver   :", expected_app_mgr_version or "(not set)")
    print("  Platform      :", platform_vars_name, "(vars/%s.yml)" % platform_vars_name)
    print("-" * 50)

    vars_path = _get_vars_path(platform_vars_name)
    if not os.path.isdir(M4350_ANSIBLE_DIR):
        return jsonify({"success": False, "message": "m4350_ansible directory not found"}), 500
    if not os.path.isfile(vars_path) or not os.path.isfile(M4350_INVENTORY_FILE):
        return jsonify({"success": False, "message": "Ansible vars or inventory file not found"}), 500

    try:
        _update_platform_vars(vars_path, filename, VALIDATION_STK_DIR, expected_app_mgr_version or "0.0.0.0")
        _update_m4350_inventory(switch_ip, switch_username, switch_password)
    except Exception as e:
        print("[Validate] Failed to update Ansible files:", e)
        return jsonify({"success": False, "message": "Failed to update Ansible config: %s" % str(e)}), 500

    # Stream playbook output line by line, then send result JSON
    def generate():
        cmd = [
            "ansible-playbook",
            "-i", "inventory/hosts.yml",
            "playbooks/03_image_upgrade.yml",
            "-e", "platform=%s" % platform_vars_name,
            "-vv",
        ]
        full_lines = []
        try:
            process = subprocess.Popen(
                cmd,
                cwd=M4350_ANSIBLE_DIR,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
                bufsize=1,
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
                "application_table": "", "appmgr_version": None, "expected_version": expected_app_mgr_version.strip() or None, "version_match": False,
            })
            return
        except FileNotFoundError:
            msg = "ansible-playbook not found. Ensure Ansible is installed and on PATH."
            full_lines.append("\n" + msg + "\n")
            yield "\n" + msg + "\n"
            yield VALIDATION_STREAM_RESULT_MARKER + json.dumps({
                "success": False, "message": msg,
                "application_table": "", "appmgr_version": None, "expected_version": expected_app_mgr_version.strip() or None, "version_match": False,
            })
            return
        except Exception as e:
            full_lines.append("\n" + str(e) + "\n")
            yield "\n" + str(e) + "\n"
            yield VALIDATION_STREAM_RESULT_MARKER + json.dumps({
                "success": False, "message": str(e),
                "application_table": "", "appmgr_version": None, "expected_version": expected_app_mgr_version.strip() or None, "version_match": False,
            })
            return

        output = "".join(full_lines)
        success = process.returncode == 0
        log_filename = _write_validation_log(output)
        application_table, appmgr_version = _parse_application_table_and_version(output)
        expected_ver = (expected_app_mgr_version or "").strip()
        version_match = bool(expected_ver and appmgr_version and expected_ver == appmgr_version.strip())
        result = {
            "success": success,
            "message": "Validation completed successfully" if (success and version_match) else ("Validation completed; version mismatch" if success else "Validation (Ansible) failed"),
            "stk_file_path": dest_path,
            "log_file": log_filename,
            "application_table": application_table,
            "appmgr_version": appmgr_version or None,
            "expected_version": expected_ver or None,
            "version_match": version_match,
        }
        if not success:
            result["details"] = output[:2000] if output else ""
        yield VALIDATION_STREAM_RESULT_MARKER + json.dumps(result)

    return Response(
        stream_with_context(generate()),
        content_type="text/plain; charset=utf-8",
        headers={"X-Content-Type-Options": "nosniff"},
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
