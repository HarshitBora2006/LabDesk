// Tests for the OS Simulator (src/ossimulator.*). Plain C++17, no Qt.
// Hand-traced cases are explained next to each check.

#include "ossimulator.h"
#include "scheduler.h"

#include <algorithm>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using namespace labdesk::os;

namespace {
int g_passed = 0;
int g_failed = 0;

void check(bool condition, const std::string& name)
{
    if (condition) {
        ++g_passed;
    } else {
        ++g_failed;
        std::cout << "  FAIL  " << name << "\n";
    }
}

SimProgram prog(const std::string& name, int arrival, int burst, int priority = 0, int memory = 1, int diskAt = -1,
                int diskTime = 0, int printerAt = -1)
{
    return {name, arrival, burst, priority, memory, diskAt, diskTime, printerAt};
}

std::string ganttText(const OsSimulator& s)
{
    std::string out;
    for (int who : s.gantt())
        out += who < 0 ? "-" : s.processes()[static_cast<std::size_t>(who)].program.name;
    return out;
}

void testMatchesScheduler()
{
    // Without memory limits, disk or printer the simulator must give exactly the
    // same schedule as the existing engine (os::Scheduler), for every algorithm.
    std::mt19937 rng(7);
    std::uniform_int_distribution<int> count(1, 10), arrival(0, 15), burst(1, 9), prio(0, 5), quantum(1, 4);
    for (int trial = 0; trial < 400; ++trial) {
        std::vector<Process> procs;
        const int n = count(rng);
        for (int i = 0; i < n; ++i)
            procs.push_back({"P" + std::to_string(i + 1), arrival(rng), burst(rng), prio(rng)});
        const int q = quantum(rng);
        for (Algorithm a : {Algorithm::FCFS, Algorithm::SJF, Algorithm::Priority, Algorithm::RoundRobin}) {
            const ScheduleResult expected = Scheduler::run(a, procs, q);
            OsSimulator sim;
            SimConfig cfg;
            cfg.algorithm = a;
            cfg.quantum = q;
            cfg.memoryBlocks = 16;
            if (!sim.load(OsSimulator::fromProcesses(procs), cfg).empty()) {
                check(false, "load failed in trial " + std::to_string(trial));
                return;
            }
            sim.runToEnd();
            bool same = sim.finished() && sim.contextSwitches() == expected.contextSwitches;
            for (std::size_t i = 0; i < procs.size(); ++i)
                same &= sim.processes()[i].completion == expected.processes[i].completion
                        && sim.processes()[i].readyWait == expected.processes[i].waiting
                        && sim.processes()[i].firstRun == expected.processes[i].firstStart;
            // expand the engine's timeline into one entry per tick and compare
            std::vector<std::string> ticks;
            for (const TimelineSlice& sl : expected.timeline)
                for (int t = sl.start; t < sl.end; ++t)
                    ticks.push_back(sl.idle ? "-" : sl.processId);
            std::vector<std::string> simTicks;
            for (int who : sim.gantt())
                simTicks.push_back(who < 0 ? "-" : sim.processes()[static_cast<std::size_t>(who)].program.name);
            same &= ticks == simTicks;
            if (!same) {
                check(false, "simulator differs from os::Scheduler in trial " + std::to_string(trial) + " ("
                                 + algorithmName(a) + ")");
                return;
            }
        }
    }
    check(true, "400 random workloads x 4 algorithms: simulator = os::Scheduler (completion, waiting, first run, "
                "Gantt, context switches)");
}

void testDisk()
{
    // A needs 4 units and asks the disk after 2 for 3 ticks; B needs 3. FCFS.
    // t0-1 A runs | t2 A -> WAITING(disk), B runs t2-4 (done at 5) | t5 A back, runs t5-6 (done at 7)
    OsSimulator sim;
    sim.load({prog("A", 0, 4, 0, 1, 2, 3), prog("B", 0, 3)}, SimConfig{});
    sim.runToEnd();
    check(ganttText(sim) == "AABBBAA", "disk: Gantt AABBBAA (got " + ganttText(sim) + ")");
    const SimProcess& a = sim.processes()[0];
    const SimProcess& b = sim.processes()[1];
    check(a.completion == 7 && a.diskWait == 3 && a.readyWait == 0, "disk: A finishes at 7 after 3 ticks on the disk");
    check(b.completion == 5 && b.readyWait == 2, "disk: B uses the CPU while A waits; B waited 2 in READY");
    check(sim.cpuUtilisation() == 1.0, "disk: CPU never idle because B filled the gap");

    OsSimulator alone;
    alone.load({prog("A", 0, 4, 0, 1, 2, 3)}, SimConfig{});
    alone.runToEnd();
    check(ganttText(alone) == "AA---AA", "disk alone: CPU idle while A waits for the disk (got " + ganttText(alone) + ")");
}

void testMemory()
{
    // 4 blocks. A needs 3 (2 units), B needs 2 (1 unit): only 1 block left, B waits in NEW.
    SimConfig cfg;
    cfg.memoryBlocks = 4;
    OsSimulator sim;
    sim.load({prog("A", 0, 2, 0, 3), prog("B", 0, 1, 0, 2)}, cfg);
    sim.step();
    check(sim.memory() == std::vector<int>({0, 0, 0, -1}), "memory: A holds blocks 0-2 (first fit)");
    check(sim.processes()[1].state == SimState::New, "memory: B stays NEW without 2 free blocks");
    sim.runToEnd();
    check(sim.processes()[1].completion == 3 && sim.processes()[1].memoryWait == 2,
          "memory: B loaded when A ends, finishes at 3 after waiting 2 for memory");
    check(std::all_of(sim.memory().begin(), sim.memory().end(), [](int b) { return b == -1; }),
          "memory: all blocks free at the end");
}

void testPrinter()
{
    // Round Robin q=1. A locks the printer at once. At t1 B is dispatched, needs the
    // printer, waits; A continues t1-2 and finishes at 3, handing the printer to B.
    SimConfig cfg;
    cfg.algorithm = Algorithm::RoundRobin;
    cfg.quantum = 1;
    OsSimulator sim;
    sim.load({prog("A", 0, 3, 0, 1, -1, 0, 0), prog("B", 0, 2, 0, 1, -1, 0, 0)}, cfg);
    sim.step();
    check(sim.printerHolder() == 0, "printer: A locks it");
    sim.step();
    check(sim.processes()[1].state == SimState::WaitingPrinter, "printer: B waits while A holds it");
    sim.runToEnd();
    check(ganttText(sim) == "AAABB", "printer: Gantt AAABB (got " + ganttText(sim) + ")");
    const SimProcess& b = sim.processes()[1];
    check(b.completion == 5 && b.printerWait == 2 && b.readyWait == 1, "printer: B waited 1 ready + 2 for the printer");
    check(sim.printerHolder() == -1, "printer: free at the end");
}

void testValidation()
{
    OsSimulator sim;
    check(!sim.load({}, SimConfig{}).empty(), "no programs rejected");
    check(!sim.load({prog("A", 0, 3, 0, 1, 3, 2)}, SimConfig{}).empty(), "disk request at the last unit rejected");
    check(!sim.load({prog("A", 0, 3, 0, 9)}, SimConfig{}).empty(), "memory larger than the machine rejected");
    check(!sim.load({prog("A", 0, 3), prog("A", 1, 2)}, SimConfig{}).empty(), "duplicate names rejected");
    SimConfig srtf;
    srtf.algorithm = Algorithm::SRTF;
    check(!sim.load({prog("A", 0, 3)}, srtf).empty(), "SRTF is not offered by the simulator");
    check(sim.load(OsSimulator::example(), SimConfig{}).empty(), "the built-in example is valid");
    sim.runToEnd();
    check(sim.finished(), "the built-in example runs to the end");
}

void testRandomEverything()
{
    std::mt19937 rng(11);
    std::uniform_int_distribution<int> count(1, 8), arrival(0, 10), burst(2, 8), prio(0, 4), mem(1, 4), coin(0, 2),
        dtime(1, 4), algo(0, 3), quantum(1, 3);
    const Algorithm algos[] = {Algorithm::FCFS, Algorithm::SJF, Algorithm::Priority, Algorithm::RoundRobin};
    for (int trial = 0; trial < 500; ++trial) {
        std::vector<SimProgram> programs;
        const int n = count(rng);
        for (int i = 0; i < n; ++i) {
            SimProgram p = prog("p" + std::to_string(i), arrival(rng), burst(rng), prio(rng), mem(rng));
            if (coin(rng) == 0) {
                p.diskAt = std::uniform_int_distribution<int>(1, p.burst - 1)(rng);
                p.diskTime = dtime(rng);
            }
            if (coin(rng) == 0)
                p.printerAt = std::uniform_int_distribution<int>(0, p.burst - 1)(rng);
            programs.push_back(p);
        }
        SimConfig cfg;
        cfg.algorithm = algos[algo(rng)];
        cfg.quantum = quantum(rng);
        cfg.memoryBlocks = 6;
        OsSimulator sim;
        if (!sim.load(programs, cfg).empty()) {
            check(false, "random load failed " + std::to_string(trial));
            return;
        }
        bool ok = true;
        int ticks = 0;
        while (sim.step() && ticks++ < 10000) {
            int holders = 0;
            for (const SimProcess& p : sim.processes())
                holders += p.holdsPrinter ? 1 : 0;
            ok &= holders <= 1;
            for (std::size_t i = 0; i < sim.processes().size(); ++i) {
                const auto used = std::count(sim.memory().begin(), sim.memory().end(), static_cast<int>(i));
                const SimProcess& p = sim.processes()[i];
                ok &= used == 0 || used == p.program.memory;
                ok &= (p.state == SimState::New || p.state == SimState::Terminated) ? used == 0 : used == p.program.memory;
            }
        }
        ok &= sim.finished();
        for (std::size_t i = 0; i < sim.processes().size(); ++i) {
            const SimProcess& p = sim.processes()[i];
            ok &= p.completion - p.program.arrival
                  == p.program.burst + p.readyWait + p.diskWait + p.printerWait + p.memoryWait;
            ok &= std::count(sim.gantt().begin(), sim.gantt().end(), static_cast<int>(i)) == p.program.burst;
        }
        if (!ok) {
            check(false, "random simulation invariant broken in trial " + std::to_string(trial));
            return;
        }
    }
    check(true, "500 random runs with memory, disk and printer: all finish; turnaround = CPU + ready + disk + "
                "printer + memory waits; memory never over-allocated; at most one printer holder");
}
}  // namespace

int main()
{
    std::cout << "LabDesk OS simulator tests\n";
    testMatchesScheduler();
    testDisk();
    testMemory();
    testPrinter();
    testValidation();
    testRandomEverything();
    std::cout << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
