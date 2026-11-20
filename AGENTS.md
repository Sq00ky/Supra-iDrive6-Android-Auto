# AGENTS.md

The rules for AI coding agents working in this repository are in [`CLAUDE.md`](CLAUDE.md). Read it first and follow it
exactly, whichever tool you are. It points to the full standards:

- [`docs/CODING-STANDARDS.md`](docs/CODING-STANDARDS.md)
- [`docs/TESTING.md`](docs/TESTING.md)

The short version: write C++03-style code the car's GCC 4.4.2 accepts, write a host test for every change (and a
regression test for every fix), run the full suite and report the real result, never claim something was verified on
a car unless it was, keep private data out of code, logs and fixtures, and do not push or post anything without the
user's explicit instruction.
