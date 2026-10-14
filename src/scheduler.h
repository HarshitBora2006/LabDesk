#pragma once
// CPU scheduling engine: FCFS, SJF (non-preemptive), SRTF (preemptive SJF),
// Priority (non-preemptive) and Round Robin. Pure C++17, independent of Qt.
//
// Conventions (documented in README.md, tested in tests/scheduler_tests.cpp):
//  * Time is discrete integer units starting at 0.
//  * Priority: LOWER number = HIGHER priority, valid range 0..99.
//  * Ties are broken by earlier arrival, then by input order.
//  * If no process has arrived, the CPU is idle until the next arrival
//    (recorded as an idle slice in the timeline).
//  * SRTF: whenever a process arrives, the ready process with the shortest
//    REMAINING time runs. The running process is preempted only by a process
//    with strictly shorter remaining time (equal remaining time: no switch).
//  * Round Robin: processes that arrive at time t are enqueued BEFORE a
//    process preempted at the same time t. If the ready queue is empty when
//    a quantum expires, the current process simply continues (no switch).
//
// Metrics:  TAT = CT - AT,   WT = TAT - BT,   RT = first start - AT

#include "processstate.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace labdesk::os {

inline constexpr int kMinPriority = 0;
inline constexpr int kMaxPriority = 99;
// Limits allow second-level real data (measured lab visits): up to one year of
// arrivals and one day per burst, in whatever time unit the workload uses.
inline constexpr int kMaxArrival = 31'536'000;
inline constexpr int kMaxBurst = 86'400;
inline constexpr int kMaxQuantum = 86'400;
// Round Robin safety limit: total burst / quantum may not exceed this many slices.
inline constexpr long long kMaxRoundRobinSlices = 50'000;
inline constexpr std::size_t kMaxProcesses = 100;

enum class Algorithm { FCFS, SJF, SRTF, Priority, RoundRobin };

std::string algorithmName(Algorithm algorithm);
bool isPreemptive(Algorithm algorithm);
// Short plain-language description used by the GUI.
std::string algorithmDescription(Algorithm algorithm);

struct Process {
    std::string id;
    int arrival = 0;
    int burst = 1;
    int priority = 0;
};

struct ProcessResult {
    Process process;
    int firstStart = -1;
    int completion = 0;
    int turnaround = 0;
    int waiting = 0;
    int response = 0;
};

struct TimelineSlice {
    std::string processId;  // empty when idle
    int start = 0;
    int end = 0;
    bool idle = false;
};

struct ScheduleResult {
    Algorithm algorithm = Algorithm::FCFS;
    int quantum = 0;                        // only meaningful for Round Robin
    std::vector<ProcessResult> processes;   // same order as the input
    std::vector<TimelineSlice> timeline;    // chronological Gantt chart
    std::vector<StateEvent> events;         // NEW->READY->RUNNING->... trace
    double averageWaiting = 0.0;
    double averageTurnaround = 0.0;
    double averageResponse = 0.0;
    int makespan = 0;          // completion time of the last process
    int idleTime = 0;
    int contextSwitches = 0;   // switches from one process to a different one
    double cpuUtilisation = 0.0;  // busy time / makespan, in [0, 1]
};

// Returns human-readable problems; empty vector means the input is valid.
std::vector<std::string> validateProcesses(const std::vector<Process>& processes);
std::string validateQuantum(int quantum);

class Scheduler {
public:
    // Throws std::invalid_argument when the input is invalid.
    static ScheduleResult run(Algorithm algorithm, const std::vector<Process>& processes,
                              int quantum = 2);

private:
    static ScheduleResult runNonPreemptive(Algorithm algorithm, const std::vector<Process>& processes);
    static ScheduleResult runShortestRemaining(const std::vector<Process>& processes);
    static ScheduleResult runRoundRobin(const std::vector<Process>& processes, int quantum);
    static void finalise(ScheduleResult& result);
};

}  // namespace labdesk::os
