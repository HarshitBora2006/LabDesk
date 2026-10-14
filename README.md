# LabDesk: Smart Computer Lab Management and CPU Scheduling System

Milestone 1 (first 50%) of a combined DBMS + Operating Systems project.

LabDesk is a C++17 / Qt 6 Widgets desktop application backed by one SQLite
database. Administrators manage students, computers and maintenance. Students
sign in, take an available computer and end their session. A separate module
simulates CPU scheduling (FCFS, SJF, Priority, Round Robin) and the process
lifecycle.

> **Build status, read this first (version 0.7.3).** Version 0.7.1 was built
> and run by the user on Windows with Qt 6.12 / MinGW (screenshots of the Lab
> queue and CPU scheduling pages). 0.7.2–0.7.3 make measured lab data both
> honest and readable: a Minutes / Seconds unit choice for the measured CPU
> scheduling workload (minutes by default) with a distortion warning, and
> m:ss durations on the Lab queue page. The
> author compiled and ran everything that does not need Qt:
> `scheduler_tests` 204 / 204, `labqueue_tests` 49 / 49 and the SQL validator
> 91 / 91. The Qt changes in 0.7.2–0.7.3 and `database_tests` (98 checks) have **not
> been compiled by the author**; rebuild and run `ctest`.

---

## Contents

1. [What changed in 0.7.3](#what-changed-in-073)
1. [What changed in 0.7.2](#what-changed-in-072)
1. [What changed in 0.7.1](#what-changed-in-071)
1. [What changed in 0.7.0: lab queue with real data](#what-changed-in-070-lab-queue-with-real-data)
1. [What changed in 0.6.0](#what-changed-in-060)
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

## What changed in 0.7.3

Real lab sessions last minutes or hours, so seconds produce large numbers
(a 2-hour session is burst 7200). The measured workload therefore has a
**Time unit** selector:

* **Minutes (default).** Rounded to the nearest minute, so a value is off by
  at most 30 s. A 1-hour session is 60 and a 2-hour session is 120, with
  under 1 % error.
* **Seconds.** Exact, for very short test visits.

When minutes are chosen and rounding changes any visit's length by more than
10 % (typical for quick test visits), the page shows a Warning with the count
and suggests Seconds. When nothing is distorted, the Note says so. The column
headings, the quantum label and the result text always name the unit in use.

## What changed in 0.7.2

0.7.1 converted measured visits to **whole minutes**. That distorted short
visits: a 4-second visit became a 1-minute burst, and 2 min 27 s became 2.
Real data must not be inflated, so:

* **CPU scheduling → "Measured lab visits" now uses seconds** (0.7.3 adds a
  Minutes / Seconds choice, with minutes as the default). A visit of
  2 min 27 s has burst 147, a 4-second visit has burst 4, and a request
  18 min 30 s after the first has arrival 1110. Values are rounded only to the
  nearest second, and a sub-second visit counts as 1 s. The column headings
  read "Arrival time (s)" and "Burst time (s)", the quantum label says
  "in seconds", and the result text states that all times are in seconds.
* The engine limits were raised so real second-level data fits: arrival up to
  31 536 000 (one year of seconds), and burst and quantum up to 86 400 (one
  day). Round Robin refuses a quantum that would cut the workload into more
  than 50 000 slices and says which minimum quantum to use (e.g. 60 s).
* **Lab queue page:** durations are shown exactly as m:ss (or h:mm:ss), e.g.
  `0:04`, `2:27`, `18:30`, instead of decimal minutes such as "0.1". This
  applies to the tiles, the measured table, the waiting list and the replay
  table.

## What changed in 0.7.1

* **CPU scheduling → Workload → "Measured lab visits".** Every finished lab
  visit becomes one process, read straight from the database: Process ID =
  `R` + request number, arrival = minutes after the first request, burst =
  the **actual** session length (whole minutes in 0.7.1, **seconds since
  0.7.2**), and priority =
  purpose. The Source column marks each row "Measured, request #n (student)".
  * The rows are read-only. *Save* is disabled, edited rows are marked
    "edited", and *Revert to saved* reloads the real data.
  * The table refreshes automatically when another visit finishes.
  * It is simulated as **one computer**. SJF and SRTF therefore know the real
    lengths in advance, which a real lab cannot; for several computers and
    estimate-based SJF, use Lab queue → What-if replay. Up to the 100 most
    recent visits are loaded (the engine's limit).
* **Lab queue.** The tile is now "Avg estimate error, absolute (min)". The
  per-row error keeps its sign (negative = shorter than expected), and the
  caption explains why the average uses the size of each error. Units have
  moved into the column headings.
* **Numeric columns.** Headings of number columns are right-aligned, so each
  heading sits above its numbers. This applies to the CPU scheduling results
  and comparison tables, the Lab queue tables, the replay table and the
  process-state tables.

---

## What changed in 0.7.0: lab queue with real data

Students no longer pick a computer anonymously. Every visit starts with a
**request** stating its purpose and expected length, and the timings are
recorded by the database as they happen:

| Value | Recorded when | Source |
|---|---|---|
| Arrival time | the student presses *Start session* or *Join the waiting queue* | `lab_request.requested_at` |
| Priority | at the request, from the purpose: exam 1, assignment 2, practice 3 | `lab_request.priority` (a CHECK keeps it consistent with `purpose`) |
| Expected length | at the request, typed by the student (5–480 min) | `lab_request.expected_minutes`; SJF decides with this estimate |
| Start | a computer is assigned (at once, or later from the queue) | `session.start_time` = `lab_request.allocated_at` |
| **Burst** (actual length) | the session ends | `session.end_time − session.start_time`; measured, never typed in |
| Waiting time | derived | start − arrival (0 when a computer was free) |
| Turnaround | derived | end − arrival |

How the queue works:

* If no computer is free (or others are already waiting), the student joins
  the queue.
* Whenever a computer becomes free, the database transaction that freed it
  also assigns it to the next waiting student, chosen by the administrator's
  policy: **FCFS**, **SJF** (by expected length) or **Priority** (by
  purpose). A computer becomes free when a session ends, a repair is resolved
  or a computer is added. If the hand-over fails, the whole transaction rolls
  back.
* All policies are non-preemptive, because sessions are never interrupted.

The new **Lab queue** page (Administration) shows:

* the policy selector;
* waiting students in the order they will be served;
* measured results per visit and their averages (waiting, actual burst,
  turnaround, longest wait, estimate error);
* a **what-if replay** that re-runs the recorded visits under all three
  policies on N computers.

In a report, describe this as an *application* of CPU-scheduling ideas to the
lab: students are jobs and computers are processors. It is not CPU scheduling
inside a computer; the CPU scheduling page remains the simulator for that.

Schema version 2 only **adds** `lab_setting` and `lab_request` (plus indexes
and triggers). Opening a 0.5 / 0.6 database migrates it automatically and
keeps all existing data. Sessions created before 0.7, or through the admin
transaction demonstration, have no request and therefore no arrival time;
they are excluded from the measured results.

---

## What changed in 0.6.0

| Problem | Root cause found | Fix |
|---|---|---|
| Black bars and black empty areas | No `QPalette` was ever set. With Fusion on Windows (Qt 6.5+ follows dark mode), every surface the stylesheet did not paint (tab pages, scroll-area viewports, scrollbars, header gaps, combo pop-ups, message boxes) used the dark system palette. Page subclasses also relied on stylesheet backgrounds without `Qt::WA_StyledBackground`. | `theme::applyApplicationTheme()` sets a complete light palette for all colour groups before any widget exists. On Qt 6.8+ it also forces the light colour scheme, which keeps the Windows title bar light. The stylesheet now covers scrollbars, headers, pop-ups, dialogs and tab pages, and pages set `WA_StyledBackground`. |
| Unreadable dark notifications | A global `QWidget { color: navy }` rule put dark text on those dark surfaces, and tooltips were deliberately dark. | The global colour rule is removed (text comes from the palette) and tooltips are light. `MessageBanner` has Success, Note, Warning and Error variants, each with its own tint, accent bar **and** leading word, so the type is not shown by colour alone. |
| Dashboard and lab floor cramped or misaligned | One `QGridLayout` with fixed 6 columns inside a nested scroll area, and no laboratory grouping. | The dashboard is a scrollable page. Stat cards and laboratory cards sit in a `ResponsiveGrid` (equal-width columns that reflow on resize), and computer tiles wrap in a `FlowLayout` with equal spacing. New figures: number of laboratories (distinct locations) and occupancy, overall and per lab. |
| Empty pages looked broken | Hidden tables left blank space; query failures looked like empty data. | Each section shows a message when its query **succeeds** with no rows (e.g. "No active maintenance issues.", "No active computer session.", "No computers available for allocation.") and a separate error box when the query **fails**. |
| CPU scheduling confusing; saved workloads unclear | The workload selector only acted on Load/Save buttons, and nothing explained what a workload is. | Numbered sections; plain-language explanations; per-algorithm descriptions. Choosing a session loads its details and saved workload from SQLite, and every row shows its source (database row #, new, or example). There is an unsaved-changes indicator, Save / Update / Revert, and no values are invented. **SRTF** is added and tested. |
| Process states confusing | Terse labels, no explanation of states or of refused actions. | Introduction, state legend, one button per textbook transition, a plain-language reason when an action is not allowed, current state, last action (previous and new state), CPU indicator, history, diagram, and "Create from last CPU scheduling run". |

No database schema change was needed. Laboratories are derived from
`computer.location`, and saved workloads continue to use the existing
`process` table (`session_id` NULL = demo workload).

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

### Windows with Qt Creator (recommended)

1. Install Qt 6 (6.5 or newer recommended) with the **Qt Online Installer**.
   Select a desktop kit, e.g. *MSVC 2022 64-bit* or *MinGW 64-bit*. Qt SQL
   and its SQLite driver are part of the standard Qt installation. CMake and
   Ninja are offered under *Developer and Designer Tools*.
2. Extract `LabDesk_Improved_Final.zip` to a path without special characters,
   e.g. `C:\Projects\LabDesk`.
3. In Qt Creator choose **File → Open File or Project…** and select
   `CMakeLists.txt`. Pick your Qt 6 kit and press **Configure Project**.
4. Build with **Ctrl+B**. Four targets are built: `labdesk`,
   `scheduler_tests`, `labqueue_tests` and `database_tests`.
5. Run: choose the `labdesk` run target and press **Ctrl+R**.
6. Test: run `scheduler_tests`, `labqueue_tests` and `database_tests` as run targets (each
   prints every check and ends with `N passed, 0 failed`), or run `ctest` in
   the build folder from a Qt command prompt (below). Qt Creator can also list
   them under **Tools → Tests** once CTest is enabled in *Preferences →
   Testing*.

### Windows from a command prompt

Open the *Qt 6.x (MSVC or MinGW) command prompt* from the Start menu, so the
Qt `bin` folder is on `PATH`, then:

```bat
cd C:\Projects\LabDesk
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.x.x\msvc2022_64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
build\Release\labdesk.exe
```

With MinGW or Ninja generators the executable is `build\labdesk.exe`.

To run outside the Qt prompt, copy the Qt runtime next to the executable:

```bat
windeployqt build\Release\labdesk.exe
```

`windeployqt` also copies `sqldrivers\qsqlite.dll`. Without that plugin the
app reports "The Qt SQLite driver (QSQLITE) is not available".

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

* **Dashboard.** Figures for laboratories, total computers, available,
  occupied, under maintenance, active sessions and overall occupancy, all from
  one SQL query. A consistency check confirms that every OCCUPIED computer has
  exactly one active session and that every MAINTENANCE computer has an open
  issue.
* **Laboratory cards.** One card per laboratory (`GROUP BY location`) with
  status counts, an occupancy bar, colour-coded computer tiles that wrap to the
  card width, and **View computers**, which opens the Computers page filtered
  to that lab. The active-sessions table and the cards reflow with the window
  width.
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
* **Lab queue.** Policy selector, live waiting list in dispatch order (with
  *Remove selected request*), measured results and the what-if replay. See
  [What changed in 0.7.0](#what-changed-in-070-lab-queue-with-real-data).
* **Sessions and transactions.** Full session history, plus an admin
  "end selected session" action. The **transaction demonstration** tab runs
  the real service code, prints every SQL step with COMMIT or ROLLBACK, shows
  before/after state, and can inject a failure mid-transaction.

### Student portal

* Sign in with student ID and password, checked against the salted hash
  stored in `student`. Nothing else is reachable before sign-in.
* **Request a computer**: choose a purpose (sets the priority) and an expected
  length, then either start on a free computer (waiting 0) or join the waiting
  queue. While waiting, the page shows the student's live position under the
  current policy, the time waited so far and a *Leave queue* button.
* The dashboard shows name, ID, department, the current session (computer,
  location, start time, live elapsed time) and the table of AVAILABLE
  computers.
* **Start session on selected computer** writes the request and the session
  in one transaction, rechecking availability, the queue and the
  one-session rule inside it. **End session** runs the termination
  transaction, which also hands the computer to the next waiting student.
  Every change refreshes all screens from the database.

### Operating systems

* **CPU scheduling page**, organised in four steps:
  1. **Choose a workload.** Choosing a lab session loads that session's
     details (student, computer, lab, times, duration) and its saved processes
     from SQLite. The selector shows how many processes each workload has
     saved. If none are saved, the page says so and leaves the table empty. It
     never derives burst times from session duration.
  2. **Process details.** An editable table with a *Source* column (database
     row #, new, or example) and an unsaved-changes indicator, plus Save /
     Update saved workload / Revert to saved.
  3. **Algorithm and simulation.** FCFS, SJF, SRTF, Priority or Round Robin,
     each with a plain-language description. Time quantum is enabled for Round
     Robin only. Run a single algorithm or compare all five.
  4. **Gantt chart and results.** Average waiting, turnaround and response
     time; CPU utilisation; context switches; and per-process arrival, burst,
     priority, first run, completion, turnaround, waiting and response.
* **Process states page.** An introduction and a legend explaining NEW,
  READY, RUNNING, WAITING/BLOCKED and TERMINATED.
  * Controls: Create process, Admit to ready queue, Dispatch to CPU, Preempt /
    return to ready, Block for I/O, Complete I/O, Terminate process, and Reset
    simulation.
  * A not-allowed action is refused with a plain-language reason (for example
    "P1 is WAITING. It is still waiting for I/O…"), and the single-CPU rule is
    enforced.
  * The page shows the selected process's state, the last action with its
    previous and new state, CPU status, a live diagram highlighting the
    current state and legal moves, and the transition history.
  * **Create from last CPU scheduling run** seeds the simulator with that
    run's process IDs. A second tab replays the scheduler's own state trace
    with a time slider.

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
| **Business rules in the DB** | Partial unique indexes (one ACTIVE session per computer and per student; one WAITING request per student) and 13 triggers | `tools/validate_schema.py`, sections "Database-level guards" and "Lab queue" |
| **Multi-step atomicity** | Ending a session, releasing the computer and assigning it to the next waiting student happen in ONE transaction (`SessionService::endSession` → `QueueService::dispatchWithin`) | Validator: an injected failure in the hand-over leaves the old session ACTIVE and the queue unchanged |
| **Schema migration** | Schema v2 is additive (`CREATE … IF NOT EXISTS`); `PRAGMA user_version` 1 → 2 | Validator: a real version-1 database keeps its sessions and gains the new tables |

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
* **SRTF** (preemptive SJF): when a process arrives, the ready process with the
  shortest *remaining* time runs. The running process is preempted only by a
  strictly shorter one; equal remaining time causes no switch.
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
| SRTF           | P1 0–1, P2 1–4, P1 4–8, P4 8–14, P3 14–22    | 5.00   | 10.50   |
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
  scheduler.h/.cpp         FCFS, SJF, SRTF, Priority, RR (pure C++17, no Qt)
  labqueue.h/.cpp          lab queue policies, measured metrics, what-if replay (pure C++17)
  queueservice.*           requests, waiting queue, transactional dispatch
  processstate.h/.cpp      state machine + lifecycle simulator (pure C++17)
  mainwindow.h/.cpp        sidebar navigation, cross-page refresh
  ui/                      one class per page, plus theme (palette + stylesheet),
                           helpers (banners, empty/error states), layout helpers
                           (FlowLayout, ResponsiveGrid), Gantt chart and
                           state-diagram widgets
tests/
  scheduler_tests.cpp      208 checks, no Qt needed
  labqueue_tests.cpp       49 checks, no Qt needed
  database_tests.cpp       98 service-level integration checks (Qt Core + Sql)
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
| Scheduling engine + process-state model (incl. second-scale limits, RR slice guard) | `scheduler_tests`, g++ 13, `-Wall -Wextra -Wpedantic -Werror`, AddressSanitizer + UBSan | **208 / 208 passed** (author) |
| Lab queue logic: dispatch order, measured metrics, what-if replay, measured → workload conversion in minutes or seconds, with rounding-error measure | `labqueue_tests`, same flags | **49 / 49 passed** (author) |
| Test strength | Mutation checks: SRTF preemption disabled; SJF ordering broken | Both detected (26 and 4 failures) |
| Schema v2, constraints, triggers, all service SQL, queue hand-over atomicity, v1 → v2 migration | `python3 tools/validate_schema.py` (SQLite 3.45) | **91 / 91 passed** (author) |
| Source consistency | Script checks: members, service methods and helpers declared; methods defined; files in CMake | Passed (author) |
| Qt application 0.5.0 / `database_tests` 0.5.0 | Built and run on Windows | Built; **59 / 59** (user report) |
| Qt application 0.7.0 | Built and run on Windows, Qt 6.12 MinGW | Ran (user screenshots) |
| Qt application 0.7.1 | Built and run on Windows | Ran (user screenshots) |
| Qt application 0.7.3 (`labdesk`) | — | **Not compiled by the author** |
| `database_tests` 0.7.x | 59 original + 13 (0.6) + 26 (queue) | **Result not yet reported** |
| GUI behaviour 0.7.1 | Manual guide in `docs/TESTING.md` (M1–M15, V1–V11, Q1–Q10) | **Not performed yet** |

## Known limitations

* **No administrator authentication.** Anyone who can run the app can open
  the admin pages.
* **Password hashing** is salted SHA-256 iterated 20 000 times. That is far
  better than plaintext but weaker than a memory-hard KDF such as Argon2id or
  bcrypt.
* **Single process, single connection.** `BEGIN IMMEDIATE` and guarded
  updates are correct for concurrent writers, but no multi-client scenario has
  been tested. SQLite on a network share is not suitable for many clients.
* Priority scheduling is non-preemptive only, and there is no aging.
  Preemption is available through SRTF and Round Robin.
* "Laboratory" means a distinct `computer.location` value. Two spellings of
  the same room (e.g. "Lab A" and "Lab A ") would show as two laboratories.
  A dedicated laboratory table would be the proper fix (next milestone).
* The process-state simulator is educational. It does not observe real OS
  processes, and the scheduler trace contains no WAITING state because the
  simulated workloads are CPU-bound.
* No reports or exports and no session time limits. The queue serves one
  lab-wide pool: a student cannot wait for a particular laboratory.
* SJF and Priority can make long or practice requests wait a long time
  (starvation); there is no aging yet. The replay's "longest wait" column
  makes this visible.
* "Measured lab visits" on the CPU scheduling page assumes one computer, and
  arrivals more than 100 000 minutes (about 69 days) apart exceed the engine's
  input limit; run such data through the Lab queue replay instead.
* Queue positions refresh when data changes in this application. Several
  copies of the app sharing one database file would not notify each other.
* Version 0.6.0 of the GUI has not yet been seen running by the author. Check
  it at several window sizes and at 125 % / 150 % Windows display scaling.

---

## Remaining work (next 50%)

1. **Verify 0.6.0:** build, run both CTest targets, work through the manual
   guide in `docs/TESTING.md`, and record the results.
2. Administrator accounts and roles (an `admin` table and a login screen),
   plus an audit log of admin actions.
3. Preemptive priority, priority aging, and process I/O bursts so the
   scheduler trace includes WAITING.
4. Usage reports (hours per computer and per student, peak times) exported
   to CSV.
5. Session policies (maximum duration, automatic end), aging in the lab queue,
   and per-laboratory queues.
6. A multi-client concurrency test (two processes allocating the same
   computer) to demonstrate isolation under real contention.
7. Packaging: an installer via `windeployqt` or `macdeployqt`.
