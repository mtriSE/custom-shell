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
import tempfile
import time

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
