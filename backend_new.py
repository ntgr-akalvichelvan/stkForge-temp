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

os.makedirs(LOG_DIR, exist_ok=True)
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
    app.run(host="0.0.0.0", port=5000)
