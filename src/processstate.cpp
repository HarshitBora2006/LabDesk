#include "processstate.h"

#include <algorithm>

namespace labdesk::os {

const char* toString(ProcessState state)
{
    switch (state) {
    case ProcessState::New: return "NEW";
    case ProcessState::Ready: return "READY";
    case ProcessState::Running: return "RUNNING";
    case ProcessState::Waiting: return "WAITING";
    case ProcessState::Terminated: return "TERMINATED";
    }
    return "UNKNOWN";
}

std::string transitionName(ProcessState from, ProcessState to)
{
    using S = ProcessState;
    if (from == S::New && to == S::Ready) return "Admit";
    if (from == S::Ready && to == S::Running) return "Dispatch";
    if (from == S::Running && to == S::Ready) return "Preempt";
    if (from == S::Running && to == S::Waiting) return "Block (I/O wait)";
    if (from == S::Waiting && to == S::Ready) return "Wake up (I/O complete)";
    if (from == S::Running && to == S::Terminated) return "Exit";
    return {};
}

bool isValidTransition(ProcessState from, ProcessState to)
{
    return !transitionName(from, to).empty();
}

std::string stateDescription(ProcessState state)
{
    switch (state) {
    case ProcessState::New: return "The process has been created but not yet admitted to the ready queue.";
    case ProcessState::Ready: return "The process is in the ready queue, waiting for CPU time.";
    case ProcessState::Running: return "The process is executing on the (simulated) CPU.";
    case ProcessState::Waiting:
        return "The process is blocked, waiting for an event such as I/O completion. It cannot use the CPU.";
    case ProcessState::Terminated: return "The process has finished execution. This state is final.";
    }
    return {};
}

std::string explainInvalidTransition(const std::string& id, ProcessState from, ProcessState to)
{
    using S = ProcessState;
    if (isValidTransition(from, to))
        return {};
    const std::string now = id + " is " + toString(from) + ". ";
    if (from == to)
        return now + "It is already in that state.";
    if (from == S::Terminated)
        return now + "A terminated process has finished for good; it cannot change state again.";
    switch (to) {
    case S::New:
        return now + "NEW is only the starting state of a newly created process; no process can return to it.";
    case S::Running:
        if (from == S::New)
            return now + "It must first be admitted to the ready queue. Only READY processes can be dispatched.";
        if (from == S::Waiting)
            return now + "It is still waiting for I/O. Complete the I/O so it returns to READY, then dispatch it.";
        break;
    case S::Ready:
        break;  // every non-final source state other than these is covered by isValidTransition
    case S::Waiting:
        return now + "Only a RUNNING process can block for I/O, because a process requests I/O while it is "
                     "executing on the CPU.";
    case S::Terminated:
        return now + "In this model a process terminates by finishing on the CPU, so it must be RUNNING first.";
    }
    return now + "The process-state diagram has no transition from " + toString(from) + " to " + toString(to) + ".";
}

std::vector<ProcessState> validNextStates(ProcessState from)
{
    std::vector<ProcessState> out;
    for (ProcessState to : {ProcessState::New, ProcessState::Ready, ProcessState::Running,
                            ProcessState::Waiting, ProcessState::Terminated}) {
        if (isValidTransition(from, to))
            out.push_back(to);
    }
    return out;
}

std::string LifecycleSimulator::createProcess()
{
    std::string id;
    do {
        id = "P" + std::to_string(m_nextNumber++);
    } while (find(id));  // skip ids already used, e.g. ones loaded from a workload
    SimulatedProcess p;
    p.id = id;
    m_processes.push_back(p);
    return id;
}

bool LifecycleSimulator::createProcess(const std::string& id, std::string* error)
{
    auto fail = [&](const std::string& message) {
        if (error)
            *error = message;
        return false;
    };
    if (id.empty())
        return fail("A process needs an ID.");
    if (id.size() > 16)
        return fail("Process IDs are limited to 16 characters.");
    if (find(id))
        return fail("A process called " + id + " already exists.");
    SimulatedProcess p;
    p.id = id;
    m_processes.push_back(p);
    return true;
}

const SimulatedProcess* LifecycleSimulator::find(const std::string& processId) const
{
    auto it = std::find_if(m_processes.begin(), m_processes.end(),
                           [&](const SimulatedProcess& p) { return p.id == processId; });
    return it == m_processes.end() ? nullptr : &*it;
}

std::optional<std::string> LifecycleSimulator::runningProcess() const
{
    for (const auto& p : m_processes) {
        if (p.state == ProcessState::Running)
            return p.id;
    }
    return std::nullopt;
}

bool LifecycleSimulator::apply(const std::string& processId, ProcessState to, std::string* error)
{
    auto fail = [&](const std::string& message) {
        if (error)
            *error = message;
        return false;
    };

    auto it = std::find_if(m_processes.begin(), m_processes.end(),
                           [&](const SimulatedProcess& p) { return p.id == processId; });
    if (it == m_processes.end())
        return fail("Process " + processId + " does not exist.");

    const ProcessState from = it->state;
    if (!isValidTransition(from, to))
        return fail(explainInvalidTransition(processId, from, to));

    if (to == ProcessState::Running) {
        if (auto running = runningProcess()) {
            return fail("The CPU is busy: " + *running
                        + " is RUNNING. This simulation has one CPU, so preempt, block or terminate "
                        + *running + " first.");
        }
    }

    it->state = to;
    ++it->transitions;
    m_events.push_back({m_clock, processId, from, to, transitionName(from, to)});
    ++m_clock;
    return true;
}

void LifecycleSimulator::reset()
{
    m_processes.clear();
    m_events.clear();
    m_clock = 0;
    m_nextNumber = 1;
}

}  // namespace labdesk::os
