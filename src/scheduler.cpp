#include "scheduler.h"

#include <algorithm>
#include <deque>
#include <numeric>
#include <set>
#include <tuple>

namespace labdesk::os {

std::string algorithmName(Algorithm algorithm)
{
    switch (algorithm) {
    case Algorithm::FCFS: return "FCFS";
    case Algorithm::SJF: return "SJF (non-preemptive)";
    case Algorithm::SRTF: return "SRTF (preemptive SJF)";
    case Algorithm::Priority: return "Priority (non-preemptive)";
    case Algorithm::RoundRobin: return "Round Robin";
    }
    return "Unknown";
}

bool isPreemptive(Algorithm algorithm)
{
    return algorithm == Algorithm::SRTF || algorithm == Algorithm::RoundRobin;
}

std::string algorithmDescription(Algorithm algorithm)
{
    switch (algorithm) {
    case Algorithm::FCFS:
        return "First Come First Serve runs processes in the order they arrive. Each one keeps the CPU "
               "until its burst finishes (non-preemptive). Simple, but short jobs can wait behind long ones.";
    case Algorithm::SJF:
        return "Shortest Job First picks, among processes that have already arrived, the one with the "
               "smallest burst time, and lets it finish (non-preemptive). Ties go to the earlier arrival.";
    case Algorithm::SRTF:
        return "Shortest Remaining Time First is the preemptive form of SJF. When a process arrives with a "
               "remaining time strictly shorter than the running process, it takes the CPU immediately.";
    case Algorithm::Priority:
        return "Priority scheduling runs the arrived process with the best priority. Here a LOWER number "
               "means a HIGHER priority (0 is best). Non-preemptive; ties go to the earlier arrival.";
    case Algorithm::RoundRobin:
        return "Round Robin gives each ready process at most one time quantum, then moves it to the back "
               "of the ready queue (preemptive time sharing). Smaller quanta mean more context switches.";
    }
    return {};
}

std::vector<std::string> validateProcesses(const std::vector<Process>& processes)
{
    std::vector<std::string> problems;
    if (processes.empty()) {
        problems.emplace_back("Add at least one process.");
        return problems;
    }
    if (processes.size() > kMaxProcesses)
        problems.push_back("At most " + std::to_string(kMaxProcesses) + " processes are supported.");

    std::set<std::string> seen;
    for (std::size_t i = 0; i < processes.size(); ++i) {
        const Process& p = processes[i];
        const std::string where = "Row " + std::to_string(i + 1) + ": ";
        if (p.id.empty())
            problems.push_back(where + "process ID is required.");
        else if (p.id.size() > 16)
            problems.push_back(where + "process ID must be at most 16 characters.");
        else if (!seen.insert(p.id).second)
            problems.push_back(where + "duplicate process ID '" + p.id + "'.");

        if (p.arrival < 0 || p.arrival > kMaxArrival)
            problems.push_back(where + "arrival time must be between 0 and " + std::to_string(kMaxArrival) + ".");
        if (p.burst < 1 || p.burst > kMaxBurst)
            problems.push_back(where + "burst time must be between 1 and " + std::to_string(kMaxBurst) + ".");
        if (p.priority < kMinPriority || p.priority > kMaxPriority)
            problems.push_back(where + "priority must be between " + std::to_string(kMinPriority) + " and "
                               + std::to_string(kMaxPriority) + " (lower number = higher priority).");
    }
    return problems;
}

std::string validateQuantum(int quantum)
{
    if (quantum < 1 || quantum > kMaxQuantum)
        return "Time quantum must be between 1 and " + std::to_string(kMaxQuantum) + ".";
    return {};
}

ScheduleResult Scheduler::run(Algorithm algorithm, const std::vector<Process>& processes, int quantum)
{
    const auto problems = validateProcesses(processes);
    if (!problems.empty()) {
        std::string joined;
        for (const auto& p : problems)
            joined += (joined.empty() ? "" : "\n") + p;
        throw std::invalid_argument(joined);
    }
    if (algorithm == Algorithm::RoundRobin) {
        const std::string qProblem = validateQuantum(quantum);
        if (!qProblem.empty())
            throw std::invalid_argument(qProblem);
        return runRoundRobin(processes, quantum);
    }
    if (algorithm == Algorithm::SRTF)
        return runShortestRemaining(processes);
    return runNonPreemptive(algorithm, processes);
}

namespace {

// Indices sorted by (arrival, input order): the order in which processes are admitted.
std::vector<std::size_t> arrivalOrder(const std::vector<Process>& processes)
{
    std::vector<std::size_t> order(processes.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return processes[a].arrival < processes[b].arrival;
    });
    return order;
}

void addIdle(std::vector<TimelineSlice>& timeline, int from, int to)
{
    if (to > from)
        timeline.push_back({std::string(), from, to, true});
}

}  // namespace

ScheduleResult Scheduler::runNonPreemptive(Algorithm algorithm, const std::vector<Process>& processes)
{
    const std::size_t n = processes.size();
    ScheduleResult result;
    result.algorithm = algorithm;
    result.processes.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        result.processes[i].process = processes[i];

    const auto order = arrivalOrder(processes);
    std::size_t nextToAdmit = 0;   // index into `order`
    std::vector<std::size_t> ready;  // admitted, not yet run
    std::size_t finished = 0;
    int time = 0;

    auto admitUpTo = [&](int t) {
        while (nextToAdmit < n && processes[order[nextToAdmit]].arrival <= t) {
            const std::size_t i = order[nextToAdmit++];
            ready.push_back(i);
            result.events.push_back({processes[i].arrival, processes[i].id, ProcessState::New,
                                     ProcessState::Ready, "Arrived; admitted to ready queue"});
        }
    };

    // Selection key: smaller tuple wins. Arrival then input index break ties.
    auto key = [&](std::size_t i) {
        const Process& p = processes[i];
        switch (algorithm) {
        case Algorithm::SJF: return std::make_tuple(p.burst, p.arrival, i);
        case Algorithm::Priority: return std::make_tuple(p.priority, p.arrival, i);
        default: return std::make_tuple(0, p.arrival, i);  // FCFS
        }
    };

    while (finished < n) {
        admitUpTo(time);
        if (ready.empty()) {
            // CPU idle until the next process arrives.
            const int next = processes[order[nextToAdmit]].arrival;
            addIdle(result.timeline, time, next);
            time = next;
            continue;
        }

        auto best = std::min_element(ready.begin(), ready.end(),
                                     [&](std::size_t a, std::size_t b) { return key(a) < key(b); });
        const std::size_t i = *best;
        ready.erase(best);

        const Process& p = processes[i];
        ProcessResult& r = result.processes[i];
        r.firstStart = time;
        result.events.push_back({time, p.id, ProcessState::Ready, ProcessState::Running, "Dispatched"});
        result.timeline.push_back({p.id, time, time + p.burst, false});
        time += p.burst;
        r.completion = time;
        result.events.push_back({time, p.id, ProcessState::Running, ProcessState::Terminated,
                                 "Burst complete"});
        ++finished;
    }

    finalise(result);
    return result;
}

ScheduleResult Scheduler::runShortestRemaining(const std::vector<Process>& processes)
{
    const std::size_t n = processes.size();
    ScheduleResult result;
    result.algorithm = Algorithm::SRTF;
    result.processes.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        result.processes[i].process = processes[i];

    const auto order = arrivalOrder(processes);
    std::vector<int> remaining(n);
    for (std::size_t i = 0; i < n; ++i)
        remaining[i] = processes[i].burst;

    std::vector<std::size_t> ready;
    std::size_t nextToAdmit = 0;
    std::size_t finished = 0;
    int time = 0;
    bool running = false;
    std::size_t current = 0;
    int sliceStart = 0;

    auto admitUpTo = [&](int t) {
        while (nextToAdmit < n && processes[order[nextToAdmit]].arrival <= t) {
            const std::size_t i = order[nextToAdmit++];
            ready.push_back(i);
            result.events.push_back({processes[i].arrival, processes[i].id, ProcessState::New,
                                     ProcessState::Ready, "Arrived; admitted to ready queue"});
        }
    };
    // Shortest remaining time; ties by earlier arrival, then input order.
    auto better = [&](std::size_t a, std::size_t b) {
        return std::make_tuple(remaining[a], processes[a].arrival, a)
               < std::make_tuple(remaining[b], processes[b].arrival, b);
    };

    while (finished < n) {
        admitUpTo(time);
        if (!running && ready.empty()) {
            const int next = processes[order[nextToAdmit]].arrival;
            addIdle(result.timeline, time, next);
            time = next;
            continue;
        }

        if (!ready.empty()) {
            auto best = std::min_element(ready.begin(), ready.end(), better);
            if (running && remaining[*best] < remaining[current]) {
                // Preemption: a strictly shorter job is ready.
                result.timeline.push_back({processes[current].id, sliceStart, time, false});
                result.events.push_back({time, processes[current].id, ProcessState::Running, ProcessState::Ready,
                                         "Preempted: " + processes[*best].id + " has a shorter remaining time"});
                ready.push_back(current);
                running = false;
                best = std::min_element(ready.begin(), ready.end(), better);
            }
            if (!running) {
                current = *best;
                ready.erase(best);
                running = true;
                sliceStart = time;
                ProcessResult& r = result.processes[current];
                if (r.firstStart < 0)
                    r.firstStart = time;
                result.events.push_back({time, processes[current].id, ProcessState::Ready, ProcessState::Running,
                                         "Dispatched (shortest remaining time)"});
            }
        }

        // Run until the current process finishes or the next process arrives,
        // whichever is first: those are the only moments a decision can change.
        int run = remaining[current];
        if (nextToAdmit < n)
            run = std::min(run, processes[order[nextToAdmit]].arrival - time);
        time += run;
        remaining[current] -= run;
        if (remaining[current] == 0) {
            result.processes[current].completion = time;
            result.timeline.push_back({processes[current].id, sliceStart, time, false});
            result.events.push_back({time, processes[current].id, ProcessState::Running, ProcessState::Terminated,
                                     "Burst complete"});
            running = false;
            ++finished;
        }
    }

    finalise(result);
    return result;
}

ScheduleResult Scheduler::runRoundRobin(const std::vector<Process>& processes, int quantum)
{
    const std::size_t n = processes.size();
    ScheduleResult result;
    result.algorithm = Algorithm::RoundRobin;
    result.quantum = quantum;
    result.processes.resize(n);
    for (std::size_t i = 0; i < n; ++i)
        result.processes[i].process = processes[i];

    const auto order = arrivalOrder(processes);
    std::vector<int> remaining(n);
    for (std::size_t i = 0; i < n; ++i)
        remaining[i] = processes[i].burst;

    std::deque<std::size_t> readyQueue;
    std::size_t nextToAdmit = 0;
    std::size_t finished = 0;
    int time = 0;

    auto admitUpTo = [&](int t) {
        while (nextToAdmit < n && processes[order[nextToAdmit]].arrival <= t) {
            const std::size_t i = order[nextToAdmit++];
            readyQueue.push_back(i);
            result.events.push_back({processes[i].arrival, processes[i].id, ProcessState::New,
                                     ProcessState::Ready, "Arrived; joined tail of ready queue"});
        }
    };

    admitUpTo(time);
    while (finished < n) {
        if (readyQueue.empty()) {
            const int next = processes[order[nextToAdmit]].arrival;
            addIdle(result.timeline, time, next);
            time = next;
            admitUpTo(time);
            continue;
        }

        const std::size_t i = readyQueue.front();
        readyQueue.pop_front();
        const Process& p = processes[i];
        ProcessResult& r = result.processes[i];
        if (r.firstStart < 0)
            r.firstStart = time;
        result.events.push_back({time, p.id, ProcessState::Ready, ProcessState::Running,
                                 "Dispatched from head of ready queue"});

        const int sliceStart = time;
        while (true) {
            const int run = std::min(quantum, remaining[i]);
            time += run;
            remaining[i] -= run;
            // Arrivals during (or exactly at the end of) this quantum join the
            // queue before the current process is requeued.
            admitUpTo(time);

            if (remaining[i] == 0) {
                r.completion = time;
                result.events.push_back({time, p.id, ProcessState::Running, ProcessState::Terminated,
                                         "Burst complete"});
                ++finished;
                break;
            }
            if (!readyQueue.empty()) {
                result.events.push_back({time, p.id, ProcessState::Running, ProcessState::Ready,
                                         "Time quantum expired; requeued at tail"});
                readyQueue.push_back(i);
                break;
            }
            // Nobody else is ready: keep running for another quantum (no context switch).
        }
        result.timeline.push_back({p.id, sliceStart, time, false});
    }

    finalise(result);
    return result;
}

void Scheduler::finalise(ScheduleResult& result)
{
    const double n = static_cast<double>(result.processes.size());
    double totalWT = 0, totalTAT = 0, totalRT = 0;
    for (ProcessResult& r : result.processes) {
        r.turnaround = r.completion - r.process.arrival;
        r.waiting = r.turnaround - r.process.burst;
        r.response = r.firstStart - r.process.arrival;
        totalWT += r.waiting;
        totalTAT += r.turnaround;
        totalRT += r.response;
        result.makespan = std::max(result.makespan, r.completion);
    }
    result.averageWaiting = totalWT / n;
    result.averageTurnaround = totalTAT / n;
    result.averageResponse = totalRT / n;

    int busy = 0;
    std::string previous;
    for (const TimelineSlice& s : result.timeline) {
        if (s.idle) {
            result.idleTime += s.end - s.start;
            continue;
        }
        busy += s.end - s.start;
        if (!previous.empty() && previous != s.processId)
            ++result.contextSwitches;
        previous = s.processId;
    }
    result.cpuUtilisation = result.makespan > 0 ? static_cast<double>(busy) / result.makespan : 0.0;

    // Events were generated in logical order; arrival events may carry an
    // earlier timestamp than the step that discovered them. A stable sort by
    // time keeps logical order among events at the same instant.
    std::stable_sort(result.events.begin(), result.events.end(),
                     [](const StateEvent& a, const StateEvent& b) { return a.time < b.time; });
}

}  // namespace labdesk::os
