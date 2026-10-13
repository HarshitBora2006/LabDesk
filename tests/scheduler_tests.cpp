// Unit tests for the scheduling engine and the process-state model.
// No external test framework: plain C++17 so it builds anywhere (CTest runs it).
//
// Every expected value below was calculated BY HAND before running the code;
// the derivations are written next to each case so they can be re-checked.

#include "processstate.h"
#include "scheduler.h"

#include <cmath>
#include <iostream>
#include <map>
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

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

const ProcessResult& byId(const ScheduleResult& r, const std::string& id)
{
    for (const auto& p : r.processes)
        if (p.process.id == id)
            return p;
    throw std::runtime_error("missing process " + id);
}

struct Expected {
    std::string id;
    int ct, tat, wt, firstStart;
};

void expectMetrics(const ScheduleResult& r, const std::vector<Expected>& expected, const std::string& tag)
{
    for (const auto& e : expected) {
        const auto& p = byId(r, e.id);
        check(p.completion == e.ct, tag + " " + e.id + " CT=" + std::to_string(e.ct)
                                        + " (got " + std::to_string(p.completion) + ")");
        check(p.turnaround == e.tat, tag + " " + e.id + " TAT=" + std::to_string(e.tat)
                                         + " (got " + std::to_string(p.turnaround) + ")");
        check(p.waiting == e.wt, tag + " " + e.id + " WT=" + std::to_string(e.wt)
                                     + " (got " + std::to_string(p.waiting) + ")");
        check(p.firstStart == e.firstStart, tag + " " + e.id + " first start=" + std::to_string(e.firstStart)
                                                + " (got " + std::to_string(p.firstStart) + ")");
    }
}

std::string ganttString(const ScheduleResult& r)
{
    std::string s;
    for (const auto& slice : r.timeline) {
        s += (slice.idle ? std::string("idle") : slice.processId) + "[" + std::to_string(slice.start) + "-"
             + std::to_string(slice.end) + "] ";
    }
    return s;
}

// Set A:   PID  AT  BT  PR
//          P1    0   5   2
//          P2    1   3   3
//          P3    2   8   1
//          P4    3   6   4
std::vector<Process> setA()
{
    return {{"P1", 0, 5, 2}, {"P2", 1, 3, 3}, {"P3", 2, 8, 1}, {"P4", 3, 6, 4}};
}

void testFCFS()
{
    // FCFS: P1 0-5, P2 5-8, P3 8-16, P4 16-22
    // CT 5,8,16,22  TAT 5,7,14,19 (sum 45 -> 11.25)  WT 0,4,6,13 (sum 23 -> 5.75)
    const auto r = Scheduler::run(Algorithm::FCFS, setA());
    expectMetrics(r, {{"P1", 5, 5, 0, 0}, {"P2", 8, 7, 4, 5}, {"P3", 16, 14, 6, 8}, {"P4", 22, 19, 13, 16}},
                  "FCFS");
    check(near(r.averageTurnaround, 11.25), "FCFS avg TAT 11.25");
    check(near(r.averageWaiting, 5.75), "FCFS avg WT 5.75");
    check(ganttString(r) == "P1[0-5] P2[5-8] P3[8-16] P4[16-22] ", "FCFS gantt: " + ganttString(r));
}

void testSJF()
{
    // t=0 only P1 -> P1 0-5. At 5 ready: P2(3) P3(8) P4(6) -> P2 5-8, P4 8-14, P3 14-22
    // CT P1 5, P2 8, P3 22, P4 14   TAT 5,7,20,11 (43 -> 10.75)   WT 0,4,12,5 (21 -> 5.25)
    const auto r = Scheduler::run(Algorithm::SJF, setA());
    expectMetrics(r, {{"P1", 5, 5, 0, 0}, {"P2", 8, 7, 4, 5}, {"P3", 22, 20, 12, 14}, {"P4", 14, 11, 5, 8}},
                  "SJF");
    check(near(r.averageTurnaround, 10.75), "SJF avg TAT 10.75");
    check(near(r.averageWaiting, 5.25), "SJF avg WT 5.25");
}

void testSJFDoesNotRunBeforeArrival()
{
    // P1 is shorter but arrives at 3; at t=0 only P2 exists -> P2 0-10, P1 10-11.
    const auto r = Scheduler::run(Algorithm::SJF, {{"P1", 3, 1, 0}, {"P2", 0, 10, 0}});
    expectMetrics(r, {{"P1", 11, 8, 7, 10}, {"P2", 10, 10, 0, 0}}, "SJF-arrival");
}

void testPriority()
{
    // t=0 only P1 -> P1 0-5. At 5: P2(pr3) P3(pr1) P4(pr4) -> P3 5-13, P2 13-16, P4 16-22
    // CT P1 5, P2 16, P3 13, P4 22   TAT 5,15,11,19 (50 -> 12.5)   WT 0,12,3,13 (28 -> 7.0)
    const auto r = Scheduler::run(Algorithm::Priority, setA());
    expectMetrics(r, {{"P1", 5, 5, 0, 0}, {"P2", 16, 15, 12, 13}, {"P3", 13, 11, 3, 5}, {"P4", 22, 19, 13, 16}},
                  "PRIORITY");
    check(near(r.averageTurnaround, 12.5), "Priority avg TAT 12.5");
    check(near(r.averageWaiting, 7.0), "Priority avg WT 7.0");
}

void testPriorityTieBreak()
{
    // P2 and P3 share priority 2 and arrival 1 -> input order: P2 before P3.
    // P1 0-2, P2 2-5, P3 5-6
    const auto r = Scheduler::run(Algorithm::Priority, {{"P1", 0, 2, 5}, {"P2", 1, 3, 2}, {"P3", 1, 1, 2}});
    check(ganttString(r) == "P1[0-2] P2[2-5] P3[5-6] ", "Priority tie-break by input order: " + ganttString(r));
}

void testRoundRobin()
{
    // q=2. Arrivals at t are queued before the preempted process.
    // P1 0-2 | q:[P2,P3,P1]  P2 2-4 | q:[P3,P1,P4,P2]  P3 4-6  P1 6-8  P4 8-10
    // P2 10-11 (done)  P3 11-13  P1 13-14 (done)  P4 14-16  P3 16-18  P4 18-20 (done)  P3 20-22 (done)
    // CT P1 14, P2 11, P3 22, P4 20  TAT 14,10,20,17 (61 -> 15.25)  WT 9,7,12,11 (39 -> 9.75)
    const auto r = Scheduler::run(Algorithm::RoundRobin, setA(), 2);
    expectMetrics(r, {{"P1", 14, 14, 9, 0}, {"P2", 11, 10, 7, 2}, {"P3", 22, 20, 12, 4}, {"P4", 20, 17, 11, 8}},
                  "RR");
    check(near(r.averageTurnaround, 15.25), "RR avg TAT 15.25");
    check(near(r.averageWaiting, 9.75), "RR avg WT 9.75");
    check(r.timeline.size() == 12, "RR q=2 has 12 slices (got " + std::to_string(r.timeline.size()) + ")");
    check(r.contextSwitches == 11, "RR context switches 11");
}

void testRoundRobinArrivalBeforeRequeue()
{
    // q=2: P1 0-2; P2 arrives at exactly 2 and must run before P1 resumes.
    // P1 0-2, P2 2-4 (CT 4), P1 4-6 (CT 6)
    const auto r = Scheduler::run(Algorithm::RoundRobin, {{"P1", 0, 4, 0}, {"P2", 2, 2, 0}}, 2);
    check(byId(r, "P2").completion == 4, "RR: arrival at quantum boundary queued before preempted process");
    check(byId(r, "P1").completion == 6, "RR: preempted process resumes after new arrival");
}

void testIdleHandling()
{
    // P1 arrives 2 (BT 3), P2 arrives 10 (BT 2): idle 0-2, P1 2-5, idle 5-10, P2 10-12
    const std::vector<Process> procs{{"P1", 2, 3, 2}, {"P2", 10, 2, 1}};
    for (Algorithm a : {Algorithm::FCFS, Algorithm::SJF, Algorithm::Priority, Algorithm::RoundRobin}) {
        const auto r = Scheduler::run(a, procs, 1);
        const std::string tag = algorithmName(a);
        check(ganttString(r) == "idle[0-2] P1[2-5] idle[5-10] P2[10-12] ", tag + " idle gantt: " + ganttString(r));
        check(byId(r, "P1").completion == 5 && byId(r, "P2").completion == 12, tag + " idle CTs");
        check(byId(r, "P1").waiting == 0 && byId(r, "P2").waiting == 0, tag + " idle WT 0");
        check(r.idleTime == 7, tag + " idle time 7");
        check(near(r.cpuUtilisation, 5.0 / 12.0), tag + " utilisation 5/12");
    }
}

void testValidation()
{
    auto throws = [](Algorithm a, std::vector<Process> p, int q) {
        try {
            Scheduler::run(a, p, q);
        } catch (const std::invalid_argument&) {
            return true;
        }
        return false;
    };
    check(throws(Algorithm::FCFS, {}, 2), "empty process list rejected");
    check(throws(Algorithm::FCFS, {{"P1", -1, 3, 1}}, 2), "negative arrival rejected");
    check(throws(Algorithm::FCFS, {{"P1", 0, 0, 1}}, 2), "zero burst rejected");
    check(throws(Algorithm::FCFS, {{"P1", 0, 3, 100}}, 2), "priority > 99 rejected");
    check(throws(Algorithm::FCFS, {{"P1", 0, 3, -1}}, 2), "priority < 0 rejected");
    check(throws(Algorithm::FCFS, {{"P1", 0, 3, 1}, {"P1", 1, 2, 1}}, 2), "duplicate ID rejected");
    check(throws(Algorithm::FCFS, {{"", 0, 3, 1}}, 2), "empty ID rejected");
    check(throws(Algorithm::RoundRobin, {{"P1", 0, 3, 1}}, 0), "RR quantum 0 rejected");
    check(throws(Algorithm::RoundRobin, {{"P1", 0, 3, 1}}, -2), "RR negative quantum rejected");
    check(!throws(Algorithm::FCFS, {{"P1", 0, 3, 1}}, 0), "quantum ignored for non-RR algorithms");
}

// Property checks on random workloads: these do not depend on hand calculations.
void testRandomInvariants()
{
    std::mt19937 rng(20261013);
    std::uniform_int_distribution<int> count(1, 12), arrival(0, 30), burst(1, 15), prio(0, 9), quantum(1, 6);

    for (int trial = 0; trial < 400; ++trial) {
        std::vector<Process> procs;
        const int n = count(rng);
        int totalBurst = 0;
        for (int i = 0; i < n; ++i) {
            procs.push_back({"P" + std::to_string(i + 1), arrival(rng), burst(rng), prio(rng)});
            totalBurst += procs.back().burst;
        }
        for (Algorithm a : {Algorithm::FCFS, Algorithm::SJF, Algorithm::Priority, Algorithm::RoundRobin}) {
            const auto r = Scheduler::run(a, procs, quantum(rng));
            bool ok = true;
            std::map<std::string, int> executed;
            std::map<std::string, int> slices;
            int cursor = 0;
            int busy = 0;
            for (const auto& s : r.timeline) {
                ok &= s.start == cursor && s.end > s.start;  // contiguous, non-empty
                cursor = s.end;
                if (!s.idle) {
                    executed[s.processId] += s.end - s.start;
                    ++slices[s.processId];
                    busy += s.end - s.start;
                }
            }
            ok &= busy == totalBurst;
            ok &= cursor == r.makespan;
            for (const auto& p : r.processes) {
                ok &= executed[p.process.id] == p.process.burst;
                ok &= p.firstStart >= p.process.arrival;
                ok &= p.completion >= p.process.arrival + p.process.burst;
                ok &= p.turnaround == p.completion - p.process.arrival;
                ok &= p.waiting == p.turnaround - p.process.burst;
                ok &= p.waiting >= 0;
                if (a != Algorithm::RoundRobin)
                    ok &= slices[p.process.id] == 1;  // non-preemptive: one contiguous slice
            }
            // Every process's event trace must be a valid walk NEW -> ... -> TERMINATED.
            std::map<std::string, ProcessState> state;
            for (const auto& p : r.processes)
                state[p.process.id] = ProcessState::New;
            int lastTime = 0;
            for (const auto& e : r.events) {
                ok &= e.time >= lastTime;
                lastTime = e.time;
                ok &= state[e.processId] == e.from && isValidTransition(e.from, e.to);
                state[e.processId] = e.to;
            }
            for (const auto& kv : state)
                ok &= kv.second == ProcessState::Terminated;

            if (!ok) {
                check(false, "random invariant trial " + std::to_string(trial) + " " + algorithmName(a));
                return;
            }
        }
    }
    check(true, "random invariants (400 workloads x 4 algorithms)");
}

void testProcessStateMachine()
{
    using S = ProcessState;
    check(isValidTransition(S::New, S::Ready), "NEW->READY valid");
    check(isValidTransition(S::Ready, S::Running), "READY->RUNNING valid");
    check(isValidTransition(S::Running, S::Ready), "RUNNING->READY valid (preempt)");
    check(isValidTransition(S::Running, S::Waiting), "RUNNING->WAITING valid (block)");
    check(isValidTransition(S::Waiting, S::Ready), "WAITING->READY valid (wake)");
    check(isValidTransition(S::Running, S::Terminated), "RUNNING->TERMINATED valid");
    check(!isValidTransition(S::New, S::Running), "NEW->RUNNING invalid");
    check(!isValidTransition(S::Waiting, S::Running), "WAITING->RUNNING invalid");
    check(!isValidTransition(S::Ready, S::Terminated), "READY->TERMINATED invalid");
    check(!isValidTransition(S::Terminated, S::Ready), "TERMINATED is final");

    LifecycleSimulator sim;
    const auto p1 = sim.createProcess();
    const auto p2 = sim.createProcess();
    std::string err;
    check(!sim.apply(p1, S::Running, &err), "simulator rejects NEW->RUNNING");
    check(sim.apply(p1, S::Ready) && sim.apply(p2, S::Ready), "admit both");
    check(sim.apply(p1, S::Running), "dispatch P1");
    check(!sim.apply(p2, S::Running, &err) && err.find("CPU is busy") != std::string::npos,
          "single CPU: cannot dispatch P2 while P1 runs");
    check(sim.apply(p1, S::Waiting), "P1 blocks for I/O");
    check(sim.apply(p2, S::Running), "P2 dispatched while P1 waits");
    check(sim.apply(p1, S::Ready), "P1 I/O completes -> READY");
    check(sim.apply(p2, S::Terminated), "P2 exits");
    check(sim.apply(p1, S::Running) && sim.apply(p1, S::Terminated), "P1 runs and exits");
    check(sim.events().size() == 9 && sim.clock() == 9, "9 transitions logged, clock = 9");
    check(!sim.runningProcess().has_value(), "CPU idle at end");
}

}  // namespace

int main()
{
    std::cout << "LabDesk scheduler tests\n";
    testFCFS();
    testSJF();
    testSJFDoesNotRunBeforeArrival();
    testPriority();
    testPriorityTieBreak();
    testRoundRobin();
    testRoundRobinArrivalBeforeRequeue();
    testIdleHandling();
    testValidation();
    testRandomInvariants();
    testProcessStateMachine();
    std::cout << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
