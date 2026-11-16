#include "ossimulator.h"

#include <algorithm>
#include <set>
#include <tuple>

namespace labdesk::os {

const char* simStateName(SimState s)
{
    switch (s) {
    case SimState::New: return "NEW";
    case SimState::Ready: return "READY";
    case SimState::Running: return "RUNNING";
    case SimState::WaitingDisk:
    case SimState::WaitingPrinter: return "WAITING";
    case SimState::Terminated: return "TERMINATED";
    }
    return "NEW";
}

std::string simStateDetail(SimState s)
{
    if (s == SimState::WaitingDisk)
        return "WAITING (disk)";
    if (s == SimState::WaitingPrinter)
        return "WAITING (printer)";
    return simStateName(s);
}

std::vector<std::string> OsSimulator::load(const std::vector<SimProgram>& programs, const SimConfig& config)
{
    std::vector<std::string> problems;
    if (programs.empty())
        problems.emplace_back("Add at least one program.");
    if (programs.size() > 12)
        problems.emplace_back("Use at most 12 programs so the screen stays readable.");
    if (config.algorithm == Algorithm::SRTF)
        problems.emplace_back("The simulator offers FCFS, SJF, Priority and Round Robin.");
    if (config.quantum < 1 || config.quantum > 20)
        problems.emplace_back("Time quantum must be between 1 and 20.");
    if (config.memoryBlocks < 4 || config.memoryBlocks > 16)
        problems.emplace_back("Memory must have between 4 and 16 blocks.");
    std::set<std::string> names;
    for (std::size_t i = 0; i < programs.size(); ++i) {
        const SimProgram& p = programs[i];
        const std::string row = "Program " + std::to_string(i + 1) + ": ";
        if (p.name.empty() || p.name.size() > 20)
            problems.push_back(row + "the name must have 1 to 20 characters.");
        else if (!names.insert(p.name).second)
            problems.push_back(row + "the name " + p.name + " is used twice.");
        if (p.arrival < 0 || p.arrival > 100)
            problems.push_back(row + "arrival must be between 0 and 100.");
        if (p.burst < 1 || p.burst > 50)
            problems.push_back(row + "CPU time must be between 1 and 50.");
        if (p.priority < 0 || p.priority > 99)
            problems.push_back(row + "priority must be between 0 and 99.");
        if (p.memory < 1 || p.memory > config.memoryBlocks)
            problems.push_back(row + "memory must be between 1 and " + std::to_string(config.memoryBlocks) + " blocks.");
        if (p.diskAt != -1 && (p.diskAt < 1 || p.diskAt >= p.burst))
            problems.push_back(row + "the disk request must come after 1 to " + std::to_string(p.burst - 1)
                               + " CPU units (it needs CPU time before and after).");
        if (p.diskAt != -1 && (p.diskTime < 1 || p.diskTime > 20))
            problems.push_back(row + "disk time must be between 1 and 20.");
        if (p.printerAt != -1 && (p.printerAt < 0 || p.printerAt >= p.burst))
            problems.push_back(row + "the printer request must come after 0 to " + std::to_string(p.burst - 1)
                               + " CPU units.");
    }
    if (!problems.empty())
        return problems;

    *this = OsSimulator();
    m_config = config;
    m_memory.assign(static_cast<std::size_t>(config.memoryBlocks), -1);
    m_memoryNoticeShown.assign(programs.size(), false);
    for (std::size_t i = 0; i < programs.size(); ++i) {
        SimProcess p;
        p.program = programs[i];
        p.pid = 1001 + static_cast<int>(i);
        p.remaining = programs[i].burst;
        m_processes.push_back(p);
    }
    return problems;
}

void OsSimulator::log(int time, int index, const std::string& text)
{
    const SimProcess& p = m_processes[static_cast<std::size_t>(index)];
    m_events.push_back({time, p.pid, p.program.name + " " + text});
}

bool OsSimulator::allocateMemory(int index)
{
    const int need = m_processes[static_cast<std::size_t>(index)].program.memory;
    const int blocks = static_cast<int>(m_memory.size());
    for (int start = 0; start + need <= blocks; ++start) {  // first fit: lowest free run that is big enough
        bool free = true;
        for (int b = start; b < start + need && free; ++b)
            free = m_memory[static_cast<std::size_t>(b)] == -1;
        if (!free)
            continue;
        for (int b = start; b < start + need; ++b)
            m_memory[static_cast<std::size_t>(b)] = index;
        m_processes[static_cast<std::size_t>(index)].memoryStart = start;
        return true;
    }
    return false;
}

void OsSimulator::releaseMemory(int index)
{
    for (int& owner : m_memory)
        if (owner == index)
            owner = -1;
    m_processes[static_cast<std::size_t>(index)].memoryStart = -1;
}

int OsSimulator::pickNext() const
{
    if (m_ready.empty())
        return -1;
    if (m_config.algorithm == Algorithm::FCFS || m_config.algorithm == Algorithm::RoundRobin)
        return 0;  // the ready queue is kept in FIFO order
    int best = 0;
    for (int i = 1; i < static_cast<int>(m_ready.size()); ++i) {
        const SimProcess& a = m_processes[static_cast<std::size_t>(m_ready[static_cast<std::size_t>(i)])];
        const SimProcess& b = m_processes[static_cast<std::size_t>(m_ready[static_cast<std::size_t>(best)])];
        const int ka = m_config.algorithm == Algorithm::SJF ? a.remaining : a.program.priority;
        const int kb = m_config.algorithm == Algorithm::SJF ? b.remaining : b.program.priority;
        // Ties: earlier arrival, then lower PID (same rule as os::Scheduler).
        if (std::make_tuple(ka, a.program.arrival, a.pid) < std::make_tuple(kb, b.program.arrival, b.pid))
            best = i;
    }
    return best;
}

void OsSimulator::terminate(int index)
{
    SimProcess& p = m_processes[static_cast<std::size_t>(index)];
    p.state = SimState::Terminated;
    p.completion = m_time + 1;
    log(m_time + 1, index, "finished: RUNNING -> TERMINATED, memory released");
    releaseMemory(index);
    if (p.holdsPrinter) {
        p.holdsPrinter = false;
        m_printerHolder = -1;
        if (!m_printerQueue.empty()) {
            const int next = m_printerQueue.front();
            m_printerQueue.pop_front();
            SimProcess& n = m_processes[static_cast<std::size_t>(next)];
            n.holdsPrinter = true;
            n.state = SimState::Ready;
            m_printerHolder = next;
            m_ready.push_back(next);
            log(m_time + 1, next, "got the printer: WAITING -> READY");
        } else {
            log(m_time + 1, index, "released the printer");
        }
    }
    m_running = -1;
}

bool OsSimulator::finished() const
{
    return std::all_of(m_processes.begin(), m_processes.end(),
                       [](const SimProcess& p) { return p.state == SimState::Terminated; });
}

bool OsSimulator::step()
{
    if (!loaded() || finished())
        return false;
    const int n = static_cast<int>(m_processes.size());

    // 1. disk I/O finished -> READY
    for (int i = 0; i < n; ++i) {
        SimProcess& p = m_processes[static_cast<std::size_t>(i)];
        if (p.state == SimState::WaitingDisk && p.diskLeft == 0) {
            p.state = SimState::Ready;
            m_ready.push_back(i);
            log(m_time, i, "disk I/O complete (interrupt): WAITING -> READY");
        }
    }
    // 2. arrivals get memory -> READY
    for (int i = 0; i < n; ++i) {
        SimProcess& p = m_processes[static_cast<std::size_t>(i)];
        if (p.state != SimState::New || p.program.arrival > m_time)
            continue;
        if (allocateMemory(i)) {
            p.state = SimState::Ready;
            m_ready.push_back(i);
            log(m_time, i,
                "loaded into memory blocks " + std::to_string(p.memoryStart) + "-"
                    + std::to_string(p.memoryStart + p.program.memory - 1) + ": NEW -> READY");
        } else if (!m_memoryNoticeShown[static_cast<std::size_t>(i)]) {
            m_memoryNoticeShown[static_cast<std::size_t>(i)] = true;
            log(m_time, i, "arrived but needs " + std::to_string(p.program.memory)
                               + " free blocks next to each other; it waits in NEW");
        }
    }
    // 3. Round Robin quantum
    if (m_running != -1 && m_config.algorithm == Algorithm::RoundRobin && m_sliceUsed >= m_config.quantum) {
        if (!m_ready.empty()) {
            m_processes[static_cast<std::size_t>(m_running)].state = SimState::Ready;
            m_ready.push_back(m_running);
            log(m_time, m_running, "used its time quantum: RUNNING -> READY (preempted)");
            m_running = -1;
        } else {
            m_sliceUsed = 0;  // nobody else is ready: keep running
        }
    }
    // 4 + 5. dispatch; the dispatched program may block at once, then dispatch again
    for (int guard = 0; guard <= n; ++guard) {
        if (m_running == -1) {
            const int pos = pickNext();
            if (pos < 0)
                break;
            m_running = m_ready[static_cast<std::size_t>(pos)];
            m_ready.erase(m_ready.begin() + pos);
            m_sliceUsed = 0;
            SimProcess& d = m_processes[static_cast<std::size_t>(m_running)];
            d.state = SimState::Running;
            if (d.firstRun < 0)
                d.firstRun = m_time;
            if (m_lastRun != -1 && m_lastRun != m_running)
                log(m_time, m_running,
                    "dispatched: READY -> RUNNING (context switch from "
                        + m_processes[static_cast<std::size_t>(m_lastRun)].program.name + ")");
            else
                log(m_time, m_running, "dispatched: READY -> RUNNING");
        }
        SimProcess& p = m_processes[static_cast<std::size_t>(m_running)];
        if (p.program.diskAt >= 0 && !p.diskDone && p.cpuUsed == p.program.diskAt) {
            p.diskDone = true;
            p.diskLeft = p.program.diskTime;
            p.state = SimState::WaitingDisk;
            log(m_time, m_running, "asked the disk for data: RUNNING -> WAITING");
            m_running = -1;
            continue;
        }
        if (p.program.printerAt >= 0 && !p.holdsPrinter && p.cpuUsed == p.program.printerAt) {
            if (m_printerHolder == -1) {
                m_printerHolder = m_running;
                p.holdsPrinter = true;
                log(m_time, m_running, "locked the printer (mutual exclusion)");
            } else {
                p.state = SimState::WaitingPrinter;
                m_printerQueue.push_back(m_running);
                log(m_time, m_running,
                    "needs the printer, which "
                        + m_processes[static_cast<std::size_t>(m_printerHolder)].program.name
                        + " is using: RUNNING -> WAITING");
                m_running = -1;
                continue;
            }
        }
        break;
    }
    // execute one unit
    if (m_running != -1) {
        SimProcess& p = m_processes[static_cast<std::size_t>(m_running)];
        ++p.cpuUsed;
        --p.remaining;
        ++m_sliceUsed;
        m_lastRun = m_running;
    }
    m_gantt.push_back(m_running);
    // account this tick
    for (SimProcess& p : m_processes) {
        switch (p.state) {
        case SimState::Ready: ++p.readyWait; break;
        case SimState::WaitingDisk:
            ++p.diskWait;
            --p.diskLeft;
            break;
        case SimState::WaitingPrinter: ++p.printerWait; break;
        case SimState::New:
            if (p.program.arrival <= m_time)
                ++p.memoryWait;
            break;
        default: break;
        }
    }
    if (m_running != -1 && m_processes[static_cast<std::size_t>(m_running)].remaining == 0)
        terminate(m_running);
    ++m_time;
    return true;
}

void OsSimulator::runToEnd(int maxTicks)
{
    for (int i = 0; i < maxTicks && step(); ++i) {
    }
}

int OsSimulator::contextSwitches() const
{
    int switches = 0;
    int previous = -1;
    for (int who : m_gantt) {
        if (who < 0)
            continue;
        if (previous != -1 && previous != who)
            ++switches;
        previous = who;
    }
    return switches;
}

double OsSimulator::cpuUtilisation() const
{
    if (m_gantt.empty())
        return 0.0;
    const auto busy = std::count_if(m_gantt.begin(), m_gantt.end(), [](int w) { return w >= 0; });
    return static_cast<double>(busy) / static_cast<double>(m_gantt.size());
}

double OsSimulator::averageTurnaround() const
{
    double total = 0;
    int done = 0;
    for (const SimProcess& p : m_processes)
        if (p.completion >= 0) {
            total += p.completion - p.program.arrival;
            ++done;
        }
    return done ? total / done : 0.0;
}

double OsSimulator::averageReadyWait() const
{
    double total = 0;
    int done = 0;
    for (const SimProcess& p : m_processes)
        if (p.completion >= 0) {
            total += p.readyWait;
            ++done;
        }
    return done ? total / done : 0.0;
}

std::vector<SimProgram> OsSimulator::fromProcesses(const std::vector<Process>& processes)
{
    std::vector<SimProgram> out;
    for (const Process& p : processes) {
        SimProgram s;
        s.name = p.id;
        s.arrival = p.arrival;
        s.burst = p.burst;
        s.priority = p.priority;
        out.push_back(s);
    }
    return out;
}

std::vector<SimProgram> OsSimulator::example()
{
    //        name          arrival burst prio memory (no disk, no printer)
    return {{"chrome.exe", 0, 5, 2, 2, -1, 0, -1},
            {"python.exe", 1, 4, 1, 3, -1, 0, -1},
            {"Code.exe", 2, 4, 3, 2, -1, 0, -1},
            {"notepad.exe", 3, 3, 2, 2, -1, 0, -1}};
}

}  // namespace labdesk::os
