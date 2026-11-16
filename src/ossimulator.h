#pragma once
// OS Simulator: one simulated lab computer running a few programs, advanced
// one clock tick at a time. Pure C++17 (no Qt), tested in tests/ossimulator_tests.cpp.
//
// Concepts on one screen:
//   process and PCB (PID, state, time left)   process states NEW..TERMINATED
//   CPU scheduling: FCFS, SJF, Priority, RR    context switches
//   I/O: a program can block for the disk      memory: first-fit contiguous blocks
//   mutual exclusion: one shared printer
//
// One tick is the interval [t, t+1). At the start of a tick:
//   1. programs whose disk I/O has finished go back to READY
//   2. programs that have arrived get memory (first fit) and join READY;
//      without enough free memory they stay NEW and try again next tick
//   3. Round Robin: a program that used its whole quantum goes to the back of
//      READY if another program is waiting (otherwise it just continues)
//   4. if the CPU is free, the scheduler dispatches a READY program
//   5. the running program asks for the disk or the printer at its chosen
//      moment; otherwise it executes one unit. At 0 units left it terminates
//      and releases its memory and the printer.
// With 1 memory block per program, no disk and no printer, the results are
// identical to os::Scheduler (checked by the tests).

#include "scheduler.h"

#include <deque>
#include <string>
#include <vector>

namespace labdesk::os {

struct SimProgram {
    std::string name;
    int arrival = 0;
    int burst = 1;       // CPU time units needed
    int priority = 0;    // lower number = higher priority
    int memory = 1;      // memory blocks needed
    int diskAt = -1;     // after this many CPU units it asks for the disk (-1 = never)
    int diskTime = 0;    // how long the disk takes
    int printerAt = -1;  // after this many CPU units it needs the printer until it ends (-1 = never)
};

enum class SimState { New, Ready, Running, WaitingDisk, WaitingPrinter, Terminated };
const char* simStateName(SimState state);    // NEW, READY, RUNNING, WAITING, TERMINATED
std::string simStateDetail(SimState state);  // adds "(disk)" or "(printer)" for WAITING

struct SimProcess {
    SimProgram program;
    int pid = 0;
    SimState state = SimState::New;
    int cpuUsed = 0;
    int remaining = 0;
    bool diskDone = false;
    int diskLeft = 0;
    bool holdsPrinter = false;
    int memoryStart = -1;
    int firstRun = -1;
    int completion = -1;
    // Time spent in each situation. Turnaround = burst + these four.
    int readyWait = 0;
    int diskWait = 0;
    int printerWait = 0;
    int memoryWait = 0;
};

struct SimEvent {
    int time = 0;
    int pid = 0;
    std::string text;
};

struct SimConfig {
    Algorithm algorithm = Algorithm::FCFS;  // FCFS, SJF, Priority or RoundRobin
    int quantum = 2;
    int memoryBlocks = 8;
};

class OsSimulator {
public:
    // Validates and loads; returns problems (empty = loaded, clock at 0).
    std::vector<std::string> load(const std::vector<SimProgram>& programs, const SimConfig& config);

    bool step();  // one tick; false if everything had already finished
    void runToEnd(int maxTicks = 100000);
    bool finished() const;
    bool loaded() const { return !m_processes.empty(); }

    int time() const { return m_time; }
    const SimConfig& config() const { return m_config; }
    const std::vector<SimProcess>& processes() const { return m_processes; }
    int running() const { return m_running; }                    // index or -1
    const std::deque<int>& readyQueue() const { return m_ready; }
    const std::vector<int>& memory() const { return m_memory; }  // owner index per block, -1 = free
    int printerHolder() const { return m_printerHolder; }        // index or -1
    const std::deque<int>& printerQueue() const { return m_printerQueue; }
    const std::vector<int>& gantt() const { return m_gantt; }    // index per tick, -1 = idle
    const std::vector<SimEvent>& events() const { return m_events; }
    int contextSwitches() const;  // changes of the executing program (idle ignored)
    double cpuUtilisation() const;
    double averageTurnaround() const;
    double averageReadyWait() const;

    static std::vector<SimProgram> fromProcesses(const std::vector<Process>& processes);
    static std::vector<SimProgram> example();

private:
    void log(int time, int index, const std::string& text);
    bool allocateMemory(int index);
    void releaseMemory(int index);
    int pickNext() const;  // position in m_ready, -1 if empty
    void terminate(int index);

    SimConfig m_config;
    std::vector<SimProcess> m_processes;
    std::deque<int> m_ready;
    std::vector<int> m_memory;
    std::deque<int> m_printerQueue;
    std::vector<int> m_gantt;
    std::vector<SimEvent> m_events;
    std::vector<bool> m_memoryNoticeShown;
    int m_time = 0;
    int m_running = -1;
    int m_sliceUsed = 0;
    int m_printerHolder = -1;
    int m_lastRun = -1;
};

}  // namespace labdesk::os
