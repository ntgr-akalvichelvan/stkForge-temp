#!/usr/bin/env python3
import os
import subprocess
import sys
import time

# --------------------------------------------------
# BASE DIRECTORY (directory of this Python file)
# --------------------------------------------------
BASE_DIR = os.path.abspath(os.path.dirname(__file__))

# --------------------------------------------------
# FIXED TEST INPUTS (CHANGE IF NEEDED)
# --------------------------------------------------
SCRIPT_NAME = "run_packaging.sh"

PLATFORM = "M4350"
AGENT_FILE = "M4350_agent_appmgr_1.0.5.27_devicemgr_2.2.13.25.tar.gz"
IMAGE_FILE = "M4350-v14.0.6.14.stk"
NEW_VERSION = "14.0.6.16"

# --------------------------------------------------
# FULL PATHS
# --------------------------------------------------
SCRIPT_PATH = os.path.join(BASE_DIR, SCRIPT_NAME)
AGENT_PATH = os.path.join(BASE_DIR, AGENT_FILE)
IMAGE_PATH = os.path.join(BASE_DIR, IMAGE_FILE)
OUTPUT_DIR = os.path.join(BASE_DIR, "output")

# --------------------------------------------------
# PRE-FLIGHT CHECKS
# --------------------------------------------------
print("=== PRE-FLIGHT CHECKS ===")
print("BASE_DIR :", BASE_DIR)

required_files = {
    "Script": SCRIPT_PATH,
    "Agent": AGENT_PATH,
    "Image": IMAGE_PATH,
}

for name, path in required_files.items():
    if not os.path.exists(path):
        print(f"❌ {name} missing: {path}")
        sys.exit(1)
    else:
        print(f"✅ {name} found: {path}")

# Script must be executable
if not os.access(SCRIPT_PATH, os.X_OK):
    print("⚠ Script not executable, fixing permissions...")
    os.chmod(SCRIPT_PATH, 0o755)

# --------------------------------------------------
# RUN SCRIPT
# --------------------------------------------------
print("\n=== RUNNING SCRIPT ===")

cmd = [
    "/bin/bash",
    SCRIPT_PATH,
    PLATFORM,
    AGENT_FILE,
    IMAGE_FILE,
    NEW_VERSION
]

print("Command:")
print(" ".join(cmd))
print("Working directory:", BASE_DIR)

start_time = time.time()

result = subprocess.run(
    cmd,
    cwd=BASE_DIR,
    stdout=subprocess.PIPE,
    stderr=subprocess.PIPE,
    text=True
)

elapsed = time.time() - start_time

# --------------------------------------------------
# OUTPUT
# --------------------------------------------------
print("\n=== SCRIPT STDOUT ===")
print(result.stdout if result.stdout else "(no stdout)")

print("\n=== SCRIPT STDERR ===")
print(result.stderr if result.stderr else "(no stderr)")

print("\nReturn code:", result.returncode)
print(f"Execution time: {elapsed:.2f} seconds")

if result.returncode != 0:
    print("\n❌ Script execution FAILED")
    sys.exit(1)

print("\n✅ Script execution SUCCESS")

# --------------------------------------------------
# VERIFY OUTPUT FILES
# --------------------------------------------------
print("\n=== OUTPUT VERIFICATION ===")

if not os.path.isdir(OUTPUT_DIR):
    print("❌ Output directory not found:", OUTPUT_DIR)
    sys.exit(1)

output_files = sorted(
    os.listdir(OUTPUT_DIR),
    key=lambda f: os.path.getmtime(os.path.join(OUTPUT_DIR, f)),
    reverse=True
)

if not output_files:
    print("❌ No output files generated")
    sys.exit(1)

print("Generated output files:")
for f in output_files:
    path = os.path.join(OUTPUT_DIR, f)
    size = os.path.getsize(path)
    print(f"  {f}  -->  {size:,} bytes")

# --------------------------------------------------
# CHECKSUM (OPTIONAL BUT RECOMMENDED)
# --------------------------------------------------
print("\n=== SHA256 CHECKSUMS ===")
for f in output_files:
    path = os.path.join(OUTPUT_DIR, f)
    checksum = subprocess.check_output(["sha256sum", path]).decode().strip()
    print(checksum)

print("\n🎯 TEST COMPLETED SUCCESSFULLY")
'''
import os
import re
import shutil
from flask import Flask, request, send_file, jsonify
from flask_cors import CORS

app = Flask(__name__)
CORS(app)

# ---------------- CONFIG ----------------
WORK_DIR = "/home/vspl007/Downloads/Management_switch_Package/ImagePacking"

def parse_version(version):
    return tuple(map(int, version.split(".")))

def extract_version_from_stk(filename):
    match = re.search(r"-V([\d.]+)\.stk$", filename)
    return match.group(1) if match else None

# ---------------- ROUTE ----------------
@app.route("/generate", methods=["POST"])
def generate():
    stk_file = request.files.get("stkFile")
    agent_file = request.files.get("agentFile")
    new_version = request.form.get("newVersion")

    if not stk_file or not agent_file or not new_version:
        return jsonify({
            "success": False,
            "message": "Missing required inputs"
        }), 400

    # ---------- Extract current version ----------
    current_version = extract_version_from_stk(stk_file.filename)
    if not current_version:
        return jsonify({
            "success": False,
            "message": "Invalid STK filename format"
        }), 400

    try:
        if parse_version(new_version) <= parse_version(current_version):
            return jsonify({
                "success": False,
                "message": f"New version must be greater than current version ({current_version})"
            }), 400
    except Exception:
        return jsonify({
            "success": False,
            "message": "Invalid version format"
        }), 400

    # ---------- Save uploaded files ----------
    stk_path = os.path.join(WORK_DIR, stk_file.filename)
    agent_path = os.path.join(WORK_DIR, agent_file.filename)

    stk_file.save(stk_path)
    agent_file.save(agent_path)

    print("===== FILES RECEIVED =====")
    print(stk_path)
    print(agent_path)
    print("==========================")

    # ---------- Simulated output (replace with script output later) ----------
    output_filename = stk_file.filename.replace(current_version, new_version)
    output_path = os.path.join(WORK_DIR, output_filename)

    shutil.copy(stk_path, output_path)

    print("===== OUTPUT GENERATED =====")
    print(output_path)
    print("============================")

    # ---------- Send file ----------
    response = send_file(
        output_path,
        as_attachment=True,
        download_name=output_filename,
        mimetype="application/octet-stream"
    )

    # ---------- CLEANUP AFTER DOWNLOAD ----------
    def cleanup():
        for f in [stk_path, agent_path, output_path]:
            try:
                if os.path.exists(f):
                    os.remove(f)
                    print(f"Deleted: {f}")
            except Exception as e:
                print("Cleanup error:", e)

    response.call_on_close(cleanup)

    return response

# ---------------- MAIN ----------------
if __name__ == "__main__":
    app.run(host="127.0.0.1", port=5000, debug=True)

'''
