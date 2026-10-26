# shell

A POSIX-style shell written in C++23.

Features: quoting and backslash escapes, `$?`, `~` expansion, redirection (`>`, `>>`, `N>`,
`>&N`, and `<` for input), pipelines (`a | b | c`), command lists with `;`, `&&` and `||`, background jobs (`&`, also in the middle of a line: `sleep 1 & echo hi`; `jobs`, `Done` reports), job
control (Ctrl-Z stops the running command, `fg` and `bg` continue it, a warning when you
leave with stopped jobs), the builtins
`exit`, `echo`, `type`, `pwd`, `cd`, `jobs`, `fg`, `bg` and `complete`, and TAB
completion (commands, filenames, and scripts registered with `complete -C`).

## Build and run

## Prerequisites

Install these **before** building. Besides the build tools, you need the GNU readline
*development* package (headers and `libreadline.so`); the plain runtime package is not enough.

- CMake 3.13 or newer
- A C++23 compiler (GCC 13+ or Clang 17+ recommended)
- GNU readline development files
- A POSIX system (Linux); Windows is not supported
- `python3` (standard library only) to run the terminal tests in `tests/pty/`; it is not needed to
  build or run the shell. `tests/run.sh` fails with a clear message if it is missing
- Catch2 v3 to build and run the unit tests in `tests/unit/` (`build/unit_tests`); it is not needed
  to build or run the shell. `tests/run.sh` fails with a clear message if it is not configured

| Distribution | Install command |
|---|---|
| Debian, Ubuntu, Linux Mint, Pop!_OS | `sudo apt install build-essential cmake libreadline-dev` |
| Fedora, RHEL, CentOS Stream, Rocky, Alma | `sudo dnf install gcc-c++ cmake readline-devel` |
| Arch, Manjaro, EndeavourOS | `sudo pacman -S --needed base-devel cmake readline` |
| openSUSE | `sudo zypper install gcc-c++ cmake readline-devel` |
| Alpine | `sudo apk add build-base cmake readline-dev` |
| Void | `sudo xbps-install -S base-devel cmake readline-devel` |
| Gentoo | `sudo emerge --ask dev-build/cmake sys-libs/readline` |
| NixOS / Nix | `nix-shell -p cmake gcc readline` |

Catch2 v3 (tests only): Debian, Ubuntu `sudo apt install catch2`; Fedora, RHEL `sudo dnf install
catch-devel`; Arch `sudo pacman -S catch2`; openSUSE `sudo zypper install Catch2-devel`; Alpine
`sudo apk add catch2`. Without a package, configure with `cmake -B build -S . -DSHELL_FETCH_CATCH2=ON`
(downloads a pinned release; needs network and git). `-DSHELL_UNIT_TESTS=OFF` skips the unit tests.

On Arch and Gentoo the headers ship with the main readline package. On older releases whose
default compiler is too old for C++23 (for example Debian 12 or Ubuntu 22.04), install a newer
`g++` or `clang` and pass it to CMake with `-DCMAKE_CXX_COMPILER=...`.

To check the install: `ls /usr/include/readline/readline.h` should exist (the path can differ on
Nix or Alpine).

Troubleshooting: `fatal error: readline/readline.h: No such file or directory` (or the linker
error `cannot find -lreadline`) means the readline development package is missing. Install it
from the table above, then re-run cmake in a clean build directory (`rm -rf build`).

## Build and run

```sh
./run.sh                 # configure, build, then start the shell
tests/run.sh             # golden cases, unit tests, then terminal (pty) tests; needs build/shell, Catch2 and python3
tests/pty/run.sh         # terminal tests only
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for how the code is organised.
