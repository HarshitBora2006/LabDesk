#pragma once
// Educational process-lifecycle model (NEW, READY, RUNNING, WAITING, TERMINATED).
//
// Pure C++17 with no Qt dependency, so it can be unit-tested on its own.
// This is a SIMULATION for teaching. It does not observe or control real
// operating-system processes, and it is unrelated to the computer-session
// status (AVAILABLE/OCCUPIED/...) stored in the database.

#include <optional>
#include <string>
#include <vector>

namespace labdesk::os {

enum class ProcessState { New, Ready, Running, Waiting, Terminated };

const char* toString(ProcessState state);

// Textbook transitions:
//   NEW     -> READY       admit
//   READY   -> RUNNING     dispatch (scheduler picks it)
//   RUNNING -> READY       preempt (time-slice expired / interrupt)
//   RUNNING -> WAITING     block (I/O request or event wait)
//   WAITING -> READY       wake up (I/O or event completed)
//   RUNNING -> TERMINATED  exit
bool isValidTransition(ProcessState from, ProcessState to);

// Human-readable name of a transition ("Admit", "Dispatch", ...); empty if invalid.
std::string transitionName(ProcessState from, ProcessState to);

// Plain-language meaning of a state, for the GUI legend.
std::string stateDescription(ProcessState state);

// Why `from -> to` is not allowed, in plain language for the given process id.
// Returns an empty string when the transition IS allowed by the state diagram.
std::string explainInvalidTransition(const std::string& processId, ProcessState from, ProcessState to);

// All states reachable in one valid step from `from`.
std::vector<ProcessState> validNextStates(ProcessState from);

struct StateEvent {
    int time = 0;
    std::string processId;
    ProcessState from = ProcessState::New;
    ProcessState to = ProcessState::New;
    std::string reason;
};

struct SimulatedProcess {
    std::string id;
    ProcessState state = ProcessState::New;
    int transitions = 0;
};

// Interactive single-CPU lifecycle simulator. Every successful transition
// advances a logical clock by one tick and is appended to the event log.
// Invariant: at most one process is RUNNING at a time.
class LifecycleSimulator {
public:
    std::string createProcess();  // returns the new id ("P1", "P2", ...), state NEW

    // Creates a process with a caller-chosen id (e.g. taken from a scheduler
    // workload). Fails if the id is empty, longer than 16 characters or taken.
    bool createProcess(const std::string& id, std::string* error);

    // Applies a transition. Returns false and fills `error` with a plain-language
    // reason if it is not allowed by the state diagram or by the single-CPU rule.
    bool apply(const std::string& processId, ProcessState to, std::string* error = nullptr);

    std::optional<std::string> runningProcess() const;
    const std::vector<SimulatedProcess>& processes() const { return m_processes; }
    const std::vector<StateEvent>& events() const { return m_events; }
    const SimulatedProcess* find(const std::string& processId) const;
    int clock() const { return m_clock; }
    void reset();

private:
    std::vector<SimulatedProcess> m_processes;
    std::vector<StateEvent> m_events;
    int m_clock = 0;
    int m_nextNumber = 1;
};

}  // namespace labdesk::os
