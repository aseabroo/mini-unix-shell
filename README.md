# Mini Unix Shell

A small Unix-like shell written in C to demonstrate process control, command parsing, I/O redirection, background execution, built-in commands, and POSIX signal handling.

## Features

- executes external programs with `fork()` and `execvp()`;
- waits for foreground processes with `waitpid()`;
- supports background execution with a trailing `&`;
- reports completed background jobs without blocking the shell;
- supports input redirection with `<` and output redirection with `>`;
- implements built-in `cd`, `status`, and `exit` commands;
- expands `$$` to the shell process ID;
- keeps the shell itself immune to `SIGINT` while foreground children receive the default behavior;
- toggles foreground-only mode with `SIGTSTP`;
- uses bounded buffers and centralized cleanup helpers.

## Why this project exists

This is an independent portfolio implementation of operating-systems concepts I previously practiced in coursework. It is not a republished assignment submission. The source, project structure, documentation, build system, and test harness were written for this repository.

The goal is to demonstrate practical understanding of:

- Unix process creation;
- process replacement;
- parent/child synchronization;
- asynchronous child reaping;
- file-descriptor manipulation;
- signals;
- shell parsing;
- dynamic memory management.

## Build

```bash
make
```

The executable is written to:

```text
bin/minish
```

## Run

```bash
./bin/minish
```

Example session:

```text
minish$ pwd
/Users/example/mini-unix-shell

minish$ echo hello > output.txt

minish$ cat < output.txt
hello

minish$ sleep 1 &
background pid 43120

minish$ 
background pid 43120 completed: exit 0
```

## Built-in Commands

### `cd [path]`

Changes the current working directory. With no path, the shell changes to `$HOME`.

### `status`

Prints the exit status or terminating signal of the most recent foreground external command.

### `exit`

Terminates active background children and exits the shell.

## Parsing Rules

The shell recognizes:

- whitespace-separated arguments;
- `< file` for standard-input redirection;
- `> file` for standard-output redirection;
- a final `&` for background execution;
- lines beginning with `#` as comments;
- `$$` as the current shell PID.

This is intentionally a minimal shell rather than a Bash-compatible parser. Quoting, pipes, glob expansion, shell variables, and command substitution are outside the current scope.

## Signals

- the shell ignores `SIGINT`;
- foreground child processes use the default `SIGINT` behavior;
- background child processes ignore `SIGINT`;
- `SIGTSTP` toggles foreground-only mode;
- children ignore `SIGTSTP` so the shell owns the mode toggle.

## Project Structure

```text
mini-unix-shell/
├── src/
│   └── shell.c
├── tests/
│   └── smoke.sh
├── .gitignore
├── Makefile
└── README.md
```

## Test

```bash
make test
```

The smoke test checks basic command execution, PID expansion, redirection, and built-in status behavior.

## Limitations

This project is intentionally small. It does not currently implement:

- pipelines;
- quoted strings;
- wildcard expansion;
- environment-variable expansion other than `$$`;
- job-control groups;
- command history.

Those omissions keep the repository focused on the core POSIX process and signal concepts it is meant to showcase.
