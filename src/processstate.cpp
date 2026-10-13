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
    SimulatedProcess p;
    p.id = "P" + std::to_string(m_nextNumber++);
    m_processes.push_back(p);
    return p.id;
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
    if (!isValidTransition(from, to)) {
        return fail(std::string("Invalid transition ") + toString(from) + " -> " + toString(to)
                    + " for " + processId + ".");
    }

    if (to == ProcessState::Running) {
        if (auto running = runningProcess()) {
            return fail("The CPU is busy: " + *running
                        + " is RUNNING. Preempt, block or exit it first (single-CPU model).");
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
