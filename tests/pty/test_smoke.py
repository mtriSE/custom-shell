import unittest

from harness import CTRL_D, Shell


class SmokeTests(unittest.TestCase):
    def setUp(self):
        self.shell = Shell()
        self.addCleanup(self.shell.close)

    def test_runs_a_command(self):
        self.assertEqual(self.shell.run("echo hi"), "hi\n")
        self.assertEqual(self.shell.status(), 0)

    def test_ctrl_d_ends_the_shell(self):
        self.shell.send(CTRL_D)
        self.assertEqual(self.shell.wait_exit(), 0)


if __name__ == "__main__":
    unittest.main()
