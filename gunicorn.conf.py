# Gunicorn config for stkForge (long-running /validate ansible streams).
# Start: gunicorn -c gunicorn.conf.py backend_new:app
#
# timeout: workers that do not report to the master within this many seconds
# are killed. Image upgrade validation often exceeds 10–15 minutes; use 0 to
# disable the worker silence/kill timeout (recommended for this app on Linux).

bind = "0.0.0.0:8000"
workers = 4
timeout = 0
