"""Runs build/shell on a pseudo-terminal so tests can send keystrokes and read the screen.

Standard library only. Every shell gets a scratch cwd, HOME=/usr, LC_ALL=C and TERM=dumb so
results do not depend on the host.
"""
import os
import pty
import select
import shutil
import signal
import subprocess
import re
import tempfile
import termios
import time
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SHELL_BIN = os.path.join(ROOT, "build", "shell")

CTRL_C = b"\x03"
CTRL_BACKSLASH = b"\x1c"
CTRL_Z = b"\x1a"
CTRL_D = b"\x04"
ESC = b"\x1b"
CTRL_X = b"\x18"
CTRL_R = b"\x12"

PROMPT = "$ "


class ShellTestCase(unittest.TestCase):
    """One fresh shell per test as `self.shell`, closed afterwards."""

    def setUp(self):
        self.shell = Shell()
        self.addCleanup(self.shell.close)


def _pids(pattern):
    result = subprocess.run(["pgrep", "-f", "^" + pattern + "$"], capture_output=True, text=True)
    return result.stdout.split()


def process_running(pattern):
    # Anchored: an unanchored pattern also matches any shell whose command line mentions it.
    return bool(_pids(pattern))


def process_stopped(pattern):
    """True if a process whose command line is `pattern` is stopped (state T)."""
    for pid in _pids(pattern):
        state = subprocess.run(["ps", "-o", "stat=", "-p", pid], capture_output=True, text=True)
        if state.stdout.strip().startswith("T"):
            return True
    return False


def wait_for(condition, timeout=2.0):
    """Polls `condition()` until it is true or `timeout` passes; returns its last value. For
    things that happen on their own schedule (a process reaching its first read), so a loaded
    machine cannot make a test flaky."""
    deadline = time.monotonic() + timeout
    while not condition() and time.monotonic() < deadline:
        time.sleep(0.02)
    return condition()


def process_gone(pid):
    """True when `ps` knows no such process; a zombie (state Z) still shows up, so it is not."""
    state = subprocess.run(["ps", "-o", "stat=", "-p", str(pid)], capture_output=True, text=True)
    return state.stdout.strip() == ""


def normalise_pids(text):
    """`[1] 12345` becomes `[1] PID`."""
    return re.sub(r"(\[\d+\] )\d+", r"\1PID", text)


def job_line(number, state, command, marker="+"):
    """A line of `jobs` output without `&` handling and without the newline."""
    return "[%d]%s  %-24s%s" % (number, marker, state, command)


class Shell:
    def __init__(self):
        self.cwd = tempfile.mkdtemp(prefix="shell-pty-")
        env = {
            "PATH": os.environ.get("PATH", "/usr/bin:/bin"),
            "TERM": "dumb",
            "LC_ALL": "C",
            "HOME": "/usr",
        }
        master, slave = pty.openpty()
        # The shell is a session leader with the pty as its controlling terminal, like in a
        # terminal emulator, so Ctrl-C reaches its foreground process group.
        self.proc = subprocess.Popen(
            [SHELL_BIN],
            stdin=slave, stdout=slave, stderr=slave,
            cwd=self.cwd, env=env,
            start_new_session=True,
            preexec_fn=lambda: __import__("fcntl").ioctl(0, __import__("termios").TIOCSCTTY, 0),
        )
        os.close(slave)
        self.fd = master
        self.buffer = ""
        self.expect(PROMPT)

    # --- I/O ---------------------------------------------------------------

    def send(self, data):
        os.write(self.fd, data if isinstance(data, bytes) else data.encode())

    def _read_some(self, timeout):
        ready, _, _ = select.select([self.fd], [], [], timeout)
        if not ready:
            return False
        try:
            chunk = os.read(self.fd, 4096)
        except OSError:  # pty closed: the shell ended
            return False
        if not chunk:
            return False
        self.buffer += chunk.decode(errors="replace").replace("\r\n", "\n")
        return True

    def expect(self, text, timeout=1.0):
        """Waits until `text` appears in the unread output; returns everything up to and
        including it and drops that part from the buffer."""
        deadline = time.monotonic() + timeout
        while text not in self.buffer:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not self._read_some(remaining):
                if text in self.buffer:
                    break
                raise AssertionError(
                    "timed out waiting for %r; output so far: %r" % (text, self.buffer))
        index = self.buffer.index(text) + len(text)
        seen, self.buffer = self.buffer[:index], self.buffer[index:]
        return seen

    def drain(self, quiet=0.1):
        """Reads until the output has been quiet for `quiet` seconds; returns it all."""
        while self._read_some(quiet):
            pass
        seen, self.buffer = self.buffer, ""
        return seen

    # --- commands ----------------------------------------------------------

    def run(self, line, timeout=1.0):
        """Types a line, waits for the next prompt, returns the output without the echo of
        the line and without the final prompt."""
        self.send(line + "\n")
        out = self.expect(PROMPT, timeout)
        out = out[len(line) + 1:] if out.startswith(line + "\n") else out
        return out[:-len(PROMPT)]

    def start(self, line):
        """Starts a foreground command and gives it time to exec (no prompt comes back)."""
        self.send(line + "\n")
        time.sleep(0.2)
        self.drain(0.05)

    def termios_snapshot(self):
        """The terminal's attributes as the shell and its children see them."""
        return termios.tcgetattr(self.fd)

    def snapshot_during(self, line):
        """The terminal attributes while `line` runs in the foreground (Ctrl-C ends it)."""
        self.start(line)
        snapshot = self.termios_snapshot()
        self.send(CTRL_C)
        self.expect(PROMPT)
        return snapshot

    def echo_enabled(self):
        return bool(self.termios_snapshot()[3] & termios.ECHO)

    def status(self):
        return int(self.run("echo $?").strip())

    def alive(self):
        return self.proc.poll() is None

    def wait_exit(self, timeout=1.0):
        try:
            return self.proc.wait(timeout)
        except subprocess.TimeoutExpired:
            raise AssertionError("shell did not exit; output: %r" % self.buffer)

    def close(self):
        if self.proc.poll() is None:
            try:
                os.killpg(self.proc.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            self.proc.wait()
        # Anything left in the shell's session (background jobs live in their own groups).
        subprocess.run(["pkill", "-9", "-s", str(self.proc.pid)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.close(self.fd)
        shutil.rmtree(self.cwd, ignore_errors=True)
