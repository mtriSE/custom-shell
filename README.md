# shell

A POSIX-style shell written in C++23.

Features: quoting and backslash escapes, `$?`, redirection (`>`, `>>`, `N>`,
`>&N`), pipelines (`a | b | c`), background jobs (`&`, `jobs`, `Done` reports),
the builtins `exit`, `echo`, `type`, `pwd`, `cd`, `jobs` and `complete`, and TAB
completion (commands, filenames, and scripts registered with `complete -C`).

## Build and run

Requires `cmake`, a C++23 compiler and the GNU readline development files.

```sh
./run.sh                 # configure, build, then start the shell
tests/run.sh             # regression tests (needs build/shell)
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for how the code is organised.
