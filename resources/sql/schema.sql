-- =====================================================================
-- LabDesk database schema (schema version 2)
--
-- Version 2 (LabDesk 0.7.0) ADDS lab_setting and lab_request plus their
-- indexes and triggers. Nothing existing is altered, so opening a version-1
-- database simply creates the new tables; all existing rows are preserved.
--
-- Loader rule: statements are separated by a line containing only
--     -- @@
-- because trigger bodies contain semicolons, so splitting on ';' would
-- break them. The same file is loaded by the C++ app (Database::applySchema)
-- and by tools/validate_schema.py.
--
-- Timestamps: UTC ISO-8601 with milliseconds, e.g. 2026-10-13T09:15:02.481Z
-- Fixed width + UTC means text comparison == chronological comparison.
-- =====================================================================

CREATE TABLE IF NOT EXISTS student (
    student_id    TEXT NOT NULL PRIMARY KEY
                  CHECK (length(student_id) BETWEEN 2 AND 20 AND student_id = trim(student_id)),
    name          TEXT NOT NULL CHECK (length(trim(name)) > 0),
    email         TEXT NOT NULL COLLATE NOCASE UNIQUE
                  CHECK (email LIKE '%_@_%._%'),
    password_hash TEXT NOT NULL CHECK (length(password_hash) > 0),
    department    TEXT NOT NULL CHECK (length(trim(department)) > 0)
);
-- @@
CREATE TABLE IF NOT EXISTS computer (
    computer_id   TEXT NOT NULL PRIMARY KEY
                  CHECK (length(computer_id) BETWEEN 2 AND 20 AND computer_id = trim(computer_id)),
    computer_name TEXT NOT NULL CHECK (length(trim(computer_name)) > 0),
    location      TEXT NOT NULL CHECK (length(trim(location)) > 0),
    status        TEXT NOT NULL DEFAULT 'AVAILABLE'
                  CHECK (status IN ('AVAILABLE', 'OCCUPIED', 'MAINTENANCE'))
);
-- @@
CREATE TABLE IF NOT EXISTS session (
    session_id  INTEGER PRIMARY KEY AUTOINCREMENT,
    student_id  TEXT NOT NULL
                REFERENCES student (student_id) ON UPDATE RESTRICT ON DELETE RESTRICT,
    computer_id TEXT NOT NULL
                REFERENCES computer (computer_id) ON UPDATE RESTRICT ON DELETE RESTRICT,
    start_time  TEXT NOT NULL
                CHECK (start_time GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]*Z'),
    end_time    TEXT
                CHECK (end_time IS NULL OR end_time GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]*Z'),
    status      TEXT NOT NULL DEFAULT 'ACTIVE' CHECK (status IN ('ACTIVE', 'ENDED')),
    CHECK ((status = 'ACTIVE' AND end_time IS NULL) OR (status = 'ENDED' AND end_time IS NOT NULL)),
    CHECK (end_time IS NULL OR end_time >= start_time)
);
-- @@
CREATE TABLE IF NOT EXISTS maintenance (
    maintenance_id INTEGER PRIMARY KEY AUTOINCREMENT,
    computer_id    TEXT NOT NULL
                   REFERENCES computer (computer_id) ON UPDATE RESTRICT ON DELETE RESTRICT,
    description    TEXT NOT NULL CHECK (length(trim(description)) BETWEEN 1 AND 500),
    reported_at    TEXT NOT NULL
                   CHECK (reported_at GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]*Z'),
    resolved_at    TEXT
                   CHECK (resolved_at IS NULL OR resolved_at GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]*Z'),
    status         TEXT NOT NULL DEFAULT 'OPEN' CHECK (status IN ('OPEN', 'RESOLVED')),
    CHECK ((status = 'OPEN' AND resolved_at IS NULL) OR (status = 'RESOLVED' AND resolved_at IS NOT NULL)),
    CHECK (resolved_at IS NULL OR resolved_at >= reported_at)
);
-- @@
CREATE TABLE IF NOT EXISTS process (
    process_id   INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id   INTEGER REFERENCES session (session_id) ON DELETE SET NULL,
    label        TEXT NOT NULL CHECK (length(trim(label)) BETWEEN 1 AND 16),
    arrival_time INTEGER NOT NULL CHECK (typeof(arrival_time) = 'integer' AND arrival_time >= 0),
    burst_time   INTEGER NOT NULL CHECK (typeof(burst_time) = 'integer' AND burst_time > 0),
    priority     INTEGER NOT NULL CHECK (typeof(priority) = 'integer' AND priority BETWEEN 0 AND 99)
);
-- @@
-- ---------------------------------------------------------------------
-- Indexes. The two PARTIAL UNIQUE indexes are the core integrity rules:
-- at most one ACTIVE session per computer and per student. They hold
-- even if application code is bypassed.
-- ---------------------------------------------------------------------
CREATE UNIQUE INDEX IF NOT EXISTS ux_session_one_active_per_computer
    ON session (computer_id) WHERE status = 'ACTIVE';
-- @@
CREATE UNIQUE INDEX IF NOT EXISTS ux_session_one_active_per_student
    ON session (student_id) WHERE status = 'ACTIVE';
-- @@
CREATE INDEX IF NOT EXISTS ix_session_student ON session (student_id);
-- @@
CREATE INDEX IF NOT EXISTS ix_session_computer ON session (computer_id);
-- @@
CREATE INDEX IF NOT EXISTS ix_computer_status ON computer (status);
-- @@
CREATE INDEX IF NOT EXISTS ix_maintenance_computer ON maintenance (computer_id, status);
-- @@
CREATE INDEX IF NOT EXISTS ix_process_session ON process (session_id);
-- @@
-- ---------------------------------------------------------------------
-- Triggers: cross-table business rules that CHECK constraints cannot
-- express. They are a second line of defence behind the service layer.
-- ---------------------------------------------------------------------
CREATE TRIGGER IF NOT EXISTS trg_session_active_requires_occupied_computer
BEFORE INSERT ON session
WHEN NEW.status = 'ACTIVE'
 AND (SELECT status FROM computer WHERE computer_id = NEW.computer_id) IS NOT 'OCCUPIED'
BEGIN
    SELECT RAISE(ABORT, 'An ACTIVE session requires its computer to be OCCUPIED first');
END;
-- @@
CREATE TRIGGER IF NOT EXISTS trg_session_history_is_immutable
BEFORE UPDATE ON session
WHEN OLD.status = 'ENDED'
BEGIN
    SELECT RAISE(ABORT, 'Ended sessions are historical records and cannot be modified');
END;
-- @@
CREATE TRIGGER IF NOT EXISTS trg_session_history_not_deletable
BEFORE DELETE ON session
BEGIN
    SELECT RAISE(ABORT, 'Session history cannot be deleted');
END;
-- @@
CREATE TRIGGER IF NOT EXISTS trg_computer_occupied_only_from_available
BEFORE UPDATE OF status ON computer
WHEN NEW.status = 'OCCUPIED' AND OLD.status <> 'AVAILABLE'
BEGIN
    SELECT RAISE(ABORT, 'Only an AVAILABLE computer can become OCCUPIED');
END;
-- @@
CREATE TRIGGER IF NOT EXISTS trg_computer_no_maintenance_during_session
BEFORE UPDATE OF status ON computer
WHEN NEW.status = 'MAINTENANCE'
 AND EXISTS (SELECT 1 FROM session WHERE computer_id = NEW.computer_id AND status = 'ACTIVE')
BEGIN
    SELECT RAISE(ABORT, 'Computer has an active session and cannot be placed into maintenance');
END;
-- @@
CREATE TRIGGER IF NOT EXISTS trg_computer_available_requires_clear_state
BEFORE UPDATE OF status ON computer
WHEN NEW.status = 'AVAILABLE'
 AND (EXISTS (SELECT 1 FROM session WHERE computer_id = NEW.computer_id AND status = 'ACTIVE')
      OR EXISTS (SELECT 1 FROM maintenance WHERE computer_id = NEW.computer_id AND status = 'OPEN'))
BEGIN
    SELECT RAISE(ABORT, 'Computer cannot become AVAILABLE while it has an active session or an open maintenance issue');
END;
-- @@
CREATE TRIGGER IF NOT EXISTS trg_maintenance_not_on_occupied_computer
BEFORE INSERT ON maintenance
WHEN NEW.status = 'OPEN'
 AND (SELECT status FROM computer WHERE computer_id = NEW.computer_id) = 'OCCUPIED'
BEGIN
    SELECT RAISE(ABORT, 'Cannot open a maintenance issue on a computer that is in use');
END;
-- @@
CREATE TRIGGER IF NOT EXISTS trg_maintenance_resolved_is_final
BEFORE UPDATE OF status ON maintenance
WHEN OLD.status = 'RESOLVED'
BEGIN
    SELECT RAISE(ABORT, 'A resolved maintenance record cannot be reopened');
END;

-- @@
-- =====================================================================
-- Version 2: lab queue. A request records WHEN a student asked for a
-- computer (arrival), why (purpose -> priority) and how long they expect to
-- stay (needed by SJF). The linked session supplies start and end, so
-- waiting time and actual burst are measured, never typed in.
-- =====================================================================
CREATE TABLE IF NOT EXISTS lab_setting (
    setting_key TEXT NOT NULL PRIMARY KEY,
    value       TEXT NOT NULL,
    CHECK (setting_key <> 'queue_policy' OR value IN ('FCFS', 'SJF', 'PRIORITY'))
);
-- @@
INSERT OR IGNORE INTO lab_setting (setting_key, value) VALUES ('queue_policy', 'FCFS');
-- @@
CREATE TABLE IF NOT EXISTS lab_request (
    request_id       INTEGER PRIMARY KEY AUTOINCREMENT,
    student_id       TEXT NOT NULL
                     REFERENCES student (student_id) ON UPDATE RESTRICT ON DELETE RESTRICT,
    purpose          TEXT NOT NULL CHECK (purpose IN ('EXAM', 'ASSIGNMENT', 'PRACTICE')),
    priority         INTEGER NOT NULL,
    expected_minutes INTEGER NOT NULL
                     CHECK (typeof(expected_minutes) = 'integer' AND expected_minutes BETWEEN 5 AND 480),
    requested_at     TEXT NOT NULL CHECK (requested_at GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]*Z'),
    status           TEXT NOT NULL DEFAULT 'WAITING' CHECK (status IN ('WAITING', 'ALLOCATED', 'CANCELLED')),
    session_id       INTEGER UNIQUE
                     REFERENCES session (session_id) ON UPDATE RESTRICT ON DELETE RESTRICT,
    allocated_at     TEXT CHECK (allocated_at IS NULL OR allocated_at GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]*Z'),
    cancelled_at     TEXT CHECK (cancelled_at IS NULL OR cancelled_at GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]:[0-9][0-9]*Z'),
    granted_by       TEXT CHECK (granted_by IS NULL OR granted_by IN ('IMMEDIATE', 'FCFS', 'SJF', 'PRIORITY')),
    -- priority is derived from purpose and must agree with it (lower = higher priority)
    CHECK (priority = CASE purpose WHEN 'EXAM' THEN 1 WHEN 'ASSIGNMENT' THEN 2 ELSE 3 END),
    CHECK ((status = 'WAITING'   AND session_id IS NULL     AND allocated_at IS NULL     AND cancelled_at IS NULL     AND granted_by IS NULL)
        OR (status = 'ALLOCATED' AND session_id IS NOT NULL AND allocated_at IS NOT NULL AND cancelled_at IS NULL     AND granted_by IS NOT NULL)
        OR (status = 'CANCELLED' AND session_id IS NULL     AND allocated_at IS NULL     AND cancelled_at IS NOT NULL AND granted_by IS NULL)),
    CHECK (allocated_at IS NULL OR allocated_at >= requested_at),
    CHECK (cancelled_at IS NULL OR cancelled_at >= requested_at)
);
-- @@
CREATE UNIQUE INDEX IF NOT EXISTS ux_request_one_waiting_per_student
    ON lab_request (student_id) WHERE status = 'WAITING';
-- @@
CREATE INDEX IF NOT EXISTS ix_request_status ON lab_request (status, requested_at);
-- @@
CREATE TRIGGER IF NOT EXISTS trg_request_not_while_in_session
BEFORE INSERT ON lab_request
WHEN NEW.status = 'WAITING'
 AND EXISTS (SELECT 1 FROM session WHERE student_id = NEW.student_id AND status = 'ACTIVE')
BEGIN
    SELECT RAISE(ABORT, 'Student already has an active session and cannot join the queue');
END;
-- @@
CREATE TRIGGER IF NOT EXISTS trg_request_session_belongs_to_student_insert
BEFORE INSERT ON lab_request
WHEN NEW.session_id IS NOT NULL
 AND (SELECT student_id FROM session WHERE session_id = NEW.session_id) IS NOT NEW.student_id
BEGIN
    SELECT RAISE(ABORT, 'A request can only be linked to a session of the same student');
END;
-- @@
CREATE TRIGGER IF NOT EXISTS trg_request_session_belongs_to_student_update
BEFORE UPDATE OF session_id ON lab_request
WHEN NEW.session_id IS NOT NULL
 AND (SELECT student_id FROM session WHERE session_id = NEW.session_id) IS NOT NEW.student_id
BEGIN
    SELECT RAISE(ABORT, 'A request can only be linked to a session of the same student');
END;
-- @@
CREATE TRIGGER IF NOT EXISTS trg_request_history_is_final
BEFORE UPDATE ON lab_request
WHEN OLD.status IN ('ALLOCATED', 'CANCELLED')
BEGIN
    SELECT RAISE(ABORT, 'Allocated or cancelled requests are measurement history and cannot be modified');
END;
-- @@
CREATE TRIGGER IF NOT EXISTS trg_request_history_not_deletable
BEFORE DELETE ON lab_request
BEGIN
    SELECT RAISE(ABORT, 'Request history cannot be deleted');
END;
