#!/usr/bin/env python3
import pexpect
import time
import subprocess
import os

SWITCH_IP = "192.168.1.8"
USERNAME = "admin"
NEW_PASSWORD = "Netgear1@"   # 8+ characters

def first_time_telnet_password_set():
    print("[*] First-time telnet login (blank password)...")

    child = pexpect.spawn(f"telnet {SWITCH_IP}", timeout=60)
    child.logfile = None

    child.expect("User:")
    child.sendline(USERNAME)

    # First login has NO password → press Enter
    child.expect("Password:")
    child.sendline("")

    # Forced password change
    child.expect("New password:")
    child.sendline(NEW_PASSWORD)

    child.expect("Re-enter new password:")
    child.sendline(NEW_PASSWORD)

    child.expect("Password change is successful.")
    child.expect(pexpect.EOF)

    print("[+] Admin password configured successfully")


def telnet_enable_ssh():
    print("[*] Reconnecting via telnet to enable SSH...")

    child = pexpect.spawn(f"telnet {SWITCH_IP}", timeout=60)

    child.expect("User:")
    child.sendline(USERNAME)

    child.expect("Password:")
    child.sendline(NEW_PASSWORD)

    child.expect(">")
    child.sendline("en")

    child.expect("#")
    child.sendline("configure")

    # RSA
    child.expect("\(Config\)#")
    child.sendline("crypto key generate rsa")
    child.expect("overwrite.*\\(y/n\\):")
    child.sendline("y")
    child.expect("complete.")

    # ECDSA
    child.sendline("crypto key generate ecdsa")
    child.expect("overwrite.*\\(y/n\\):")
    child.sendline("y")
    child.expect("complete.")

    # DSA
    child.sendline("crypto key generate dsa")
    child.expect("overwrite.*\\(y/n\\):")
    child.sendline("y")
    child.expect("complete.")

    child.sendline("exit")
    child.expect("#")

    child.sendline("ip ssh server enable")
    child.expect("#")

    child.sendline("ip ssh port 22")
    child.expect("#")

    child.sendline("exit")
    child.expect(">")

    child.sendline("logout")
    child.expect(pexpect.EOF)

    print("[+] SSH enabled successfully")


def cleanup_known_hosts():
    print("[*] Cleaning SSH known_hosts entry...")
    subprocess.run(
        ["ssh-keygen", "-R", SWITCH_IP],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )


def ssh_login():
    print("[*] Connecting via SSH...")

    child = pexpect.spawn(f"ssh admin@{SWITCH_IP}", timeout=30)

    i = child.expect([
        "Are you sure you want to continue connecting",
        "password:",
        pexpect.TIMEOUT
    ])

    if i == 0:
        child.sendline("yes")
        child.expect("password:")
        child.sendline(NEW_PASSWORD)
    elif i == 1:
        child.sendline(NEW_PASSWORD)

    child.interact()


if __name__ == "__main__":
    first_time_telnet_password_set()
    time.sleep(2)

    telnet_enable_ssh()
    time.sleep(2)

    cleanup_known_hosts()
    ssh_login()
