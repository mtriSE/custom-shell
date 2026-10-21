# Quickstart: Validate Job Control

**Feature**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md) | **Contract**: [contracts/terminal-job-control.md](contracts/terminal-job-control.md)

## Prerequisites

- Build tools, the readline development package and `python3` (see README.md "Prerequisites").
- `bash` is only needed to compare texts by hand (optional).

## Build and run all tests

```sh
cmake -B build -S . && cmake --build build
tests/run.sh             # golden cases, then terminal (pty) cases
tests/pty/run.sh         # terminal cases only
```

Expected: every golden case passes with no change to any `.out` file (FR-013, SC-007) apart from
the new cases, and every terminal case passes, including the job-control file
`tests/pty/test_job_control.py`.

## Manual check on a real terminal

Run `./run.sh` (or `build/shell`) in a terminal emulator and try each row. Expected result in
brackets. The texts match bash 5.2 (compare with `bash` if in doubt).

1. Run `sleep 100`, press Ctrl-Z. [`^Z`, then `[1]+  Stopped                 sleep 100`, prompt back
   within 1 s]
2. Run `echo $?`. [`148`] Run `jobs`. [`[1]+  Stopped                 sleep 100`]
3. Run `bg`. [`[1]+ sleep 100 &`, prompt at once] Run `jobs`. [`Running ... sleep 100 &`]
4. Run `fg`. [`sleep 100`, the shell waits]; press Ctrl-C. [command ends, `echo $?` prints `130`]
5. Run `sleep 100 | cat`, press Ctrl-Z. [one job, `[1]+  Stopped                 sleep 100 | cat`]; `fg`,
   Ctrl-C. [both end]
6. Run `sleep 101`, Ctrl-Z, `sleep 102`, Ctrl-Z. [`jobs`: `[1]-` and `[2]+`]; `fg %1`, Ctrl-Z.
   [`jobs`: job 1 is now `+`]; `fg`, Ctrl-C, `fg`, Ctrl-C. [`jobs` prints nothing]
7. Run `fg`, `bg %9`, `fg 9` with no such job. [`fg: current: no such job`, `bg: %9: no such
   job`, `fg: 9: no such job`; `echo $?` prints `1`]
8. Run `sleep 100 &` then `bg`. [`bg: job 1 already in background`; `echo $?` prints `0`]
9. Run `cat &`, press Enter. [`[1]+  Stopped                 cat` before the next prompt]; type some text
   and Enter: it is not eaten by `cat`; `fg`, type `hi`, Enter. [`cat` echoes `hi`]; Ctrl-D, Ctrl-C.
10. Run `sleep 100 &`, `sleep 102`, press Ctrl-C. [only `sleep 102` ends; `jobs` still shows
    `sleep 100` as `Running`]
11. Run `sleep 100 | cat &`. [`[N] pid`, prompt at once; `jobs` lists the pipeline with ` &`]
12. Run `vi` (or `less /etc/passwd`), press Ctrl-Z. [prompt back, typed text is echoed again, lines
    end normally]; `fg`. [the editor returns with its screen and settings]
13. Run `sleep 100`, Ctrl-Z, then `exit`. [`There are stopped jobs.`]; `exit` again. [shell ends;
    `pgrep -x sleep` shows no stopped `sleep 100`]. Repeat with Ctrl-D, and with `true` between the
    two attempts. [the warning appears again]
14. Run `sleep 100 > out.txt`, press Ctrl-Z. [notice on the terminal, nothing written to `out.txt`;
    the next command behaves normally]
15. Run `printf 'fg\nbg\nexit\n' | build/shell` and `echo 'echo hi' | build/shell`. [`fg: no job
    control`, `bg: no job control`; behaviour as before for the rest]
16. Repeat step 1 and `fg` twenty times. [the same job number each time, no hang]

## Latency check (SC-001, 100 ms)

For a measured value run `PTY_TIMING=1 tests/pty/run.sh -k stop`; the test for scenario 1 prints
the time from sending Ctrl-Z to the `Stopped` line. It should be well under 100 ms on an idle
machine; the automated assertion is 1 s so a loaded machine cannot make it flaky.
