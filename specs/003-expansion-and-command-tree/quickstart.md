# Quickstart: Validating the Feature

Run from the repository root.

## Prerequisites

CMake >= 3.13, a C++23 compiler, the readline development package, `python3` (see CLAUDE.md), and
Catch2 v3, needed only for the tests: `sudo apt install catch2` (Debian/Ubuntu),
`sudo dnf install catch-devel` (Fedora), `sudo pacman -S catch2` (Arch). Without a package, configure
with `-DSHELL_FETCH_CATCH2=ON` (downloads a pinned release; needs network and git). Building and
running the shell needs neither. `-DSHELL_UNIT_TESTS=OFF` skips the unit tests (then `tests/run.sh`
reports them as not configured and fails).

## 1. Build

```sh
cmake -B build -S . && cmake --build build --target shell unit_tests   # add -DSHELL_FETCH_CATCH2=ON to the first command if Catch2 is not installed
```

## 2. Unit tests (no shell started)

```sh
build/unit_tests
```
Expected: every case passes; includes at least 10 expansion cases (SC-004) and the parse
characterization cases.

## 3. Whole suite

```sh
tests/run.sh
```
Expected: golden cases and pty tests pass; the only `.out` changes in the diff are the cases listed
in plan.md, and new `tilde_*` cases exist. `tests/run.sh` runs `build/unit_tests` too and fails if
it is missing or fails.

## 4. By hand (HOME=/usr)

```sh
printf 'echo ~\necho ~/x\necho "~" \x27~\x27 \\~\necho a~\ncd ~\npwd\necho hi > ~/nope\nexit\n' | HOME=/usr build/shell
```
Expected: `/usr`, `/usr/x`, `~ ~ ~`, `a~`, `/usr`, then the shell's own error for the unwritable file
(`/usr/nope: Permission denied` style, text from `errno`).

## 5. Behaviour that must not change

```sh
printf 'false\necho $?\necho $?>f\nls f\nexit\n' | build/shell
```
Expected: `1`, then `f`, as before this feature; `f` holds `0` (the status of the `echo $?` before it).
