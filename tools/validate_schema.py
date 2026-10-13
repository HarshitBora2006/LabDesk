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


def schema_statements():
    with open(SCHEMA, encoding="utf-8") as f:
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


def apply_schema(con):
    con.execute("BEGIN IMMEDIATE")
    for s in schema_statements():
        con.execute(s)
    con.execute("PRAGMA user_version = 1")
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


def main():
    tmp = tempfile.mkdtemp()
    path = os.path.join(tmp, "labdesk_validate.db")
    con = connect(path)

    print("Schema")
    apply_schema(con)
    check(len(schema_statements()) == 20, "schema file splits into 20 statements (5 tables, 7 indexes, 8 triggers)")
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
    check(count(con, "PRAGMA user_version") == 1, "schema version persisted")
    con.close()

    print(f"\n{passed} passed, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
