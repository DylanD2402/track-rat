"""
Runs the host tests with one command, and prints only what failed.

The list of tests is read from the "Host tests" job of .github/workflows/ci.yml, so it
lives in one place: add a test's step there and this script runs it too.

    python tools/run_tests.py                 # every test
    python tools/run_tests.py laptimer vbo    # the tests whose names contain these words
    python tools/run_tests.py --list          # the names and commands; nothing is run
    python tools/run_tests.py -v              # one line per test as each finishes
    python tools/run_tests.py --full          # all of a failing test's output, not the first 40 lines
    python tools/run_tests.py --install-deps  # also run the pip installs CI does (Pillow for test_overlay)

The C++ tests need a compiler: g++, or CXX set (CXX="zig c++"), or `pip install ziglang`,
which is used when there is no g++. On Windows -D_USE_MATH_DEFINES is added for you.
A test that needs node when node isn't installed is reported as skipped.

Exit code: 0 when nothing failed, 1 when a test failed, 2 when the tests couldn't be run.
"""

import argparse
import concurrent.futures
import importlib.util
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CI_FILE = os.path.join(ROOT, ".github", "workflows", "ci.yml")
WINDOWS = os.name == "nt"
SHOWN_LINES = 40      # of a failing test's output
TIMEOUT_S = 600       # for one command of one test
TELLING = re.compile(r"FAIL|error|Error|Traceback|assert|not ok|undefined reference")


def read_tests():
    """[(name, command)] for each step of the host-tests job that has a name and a one-line run."""
    with open(CI_FILE, encoding="utf-8") as f:
        text = f.read().replace("\r\n", "\n")
    job = re.search(r"^  host-tests:\n(.*?)(?=^  \S|\Z)", text, re.S | re.M)
    if not job:
        sys.exit("run_tests: no host-tests job in .github/workflows/ci.yml")
    tests = re.findall(r"^ *- name: *(\S.*?) *\n *run: *(?![|>])(\S.*?) *$", job.group(1), re.M)
    if not tests:
        sys.exit("run_tests: the host-tests job has no steps of the form '- name: ...' then 'run: ...'")
    return tests


def find_compiler():
    """The command that stands in for g++, as a list, or None."""
    if os.environ.get("CXX"):
        return shlex.split(os.environ["CXX"], posix=not WINDOWS)
    if shutil.which("g++"):
        return ["g++"]
    if importlib.util.find_spec("ziglang"):
        return [sys.executable, "-m", "ziglang", "c++"]
    if shutil.which("zig"):
        return ["zig", "c++"]
    return None


def plan(name, command, compiler, out_dir, install_deps):
    """Turns one CI command into the commands to run here: ([argv or shell string], reason it's skipped)."""
    exe = os.path.join(out_dir, name + (".exe" if WINDOWS else ""))
    steps = []
    for part in command.split(" && "):
        words = shlex.split(part)
        first = words[0]
        if first == "g++":
            if compiler is None:
                return [], "no C++ compiler (install g++, set CXX, or pip install ziglang)"
            args = words[1:]
            if "-o" in args:
                args[args.index("-o") + 1] = exe
            else:
                args += ["-o", exe]
            if WINDOWS and "-D_USE_MATH_DEFINES" not in args:
                args.insert(0, "-D_USE_MATH_DEFINES")
            steps.append(compiler + args)
        elif first.startswith("/tmp/"):
            steps.append([exe] + words[1:])
        elif first in ("python", "python3"):
            if words[1:4] == ["-m", "pip", "install"] and not install_deps:
                continue
            steps.append([sys.executable] + words[1:])
        elif first == "node":
            if not shutil.which("node"):
                return [], "node isn't installed"
            steps.append(words)
        else:
            steps.append(part)  # something new in ci.yml: hand it to the shell as written
    return steps, None


def run_one(name, command, compiler, out_dir, install_deps):
    """(name, "pass" | "fail" | "skip", detail, seconds)"""
    start = time.time()
    steps, skipped = plan(name, command, compiler, out_dir, install_deps)
    if skipped:
        return name, "skip", skipped, 0.0
    output = ""
    for step in steps:
        try:
            done = subprocess.run(step, cwd=ROOT, shell=isinstance(step, str), stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, timeout=TIMEOUT_S)
        except subprocess.TimeoutExpired as e:
            output += (e.output or b"").decode("utf-8", "replace")
            return name, "fail", output + f"\n(stopped after {TIMEOUT_S} s)", time.time() - start
        except OSError as e:
            return name, "fail", f"{output}\ncouldn't run {step}: {e}", time.time() - start
        output += done.stdout.decode("utf-8", "replace")
        if done.returncode != 0:
            return name, "fail", output + f"\n(exit code {done.returncode})", time.time() - start
    return name, "pass", output, time.time() - start


def shorten(output):
    """A failing test's output, cut to SHOWN_LINES: the lines that say what failed, then the end."""
    lines = output.rstrip().splitlines()
    if len(lines) <= SHOWN_LINES:
        return lines
    tail = lines[-8:]
    telling = [l for l in lines[:-8] if TELLING.search(l)][: SHOWN_LINES - len(tail) - 1]
    hidden = len(lines) - len(telling) - len(tail)
    return telling + [f"  ... {hidden} more lines; run this test with --full to see them all"] + tail


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("names", nargs="*", help="run only the tests whose names contain one of these")
    ap.add_argument("--list", action="store_true", help="print the tests and their commands, and stop")
    ap.add_argument("-v", "--verbose", action="store_true", help="a line for each test as it finishes")
    ap.add_argument("--full", action="store_true", help="print all of a failing test's output")
    ap.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 4, help="tests to run at once")
    ap.add_argument("--install-deps", action="store_true", help="run the pip installs that CI runs")
    args = ap.parse_args()

    tests = read_tests()
    if args.names:
        tests = [t for t in tests if any(n.lower() in t[0].lower() for n in args.names)]
        if not tests:
            print(f"run_tests: no test's name contains {' or '.join(args.names)} (--list shows the names)")
            return 2
    if args.list:
        for name, command in tests:
            print(f"{name}: {command}")
        return 0

    compiler = find_compiler()
    start = time.time()
    results = []
    with tempfile.TemporaryDirectory(prefix="trackrat-tests-", ignore_cleanup_errors=True) as out_dir:
        with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
            running = [pool.submit(run_one, name, command, compiler, out_dir, args.install_deps)
                       for name, command in tests]
            for finished in concurrent.futures.as_completed(running):
                result = finished.result()
                results.append(result)
                if args.verbose:
                    print(f"{result[1]:4} {result[0]} ({result[3]:.1f} s)", flush=True)

    order = {name: i for i, (name, _) in enumerate(tests)}
    results.sort(key=lambda r: order[r[0]])
    failed = [r for r in results if r[1] == "fail"]
    skipped = [r for r in results if r[1] == "skip"]
    passed = len(results) - len(failed) - len(skipped)

    for name, _, output, _ in failed:
        print(f"FAILED {name}")
        for line in (output.rstrip().splitlines() if args.full else shorten(output)):
            print("  " + line)
        print()
    for name, _, why, _ in skipped:
        print(f"skipped {name}: {why}")

    summary = f"{passed} passed, {len(failed)} failed"
    if skipped:
        summary += f", {len(skipped)} skipped"
    print(f"run_tests: {summary} ({time.time() - start:.0f} s)")
    if failed:
        print("run_tests: failed: " + " ".join(r[0] for r in failed))
        return 1
    return 0 if passed else 2


if __name__ == "__main__":
    sys.exit(main())
