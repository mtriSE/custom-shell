"""Ctrl-C, Ctrl-\\ and Ctrl-Z behaviour on a terminal (specs/001-ctrl-c-handling)."""
import os
import subprocess
import sys
import time
import unittest

from harness import (CTRL_BACKSLASH, CTRL_C, CTRL_D, CTRL_R, CTRL_X, CTRL_Z, ESC, PROMPT,
                     SHELL_BIN, ShellTestCase, process_running)


class PromptTests(ShellTestCase):
    def test_ctrl_c_discards_the_typed_line(self):
        self.shell.send("echo hel")
        self.shell.drain()
        start = time.monotonic()
        self.shell.send(CTRL_C)
        self.shell.expect("\n" + PROMPT)
        if os.environ.get("PTY_TIMING"):
            print("ctrl-c at prompt: %.1f ms" % ((time.monotonic() - start) * 1000),
                  file=sys.stderr)
        self.assertTrue(self.shell.alive())
        # The abandoned text was not run and is not part of the next line.
        self.assertEqual(self.shell.run("pwd"), self.shell.cwd + "\n")

    def test_ctrl_c_at_an_empty_prompt(self):
        self.shell.send(CTRL_C)
        self.shell.expect("\n" + PROMPT)
        self.assertTrue(self.shell.alive())
        self.assertEqual(self.shell.run("echo ok"), "ok\n")

    def test_ctrl_c_sets_status_130(self):
        self.assertEqual(self.shell.run("echo first"), "first\n")
        self.shell.send(CTRL_C)
        self.shell.expect("\n" + PROMPT)
        self.assertEqual(self.shell.status(), 130)

    def test_ctrl_c_keeps_the_working_directory(self):
        self.shell.run("cd /usr")
        self.shell.send(CTRL_C)
        self.shell.expect("\n" + PROMPT)
        self.assertEqual(self.shell.run("pwd"), "/usr\n")

    def test_twenty_ctrl_c_presses(self):
        self.shell.run("cd /usr")
        for _ in range(20):
            self.shell.send(CTRL_C)
            self.shell.expect("\n" + PROMPT)
        self.assertTrue(self.shell.alive())
        self.assertEqual(self.shell.run("pwd"), "/usr\n")

    def test_ctrl_backslash_and_ctrl_z_do_nothing(self):
        self.shell.run("true")
        self.shell.send(CTRL_BACKSLASH)
        self.shell.send(CTRL_Z)
        self.shell.drain()
        self.assertTrue(self.shell.alive())
        self.assertEqual(self.shell.run("echo ok"), "ok\n")
        self.assertEqual(self.shell.status(), 0)

    def test_ctrl_d_still_ends_the_shell(self):
        self.shell.send(CTRL_D)
        self.assertEqual(self.shell.wait_exit(), 0)


class ForegroundTests(ShellTestCase):
    def end_with(self, key, timeout=1.0):
        """Sends a key and waits for the prompt; returns the output with the terminal's own
        echo of the key (^C, ^\\) removed."""
        self.shell.send(key)
        out = self.shell.expect(PROMPT, timeout)
        return out.replace("^C", "").replace("^\\", "")

    def test_ctrl_c_ends_a_foreground_command(self):
        self.shell.start("sleep 100")
        self.assertEqual(self.end_with(CTRL_C), "\n" + PROMPT)  # one newline, then the prompt
        self.assertTrue(self.shell.alive())
        self.assertEqual(self.shell.status(), 130)

    def test_ctrl_c_ends_every_stage_of_a_pipeline(self):
        self.shell.start("sleep 101 | cat")
        self.end_with(CTRL_C)
        self.assertEqual(self.shell.status(), 130)
        self.assertFalse(process_running("sleep 101"))

    def test_next_command_is_not_affected(self):
        self.shell.start("sleep 100")
        self.end_with(CTRL_C)
        self.assertEqual(self.shell.run("echo ok"), "ok\n")
        self.assertEqual(self.shell.status(), 0)
        self.shell.start("sleep 100")
        self.end_with(CTRL_C)
        self.assertEqual(self.shell.status(), 130)

    def test_ctrl_backslash_gives_131(self):
        self.shell.start("sleep 100")
        self.end_with(CTRL_BACKSLASH)
        self.assertTrue(self.shell.alive())
        self.assertEqual(self.shell.status(), 131)

    def test_interrupt_with_a_redirect_restores_the_shell(self):
        self.shell.start("sleep 100 > out.txt")
        self.end_with(CTRL_C)
        self.assertEqual(self.shell.run("echo back"), "back\n")
        self.assertEqual(os.path.getsize(os.path.join(self.shell.cwd, "out.txt")), 0)

    def test_a_command_that_handles_ctrl_c_keeps_its_own_status(self):
        self.shell.start("sh -c 'trap \"exit 7\" INT; sleep 100 & wait'")
        self.end_with(CTRL_C)
        self.assertEqual(self.shell.status(), 7)

    def test_twenty_interrupted_commands(self):
        self.shell.run("cd /usr")
        for _ in range(20):
            self.shell.start("sleep 100")
            self.end_with(CTRL_C)
        self.assertTrue(self.shell.alive())
        self.assertEqual(self.shell.run("pwd"), "/usr\n")


class BackgroundTests(ShellTestCase):
    def test_keys_at_the_prompt_leave_a_background_job_running(self):
        self.shell.run("sleep 100 &")
        for key in (CTRL_C, CTRL_BACKSLASH, CTRL_Z):
            self.shell.send(key)
            self.shell.drain()
        self.assertTrue(self.shell.alive())
        jobs = self.shell.run("jobs")
        self.assertIn("Running", jobs)
        self.assertIn("sleep 100 &", jobs)

    def test_interrupting_a_foreground_command_spares_a_background_job(self):
        self.shell.run("sleep 100 &")
        self.shell.send("sleep 102\n")
        time.sleep(0.2)
        self.shell.drain(0.05)
        self.shell.send(CTRL_C)
        self.shell.expect(PROMPT)
        jobs = self.shell.run("jobs")
        self.assertIn("Running", jobs)
        self.assertIn("sleep 100 &", jobs)


class CompletionTests(ShellTestCase):
    def test_ctrl_c_cancels_a_running_completion_script(self):
        script = os.path.join(self.shell.cwd, "slow.sh")
        with open(script, "w") as f:
            f.write("#!/bin/sh\nsleep 103\n")
        os.chmod(script, 0o755)
        self.shell.run("complete -C ./slow.sh mycmd")

        self.shell.send("mycmd a\t")
        time.sleep(0.3)  # the script is running now
        self.assertEqual(self.shell.drain(0.05), "mycmd a")
        self.shell.send(CTRL_C)
        self.shell.drain(0.3)
        self.assertTrue(self.shell.alive())
        self.assertFalse(process_running("sleep 103"))

        # Ctrl-C cancelled the completion, not the line: it is still there to finish.
        # (An interrupted prompt would have discarded it; this one must still run.)
        self.shell.send("\n")
        out = self.shell.expect(PROMPT)
        self.assertIn("mycmd: command not found", out)
        self.assertEqual(self.shell.status(), 127)


class InterruptedKeySequenceTests(ShellTestCase):
    """Ctrl-C in the middle of a key sequence or a history search behaves as at a normal
    prompt (spec edge case "Ctrl-C ... in the middle of a key sequence")."""

    def interrupt_after(self, *keys):
        """Puts the line editor in the middle of something with `keys`, then presses Ctrl-C."""
        self.shell.run("echo x")  # gives Ctrl-R a history line to find
        for key in keys:
            self.shell.send(key)
            time.sleep(0.1)
        self.shell.drain(0.2)
        self.shell.send(CTRL_C)

    def assert_fresh_prompt(self):
        self.shell.expect("\n" + PROMPT)
        self.assertEqual(self.shell.status(), 130)

    def assert_line_is_empty(self):
        self.shell.drain(0.3)
        # No Ctrl-U: the line must already be empty, so this is all that runs.
        self.shell.send("echo hi\n")
        out = self.shell.expect(PROMPT)
        # The terminal echoes the typed line, so the second "hi" is the command's own output.
        self.assertTrue(out.endswith("echo hi\nhi\n" + PROMPT), repr(out))

    def test_ctrl_c_after_esc_gives_a_fresh_prompt(self):
        self.interrupt_after(ESC)
        self.assert_fresh_prompt()

    def test_ctrl_c_after_esc_leaves_an_empty_line(self):
        self.interrupt_after(ESC)
        self.assert_line_is_empty()

    def test_ctrl_c_after_ctrl_x_gives_a_fresh_prompt(self):
        self.interrupt_after(CTRL_X)
        self.assert_fresh_prompt()

    def test_ctrl_c_after_ctrl_x_leaves_an_empty_line(self):
        self.interrupt_after(CTRL_X)
        self.assert_line_is_empty()

    def test_ctrl_c_in_a_history_search_gives_a_fresh_prompt(self):
        self.interrupt_after(CTRL_R, "ec")
        self.assert_fresh_prompt()

    def test_ctrl_c_in_a_history_search_leaves_an_empty_line(self):
        self.interrupt_after(CTRL_R, "ec")
        self.assert_line_is_empty()


class PipedInputTests(unittest.TestCase):
    """FR-011: with stdin not a terminal nothing changes."""

    def test_output_is_unchanged(self):
        result = subprocess.run([SHELL_BIN], input="echo a\nexit\n", capture_output=True,
                                text=True, timeout=5)
        self.assertEqual(result.stdout, "$ a\n$ ")
        self.assertEqual(result.stderr, "")
        self.assertEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
