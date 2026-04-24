# Project title: stkForge

## description: 
**Input:** "M4xxx---.stk" | "App_Mgr.tar.gz" | new-version
**Output:** "M4xxx----.stk"
**function:** Unpacks the Image file (.stk), swaps old app-mgr file with new app-mgr file and changes the version
              to give new Image file

   *Hosting in NUC -> swnuc01 (IP: 10.25.4.201)*

## Path in NUC:
    `/home/swnuc04/arun/stkForge-temp/ImagePacking`

## dependecies:
    1) python3
    2) gunicorn
    3) flask
    4) 'flask-cors' 
    5) export mkimage to PATH
    6) Install openlibssl-1.1 version (latest Ubuntu have openlibssl-1.3)
    7) redis-server
    8) python3-redis
    9) apt install device-tree-compiler (needed for dtc compilation)

## Start Backend & Frontend Server
Validation (`/validate`) streams ansible for a long time. **Do not use `--timeout 600`** — Gunicorn will kill the worker (~10 min) mid-playbook, the browser stream dies, and no `---RESULT---` JSON is sent. Prefer **`--timeout 0`** (disable worker timeout) or **`7200`** or higher.

```bash
gunicorn -w 4 -b 0.0.0.0:8000 --timeout 0 backend_new:app
# or: gunicorn -c gunicorn.conf.py backend_new:app
```
**for Running the server in BackGround**
```bash
nohup gunicorn -w 4 -b 0.0.0.0:8000 --timeout 0 backend_new:app > gunicorn.log 2>&1 &
```

## New handling of Image

Image for Download will not be stacked in browser cache (first) and downloaded when the button pressed
It will initate download after the download button is pressed.

## Minor bug fix and New feature

Adding Ability to view logs without downloading

## Ansible -> validation

Adding UI and Ansible script to validate the image script for detecting app-mgr version

custom ansible script for all platform image validation added,
UN-TESTED (M4250, M4350, M4300)
