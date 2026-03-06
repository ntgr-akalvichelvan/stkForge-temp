# Project title: stkForge

## description: 
**Input:** "M4xxx---.stk" | "App_Mgr.tar.gz" | new-version
**Output:** "M4xxx----.stk"
**function:** Unpacks the Image file (.stk), swaps old app-mgr file with new app-mgr file and changes the version
              to give new Image file

   *Hosting in NUC -> swnuc04 (IP: 10.25.5.0)*
    
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
```bash
        gunicorn -w 4 -b 0.0.0.0:5000 backend_new:app
```
**for Running the server in BackGround**
```bash
    nohup gunicorn -w 4 -b 0.0.0.0:5000 backend_new:app > gunicorn.log 2>&1 &
```
