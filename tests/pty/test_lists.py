"""Command lists on a terminal (specs/004-command-lists-and-input-redirect)."""
import time
import unittest

from harness import CTRL_C, CTRL_Z, PROMPT, ShellTestCase, job_line, process_running, wait_for


class SemicolonTests(ShellTestCase):
    def test_ctrl_c_stops_the_rest_of_the_list(self):
        self.shell.start("sleep 100; echo after")
        self.assertTrue(process_running("sleep 100"))  # the list really started it
        self.shell.send(CTRL_C)
        out = self.shell.expect(PROMPT, 1.0)
        self.assertNotIn("after", out)
        self.assertTrue(wait_for(lambda: not process_running("sleep 100")))
        self.assertEqual(self.shell.status(), 130)

    def test_ctrl_c_in_the_middle_of_three_commands(self):
        self.shell.send("echo a; sleep 100; echo after\n")
        self.shell.expect("a\n")
        # Ctrl-C reaches the command only once it is the foreground job.
        self.assertTrue(wait_for(lambda: process_running("sleep 100")))
        time.sleep(0.1)
        self.shell.send(CTRL_C)
        out = self.shell.expect(PROMPT, 1.0)
        self.assertNotIn("after", out)
        self.assertEqual(self.shell.status(), 130)

    def test_ctrl_z_stops_one_command_and_the_list_goes_on(self):
        self.shell.start("sleep 100; echo after")
        self.shell.send(CTRL_Z)
        out = self.shell.expect(PROMPT, 1.0).replace("^Z", "")
        self.assertEqual(out, "\n" + job_line(1, "Stopped", "sleep 100") + "\nafter\n" + PROMPT)
        # The job is the pipeline, not the whole line.
        self.assertEqual(self.shell.run("jobs"), job_line(1, "Stopped", "sleep 100") + "\n")

    def test_a_refused_exit_is_a_failed_command_and_the_list_goes_on(self):
        self.shell.start("sleep 100")
        self.shell.send(CTRL_Z)
        self.shell.expect(PROMPT, 1.0)
        out = self.shell.run("echo a; exit; echo b")
        self.assertEqual(out, "a\nThere are stopped jobs.\nb\n")
        self.assertEqual(self.shell.status(), 0)

    def test_the_second_exit_leaves(self):
        self.shell.start("sleep 100")
        self.shell.send(CTRL_Z)
        self.shell.expect(PROMPT, 1.0)
        self.assertEqual(self.shell.run("echo a; exit"), "a\nThere are stopped jobs.\n")
        self.shell.send("exit\n")
        self.assertEqual(self.shell.wait_exit(), 0)

    def test_exit_in_a_list_leaves_the_shell(self):
        self.shell.send("echo a; exit\n")
        self.shell.expect("a\n")
        self.shell.wait_exit()
        self.assertFalse(self.shell.alive())


if __name__ == "__main__":
    unittest.main()
