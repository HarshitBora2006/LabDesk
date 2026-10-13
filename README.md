# LabDesk: Smart Computer Lab Management and CPU Scheduling System

Milestone 1 (first 50%) of a combined DBMS + Operating Systems project.

LabDesk is a C++17 / Qt 6 Widgets desktop application backed by one SQLite
database. Administrators manage students, computers and maintenance. Students
sign in, take an available computer and end their session. A separate module
simulates CPU scheduling (FCFS, SJF, Priority, Round Robin) and the process
lifecycle.

> **Build status, read this first.** The scheduling engine and the SQL schema
> were compiled/executed and tested by the author. The Qt application and
> `database_tests` were written against the Qt 6 API but have **not yet been
> compiled**, because the development sandbox had no Qt or CMake. Expect to fix
> a small number of compile errors on the first build. See
> [Testing status](#testing-status) and record your own results in
> `docs/TESTING.md`.

---

## Contents

1. [Technology stack](#technology-stack)
2. [Building and running](#building-and-running)
3. [Demo credentials and data](#demo-credentials-and-data)
4. [Implemented functionality](#implemented-functionality)
5. [Where each DBMS concept is demonstrated](#where-each-dbms-concept-is-demonstrated)
6. [Operating-systems module](#operating-systems-module)
7. [Architecture](#architecture)
8. [Testing status](#testing-status)
9. [Known limitations](#known-limitations)
10. [Remaining work (next 50%)](#remaining-work-next-50)

Further documents: [`docs/SCHEMA.md`](docs/SCHEMA.md) (database design) and
[`docs/TESTING.md`](docs/TESTING.md) (automated tests and manual test guide).

---

## Technology stack

| Concern        | Technology                                       |
|----------------|--------------------------------------------------|
| Language       | C++17                                            |
| GUI            | Qt 6 Widgets (Fusion style + one stylesheet)     |
| Database API   | Qt SQL with the `QSQLITE` driver                 |
| Database       | SQLite (bundled with Qt)                         |
| Build          | CMake 3.19+                                      |
| Tests          | Plain C++ test executables run by CTest; an extra Python `sqlite3` validator for the schema |

No other libraries are used. The scheduling engine (`src/scheduler.*`,
`src/processstate.*`) uses only the C++ standard library.

---

## Building and running

### Prerequisites

* Qt **6.2 or newer** with the *Qt SQL* module (it includes the SQLite driver).
* CMake 3.19+ and a C++17 compiler (MSVC 2019+, GCC 9+, or Clang 10+).

### Linux / macOS

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt/6.x.x/gcc_64
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/labdesk
```

On macOS the executable is `build/labdesk.app/Contents/MacOS/labdesk`.

### Windows (Qt installer + MSVC)

Open the *Qt 6.x (MSVC) command prompt* so Qt's `bin` folder is on `PATH`, then:

```bat
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2019_64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\Release\labdesk.exe
```

To run it outside the Qt prompt, copy the Qt runtime next to the executable:

```bat
windeployqt build\Release\labdesk.exe
```

`windeployqt` also copies `sqldrivers\qsqlite.dll`. Without that plugin the
app reports "The Qt SQLite driver (QSQLITE) is not available".

### Qt Creator

Open `CMakeLists.txt`, choose a Qt 6 kit, and build. `labdesk` is the run
target; `scheduler_tests` and `database_tests` appear as test targets.

### Command-line options

```
labdesk [--db <path>] [--no-seed]
```

* `--db <path>` uses a specific database file. By default the file is
  `labdesk.db` in the per-user application-data folder; the full path is shown
  in the window's status bar.
* `--no-seed` starts with an empty database instead of demo data.

To start over, close the app and delete the database file.

---

## Demo credentials and data

Demo data is inserted **only** when both the `student` and `computer` tables
are empty, and every record is labelled as demo data.

| Student ID | Name                  | Password  |
|------------|-----------------------|-----------|
| DEMO01     | Ananya Rawat (demo)   | `demo123` |
| DEMO02     | Kabir Negi (demo)     | `demo123` |
| DEMO03     | Meera Bisht (demo)    | `demo123` |
| DEMO04     | Rohan Joshi (demo)    | `demo123` |

The demo data also contains:

* 20 computers: `LAB-A-01` to `LAB-A-12` (Lab A, Block 1) and `LAB-B-01` to
  `LAB-B-08` (Lab B, Block 2), all locations tagged "(demo)".
* `LAB-B-08` is in `MAINTENANCE` with one open issue, so the resolve workflow
  can be shown immediately.
* An unlinked scheduler workload P1 to P4 (the textbook set used in the tests).

The administrator screens have no login in this milestone (see limitations).

---

## Implemented functionality

### Administration

* **Dashboard.** Six counts (total, available, occupied, maintenance, active
  sessions, students), all from one SQL query. A consistency check confirms
  that every OCCUPIED computer has exactly one active session and that every
  MAINTENANCE computer has an open issue. A colour-coded lab-floor grid shows
  every computer, and a table lists the active sessions.
* **Students.** Add, view, search, update (optional new password) and delete.
  Fields are validated, and duplicate IDs and emails (case-insensitive) are
  rejected in the service and again by the database. Students with any
  session history cannot be deleted.
* **Computers.** Add (always `AVAILABLE`), view, filter by status, update the
  name and location, and delete only when there is no history. Status is
  deliberately not editable by hand: it changes only through the workflows.
* **Maintenance.** Report an issue: computers that are in use are refused, and
  an AVAILABLE computer moves to MAINTENANCE. Edit descriptions and view the
  history. Resolving an issue releases the computer only if no other open
  issue and no active session remain.
* **Sessions and transactions.** Full session history, plus an admin
  "end selected session" action. The **transaction demonstration** tab runs
  the real service code, prints every SQL step with COMMIT or ROLLBACK, shows
  before/after state, and can inject a failure mid-transaction.

### Student portal

* Sign in with student ID and password, checked against the salted hash
  stored in `student`. Nothing else is reachable before sign-in.
* The dashboard shows name, ID, department, the current session (computer,
  location, start time, live elapsed time) and the table of AVAILABLE
  computers.
* **Use selected computer** runs the allocation transaction, which rechecks
  availability inside the transaction. **End session** runs the termination
  transaction. Every change refreshes all screens from the database.

### Operating systems

* **CPU scheduling page.** Editable process table with bounded spin-box
  editors, and a textbook example. Workloads can be saved to and loaded from
  the `PROCESS` table, either unlinked or linked to a lab session. Choose one
  algorithm or compare all four. Results include a Gantt chart with hatched
  idle periods, and per-process arrival, burst, priority, first run, CT, TAT,
  WT and RT. Averages, CPU utilisation and context switches are also shown.
* **Process states page.** An interactive single-CPU lifecycle simulator with
  six transition buttons, illegal moves refused and WAITING included. It
  draws a live state diagram and keeps a transition log. A second tab replays
  the last scheduler run as a state trace, with a time slider showing every
  process's state at any instant.

---

## Where each DBMS concept is demonstrated

| Concept | Implementation | Evidence |
|---|---|---|
| **Atomicity** | `SessionService::allocateComputer` and `endSession` run every step inside one `Transaction` (`src/database.*`) | Transaction demo tab; TEST 12 |
| **COMMIT** | `Transaction::commit()` runs `COMMIT`; the step log prints it | Transaction demo tab |
| **ROLLBACK** | Any failed step calls `Workflow::fail`, which issues `ROLLBACK`. The RAII destructor also rolls back if `commit()` is never reached | `FaultInjector` makes the INSERT fail after the UPDATE; the computer stays AVAILABLE |
| **Isolation (basic)** | `BEGIN IMMEDIATE` takes SQLite's write lock before the checks. Updates are guarded (`... AND status = 'AVAILABLE'`) and the affected-row count is verified | `sessionservice.cpp` |
| **Referential integrity** | Foreign keys with `ON DELETE RESTRICT`. `PRAGMA foreign_keys = ON` is set on every connection and **read back** to verify | TEST 9 (raw `DELETE` fails with `FOREIGN KEY constraint failed`) |
| **Prepared statements** | Every query that takes input uses `prepare()` and `addBindValue()` | All `*service.cpp` files |
| **Constraints** | `NOT NULL`, `UNIQUE` (case-insensitive email), `CHECK` (status values, timestamp format, non-negative times, priority 0 to 99, end ≥ start) | `resources/sql/schema.sql` |
| **Business rules in the DB** | Partial unique indexes (one ACTIVE session per computer and per student) and 8 triggers | `tools/validate_schema.py`, section "Database-level guards" |

Why have both service checks **and** database constraints? Service checks
produce clear messages for users. The database constraints still hold if code
has a bug, if someone edits the file with another SQLite tool, or if a future
client bypasses the services. The validator's "application code bypassed"
tests show this directly.

---

## Operating-systems module

Conventions, implemented in `src/scheduler.cpp` and covered by tests:

* Time is in discrete integer units starting at 0.
* **Priority: lower number = higher priority**, valid range 0 to 99.
* Ties go to the earlier arrival, then to the earlier row in the input.
* If no process has arrived, the CPU is idle until the next arrival, and the
  idle period appears in the Gantt chart.
* FCFS, SJF and Priority are **non-preemptive**. SJF and Priority choose only
  among processes that have already arrived.
* **Round Robin:**
  * The quantum must be 1 or more.
  * A process that arrives exactly when a quantum expires joins the ready
    queue *before* the preempted process.
  * If the ready queue is empty when a quantum expires, the running process
    simply continues; this is not counted as a context switch.
* Formulas: `TAT = CT − AT`, `WT = TAT − BT`, `RT = first run − AT`.

Hand-checked example (set A, also in the tests):

| PID | AT | BT | PR |
|-----|----|----|----|
| P1  | 0  | 5  | 2  |
| P2  | 1  | 3  | 3  |
| P3  | 2  | 8  | 1  |
| P4  | 3  | 6  | 4  |

| Algorithm      | Order / CT                                   | Avg WT | Avg TAT |
|----------------|----------------------------------------------|--------|---------|
| FCFS           | P1 0–5, P2 5–8, P3 8–16, P4 16–22            | 5.75   | 11.25   |
| SJF            | P1 0–5, P2 5–8, P4 8–14, P3 14–22            | 5.25   | 10.75   |
| Priority       | P1 0–5, P3 5–13, P2 13–16, P4 16–22          | 7.00   | 12.50   |
| RR (q = 2)     | CT: P1 14, P2 11, P3 22, P4 20               | 9.75   | 15.25   |

**Computer allocation is not CPU scheduling.** Allocation decides which
*physical computer* a student may use, and it is a database transaction.
Scheduling decides which *simulated process* gets CPU time. The process-state
simulation never reads or writes computer or session status, and it does not
control real operating-system processes.

---

## Architecture

```
CMakeLists.txt
resources/
  resources.qrc            embeds the schema into the executables
  sql/schema.sql           single source of truth for tables, indexes, triggers
src/
  main.cpp                 command line, database open + seed, main window
  database.h/.cpp          connection, FK verification, schema loader, seed,
                           Transaction (RAII), FaultInjector, error mapping
  models.h, opresult.h     plain records and a uniform result type
  studentservice.*         CRUD + authentication
  computerservice.*        CRUD, dashboard stats, consistency check
  sessionservice.*         allocation and termination transactions
  maintenanceservice.*     report / edit / resolve workflows
  processservice.*         atomic save/load of scheduler workloads
  services.h               bundles the services for the GUI
  passwordhasher.*, timeutil.*
  scheduler.h/.cpp         FCFS, SJF, Priority, RR (pure C++17, no Qt)
  processstate.h/.cpp      state machine + lifecycle simulator (pure C++17)
  mainwindow.h/.cpp        sidebar navigation, cross-page refresh
  ui/                      one class per page, plus theme, helpers,
                           Gantt chart and state-diagram widgets
tests/
  scheduler_tests.cpp      138 checks, no Qt needed
  database_tests.cpp       service-level integration tests (Qt Core + Sql)
tools/
  validate_schema.py       Qt-independent SQL validator (Python 3, stdlib only)
docs/
  SCHEMA.md, TESTING.md
```

The project is built as three CMake targets:

* `labdesk_os` contains the scheduling engine and has no Qt dependency.
* `labdesk_core` contains the database and services, using Qt Core and Sql
  only.
* The `labdesk` application adds the Widgets UI.

GUI classes never contain SQL; they call services, which return `OpResult`
values with a message the user can read. After any change, a page emits
`dataChanged()` and `MainWindow` refreshes every page. This keeps the admin
and student views on the same committed state.

---

## Testing status

| Item | How | Status |
|---|---|---|
| Scheduling engine + process-state model | `scheduler_tests` built with g++ 13, `-Wall -Wextra -Wpedantic -Werror`, AddressSanitizer + UBSan | **138 / 138 passed** |
| Schema, constraints, triggers, transaction SQL sequences | `python3 tools/validate_schema.py` (SQLite 3.45) | **53 / 53 passed** |
| Source completeness | Script check: all sources in CMake, all `Q_OBJECT` headers listed for AUTOMOC, every declared method defined | Passed |
| Qt application (`labdesk`) | — | **Not compiled yet** |
| `database_tests` (services through Qt SQL) | — | **Not compiled or run yet** |
| GUI behaviour | Manual guide in `docs/TESTING.md` | **Not performed yet** |

The Python validator replays the *same SQL statements* the services run, but
it does not execute the C++ code. Only `database_tests` proves that the C++
services call that SQL correctly.

---

## Known limitations

* **No administrator authentication.** Anyone who can run the app can open
  the admin pages.
* **Password hashing** is salted SHA-256 iterated 20 000 times. That is far
  better than plaintext but weaker than a memory-hard KDF such as Argon2id or
  bcrypt.
* **Single process, single connection.** `BEGIN IMMEDIATE` and guarded
  updates are correct for concurrent writers, but no multi-client scenario has
  been tested. SQLite on a network share is not suitable for many clients.
* SJF and Priority are non-preemptive only (as specified). There is no SRTF,
  no preemptive priority and no aging.
* The process-state simulator is educational. It does not observe real OS
  processes, and the scheduler trace contains no WAITING state because the
  simulated workloads are CPU-bound.
* No reports or exports, no session time limits, no booking or queueing.
* The UI has not been checked for accessibility or high-DPI layout because it
  has not been run yet.

---

## Remaining work (next 50%)

1. **Finish milestone 1 properly:** compile, fix errors, run both CTest
   targets, and work through the manual guide, recording results.
2. Administrator accounts and roles (an `admin` table and a login screen),
   plus an audit log of admin actions.
3. Preemptive algorithms (SRTF, preemptive priority), priority aging, and
   process I/O bursts so the scheduler trace includes WAITING.
4. Usage reports (hours per computer and per student, peak times) exported
   to CSV.
5. Session policies (maximum duration, automatic end) and a waiting queue for
   when every computer is busy. This is a natural place to reuse the
   scheduling ideas.
6. A multi-client concurrency test (two processes allocating the same
   computer) to demonstrate isolation under real contention.
7. Packaging: an installer via `windeployqt` or `macdeployqt`.
