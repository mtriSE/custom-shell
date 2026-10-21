"""Job control behaviour on a terminal (specs/002-job-control)."""
import os
import re
import select
import signal
import subprocess
import sys
import time
import unittest

from harness import (CTRL_BACKSLASH, CTRL_C, CTRL_D, CTRL_Z, PROMPT, SHELL_BIN, ShellTestCase,
                     job_line, normalise_pids, process_gone, process_running, process_stopped,
                     wait_for)


def pid_of(pattern):
    """The pid of the one process whose command line is exactly `pattern`."""
    pids = subprocess.run(["pgrep", "-f", "^" + pattern + "$"], capture_output=True,
                          text=True).stdout.split()
    assert len(pids) == 1, "expected one process for %r, found %r" % (pattern, pids)
    return int(pids[0])


class StopTests(ShellTestCase):
    def stop(self, line, timeout=1.0):
        """Runs `line` in the foreground, presses Ctrl-Z and returns the output up to the next
        prompt, with the terminal's own echo of the key (^Z) removed."""
        self.shell.start(line)
        self.shell.send(CTRL_Z)
        return self.shell.expect(PROMPT, timeout).replace("^Z", "")

    def notice(self, number, command, marker="+"):
        return "\n" + job_line(number, "Stopped", command, marker) + "\n" + PROMPT

    def test_ctrl_z_stops_a_foreground_command(self):
        self.shell.start("sleep 100")
        start = time.monotonic()
        self.shell.send(CTRL_Z)
        out = self.shell.expect(PROMPT).replace("^Z", "")
        if os.environ.get("PTY_TIMING"):
            print("ctrl-z stop: %.1f ms" % ((time.monotonic() - start) * 1000), file=sys.stderr)
        self.assertEqual(out, self.notice(1, "sleep 100"))
        self.assertEqual(self.shell.status(), 148)

    def test_a_stopped_job_is_listed_and_really_stopped(self):
        self.stop("sleep 100")
        self.assertEqual(self.shell.run("jobs"), job_line(1, "Stopped", "sleep 100") + "\n")
        self.assertTrue(process_stopped("sleep 100"))

    def test_a_pipeline_is_one_job(self):
        out = self.stop("sleep 101 | cat")
        self.assertEqual(out, self.notice(1, "sleep 101 | cat"))
        self.assertTrue(process_stopped("sleep 101"))
        self.assertTrue(process_stopped("cat"))

    def test_ctrl_z_at_the_prompt_does_nothing(self):
        self.shell.send("echo hel")
        self.shell.drain()
        self.shell.send(CTRL_Z)
        self.shell.drain()
        self.shell.send("\n")
        out = self.shell.expect(PROMPT)
        self.assertTrue(out.endswith("hel\n" + PROMPT), repr(out))
        self.assertEqual(self.shell.run("jobs"), "")

    def test_a_redirect_is_undone_before_the_notice(self):
        out = self.stop("sleep 100 > out.txt")
        self.assertEqual(out, self.notice(1, "sleep 100 > out.txt"))
        self.assertEqual(self.shell.run("echo back"), "back\n")
        self.assertEqual(os.path.getsize(os.path.join(self.shell.cwd, "out.txt")), 0)

    def test_the_shell_is_back_in_front_after_a_stop(self):
        self.stop("sleep 100")
        self.assertEqual(self.shell.run("echo ok"), "ok\n")
        self.assertEqual(self.shell.status(), 0)

    def test_terminal_settings_are_back_after_a_stop(self):
        baseline = self.shell.snapshot_during("sleep 108")
        self.stop("sh -c 'stty -echo; sleep 103'")
        self.assertEqual(self.shell.snapshot_during("sleep 109"), baseline)
        self.assertEqual(self.shell.run("echo ok"), "ok\n")

    def test_two_stopped_jobs_get_numbers_one_and_two(self):
        self.stop("sleep 104")
        self.stop("sleep 105")
        self.assertEqual(self.shell.run("jobs"),
                         job_line(1, "Stopped", "sleep 104", "-") + "\n"
                         + job_line(2, "Stopped", "sleep 105", "+") + "\n")

    def test_a_stopped_job_killed_from_outside_is_done_once(self):
        self.stop("sleep 106")
        pid = pid_of("sleep 106")
        os.kill(pid, signal.SIGKILL)
        time.sleep(0.2)
        self.shell.send("\n")
        out = self.shell.expect(PROMPT)
        self.assertEqual(out.count(job_line(1, "Done", "sleep 106")), 1, repr(out))
        self.assertEqual(self.shell.run("jobs"), "")
        self.assertTrue(process_gone(pid))


class StopEdgeCaseTests(ShellTestCase):
    def test_a_job_stopped_and_continued_from_outside_shows_its_real_state(self):
        self.shell.run("sleep 110 &")
        pid = pid_of("sleep 110")
        os.kill(pid, signal.SIGSTOP)
        self.assertTrue(wait_for(lambda: process_stopped("sleep 110")))
        self.assertEqual(self.shell.run("jobs"), job_line(1, "Stopped", "sleep 110") + "\n")
        os.kill(pid, signal.SIGCONT)
        self.assertTrue(wait_for(lambda: not process_stopped("sleep 110")))
        self.assertEqual(self.shell.run("jobs"), job_line(1, "Running", "sleep 110 &") + "\n")

    def test_a_program_that_ignores_ctrl_z_keeps_running(self):
        self.shell.start("sh -c 'trap \"\" TSTP; sleep 1; echo done'")
        self.shell.send(CTRL_Z)
        out = self.shell.expect("done\n" + PROMPT, 3.0)
        self.assertNotIn("Stopped", out)
        self.assertEqual(self.shell.run("jobs"), "")

    def test_ctrl_z_during_a_completion_script_does_not_stop_it(self):
        script = os.path.join(self.shell.cwd, "slow.sh")
        with open(script, "w") as f:
            f.write("#!/bin/sh\nsleep 103\n")
        os.chmod(script, 0o755)
        self.shell.run("complete -C ./slow.sh mycmd")

        self.shell.send("mycmd a\t")
        time.sleep(0.3)
        self.shell.drain(0.05)
        self.shell.send(CTRL_Z)
        time.sleep(0.2)
        self.assertTrue(process_running("sleep 103"))
        self.assertFalse(process_stopped("sleep 103"))

        self.shell.send(CTRL_C)  # still cancels the completion, and the line is intact
        self.shell.drain(0.3)
        self.assertFalse(process_running("sleep 103"))
        self.shell.send("\n")
        out = self.shell.expect(PROMPT)
        self.assertIn("mycmd: command not found", out)


class ResumeTests(ShellTestCase):
    def stop(self, line):
        self.shell.start(line)
        self.shell.send(CTRL_Z)
        return self.shell.expect(PROMPT).replace("^Z", "")

    def fg(self, command):
        """Types `fg`, waits for the echo of the job's command line; no prompt comes back."""
        self.shell.send("fg\n")
        self.shell.expect(command + "\n")
        self.assertEqual(self.shell.drain(0.2), "")

    def test_bg_continues_the_job_in_the_background(self):
        self.stop("sleep 100")
        self.assertEqual(self.shell.run("bg"), "[1]+ sleep 100 &\n")
        self.assertEqual(self.shell.status(), 0)
        self.assertEqual(self.shell.run("jobs"), job_line(1, "Running", "sleep 100 &") + "\n")
        self.assertFalse(process_stopped("sleep 100"))

    def test_fg_continues_the_job_in_front(self):
        self.stop("sleep 100")
        self.fg("sleep 100")
        self.shell.send(CTRL_C)
        self.shell.expect(PROMPT)
        self.assertEqual(self.shell.status(), 130)
        self.assertEqual(self.shell.run("jobs"), "")

    def test_a_job_stopped_again_keeps_its_number(self):
        self.stop("sleep 100")
        self.fg("sleep 100")
        self.shell.send(CTRL_Z)
        out = self.shell.expect(PROMPT).replace("^Z", "")
        self.assertEqual(out, "\n" + job_line(1, "Stopped", "sleep 100") + "\n" + PROMPT)
        self.assertEqual(self.shell.status(), 148)

    def test_a_job_that_finishes_in_the_background_is_done_once(self):
        self.stop("sleep 1")
        self.shell.run("bg")
        time.sleep(1.4)
        self.shell.send("\n")
        out = self.shell.expect(PROMPT)
        self.assertEqual(out.count(job_line(1, "Done", "sleep 1")), 1, repr(out))
        self.shell.send("\n")
        self.assertNotIn("Done", self.shell.expect(PROMPT))

    def test_twenty_stop_and_resume_cycles(self):
        self.stop("sleep 100")
        for _ in range(20):
            self.fg("sleep 100")
            self.shell.send(CTRL_Z)
            out = self.shell.expect(PROMPT).replace("^Z", "")
            self.assertEqual(out, "\n" + job_line(1, "Stopped", "sleep 100") + "\n" + PROMPT)
        self.fg("sleep 100")
        self.shell.send(CTRL_C)
        self.shell.expect(PROMPT)
        self.assertEqual(self.shell.status(), 130)

    def test_a_job_gets_back_the_terminal_settings_it_had(self):
        baseline = self.shell.snapshot_during("sleep 108")
        command = "sh -c 'stty -echo; sleep 106'"
        self.stop(command)
        self.assertEqual(self.shell.run("echo ok"), "ok\n")
        self.fg(command)
        self.assertFalse(self.shell.echo_enabled())  # the job's own settings are back
        self.shell.send(CTRL_C)
        self.shell.expect(PROMPT)
        self.assertEqual(self.shell.snapshot_during("sleep 109"), baseline)

    def test_a_pipeline_stops_and_resumes_as_one_job(self):
        self.stop("sleep 107 | cat")
        self.assertEqual(self.shell.run("bg"), "[1]+ sleep 107 | cat &\n")
        self.assertEqual(self.shell.run("jobs"), job_line(1, "Running", "sleep 107 | cat &") + "\n")
        self.fg("sleep 107 | cat")
        self.shell.send(CTRL_C)
        self.shell.expect(PROMPT)
        self.assertFalse(process_running("sleep 107"))
        self.assertFalse(process_running("cat"))

    def test_a_builtin_stage_is_part_of_the_job(self):
        self.stop("echo hi | sleep 108")
        self.assertEqual(self.shell.run("jobs"),
                         job_line(1, "Stopped", "echo hi | sleep 108") + "\n")
        self.fg("echo hi | sleep 108")
        self.shell.send(CTRL_C)
        self.shell.expect(PROMPT)
        self.assertEqual(self.shell.run("jobs"), "")

    def test_every_ended_job_is_just_done(self):
        # Limitation: no `Exit N` or `Terminated` as in bash; any ending is `Done`.
        self.shell.run("sh -c 'exit 3' &")
        time.sleep(0.4)
        self.shell.send("\n")
        out = self.shell.expect(PROMPT)
        self.assertIn(job_line(1, "Done", "sh -c 'exit 3'"), out)
        self.assertNotIn("Exit", out)

    def test_job_builtins_beyond_the_spec_are_not_there(self):
        # Limitation: no wait, disown, suspend (a later feature).
        for name in ("wait", "disown", "suspend"):
            with self.subTest(name=name):
                self.assertEqual(self.shell.run(name), name + ": command not found\n")
                self.assertEqual(self.shell.status(), 127)


class JobSelectionTests(ShellTestCase):
    def stop(self, line):
        self.shell.start(line)
        self.shell.send(CTRL_Z)
        return self.shell.expect(PROMPT).replace("^Z", "")

    def stop_three(self):
        for number in (120, 121, 122):
            self.stop("sleep %d" % number)

    def resume_and_interrupt(self, command):
        """Runs an fg or bg line, then Ctrl-C (it ends the job if the line brought it to the
        foreground)."""
        self.shell.send(command + "\n")
        time.sleep(0.2)
        self.shell.drain(0.05)
        self.shell.send(CTRL_C)
        self.shell.expect(PROMPT)

    def assert_only_ended(self, number):
        for other in (120, 121, 122):
            self.assertEqual(process_running("sleep %d" % other), other != number, other)

    def test_markers_follow_the_most_recently_stopped_job(self):
        self.stop("sleep 101")
        self.stop("sleep 102")
        self.assertEqual(self.shell.run("jobs"),
                         job_line(1, "Stopped", "sleep 101", "-") + "\n"
                         + job_line(2, "Stopped", "sleep 102", "+") + "\n")
        self.shell.send("fg %1\n")
        time.sleep(0.2)
        self.shell.drain(0.05)
        self.shell.send(CTRL_Z)
        self.shell.expect(PROMPT)
        self.assertEqual(self.shell.run("jobs"),
                         job_line(1, "Stopped", "sleep 101", "+") + "\n"
                         + job_line(2, "Stopped", "sleep 102", "-") + "\n")

    def test_a_stopped_job_outranks_a_newer_background_job(self):
        self.shell.run("sleep 91 &")
        self.stop("sleep 92")
        self.shell.run("sleep 93 &")
        self.assertEqual(self.shell.run("jobs"),
                         job_line(1, "Running", "sleep 91 &", " ") + "\n"
                         + job_line(2, "Stopped", "sleep 92", "+") + "\n"
                         + job_line(3, "Running", "sleep 93 &", "-") + "\n")

    def test_fg_with_a_job_number(self):
        self.stop_three()
        self.resume_and_interrupt("fg 2")
        self.assert_only_ended(121)

    def test_fg_with_percent_number(self):
        self.stop_three()
        self.resume_and_interrupt("fg %1")
        self.assert_only_ended(120)

    def test_fg_with_percent_percent(self):
        self.stop_three()
        self.resume_and_interrupt("fg %%")
        self.assert_only_ended(122)

    def test_fg_with_percent_plus(self):
        self.stop_three()
        self.resume_and_interrupt("fg %+")
        self.assert_only_ended(122)

    def test_fg_with_the_previous_job(self):
        self.stop_three()
        self.resume_and_interrupt("fg %-")
        self.assert_only_ended(121)

    def test_bg_with_a_job_number(self):
        self.stop_three()
        self.assertEqual(self.shell.run("bg 1"), "[1] sleep 120 &\n")
        self.assertFalse(process_stopped("sleep 120"))
        self.assertTrue(process_stopped("sleep 121"))
        self.assertTrue(process_stopped("sleep 122"))

    def test_bg_resumes_every_job_it_is_given(self):
        self.stop("sleep 101")
        self.stop("sleep 102")
        self.assertEqual(self.shell.run("bg 1 2"), "[1]- sleep 101 &\n[2]+ sleep 102 &\n")

    def test_fg_uses_only_the_first_job_given(self):
        self.stop("sleep 101")
        self.stop("sleep 102")
        self.resume_and_interrupt("fg 1 2")
        self.assertFalse(process_running("sleep 101"))
        self.assertTrue(process_stopped("sleep 102"))

    def test_a_new_job_is_numbered_one_above_the_highest(self):
        for number in (91, 92, 93):
            self.shell.run("sleep %d &" % number)
        os.kill(pid_of("sleep 92"), signal.SIGKILL)
        time.sleep(0.2)
        self.shell.send("\n")
        self.shell.expect(PROMPT)  # `Done` for job 2 is reported here
        self.assertEqual(normalise_pids(self.shell.run("sleep 94 &")), "[4] PID\n")
        for number in (91, 93, 94):
            os.kill(pid_of("sleep %d" % number), signal.SIGKILL)
        time.sleep(0.2)
        self.shell.send("\n")
        self.shell.expect(PROMPT)
        self.assertEqual(normalise_pids(self.shell.run("sleep 95 &")), "[1] PID\n")

    def test_errors_name_the_problem_in_one_line(self):
        cases = [
            ("fg", "fg: current: no such job"),
            ("bg", "bg: current: no such job"),
            ("fg %9", "fg: %9: no such job"),
            ("fg 9", "fg: 9: no such job"),
            ("bg %9", "bg: %9: no such job"),
            ("fg %sleep", "fg: %sleep: no such job"),
        ]
        for line, message in cases:
            with self.subTest(line=line):
                self.assertEqual(self.shell.run(line), message + "\n")
                self.assertEqual(self.shell.status(), 1)

    def test_bg_on_a_running_job_changes_nothing(self):
        self.stop("sleep 100")
        self.shell.run("bg")
        self.assertEqual(self.shell.run("bg"), "bg: job 1 already in background\n")
        self.assertEqual(self.shell.status(), 0)
        self.assertEqual(self.shell.run("jobs"), job_line(1, "Running", "sleep 100 &") + "\n")

    def test_a_job_already_reported_done_is_gone(self):
        self.shell.run("sleep 1 &")
        time.sleep(1.4)
        self.shell.send("\n")
        self.shell.expect(PROMPT)
        self.assertEqual(self.shell.run("fg %1"), "fg: %1: no such job\n")

    def test_fg_in_a_pipeline_stage_cannot_touch_the_jobs(self):
        self.stop("sleep 100")
        self.assertEqual(self.shell.run("echo a | fg"), "fg: no job control\n")
        self.assertEqual(self.shell.status(), 1)
        self.assertEqual(self.shell.run("jobs"), job_line(1, "Stopped", "sleep 100") + "\n")


class BackgroundJobTests(ShellTestCase):
    def stop(self, line):
        self.shell.start(line)
        self.shell.send(CTRL_Z)
        return self.shell.expect(PROMPT).replace("^Z", "")

    def test_a_background_reader_is_stopped_and_listed(self):
        self.assertEqual(normalise_pids(self.shell.run("cat &")), "[1] PID\n")
        self.assertTrue(wait_for(lambda: process_stopped("cat")))  # it stops at its first read
        self.shell.send("\n")
        out = self.shell.expect(PROMPT)
        # The echo of Enter, then the notice once. (bash adds a blank line before it, because it
        # prints the notice asynchronously with the cursor still after the prompt.)
        self.assertEqual(out, "\n" + job_line(1, "Stopped", "cat") + "\n" + PROMPT)
        self.assertEqual(self.shell.run("jobs"), job_line(1, "Stopped", "cat") + "\n")
        self.assertTrue(process_stopped("cat"))

    def test_a_background_reader_does_not_eat_what_the_user_types(self):
        self.shell.run("cat &")
        self.assertTrue(wait_for(lambda: process_stopped("cat")))  # it stops at its first read
        self.shell.send("\n")
        self.shell.expect(PROMPT)
        self.assertEqual(self.shell.run("echo mine"), "mine\n")

    def test_fg_lets_a_stopped_reader_read(self):
        self.shell.run("cat &")
        self.assertTrue(wait_for(lambda: process_stopped("cat")))  # it stops at its first read
        self.shell.send("\n")
        self.shell.expect(PROMPT)
        self.shell.send("fg\n")
        self.shell.expect("cat\n")
        self.shell.drain(0.2)
        self.shell.send("hi\n")
        self.shell.expect("hi\nhi\n")  # the terminal's echo, then what cat wrote
        self.shell.send(CTRL_D)
        self.shell.expect(PROMPT)
        self.assertEqual(self.shell.status(), 0)

    def test_keys_reach_only_the_foreground_job(self):
        self.shell.run("sleep 100 &")
        self.stop("sleep 101")
        for key, status, victim in ((CTRL_C, 130, "sleep 102"), (CTRL_BACKSLASH, 131, "sleep 103"),
                                    (CTRL_Z, 148, "sleep 104")):
            with self.subTest(key=key):
                self.shell.start(victim)
                self.shell.send(key)
                self.shell.expect(PROMPT)
                self.assertEqual(self.shell.status(), status)
                self.assertTrue(process_running("sleep 100"))
                self.assertFalse(process_stopped("sleep 100"))
                self.assertTrue(process_stopped("sleep 101"))

    def test_output_of_a_background_job_is_shown(self):
        self.shell.send("sh -c 'echo from-bg' &\n")
        self.shell.expect("from-bg\n")

    def test_a_background_pipeline_is_one_job(self):
        out = self.shell.run("sleep 100 | cat &")
        self.assertEqual(normalise_pids(out), "[1] PID\n")
        # The pid shown is the last stage's, as in bash.
        self.assertEqual(int(re.search(r"\[1\] (\d+)", out).group(1)), pid_of("cat"))
        self.assertEqual(self.shell.run("jobs"),
                         job_line(1, "Running", "sleep 100 | cat &") + "\n")

    def test_ctrl_c_at_the_prompt_leaves_a_background_pipeline_alone(self):
        self.shell.run("sleep 100 | cat &")
        self.shell.send(CTRL_C)
        self.shell.expect("\n" + PROMPT)
        self.assertTrue(process_running("sleep 100"))
        self.assertEqual(self.shell.run("jobs"),
                         job_line(1, "Running", "sleep 100 | cat &") + "\n")

    def test_twenty_stopped_jobs(self):
        for number in range(200, 220):
            self.stop("sleep %d" % number)
        lines = self.shell.run("jobs").splitlines()
        self.assertEqual(len(lines), 20)
        self.assertTrue(all("Stopped" in line for line in lines))
        self.assertEqual(self.shell.run("echo ok"), "ok\n")


class ExitWarningTests(ShellTestCase):
    WARNING = "There are stopped jobs.\n"

    def stop(self, line):
        self.shell.start(line)
        self.shell.send(CTRL_Z)
        return self.shell.expect(PROMPT).replace("^Z", "")

    def test_exit_with_a_stopped_job_warns_and_stays(self):
        self.stop("sleep 100")
        self.assertEqual(self.shell.run("exit"), self.WARNING)
        self.assertTrue(self.shell.alive())
        self.assertEqual(self.shell.status(), 1)

    def test_the_next_exit_leaves_and_ends_the_stopped_job(self):
        self.stop("sleep 100")
        self.shell.run("exit")
        self.shell.send("exit\n")
        self.shell.wait_exit()
        # SIGHUP and SIGCONT need a moment to act.
        self.assertTrue(wait_for(lambda: not process_running("sleep 100")))

    def test_another_line_in_between_starts_the_warning_over(self):
        self.stop("sleep 100")
        self.assertEqual(self.shell.run("exit"), self.WARNING)
        self.assertEqual(self.shell.run("true"), "")
        self.assertEqual(self.shell.run("exit"), self.WARNING)
        self.assertTrue(self.shell.alive())

    def test_ctrl_d_warns_the_same_way(self):
        self.stop("sleep 100")
        self.shell.send(CTRL_D)
        out = self.shell.expect(PROMPT)
        self.assertEqual(out, "\n" + self.WARNING + PROMPT)  # the cursor was after the prompt
        self.assertTrue(self.shell.alive())
        self.shell.send(CTRL_D)
        self.shell.wait_exit()
        self.assertTrue(wait_for(lambda: not process_running("sleep 100")))

    def test_exit_after_a_ctrl_d_warning_leaves(self):
        self.stop("sleep 100")
        self.shell.send(CTRL_D)
        self.shell.expect(PROMPT)
        self.shell.send("exit\n")
        self.shell.wait_exit()

    def test_running_background_jobs_do_not_stop_the_exit_and_keep_running(self):
        self.shell.run("sleep 100 &")
        self.shell.send("exit\n")
        self.shell.wait_exit()
        self.assertTrue(process_running("sleep 100"))  # (the test's cleanup ends it)

    def test_twenty_stopped_jobs_are_all_ended_by_the_second_exit(self):
        for number in range(200, 220):
            self.stop("sleep %d" % number)
        self.assertEqual(self.shell.run("exit"), self.WARNING)
        self.shell.send("exit\n")
        self.shell.wait_exit()
        self.assertTrue(wait_for(lambda: not process_running("sleep 2[0-9]{2}")))

    def test_exit_with_no_jobs_leaves_at_once(self):
        self.shell.send("exit\n")
        self.shell.wait_exit()


class PipedBackgroundTests(unittest.TestCase):
    """FR-012 and FR-013: with piped input a pipeline with `&` is still a background job, and
    there are no process groups."""

    def test_a_background_pipeline_without_a_terminal(self):
        proc = subprocess.Popen([SHELL_BIN], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        try:
            proc.stdin.write("sleep 2 | cat &\n")
            proc.stdin.flush()
            # Bounded: a shell that waits for the pipeline prints no announcement until it ends.
            seen = ""
            deadline = time.monotonic() + 1.5
            while not re.search(r"\[1\] \d+\n", seen) and time.monotonic() < deadline:
                ready, _, _ = select.select([proc.stdout], [], [], 0.2)
                if ready:
                    seen += os.read(proc.stdout.fileno(), 4096).decode()
            self.assertRegex(seen, r"^\$ \[1\] \d+\n", "the pipeline was not run in the background")
            # No terminal: no process group of its own, the stages stay in the shell's group.
            with open("/proc/%d/stat" % pid_of("sleep 2")) as stat:
                group = int(stat.read().rsplit(")", 1)[1].split()[2])
            self.assertEqual(group, os.getpgid(proc.pid))
            output, _ = proc.communicate("jobs\nfg\nexit\n", timeout=5)
        finally:
            proc.kill()
        self.assertIn("[1]+  Running                 sleep 2 | cat &", output)
        self.assertIn("fg: no job control", output)


if __name__ == "__main__":
    unittest.main()
