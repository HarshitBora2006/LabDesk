# LabDesk

Computer Lab Management System with OS and DBMS modules.
C++17, Qt 6 Widgets, Qt SQL (SQLite) and CMake.

## Features

**Administrator**
- Dashboard with lab statistics, computer status chart, recent activity and laboratory overview
- Student and computer management (add, edit, delete, search)
- Maintenance reports
- Allocation management: allocate computers, waiting queue (FCFS, SJF or priority), measured waiting and usage times
- Usage logs and reports
- Settings: password change and administrator accounts

**Student**
- Dashboard with current session and computer availability
- Request a computer, join the waiting queue, end a session
- Personal allocation history

**OS Management**
- OS Simulator: processes, process states, CPU scheduling (FCFS, SJF, Priority, Round Robin), context switches and memory allocation, step by step, with Gantt chart and metrics

**DBMS Management**
- Database overview: engine, schema version, tables and record counts
- Schema, keys and relationships read from the database
- SQL operations used by the application and a read-only query console
- Transaction demonstration (COMMIT / ROLLBACK)
- DBMS Concept Guide

## Requirements

- Qt 6.5 or newer with Qt SQL (developed with Qt 6.12, MinGW 64-bit)
- CMake 3.19 or newer
- A C++17 compiler

## Build and run

### Qt Creator (Windows)

1. Open `CMakeLists.txt` with **File > Open File or Project**.
2. Select a Qt 6 kit and press **Configure Project**.
3. Build with **Ctrl+B** and run `labdesk` with **Ctrl+R**.

### Command line

From a Qt command prompt:

```bat
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\mingw_64
cmake --build build
ctest --test-dir build --output-on-failure
build\labdesk.exe
```

To run outside the Qt prompt, run `windeployqt build\labdesk.exe` first.

### Options

```
labdesk [--db <file>] [--no-seed] [--reset-admin]
```

`--db` selects a database file (default: `labdesk.db` in the user's application data folder).
`--no-seed` starts without demo data.
`--reset-admin` removes all administrator accounts (after confirmation) so a new one can be created. Other data is kept.

## First login

- **Administrator:** there is no default account. On first start, choose Admin Login and create the administrator account. If the login screen shows "Admin sign in", an account already exists; start once with `--reset-admin` to create a new one.
- **Students:** the demo data contains `DEMO01` to `DEMO04` with password `demo123`.

Passwords are stored as salted hashes.

## Database

SQLite with foreign keys enabled. Tables: `student`, `computer`, `session`, `maintenance`, `lab_request`, `lab_setting`, `process` and `admin`.
The schema is in `resources/sql/schema.sql` and is applied automatically on start; older databases are upgraded without data loss.
See [docs/SCHEMA.md](docs/SCHEMA.md) for details.

Lab records (students, computers, sessions, requests) are real data. The OS modules work on simulated processes and do not affect the database or the operating system.

## Tests

| Program | Covers |
|---|---|
| `scheduler_tests` | Scheduling algorithms used by the simulator |
| `ossimulator_tests` | OS Simulator |
| `os_extra_tests` | Process model |
| `labqueue_tests` | Waiting queue and replay |
| `database_tests` | Database services (needs Qt) |

Run them with `ctest`. `tools/validate_schema.py` checks the SQL schema with Python's `sqlite3`.
The manual test steps are in [docs/TESTING.md](docs/TESTING.md).

## Project structure

```
CMakeLists.txt
resources/sql/schema.sql   database schema
src/                       services, database access, OS engines
src/ui/                    pages and widgets
tests/                     test programs
tools/                     schema validator
docs/                      schema and testing documentation
```

## Limitations

- Single database file; several computers sharing one file over a network is not supported.
- The OS Simulator models one CPU with simple block-based memory.
- No password reset; another administrator can create a new account.
