#!/usr/bin/env python3
"""
Watch GitHub for new commits, then build, flash and capture the board's log.

    python tools/autoflash.py --port COM4

Leave it running with the board plugged in. Every --interval seconds it
fetches the branch; when there's a new commit it pulls, flashes the board
(pio run -t upload) and saves the start-up log to logs/. When the commit
changes the Wi-Fi page (web/) or data/, it also uploads the file system
(pio run -t uploadfs). That wipes sessions saved in flash (not the SD card),
so it only happens when those files change; --uploadfs forces it.

--push-logs also commits each log to a "device-logs" branch so it can be
read without copying it by hand. Logs contain GPS positions (where the
board is), so only use this if the repository is private.

Needs git and PlatformIO on the PATH (or installed for this Python), and
pyserial (comes with PlatformIO).
"""

import argparse
import datetime
import os
import subprocess
import sys
import time

TOOLS = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(TOOLS)
LOG_DIR = os.path.join(REPO, "logs")
LOG_BRANCH = "device-logs"
sys.path.insert(0, TOOLS)


def run(cmd, cwd=REPO, check=True, capture=True):
    r = subprocess.run(cmd, cwd=cwd, text=True, capture_output=capture)
    if check and r.returncode != 0:
        raise RuntimeError(f"{' '.join(cmd)} failed:\n{(r.stdout or '')[-2000:]}{(r.stderr or '')[-2000:]}")
    return r


def git(*args, check=True):
    return run(["git", *args], check=check).stdout.strip()


def say(msg):
    print(f"[{datetime.datetime.now():%H:%M:%S}] {msg}", flush=True)


def pio_cmd():
    try:
        run([sys.executable, "-m", "platformio", "--version"])
        return [sys.executable, "-m", "platformio"]
    except Exception:
        return ["pio"]


def flash(env, port):
    cmd = pio_cmd() + ["run", "-e", env, "-t", "upload", "--upload-port", port]
    for attempt in (1, 2):
        say(f"building and flashing (attempt {attempt})...")
        r = run(cmd, check=False)
        out = (r.stdout or "") + (r.stderr or "")
        if r.returncode == 0:
            size = next((l.strip() for l in out.splitlines() if l.strip().startswith("Flash:")), "")
            say(f"flashed OK {size}")
            return True, out
        if "error:" in out and "Compiling" in out and "Failed to connect" not in out:
            say("BUILD FAILED - not flashed. Last lines:")
            print("\n".join(out.splitlines()[-25:]))
            return False, out
        say("upload failed:\n" + "\n".join(out.splitlines()[-8:]))
        time.sleep(3)
    say("Flashing failed twice. If it says 'Failed to connect': hold BOOT, tap RST, release BOOT.")
    return False, out


# The file system holds the Wi-Fi page (built from web/ into data/web) and data/.
FS_PATHS = ("web", "data", "tools/build_web.py")


def fs_changed(old, new):
    """True if anything that goes into the file system image changed between two commits."""
    if not old:
        return False
    r = run(["git", "diff", "--name-only", old, new, "--", *FS_PATHS], check=False)
    return r.returncode != 0 or bool(r.stdout.strip())


def upload_fs(env, port):
    cmd = pio_cmd() + ["run", "-e", env, "-t", "uploadfs", "--upload-port", port]
    for attempt in (1, 2):
        say(f"uploading the file system: Wi-Fi page and data/ (attempt {attempt})...")
        r = run(cmd, check=False)
        out = (r.stdout or "") + (r.stderr or "")
        if r.returncode == 0:
            say("file system uploaded OK (sessions saved in flash were wiped; the SD card is untouched)")
            return True, out
        say("file system upload failed:\n" + "\n".join(out.splitlines()[-8:]))
        time.sleep(3)
    return False, out


def capture(port, seconds):
    from serial_log import open_quiet, restart_board  # noqa: E402
    lines = []
    restart_board(port, 115200)
    s = open_quiet(port, 115200)
    end = time.time() + seconds
    buf = b""
    try:
        while time.time() < end:
            try:
                chunk = s.read(512)
            except Exception:
                s.close()
                s = open_quiet(port, 115200)
                continue
            if chunk:
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode("utf-8", "replace").rstrip("\r")
                    lines.append(text)
                    print("   | " + text, flush=True)
    finally:
        s.close()
    return lines


def push_log(path, sha):
    """Commit the log to the device-logs branch using a separate worktree."""
    wt = os.path.join(REPO, ".git", "device-logs-worktree")
    if not os.path.isdir(wt):
        exists = git("ls-remote", "--heads", "origin", LOG_BRANCH)
        if exists:
            git("fetch", "origin", LOG_BRANCH)
            git("worktree", "add", "-f", wt, f"origin/{LOG_BRANCH}")
            run(["git", "checkout", "-B", LOG_BRANCH, f"origin/{LOG_BRANCH}"], cwd=wt)
        else:
            git("worktree", "add", "--detach", wt)
            run(["git", "checkout", "--orphan", LOG_BRANCH], cwd=wt)
            run(["git", "rm", "-rf", "--quiet", "."], cwd=wt, check=False)
    else:
        run(["git", "pull", "--quiet", "origin", LOG_BRANCH], cwd=wt, check=False)
    dest = os.path.join(wt, os.path.basename(path))
    with open(path, encoding="utf-8") as src, open(dest, "w", encoding="utf-8") as dst:
        dst.write(src.read())
    run(["git", "add", os.path.basename(path)], cwd=wt)
    run(["git", "commit", "-q", "-m", f"Device log for {sha[:7]}"], cwd=wt)
    run(["git", "push", "-q", "origin", f"HEAD:{LOG_BRANCH}"], cwd=wt)
    say(f"log pushed to branch {LOG_BRANCH}")


def handle(sha, args, old=None):
    ok, build_out = flash(args.env, args.port)
    if ok and (args.uploadfs or fs_changed(old, sha)):
        fs_ok, fs_out = upload_fs(args.env, args.port)
        if not fs_ok:
            ok, build_out = False, fs_out
    os.makedirs(LOG_DIR, exist_ok=True)
    stamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    path = os.path.join(LOG_DIR, f"{stamp}_{sha[:7]}.log")
    lines = []
    if ok:
        say(f"capturing the board's log for {args.seconds} s...")
        try:
            lines = capture(args.port, args.seconds)
        except Exception as e:
            say(f"could not read the log: {e}")
    with open(path, "w", encoding="utf-8") as f:
        f.write(f"commit {sha}\nflash {'OK' if ok else 'FAILED'}\n\n")
        if not ok:
            f.write("\n".join(build_out.splitlines()[-60:]) + "\n")
        f.write("\n".join(lines) + "\n")
    say(f"saved {os.path.relpath(path, REPO)}")
    if args.push_logs:
        try:
            push_log(path, sha)
        except Exception as e:
            say(f"could not push the log: {e}")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", required=True, help="board's serial port, e.g. COM4")
    ap.add_argument("--env", default="es3c28p")
    ap.add_argument("--interval", type=int, default=30, help="seconds between checks for new commits")
    ap.add_argument("--seconds", type=int, default=30, help="how long to capture the log after flashing")
    ap.add_argument("--push-logs", action="store_true", help="commit logs to the device-logs branch (private repos only)")
    ap.add_argument("--now", action="store_true", help="flash the current commit right away")
    ap.add_argument("--uploadfs", action="store_true",
                    help="also upload the file system (Wi-Fi page, data/) every time, not just when it changed")
    args = ap.parse_args()

    branch = git("rev-parse", "--abbrev-ref", "HEAD")
    if branch == "HEAD":
        sys.exit("Check out a branch first (git checkout <branch>).")
    say(f"watching origin/{branch} every {args.interval} s; board on {args.port}. Ctrl+C to stop.")
    if args.push_logs:
        say("logs will be pushed to the 'device-logs' branch (they include GPS positions)")

    if args.now:
        handle(git("rev-parse", "HEAD"), args)

    while True:
        try:
            git("fetch", "--quiet", "origin", branch)
            local, remote = git("rev-parse", "HEAD"), git("rev-parse", f"origin/{branch}")
            if local != remote:
                if git("status", "--porcelain", "--untracked-files=no"):
                    say("you have uncommitted changes to tracked files - not pulling. Commit or stash them.")
                else:
                    msg = git("log", "-1", "--format=%s", f"origin/{branch}")
                    say(f"new commit {remote[:7]}: {msg}")
                    git("pull", "--ff-only", "--quiet", "origin", branch)
                    handle(remote, args, old=local)
        except KeyboardInterrupt:
            raise
        except Exception as e:
            say(f"error: {e}")
        time.sleep(args.interval)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\nstopped")
