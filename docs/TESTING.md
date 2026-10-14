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

## Status at hand-over (version 0.6.0)

| Suite | Result | Notes |
|---|---|---|
| `scheduler_tests` | **204 passed, 0 failed** (author) | g++ 13.3, `-Wall -Wextra -Wpedantic -Werror`, AddressSanitizer + UBSan. Includes SRTF, transition explanations and seeding from a schedule |
| `validate_schema.py` | **62 passed, 0 failed** (author) | SQLite 3.45.1; includes the new dashboard, lab-summary, session-lookup and workload-size queries |
| `database_tests` | **not yet run on 0.6.0** | 0.5.0 passed 59 / 59 on Windows (user report). 0.6.0 keeps those 59 checks unchanged and adds 13 |
| Manual GUI tests | **not yet run on 0.6.0** | Qt is not available in the authoring environment |

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
| 11 | FCFS, SJF, SRTF, Priority, RR verified | `scheduler_tests` (hand-computed cases, a published textbook SRTF example, random invariants, SRTF optimality) | M11, V9 |
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
| SRTF | 8 / 8 / 3 | 4 / 3 / 0 | 22 / 20 / 12 | 14 / 11 / 5 | 5.00 | 10.50 |
| RR q = 2 | 14 / 14 / 9 | 11 / 10 / 7 | 22 / 20 / 12 | 20 / 17 / 11 | 9.75 | 15.25 |

RR q = 2 timeline, with the ready queue shown after each slice:

```
P1 0-2  [P2 P3 P1]     P2 2-4  [P3 P1 P4 P2]   P3 4-6   P1 6-8   P4 8-10
P2 10-11 done          P3 11-13                P1 13-14 done     P4 14-16
P3 16-18               P4 18-20 done           P3 20-22 done
```

The timeline has 12 slices and 11 context switches.

SRTF trace for set A:

```
t0 P1 runs. t1 P2 arrives with burst 3 < P1's remaining 4: P1 is preempted.
P2 1-4 (P3 = 8 and P4 = 6 are not shorter). t4: P1 (4) is shortest: P1 4-8.
Then P4 8-14 and P3 14-22.
```

Response times are P1 0, P2 0, P3 12 and P4 5, giving an average of 4.25.

**SRTF, published example** (Silberschatz, *Operating System Concepts*):
P1 (0, 8), P2 (1, 4), P3 (2, 9), P4 (3, 5). The schedule is
`P1 0-1, P2 1-5, P4 5-10, P1 10-17, P3 17-26`, with an average waiting time of
6.5, matching the book.

**Edge cases also covered:**

* **Idle CPU:** P1 (2, 3), P2 (10, 2) gives `idle 0-2, P1 2-5, idle 5-10,
  P2 10-12` for all four algorithms. Idle time is 7 and utilisation is 5/12.
* **SJF must not run a process before it arrives:** P1 (3, 1), P2 (0, 10)
  gives P2 0-10, then P1 10-11.
* **Priority tie:** equal priority and equal arrival fall back to input order.
* **RR boundary:** P1 (0, 4) and P2 (2, 2) with q = 2. P2 arrives exactly as
  P1's quantum ends and runs first, so P2's CT is 4 and P1's CT is 6.
* **Lifecycle explanations:** every one of the 25 (from, to) state pairs has a
  plain-language explanation exactly when it is *not* a valid transition.
  Process IDs taken from a schedule are kept, and duplicate IDs are rejected.
* **Validation:** the engine rejects an empty list, negative arrival, zero
  burst, priority outside 0 to 99, duplicate or empty IDs, and quantum ≤ 0.
* **SRTF tie:** P1 (0, 4) and P2 (2, 2). At t = 2 both need 2 units, so P1
  is *not* preempted: P1 0-4, then P2 4-6.
* **SRTF optimality:** on every one of the 400 random workloads, SRTF's
  average waiting time is less than or equal to that of every other
  algorithm. This is a known theorem: shortest-remaining-processing-time
  minimises total waiting time on one CPU. It is an independent check that
  does not rely on hand calculation.
* **Random invariants:** 400 random workloads × 5 algorithms. Each check
  confirms:
  * The timeline is contiguous.
  * Each process receives exactly its burst.
  * No process starts before it arrives.
  * TAT and WT satisfy their formulas, and WT ≥ 0.
  * Non-preemptive algorithms (FCFS, SJF, Priority) give each process a single
    slice.
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

## Manual checks for version 0.6.0 (visual and workflow)

Error states (a red "Could not load this section from the database" box) are
shown only when a query fails. They are implemented in every section but have
no reliable manual trigger, so they are verified by code review, not by a step
here.

Run these on Windows in **both** light and dark Windows colour mode
(*Settings → Personalization → Colors*). Dark mode is where the old black
areas came from.

| Step | Action | Expected result |
|---|---|---|
| **V1** *(black areas)* | With Windows in dark mode, open every page and both tabs on Sessions and Process states. Open a combo-box list, a confirmation dialog and a tooltip. Scroll a long table. | No black or dark rectangles anywhere. Backgrounds are light grey (#F4F7FB) with white cards. Scrollbars, table headers, pop-ups, dialogs and tooltips are light with dark, readable text. |
| **V2** *(resizing)* | Resize the window from its minimum (1080 × 680) to full screen, and try 125 % / 150 % display scaling. | The dashboard figures and laboratory cards reflow into more or fewer equal-width columns. Cards never overlap or touch each other or the page edge. Computer tiles wrap inside their card with equal gaps. Long pages scroll vertically. |
| **V3** *(dashboard figures)* | Compare the dashboard with the Computers page (filter by status) and Sessions (active only). | Laboratories = number of distinct locations. Total, available, occupied and maintenance counts match the Computers filters. Active sessions match. Occupancy = occupied / total. Each lab card's counts add up to its total. |
| **V4** *(View computers)* | Press **View computers** on a laboratory card. | The Computers page opens with that laboratory in the search box and only its computers listed. |
| **V5** *(notifications)* | Trigger one of each kind: add a student (Success), add a process on CPU scheduling (Note), press Report issue with no description (Error), try an illegal lifecycle action (Warning). | Green, blue, red and amber panels respectively, each with dark readable text, a coloured left bar and the words "Success", "Note", "Error" or "Warning" in bold. |
| **V6** *(empty states)* | Fresh database with `--no-seed`. Visit Dashboard, Maintenance (tick "Open issues only"), the Student portal (after adding a student and signing in) and Sessions. | Messages such as "No laboratories yet…", "No active maintenance issues…", "No active computer session.", "No computers available for allocation…" and "No sessions recorded yet…" appear in dashed boxes. There are no blank areas. |
| **V7** *(session workload)* | CPU scheduling → choose a session that shows "no saved workload". Press **Use textbook example**, then **Save workload**. Choose another entry, then the first session again. | Session details (student, computer, lab, start, end, duration) appear from the database, with the note that duration is not CPU burst time. The empty case states that nothing was filled in. After saving, the selector shows "4 saved process(es)". Re-selecting loads the same IDs and values, the Source column shows "Database row #…", and the status reads "Loaded from the database…". |
| **V8** *(edit, revert, update)* | Edit a burst time in a loaded workload, then try to switch workload. Cancel, press **Revert to saved**, edit again and press **Update saved workload**. | The Source cell shows "edited" and the Warning "Unsaved changes" appears. Switching asks to discard. Revert restores the stored values. Update asks for confirmation, then reloads with new database row numbers. |
| **V9** *(algorithms)* | With the textbook example, run each of the five algorithms, then **Compare all five algorithms**. | The values match the tables above (SRTF: average WT 5.00, average TAT 10.50, average RT 4.25). The quantum field is enabled only for Round Robin, and the description changes with the algorithm. |
| **V10** *(lifecycle)* | Process states → Create process → Dispatch to CPU (refused) → Admit → Dispatch → Block for I/O → Dispatch (refused) → Complete I/O → Dispatch → Terminate. Create a second process and try to dispatch it while the first runs. | Refused actions show a Warning with the reason (must be admitted first; still waiting for I/O; CPU busy). Allowed actions update the current state, the last action (previous and new state), the CPU indicator, the history and the highlighted diagram node. |
| **V11** *(scheduler to lifecycle)* | Run any algorithm on CPU scheduling, then on Process states press **Create from last CPU scheduling run**. | The scheduler's process IDs appear in the NEW state. The trace tab shows the scheduler's own state changes with the time slider. |

## Results log

Fill this in when you run the tests. Leave cells empty rather than guessing.

| Date | Tester | Qt / compiler / OS | Suite or step | Result | Notes / defects found |
|---|---|---|---|---|---|
| | | | `cmake --build` | | |
| | | | `scheduler_tests` | | |
| | | | `database_tests` | | |
| | | | `validate_schema.py` | | |
| | | | M1 – M15 | | |
| | | | V1 – V11 (0.6.0) | | |

## Writing up failures

For each defect, record four things:

* What you did.
* What you expected.
* What happened, including the exact error text, which every service returns.
* The root cause once you find it.

If a test fails, fix the cause rather than the assertion, unless the
expected value itself was wrong. In that case, show the corrected hand
calculation.
