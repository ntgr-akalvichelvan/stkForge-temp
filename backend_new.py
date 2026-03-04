import os
import re
import uuid
import shutil
import subprocess
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime

from flask import Flask, request, jsonify, send_file, Response, stream_with_context
from flask_cors import CORS

from flask import send_from_directory

FRONTEND_DIR = os.path.join(os.path.dirname(__file__), "frontend")


# =====================================================
# CONFIG
# =====================================================

WORK_DIR = "/home/vspl007/Downloads/Management_switch_Package/ImagePacking" #//home/swnuc04/arun/stkForge-temp/ImagePacking"
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
# GLOBAL JOB STORE (Thread-Safe)
# =====================================================

jobs = {}
jobs_lock = threading.Lock()

# Limit concurrent packaging jobs
executor = ThreadPoolExecutor(max_workers=4)

# =====================================================
# BACKGROUND TASK
# =====================================================

def run_packaging(job_id, job_meta):

    job_dir = job_meta["job_dir"]

    with jobs_lock:
        jobs[job_id]["status"] = "running"
        jobs[job_id]["progress"] = 0

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
                        with jobs_lock:
                            jobs[job_id]["progress"] = pct
                    except:
                        pass

        process.wait()

        if process.returncode != 0 or error_detected:

            shutil.rmtree(job_dir, ignore_errors=True)

            with jobs_lock:
                jobs[job_id]["status"] = "failed"
                jobs[job_id]["log"] = log_filename

            return

        # ✅ Look inside outputs folder
        outputs_dir = os.path.join(job_dir, "output")

        if not os.path.isdir(outputs_dir):
            with jobs_lock:
                jobs[job_id]["status"] = "failed"
                jobs[job_id]["log"] = log_filename
            return

        output_files = [
            f for f in os.listdir(outputs_dir)
            if f.lower().endswith(".stk")
        ]

        if not output_files:
            with jobs_lock:
                jobs[job_id]["status"] = "failed"
                jobs[job_id]["log"] = log_filename
            return

        output_path = os.path.join(outputs_dir, output_files[0])

        with jobs_lock:
            jobs[job_id]["progress"] = 100
            jobs[job_id]["status"] = "finished"
            jobs[job_id]["output"] = output_path

    except Exception as e:
        print("Packaging error:", e)
        with jobs_lock:
            jobs[job_id]["status"] = "failed"
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

    stk_path = os.path.join(job_dir, stk_file.filename)
    agent_path = os.path.join(job_dir, agent_file.filename)

    stk_file.save(stk_path)
    agent_file.save(agent_path)

    with jobs_lock:
        jobs[job_id] = {
            "status": "queued",
            "progress": 0,
            "output": None
        }

    executor.submit(run_packaging, job_id, {
        "platform": ALLOWED_PLATFORMS[platform_ui],
        "stk_file": stk_file.filename,
        "agent_file": agent_file.filename,
        "new_version": new_version,
        "job_dir": job_dir
    })

    return jsonify({"job_id": job_id})


# -----------------------------------------------------

@app.route("/progress/<job_id>")
def get_progress(job_id):

    with jobs_lock:
        job = jobs.get(job_id)

    if not job:
        return jsonify({"status": "failed", "progress": 0})

    return jsonify({
    "status": job["status"],
    "progress": job["progress"],
    "log": job.get("log")
    })

# -----------------------------------------------------

@app.route("/download/<job_id>")
def download(job_id):

    with jobs_lock:
        job = jobs.get(job_id)

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

    with jobs_lock:
        jobs.pop(job_id, None)

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


@app.route("/")
def serve_index():
    return send_from_directory(FRONTEND_DIR, "index.html")

@app.route("/<path:filename>")
def serve_static(filename):
    return send_from_directory(FRONTEND_DIR, filename)
# =====================================================

if __name__ == "__main__":
    app.run(host="0.0.0.0", port=5000)
