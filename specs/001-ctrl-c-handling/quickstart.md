# Quickstart: Validate Survive Ctrl-C

**Feature**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md) | **Contract**: [contracts/terminal-signals.md](contracts/terminal-signals.md)

## Prerequisites

- Build tools and the readline development package (see README.md "Prerequisites").
- `python3` (standard library only) for the terminal tests.

## Build and run all tests

```sh
cmake -B build -S . && cmake --build build
tests/run.sh             # golden cases, then terminal (pty) cases
tests/pty/run.sh         # terminal cases only
```

Expected: every golden case still passes with no change to any `.out` file (FR-011, SC-006), and
every terminal case passes. Without `python3` the run fails and says why.

## Manual check on a real terminal

Run `./run.sh` (or `build/shell`) in a terminal emulator and try each row. Expected result in
brackets.

1. Type `echo hel`, press Ctrl-C. [text discarded, new prompt on a new line]
2. Run `echo $?`. [`130`]
3. Run `cd /usr`, press Ctrl-C, run `pwd`. [`/usr`]
4. Run `sleep 100`, press Ctrl-C. [prompt returns within 1 s, one newline]
5. Run `echo $?`. [`130`]
6. Run `sleep 100 | cat`, press Ctrl-C. [both end, prompt returns]
7. Run `sleep 100`, press Ctrl-\. [command ends, `echo $?` prints `131`]
8. Run `sleep 100`, press Ctrl-Z. [nothing happens]; then Ctrl-C. [command ends]
9. Run `sleep 100 &`, press Ctrl-C, Ctrl-\, Ctrl-Z at the prompt, then `jobs`. [shell alive, job
   still `Running`]
10. Run `sleep 100 > /dev/null`, press Ctrl-C, then type any command. [shell normal afterwards]
11. Create a script that runs `sleep 100`, register it with `complete -C /path/to/script mycmd`,
    type `mycmd a`, press TAB, then Ctrl-C. [returns within 1 s, line `mycmd a` still there]
12. Run `printf 'sleep 100\n' | build/shell` and `echo 'echo hi' | build/shell`. [behaviour as
    before: no extra newlines or changes with piped input]

## Latency check (SC-001, 100 ms)

At a prompt, press Ctrl-C and watch for the new prompt; it should appear without a visible delay.
For a measured value, the terminal test for scenario 1 prints the elapsed time between sending
Ctrl-C and seeing the next prompt when run with `-v`; the figure should be well under 100 ms. (The
automated assertion is 1 s so a loaded machine cannot make it flaky.)
