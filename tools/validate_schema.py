#!/usr/bin/env python3
"""
validate_schema.py - Qt-independent validation of the LabDesk SQL layer.

It loads resources/sql/schema.sql exactly as the C++ loader does (split on
'-- @@' lines) and replays the same SQL statement sequences that
SessionService / MaintenanceService / ComputerService execute, including
BEGIN IMMEDIATE / COMMIT / ROLLBACK.

What this proves:   the schema, constraints, triggers, partial unique indexes
                    and the transactional SQL sequences behave correctly.
What it does NOT prove: that the C++/Qt code compiles or calls them correctly.
That is covered by tests/database_tests.cpp (requires Qt 6).

Usage:  python3 tools/validate_schema.py
"""
import os
import sqlite3
import sys
import tempfile
from datetime import datetime, timezone, timedelta

HERE = os.path.dirname(os.path.abspath(__file__))
SCHEMA = os.path.join(HERE, "..", "resources", "sql", "schema.sql")

V1_STATEMENTS = 20  # number of statements in schema version 1 (LabDesk 0.5.0 / 0.6.0)

passed = 0
failed = 0


def check(cond, name):
    global passed, failed
    if cond:
        passed += 1
        print(f"  PASS  {name}")
    else:
        failed += 1
        print(f"  FAIL  {name}")


def ts(offset_s=0):
    t = datetime.now(timezone.utc) + timedelta(seconds=offset_s)
    return t.strftime("%Y-%m-%dT%H:%M:%S.") + f"{t.microsecond // 1000:03d}Z"


def schema_statements(path=None):
    with open(path or SCHEMA, encoding="utf-8") as f:
        text = f.read()
    chunks, cur = [], []
    for line in text.splitlines():
        if line.strip() == "-- @@":
            chunks.append("\n".join(cur))
            cur = []
        else:
            cur.append(line)
    chunks.append("\n".join(cur))
    out = []
    for c in chunks:
        body = "\n".join(l for l in c.splitlines() if not l.strip().startswith("--")).strip()
        if body:
            out.append(body)
    return out


def connect(path):
    con = sqlite3.connect(path, isolation_level=None)  # we issue BEGIN/COMMIT ourselves
    con.execute("PRAGMA foreign_keys = ON")
    assert con.execute("PRAGMA foreign_keys").fetchone()[0] == 1
    return con


def apply_schema(con, path=None, version=2):
    con.execute("BEGIN IMMEDIATE")
    statements = schema_statements(path)
    if version == 1:
        statements = statements[:V1_STATEMENTS]  # v2 only appended statements, so v1 is the prefix
    for s in statements:
        con.execute(s)
    con.execute(f"PRAGMA user_version = {version}")
    con.execute("COMMIT")


def raises(con, sql, params=(), contains=None):
    try:
        con.execute(sql, params)
    except sqlite3.DatabaseError as e:
        return contains is None or contains in str(e)
    return False


# --- mirrors SessionService::allocateComputer ---------------------------------
def allocate(con, student_id, computer_id):
    con.execute("BEGIN IMMEDIATE")
    try:
        if con.execute("SELECT name FROM student WHERE student_id = ?", (student_id,)).fetchone() is None:
            raise RuntimeError("student does not exist")
        if con.execute("SELECT session_id FROM session WHERE student_id = ? AND status = 'ACTIVE'",
                       (student_id,)).fetchone():
            raise RuntimeError("student already has an active session")
        row = con.execute("SELECT status FROM computer WHERE computer_id = ?", (computer_id,)).fetchone()
        if row is None:
            raise RuntimeError("computer does not exist")
        if row[0] != "AVAILABLE":
            raise RuntimeError(f"computer is {row[0]}")
        cur = con.execute("UPDATE computer SET status = 'OCCUPIED' "
                          "WHERE computer_id = ? AND status = 'AVAILABLE'", (computer_id,))
        if cur.rowcount != 1:
            raise RuntimeError("status changed concurrently")
        cur = con.execute("INSERT INTO session (student_id, computer_id, start_time, status) "
                          "VALUES (?, ?, ?, 'ACTIVE')", (student_id, computer_id, ts()))
        sid = cur.lastrowid
        con.execute("COMMIT")
        return True, sid
    except (RuntimeError, sqlite3.DatabaseError) as e:
        con.execute("ROLLBACK")
        return False, str(e)


# --- mirrors SessionService::endActiveSession -----------------------------------
def end_session(con, student_id):
    con.execute("BEGIN IMMEDIATE")
    try:
        row = con.execute("SELECT session_id, computer_id FROM session "
                          "WHERE student_id = ? AND status = 'ACTIVE'", (student_id,)).fetchone()
        if row is None:
            raise RuntimeError("no active session")
        sid, cid = row
        cur = con.execute("UPDATE session SET end_time = ?, status = 'ENDED' "
                          "WHERE session_id = ? AND status = 'ACTIVE'", (ts(1), sid))
        if cur.rowcount != 1:
            raise RuntimeError("session not updated")
        cur = con.execute("UPDATE computer SET status = 'AVAILABLE' "
                          "WHERE computer_id = ? AND status = 'OCCUPIED'", (cid,))
        if cur.rowcount != 1:
            raise RuntimeError("computer not released")
        con.execute("COMMIT")
        return True, sid
    except (RuntimeError, sqlite3.DatabaseError) as e:
        con.execute("ROLLBACK")
        return False, str(e)


# --- mirrors MaintenanceService::reportIssue / resolveIssue ----------------------
def report(con, computer_id, desc):
    con.execute("BEGIN IMMEDIATE")
    try:
        row = con.execute("SELECT status FROM computer WHERE computer_id = ?", (computer_id,)).fetchone()
        if row is None:
            raise RuntimeError("no computer")
        if row[0] == "OCCUPIED":
            raise RuntimeError("occupied")
        con.execute("INSERT INTO maintenance (computer_id, description, reported_at, status) "
                    "VALUES (?, ?, ?, 'OPEN')", (computer_id, desc, ts()))
        if row[0] == "AVAILABLE":
            con.execute("UPDATE computer SET status = 'MAINTENANCE' WHERE computer_id = ? "
                        "AND status = 'AVAILABLE'", (computer_id,))
        con.execute("COMMIT")
        return True, None
    except (RuntimeError, sqlite3.DatabaseError) as e:
        con.execute("ROLLBACK")
        return False, str(e)


def resolve(con, mid):
    con.execute("BEGIN IMMEDIATE")
    try:
        row = con.execute("SELECT computer_id, status FROM maintenance WHERE maintenance_id = ?",
                          (mid,)).fetchone()
        if row is None or row[1] != "OPEN":
            raise RuntimeError("not open")
        cid = row[0]
        con.execute("UPDATE maintenance SET status = 'RESOLVED', resolved_at = ? "
                    "WHERE maintenance_id = ? AND status = 'OPEN'", (ts(1), mid))
        open_left = con.execute("SELECT COUNT(*) FROM maintenance WHERE computer_id = ? AND status = 'OPEN'",
                                (cid,)).fetchone()[0]
        active = con.execute("SELECT COUNT(*) FROM session WHERE computer_id = ? AND status = 'ACTIVE'",
                             (cid,)).fetchone()[0]
        released = False
        if open_left == 0 and active == 0:
            cur = con.execute("UPDATE computer SET status = 'AVAILABLE' WHERE computer_id = ? "
                              "AND status = 'MAINTENANCE'", (cid,))
            released = cur.rowcount == 1
        con.execute("COMMIT")
        return True, released
    except (RuntimeError, sqlite3.DatabaseError) as e:
        con.execute("ROLLBACK")
        return False, str(e)


def status_of(con, cid):
    return con.execute("SELECT status FROM computer WHERE computer_id = ?", (cid,)).fetchone()[0]


def count(con, sql, params=()):
    return con.execute(sql, params).fetchone()[0]

# --- mirrors QueueService (queueservice.cpp) ------------------------------------
PRIORITY = {"EXAM": 1, "ASSIGNMENT": 2, "PRACTICE": 3}


def policy(con):
    row = con.execute("SELECT value FROM lab_setting WHERE setting_key = 'queue_policy'").fetchone()
    return row[0] if row else "FCFS"


def order_key(pol, r):  # r = (request_id, student_id, purpose, priority, expected, requested_at)
    if pol == "SJF":
        return (r[4], r[5], r[0])
    if pol == "PRIORITY":
        return (r[3], r[5], r[0])
    return (r[5], r[0])


def dispatch_within(con):
    pol = policy(con)
    assigned = []
    while True:
        free = con.execute("SELECT computer_id FROM computer WHERE status = 'AVAILABLE' "
                           "ORDER BY computer_id LIMIT 1").fetchone()
        if not free:
            return assigned
        waiting = con.execute(
            "SELECT r.request_id, r.student_id, r.purpose, r.priority, r.expected_minutes, r.requested_at "
            "FROM lab_request r JOIN student st ON st.student_id = r.student_id WHERE r.status = 'WAITING' "
            "AND NOT EXISTS (SELECT 1 FROM session s WHERE s.student_id = r.student_id AND s.status = 'ACTIVE') "
            "ORDER BY r.request_id").fetchall()
        if not waiting:
            return assigned
        nxt = min(waiting, key=lambda r: order_key(pol, r))
        now = ts()
        cur = con.execute("UPDATE computer SET status = 'OCCUPIED' WHERE computer_id = ? AND status = 'AVAILABLE'",
                          (free[0],))
        assert cur.rowcount == 1
        sid = con.execute("INSERT INTO session (student_id, computer_id, start_time, status) VALUES (?, ?, ?, 'ACTIVE')",
                          (nxt[1], free[0], now)).lastrowid
        cur = con.execute("UPDATE lab_request SET status = 'ALLOCATED', session_id = ?, allocated_at = ?, granted_by = ? "
                          "WHERE request_id = ? AND status = 'WAITING'", (sid, now, pol, nxt[0]))
        assert cur.rowcount == 1
        assigned.append((nxt[1], free[0]))


def join_queue(con, student, purpose, minutes):
    con.execute("BEGIN IMMEDIATE")
    try:
        row = con.execute(
            "SELECT st.name, (SELECT computer_id FROM session WHERE student_id = st.student_id AND status = 'ACTIVE'),"
            " (SELECT COUNT(*) FROM lab_request WHERE student_id = st.student_id AND status = 'WAITING') "
            "FROM student st WHERE st.student_id = ?", (student,)).fetchone()
        if row is None or row[1] is not None or row[2] > 0:
            raise RuntimeError("not allowed")
        waiting = con.execute("SELECT COUNT(*) FROM lab_request WHERE status = 'WAITING'").fetchone()[0]
        now = ts()
        if waiting == 0:
            free = con.execute("SELECT computer_id FROM computer WHERE status = 'AVAILABLE' "
                               "ORDER BY computer_id LIMIT 1").fetchone()
            if free:
                con.execute("UPDATE computer SET status = 'OCCUPIED' WHERE computer_id = ? AND status = 'AVAILABLE'",
                            (free[0],))
                sid = con.execute("INSERT INTO session (student_id, computer_id, start_time, status) "
                                  "VALUES (?, ?, ?, 'ACTIVE')", (student, free[0], now)).lastrowid
                con.execute("INSERT INTO lab_request (student_id, purpose, priority, expected_minutes, requested_at,"
                            " status, session_id, allocated_at, granted_by) "
                            "VALUES (?, ?, ?, ?, ?, 'ALLOCATED', ?, ?, 'IMMEDIATE')",
                            (student, purpose, PRIORITY[purpose], minutes, now, sid, now))
                con.execute("COMMIT")
                return "IMMEDIATE", free[0]
        con.execute("INSERT INTO lab_request (student_id, purpose, priority, expected_minutes, requested_at, status) "
                    "VALUES (?, ?, ?, ?, ?, 'WAITING')", (student, purpose, PRIORITY[purpose], minutes, now))
        con.execute("COMMIT")
        return "WAITING", None
    except (RuntimeError, sqlite3.DatabaseError) as e:
        con.execute("ROLLBACK")
        return "REFUSED", str(e)


def end_session_with_dispatch(con, student_id):
    """endSession + dispatchWithin in ONE transaction, as SessionService does."""
    con.execute("BEGIN IMMEDIATE")
    try:
        sid, cid = con.execute("SELECT session_id, computer_id FROM session WHERE student_id = ? AND status = 'ACTIVE'",
                               (student_id,)).fetchone()
        con.execute("UPDATE session SET end_time = ?, status = 'ENDED' WHERE session_id = ? AND status = 'ACTIVE'",
                    (ts(1), sid))
        con.execute("UPDATE computer SET status = 'AVAILABLE' WHERE computer_id = ? AND status = 'OCCUPIED'", (cid,))
        assigned = dispatch_within(con)
        con.execute("COMMIT")
        return True, assigned
    except (TypeError, sqlite3.DatabaseError, AssertionError) as e:
        con.execute("ROLLBACK")
        return False, str(e)


def queue_section(con):
    print("Lab queue (schema v2): real arrival, waiting and burst data")
    check(policy(con) == "FCFS", "default queue policy is FCFS")
    check(raises(con, "UPDATE lab_setting SET value = 'RR' WHERE setting_key = 'queue_policy'", contains="CHECK"),
          "unknown queue policy rejected")
    for sid in ("Q1", "Q2", "Q3", "Q4"):
        con.execute("INSERT INTO student VALUES (?, ?, ?, 'h', 'CSE')", (sid, "Queue " + sid, sid.lower() + "@uni.edu"))
    # make exactly one computer free
    free_now = [r[0] for r in con.execute("SELECT computer_id FROM computer WHERE status='AVAILABLE' ORDER BY 1")]
    for extra in free_now[1:]:
        con.execute("UPDATE computer SET status = 'MAINTENANCE' WHERE computer_id = ?", (extra,))
        con.execute("INSERT INTO maintenance (computer_id, description, reported_at) VALUES (?, 'queue test', ?)",
                    (extra, ts()))
    kind, pc = join_queue(con, "Q1", "PRACTICE", 90)
    check(kind == "IMMEDIATE", "a free computer is granted at once (waiting 0)")
    r = con.execute("SELECT requested_at, allocated_at, granted_by FROM lab_request WHERE student_id = 'Q1'").fetchone()
    check(r[0] == r[1] and r[2] == "IMMEDIATE", "immediate grant: arrival = start, granted_by IMMEDIATE")
    check(join_queue(con, "Q2", "PRACTICE", 60)[0] == "WAITING", "no free computer: request WAITS")
    check(join_queue(con, "Q3", "ASSIGNMENT", 20)[0] == "WAITING", "second waiting request")
    check(join_queue(con, "Q4", "EXAM", 45)[0] == "WAITING", "third waiting request")
    check(join_queue(con, "Q2", "EXAM", 10)[0] == "REFUSED", "a student cannot wait twice")
    check(raises(con, "INSERT INTO lab_request (student_id, purpose, priority, expected_minutes, requested_at) "
                      "VALUES ('Q2', 'EXAM', 1, 10, ?)", (ts(),), contains="UNIQUE"),
          "DB: partial unique index allows one WAITING request per student")
    check(raises(con, "INSERT INTO lab_request (student_id, purpose, priority, expected_minutes, requested_at) "
                      "VALUES ('Q1', 'EXAM', 1, 10, ?)", (ts(),), contains="active session"),
          "DB trigger: student in a session cannot join the queue")
    check(raises(con, "INSERT INTO lab_request (student_id, purpose, priority, expected_minutes, requested_at) "
                      "VALUES ('S1', 'EXAM', 3, 10, ?)", (ts(),), contains="CHECK"),
          "DB: priority must match purpose")
    check(raises(con, "INSERT INTO lab_request (student_id, purpose, priority, expected_minutes, requested_at) "
                      "VALUES ('S1', 'EXAM', 1, 2, ?)", (ts(),), contains="CHECK"),
          "DB: expected length below 5 minutes rejected")

    # SJF picks Q3 (20 min) although Q2 arrived first; dispatch happens inside end-session.
    con.execute("UPDATE lab_setting SET value = 'SJF' WHERE setting_key = 'queue_policy'")
    ok, assigned = end_session_with_dispatch(con, "Q1")
    check(ok and assigned == [("Q3", pc)], f"SJF: released computer goes to Q3 in the same transaction ({assigned})")
    g = con.execute("SELECT granted_by, allocated_at >= requested_at FROM lab_request WHERE student_id='Q3'").fetchone()
    check(g == ("SJF", 1), "granted_by records the policy; allocated_at >= requested_at")

    # PRIORITY: Q4 (exam) beats Q2 (practice, earlier)
    con.execute("UPDATE lab_setting SET value = 'PRIORITY' WHERE setting_key = 'queue_policy'")
    # atomicity: injected failure in the dispatch INSERT rolls back the whole end-session
    con.execute("CREATE TEMP TRIGGER labdesk_injected_failure BEFORE INSERT ON main.session "
                "BEGIN SELECT RAISE(ABORT, 'Injected failure'); END")
    ok, msg = end_session_with_dispatch(con, "Q3")
    con.execute("DROP TRIGGER temp.labdesk_injected_failure")
    check(not ok, "dispatch failure aborts the end-session transaction")
    check(con.execute("SELECT status FROM session WHERE student_id='Q3'").fetchone()[0] == "ACTIVE",
          "ROLLBACK: Q3's session is still ACTIVE, nothing half-done")
    check(count(con, "SELECT COUNT(*) FROM lab_request WHERE status='WAITING'") == 2, "ROLLBACK: queue unchanged")
    ok, assigned = end_session_with_dispatch(con, "Q3")
    check(ok and assigned and assigned[0][0] == "Q4", "PRIORITY: exam request Q4 served before practice Q2")

    # measured burst / waiting come from timestamps, never typed in
    row = con.execute("SELECT r.requested_at, s.start_time, s.end_time FROM lab_request r "
                      "JOIN session s ON s.session_id = r.session_id WHERE r.student_id = 'Q3'").fetchone()
    check(row[0] <= row[1] <= row[2], "Q3: requested <= started <= ended (waiting and burst measurable)")

    # cancel, history immutability
    con.execute("UPDATE lab_request SET status = 'CANCELLED', cancelled_at = ? WHERE student_id = 'Q2' "
                "AND status = 'WAITING'", (ts(1),))
    check(count(con, "SELECT COUNT(*) FROM lab_request WHERE status='WAITING'") == 0, "Q2 left the queue")
    check(raises(con, "UPDATE lab_request SET status = 'WAITING', cancelled_at = NULL WHERE student_id = 'Q2'",
                 contains="history"), "cancelled request cannot be reopened")
    check(raises(con, "DELETE FROM lab_request", contains="cannot be deleted"), "request history cannot be deleted")
    check(raises(con, "INSERT INTO lab_request (student_id, purpose, priority, expected_minutes, requested_at, status,"
                      " session_id, allocated_at, granted_by) VALUES ('Q2', 'EXAM', 1, 10, ?, 'ALLOCATED',"
                      " (SELECT session_id FROM session WHERE student_id = 'Q4' AND status = 'ACTIVE'), ?, 'FCFS')",
                 (ts(), ts(1)), contains="same student"), "DB trigger: a request cannot point at another student's session")
    end_session_with_dispatch(con, "Q4")
    for (mid, cid) in con.execute("SELECT maintenance_id, computer_id FROM maintenance WHERE description='queue test'"):
        resolve(con, mid)


def migration_section():
    print("Migration: open a version-1 database with the version-2 schema")
    path = os.path.join(tempfile.mkdtemp(), "v1.db")
    con = connect(path)
    apply_schema(con, version=1)
    v1_tables = {r[0] for r in con.execute("SELECT name FROM sqlite_master WHERE type='table'")}
    check("lab_request" not in v1_tables, "starting point really is a version-1 database")
    con.execute("INSERT INTO student VALUES ('OLD1','Old','old@uni.edu','h','CSE')")
    con.execute("INSERT INTO computer (computer_id, computer_name, location) VALUES ('OLDPC','Old PC','Lab Z')")
    allocate(con, "OLD1", "OLDPC")
    end_session(con, "OLD1")
    con.close()
    con = connect(path)
    apply_schema(con)  # what Database::open() does on start-up
    tables = {r[0] for r in con.execute("SELECT name FROM sqlite_master WHERE type='table'")}
    check({"lab_request", "lab_setting"} <= tables, "v1 database gains lab_request and lab_setting")
    check(count(con, "SELECT COUNT(*) FROM session WHERE student_id='OLD1' AND status='ENDED'") == 1,
          "existing sessions preserved by the migration")
    check(count(con, "PRAGMA user_version") == 2, "user_version upgraded to 2")
    check(policy(con) == "FCFS", "default policy row created")
    apply_schema(con)
    check(count(con, "SELECT COUNT(*) FROM lab_setting") == 1, "re-applying the schema does not duplicate settings")
    con.close()


def main():
    tmp = tempfile.mkdtemp()
    path = os.path.join(tmp, "labdesk_validate.db")
    con = connect(path)

    print("Schema")
    apply_schema(con)
    check(len(schema_statements()) == 30, "schema file splits into 30 statements (v2: +lab_setting, +lab_request, 2 indexes, 5 triggers)")
    apply_schema(con)  # idempotent (IF NOT EXISTS)
    check(True, "schema can be applied twice (idempotent)")
    tables = {r[0] for r in con.execute("SELECT name FROM sqlite_master WHERE type='table'")}
    check({"student", "computer", "session", "maintenance", "process"} <= tables, "all five tables exist")

    print("TEST 1/2 - inserts and defaults")
    con.execute("INSERT INTO student VALUES ('S1','Asha','asha@uni.edu','h','CSE')")
    con.execute("INSERT INTO student VALUES ('S2','Ravi','ravi@uni.edu','h','ECE')")
    con.execute("INSERT INTO computer (computer_id, computer_name, location) VALUES ('PC1','Dell 1','Lab A')")
    con.execute("INSERT INTO computer (computer_id, computer_name, location) VALUES ('PC2','Dell 2','Lab A')")
    con.execute("INSERT INTO computer (computer_id, computer_name, location) VALUES ('PC3','Dell 3','Lab B')")
    check(count(con, "SELECT COUNT(*) FROM student WHERE student_id='S1'") == 1, "student persisted")
    check(status_of(con, "PC1") == "AVAILABLE", "new computer defaults to AVAILABLE")

    print("Constraints")
    check(raises(con, "INSERT INTO student VALUES ('S1','X','x@uni.edu','h','CSE')", contains="UNIQUE"),
          "duplicate student_id rejected")
    check(raises(con, "INSERT INTO student VALUES ('S9','X','ASHA@UNI.EDU','h','CSE')", contains="UNIQUE"),
          "duplicate email rejected case-insensitively")
    check(raises(con, "INSERT INTO student VALUES ('S9','X','not-an-email','h','CSE')", contains="CHECK"),
          "malformed email rejected by CHECK")
    check(raises(con, "INSERT INTO student VALUES (NULL,'X','n@uni.edu','h','CSE')"),
          "NULL text primary key rejected (SQLite quirk guarded by NOT NULL)")
    check(raises(con, "INSERT INTO computer VALUES ('PC9','n','l','BROKEN')", contains="CHECK"),
          "invalid computer status rejected")
    check(raises(con, "INSERT INTO process (label, arrival_time, burst_time, priority) VALUES ('P',0,0,1)",
                 contains="CHECK"), "burst_time 0 rejected")
    check(raises(con, "INSERT INTO process (label, arrival_time, burst_time, priority) VALUES ('P',-1,3,1)",
                 contains="CHECK"), "negative arrival rejected")
    check(raises(con, "INSERT INTO process (label, arrival_time, burst_time, priority) VALUES ('P',0,3,100)",
                 contains="CHECK"), "priority outside 0..99 rejected")
    check(raises(con, "INSERT INTO session (student_id, computer_id, start_time, status) "
                      "VALUES ('S1','PC1','13/10/2026 10:00','ENDED')", contains="CHECK"),
          "non-ISO timestamp rejected")

    print("TEST 5 - allocation commits atomically")
    ok, sid = allocate(con, "S1", "PC1")
    check(ok, "allocate S1 -> PC1 succeeds")
    check(status_of(con, "PC1") == "OCCUPIED", "PC1 is OCCUPIED")
    check(count(con, "SELECT COUNT(*) FROM session WHERE status='ACTIVE'") == 1, "exactly one active session")
    check(con.execute("SELECT student_id FROM session WHERE session_id=?", (sid,)).fetchone()[0] == "S1",
          "session belongs to S1")

    print("TEST 6 - occupied computer")
    ok, msg = allocate(con, "S2", "PC1")
    check(not ok and "OCCUPIED" in msg, "allocating occupied PC1 rejected")
    check(count(con, "SELECT COUNT(*) FROM session") == 1, "no extra session created")

    print("TEST 7 - one active session per student")
    ok, msg = allocate(con, "S1", "PC2")
    check(not ok, "S1 cannot take a second computer")
    check(status_of(con, "PC2") == "AVAILABLE", "PC2 untouched")

    print("Database-level guards (application code bypassed)")
    check(raises(con, "INSERT INTO session (student_id, computer_id, start_time) VALUES ('S2','PC1',?)",
                 (ts(),)), "raw INSERT of second active session on PC1 rejected")
    con.execute("BEGIN")
    con.execute("UPDATE computer SET status='OCCUPIED' WHERE computer_id='PC2'")
    check(raises(con, "INSERT INTO session (student_id, computer_id, start_time) VALUES ('S1','PC2',?)",
                 (ts(),), contains="UNIQUE constraint failed: session.student_id"),
          "partial unique index blocks 2nd active session for a student")
    con.execute("ROLLBACK")
    check(raises(con, "INSERT INTO session (student_id, computer_id, start_time) VALUES ('S2','PC3',?)",
                 (ts(),), contains="OCCUPIED first"), "trigger: ACTIVE session needs OCCUPIED computer")
    check(raises(con, "UPDATE computer SET status='MAINTENANCE' WHERE computer_id='PC1'",
                 contains="active session"), "trigger: no maintenance while in use")
    check(raises(con, "UPDATE computer SET status='AVAILABLE' WHERE computer_id='PC1'",
                 contains="cannot become AVAILABLE"), "trigger: cannot release computer with active session")
    check(raises(con, "INSERT INTO maintenance (computer_id, description, reported_at) VALUES ('PC1','x',?)",
                 (ts(),), contains="in use"), "trigger: no maintenance record on occupied computer")
    check(raises(con, "INSERT INTO session (student_id, computer_id, start_time) VALUES ('NOPE','PC2',?)",
                 (ts(),)), "unknown student rejected")

    print("TEST 12 - injected failure mid-transaction rolls back")
    con.execute("CREATE TEMP TRIGGER labdesk_injected_failure BEFORE INSERT ON main.session "
                "BEGIN SELECT RAISE(ABORT, 'Injected failure for rollback demonstration'); END")
    ok, msg = allocate(con, "S2", "PC2")
    check(not ok and "Injected failure" in msg, "allocation fails at INSERT step")
    check(status_of(con, "PC2") == "AVAILABLE", "PC2 NOT left OCCUPIED (UPDATE was rolled back)")
    check(count(con, "SELECT COUNT(*) FROM session WHERE student_id='S2'") == 0, "no session row for S2")
    con.execute("DROP TRIGGER temp.labdesk_injected_failure")
    ok, _ = allocate(con, "S2", "PC2")
    check(ok, "same allocation succeeds once the fault is removed")
    end_session(con, "S2")

    print("TEST 8 - end session")
    ok, ended = end_session(con, "S1")
    check(ok, "end S1 session succeeds")
    row = con.execute("SELECT status, end_time FROM session WHERE session_id=?", (ended,)).fetchone()
    check(row[0] == "ENDED" and row[1] is not None, "session ENDED with end_time")
    check(status_of(con, "PC1") == "AVAILABLE", "PC1 released")
    check(count(con, "SELECT COUNT(*) FROM session WHERE session_id=?", (ended,)) == 1, "history kept")
    check(raises(con, "UPDATE session SET status='ACTIVE', end_time=NULL WHERE session_id=?", (ended,),
                 contains="historical"), "ended session cannot be reactivated")
    check(raises(con, "DELETE FROM session WHERE session_id=?", (ended,)), "session history cannot be deleted")
    ok, _ = end_session(con, "S1")
    check(not ok, "ending when no session is active is rejected")

    print("TEST 9 - referential integrity on delete")
    check(raises(con, "DELETE FROM computer WHERE computer_id='PC1'", contains="FOREIGN KEY"),
          "computer with history cannot be deleted (FK RESTRICT)")
    check(raises(con, "DELETE FROM student WHERE student_id='S1'", contains="FOREIGN KEY"),
          "student with history cannot be deleted (FK RESTRICT)")
    con.execute("DELETE FROM computer WHERE computer_id='PC3'")
    check(count(con, "SELECT COUNT(*) FROM computer WHERE computer_id='PC3'") == 0,
          "computer without history can be deleted")

    print("TEST 10 - maintenance workflow")
    ok, _ = report(con, "PC2", "Keyboard missing keys")
    check(ok and status_of(con, "PC2") == "MAINTENANCE", "report on AVAILABLE -> MAINTENANCE")
    ok, _ = report(con, "PC2", "Monitor flickers")
    check(ok, "second issue on same computer accepted")
    ok, _ = allocate(con, "S1", "PC2")
    check(not ok, "computer under maintenance cannot be allocated")
    m1, m2 = [r[0] for r in con.execute("SELECT maintenance_id FROM maintenance WHERE computer_id='PC2' "
                                         "ORDER BY maintenance_id")]
    ok, released = resolve(con, m1)
    check(ok and not released and status_of(con, "PC2") == "MAINTENANCE",
          "resolving one of two issues keeps computer in MAINTENANCE")
    ok, released = resolve(con, m2)
    check(ok and released and status_of(con, "PC2") == "AVAILABLE", "resolving last issue releases computer")
    check(raises(con, "UPDATE maintenance SET status='OPEN', resolved_at=NULL WHERE maintenance_id=?", (m1,),
                 contains="reopened"), "resolved record cannot be reopened")
    allocate(con, "S1", "PC1")
    ok, msg = report(con, "PC1", "x")
    check(not ok, "cannot report maintenance on an occupied computer")
    end_session(con, "S1")

    print("Dashboard / lab summary / session lookup / workload SQL (milestone 1.1)")
    con.execute("INSERT INTO computer (computer_id, computer_name, location) VALUES ('PC7','Dell 7','Lab B')")
    allocate(con, "S2", "PC7")
    dash = con.execute(
        "SELECT (SELECT COUNT(DISTINCT location) FROM computer), (SELECT COUNT(*) FROM computer),"
        " (SELECT COUNT(*) FROM computer WHERE status = 'AVAILABLE'),"
        " (SELECT COUNT(*) FROM computer WHERE status = 'OCCUPIED'),"
        " (SELECT COUNT(*) FROM computer WHERE status = 'MAINTENANCE'),"
        " (SELECT COUNT(*) FROM session  WHERE status = 'ACTIVE'),"
        " (SELECT COUNT(*) FROM student),"
        " (SELECT COUNT(*) FROM maintenance WHERE status = 'OPEN')").fetchone()
    check(dash[0] == 2, "dashboard: 2 laboratories (distinct locations)")
    check(dash[1] == 3 and dash[3] == 1 and dash[5] == 1, "dashboard: 3 computers, 1 occupied, 1 active session")
    labs = con.execute(
        "SELECT location, COUNT(*),"
        "       SUM(CASE WHEN status = 'AVAILABLE'   THEN 1 ELSE 0 END),"
        "       SUM(CASE WHEN status = 'OCCUPIED'    THEN 1 ELSE 0 END),"
        "       SUM(CASE WHEN status = 'MAINTENANCE' THEN 1 ELSE 0 END) "
        "FROM computer GROUP BY location ORDER BY location").fetchall()
    check(labs == [("Lab A", 2, 2, 0, 0), ("Lab B", 1, 0, 1, 0)], f"lab summaries per location: {labs}")
    check(sum(l[1] for l in labs) == dash[1], "lab totals add up to total computers")
    session_select = (
        "SELECT s.session_id, s.student_id, st.name, s.computer_id, c.computer_name, c.location,"
        "       s.start_time, s.end_time, s.status "
        "FROM session s JOIN student st ON st.student_id = s.student_id "
        "JOIN computer c ON c.computer_id = s.computer_id ")
    active_sid = con.execute("SELECT session_id FROM session WHERE status='ACTIVE'").fetchone()[0]
    row = con.execute(session_select + "WHERE s.session_id = ?", (active_sid,)).fetchone()
    check(row is not None and row[1] == "S2" and row[3] == "PC7" and row[5] == "Lab B", "findSession join row")
    check(con.execute(session_select + "WHERE s.session_id = ?", (999999,)).fetchone() is None,
          "findSession: unknown id returns no row")
    # workloads are keyed by session (or NULL); 'IS ?' must keep them apart
    con.execute("BEGIN IMMEDIATE")
    con.execute("DELETE FROM process WHERE session_id IS ?", (active_sid,))
    for label, at, bt, pr in [("A1", 0, 5, 1), ("A2", 2, 3, 0)]:
        con.execute("INSERT INTO process (session_id, label, arrival_time, burst_time, priority) VALUES (?,?,?,?,?)",
                    (active_sid, label, at, bt, pr))
    con.execute("COMMIT")
    con.execute("INSERT INTO process (session_id, label, arrival_time, burst_time, priority) VALUES (NULL,'D1',0,1,0)")
    linked = con.execute("SELECT label FROM process WHERE session_id IS ? ORDER BY process_id", (active_sid,)).fetchall()
    unlinked = con.execute("SELECT label FROM process WHERE session_id IS ? ORDER BY process_id", (None,)).fetchall()
    check(linked == [("A1",), ("A2",)], "workload for the session loads only its own processes, in saved order")
    check(unlinked == [("D1",)], "unlinked demo workload kept separate")
    sizes = dict(con.execute("SELECT IFNULL(session_id, -1), COUNT(*) FROM process GROUP BY session_id").fetchall())
    check(sizes == {-1: 1, active_sid: 2}, f"workload sizes per session ({sizes})")
    end_session(con, "S2")

    queue_section(con)

    print("Consistency invariant")
    bad = count(con, """
        SELECT COUNT(*) FROM computer c
        WHERE (c.status = 'OCCUPIED') <>
              EXISTS (SELECT 1 FROM session s WHERE s.computer_id = c.computer_id AND s.status = 'ACTIVE')""")
    check(bad == 0, "OCCUPIED <=> has ACTIVE session for every computer")

    print("TEST 13 - persistence across reconnect")
    n_sessions = count(con, "SELECT COUNT(*) FROM session")
    con.close()
    con = connect(path)
    apply_schema(con)
    check(count(con, "SELECT COUNT(*) FROM session") == n_sessions, "sessions persisted after reopen")
    check(count(con, "PRAGMA user_version") == 2, "schema version 2 persisted")
    con.close()

    migration_section()

    print(f"\n{passed} passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
