# LabDesk testing guide

There are three layers of tests:

| Layer | Command | Needs |
|---|---|---|
| Scheduling engine + process-state model | `ctest --test-dir build -R scheduler_tests --output-on-failure` (or run `build/scheduler_tests`) | C++17 compiler only |
| Database services through Qt SQL | `ctest --test-dir build -R database_tests --output-on-failure` | Qt 6 Core + Sql |
| Schema and SQL sequences, independent of Qt | `python3 tools/validate_schema.py` | Python 3 (stdlib `sqlite3`) |
| GUI workflows | Manual steps below | Built application |

`scheduler_tests` can be built without CMake or Qt:

```bash
g++ -std=c++17 -Wall -Wextra -Isrc src/scheduler.cpp src/processstate.cpp tests/scheduler_tests.cpp -o scheduler_tests
./scheduler_tests
```

## Status at hand-over

| Suite | Result | Notes |
|---|---|---|
| `scheduler_tests` | **138 passed, 0 failed** | g++ 13.3, `-Wall -Wextra -Wpedantic -Werror`, AddressSanitizer + UBSan |
| `validate_schema.py` | **53 passed, 0 failed** | SQLite 3.45.1 |
| `database_tests` | **not yet run** | Requires Qt 6; not available in the authoring environment |
| Manual GUI tests | **not yet run** | Application not yet compiled |

Do not mark a test as passed until you have run it yourself. Record your
results in the [results log](#results-log) at the end of this file.

## Coverage of the required tests

| # | Requirement | Automated coverage | Manual step |
|---|---|---|---|
| 1 | Create student, verify in DB | `database_tests` TEST 1; validator | M1 |
| 2 | Create computer, initial AVAILABLE | `database_tests` TEST 2; validator | M2 |
| 3 | Valid login | `database_tests` TEST 3/4 | M3 |
| 4 | Invalid login rejected | `database_tests` TEST 3/4 | M4 |
| 5 | Allocate: one active session, OCCUPIED, correct student | `database_tests` TEST 5; validator | M5 |
| 6 | Allocate occupied computer fails without corruption | `database_tests` TEST 6; validator | M6 |
| 7 | Second computer for an active student rejected | `database_tests` TEST 7; validator | M7 |
| 8 | End session: ENDED, end time, AVAILABLE, history kept | `database_tests` TEST 8; validator | M8 |
| 9 | Delete computer with history handled | `database_tests` TEST 9 (service and raw `DELETE`); validator | M9 |
| 10 | Maintenance changes status correctly | `database_tests` TEST 10; validator | M10 |
| 11 | FCFS, SJF, Priority, RR verified | `scheduler_tests` (hand-computed cases + random invariants) | M11 |
| 12 | Failure mid-transaction rolls back | `database_tests` TEST 12 (`FaultInjector`); validator | M12 |
| 13 | Data persists after restart | `database_tests` TEST 13 (close + reopen); validator | M13 |
| 14 | Admin and student views agree | `database_tests` TEST 14 (`dashboardStats` vs `activeSessionForStudent`) | M14 |

## Scheduler test cases (calculated by hand)

The derivations are also written as comments in `tests/scheduler_tests.cpp`,
so they can be checked without running anything.

**Set A** (AT, BT, PR): P1 (0, 5, 2), P2 (1, 3, 3), P3 (2, 8, 1), P4 (3, 6, 4)

| Algorithm | P1 CT/TAT/WT | P2 CT/TAT/WT | P3 CT/TAT/WT | P4 CT/TAT/WT | Avg WT | Avg TAT |
|---|---|---|---|---|---|---|
| FCFS | 5 / 5 / 0 | 8 / 7 / 4 | 16 / 14 / 6 | 22 / 19 / 13 | 5.75 | 11.25 |
| SJF | 5 / 5 / 0 | 8 / 7 / 4 | 22 / 20 / 12 | 14 / 11 / 5 | 5.25 | 10.75 |
| Priority | 5 / 5 / 0 | 16 / 15 / 12 | 13 / 11 / 3 | 22 / 19 / 13 | 7.00 | 12.50 |
| RR q = 2 | 14 / 14 / 9 | 11 / 10 / 7 | 22 / 20 / 12 | 20 / 17 / 11 | 9.75 | 15.25 |

RR q = 2 timeline, with the ready queue shown after each slice:

```
P1 0-2  [P2 P3 P1]     P2 2-4  [P3 P1 P4 P2]   P3 4-6   P1 6-8   P4 8-10
P2 10-11 done          P3 11-13                P1 13-14 done     P4 14-16
P3 16-18               P4 18-20 done           P3 20-22 done
```

The timeline has 12 slices and 11 context switches.

**Edge cases also covered:**

* **Idle CPU:** P1 (2, 3), P2 (10, 2) gives `idle 0-2, P1 2-5, idle 5-10,
  P2 10-12` for all four algorithms. Idle time is 7 and utilisation is 5/12.
* **SJF must not run a process before it arrives:** P1 (3, 1), P2 (0, 10)
  gives P2 0-10, then P1 10-11.
* **Priority tie:** equal priority and equal arrival fall back to input order.
* **RR boundary:** P1 (0, 4) and P2 (2, 2) with q = 2. P2 arrives exactly as
  P1's quantum ends and runs first, so P2's CT is 4 and P1's CT is 6.
* **Validation:** the engine rejects an empty list, negative arrival, zero
  burst, priority outside 0 to 99, duplicate or empty IDs, and quantum ≤ 0.
* **Random invariants:** 400 random workloads × 4 algorithms. Each check
  confirms:
  * The timeline is contiguous.
  * Each process receives exactly its burst.
  * No process starts before it arrives.
  * TAT and WT satisfy their formulas, and WT ≥ 0.
  * Non-preemptive algorithms give each process a single slice.
  * Each process's state trace is a valid path from NEW to TERMINATED.

## Manual GUI test guide

Start each run with a fresh database so the results are reproducible:

```
labdesk --db test-run.db
```

| Step | Action | Expected result |
|---|---|---|
| **M1** | Students → fill ID `T001`, name, email `t001@uni.edu`, department, password `secret1` → **Add student**. Then try adding `T002` with email `T001@UNI.EDU`. | T001 appears in the table and the dashboard student count rises by 1. The second add is refused: email already registered. |
| **M2** | Computers → ID `LAB-T-01`, name, location → **Add computer**. | The row shows status AVAILABLE (green) and the dashboard total rises by 1. |
| **M3** | Student portal → `DEMO01` / `demo123` → **Sign in**. | The dashboard shows the name, ID and department, and "You are not using a computer". |
| **M4** | Sign out. Try `DEMO01` / `wrong`, then `NOPE` / `demo123`. | Both show "Invalid student ID or password." The dashboard stays hidden. |
| **M5** | Sign in as DEMO01, select `LAB-A-01` → **Use selected computer**. | Success message. The session card shows LAB-A-01 and the elapsed timer runs. LAB-A-01 leaves the available list. Dashboard: occupied +1, active sessions +1. Sessions history shows an ACTIVE row for DEMO01 on LAB-A-01. |
| **M6** | Sessions and transactions → Transaction demonstration → student DEMO02, computer LAB-A-01 → **Run allocation transaction**. | The log stops at "computer is OCCUPIED, not AVAILABLE" followed by ROLLBACK. "after" equals "before". Session count unchanged. |
| **M7** | In the demo tab: student DEMO01, computer LAB-A-02 → run allocation. In the portal (still DEMO01), check the **Use selected computer** button. | ROLLBACK with "already has an active session on LAB-A-01". LAB-A-02 stays AVAILABLE. In the portal the button is disabled, with a tooltip explaining why. |
| **M8** | Portal (DEMO01) → **End session** → confirm. | Message "Session ended". Sessions history: status ENDED, end time and duration filled. LAB-A-01 is AVAILABLE again and the row remains in history. |
| **M9** | Computers → select LAB-A-01 → **Delete computer** → confirm. Then add `LAB-T-99` and delete it. | First delete refused: "has 1 session and 0 maintenance record(s)…". LAB-T-99 is deleted. |
| **M10** | Maintenance → choose LAB-A-03, describe the problem → **Report issue**. Report a second issue for LAB-A-03. Resolve one, then the other. | After the first report LAB-A-03 is MAINTENANCE. After resolving one issue: "stays in MAINTENANCE: 1 other open issue". After resolving both: "LAB-A-03 is AVAILABLE again". Also try to report an issue for a computer in use: it is greyed out in the list. |
| **M11** | CPU scheduling → **Load textbook example** → run each algorithm (RR with quantum 2), then **Compare all four**. | Values match the hand-calculated table above, and the Gantt chart matches the listed order. Invalid input is refused with a row-specific message, for example a duplicate ID. |
| **M12** | Transaction demonstration → DEMO02 + LAB-A-05, tick **Inject a failure** → run allocation. | The log shows the `UPDATE computer ... 'OCCUPIED'` step succeeded, then the INSERT failed with "Injected failure…", then ROLLBACK. "after" shows LAB-A-05 = AVAILABLE and no active session. Untick the box and run again: COMMIT. |
| **M13** | Leave DEMO02's session active. Close the app and start it again with the same `--db`. | The session is still ACTIVE, LAB-A-05 is OCCUPIED, and every count matches the values before closing. |
| **M14** | With one session active, compare the Dashboard, Computers ("In use by"), Sessions (active only) and the portal for that student. Press **Reload from database**. | All four show the same computer and student. The dashboard consistency check is green. |
| **M15** *(process states)* | Process states → create P1 and P2 → admit both → dispatch P1 → try to dispatch P2 → block P1 → dispatch P2 → complete P1's I/O. | Dispatching P2 while P1 runs is refused ("CPU is busy"). The diagram highlights the current state and the legal next states. The log records every transition. |

## Results log

Fill this in when you run the tests. Leave cells empty rather than guessing.

| Date | Tester | Qt / compiler / OS | Suite or step | Result | Notes / defects found |
|---|---|---|---|---|---|
| | | | `cmake --build` | | |
| | | | `scheduler_tests` | | |
| | | | `database_tests` | | |
| | | | `validate_schema.py` | | |
| | | | M1 – M15 | | |

## Writing up failures

For each defect, record four things:

* What you did.
* What you expected.
* What happened, including the exact error text, which every service returns.
* The root cause once you find it.

If a test fails, fix the cause rather than the assertion, unless the
expected value itself was wrong. In that case, show the corrected hand
calculation.
