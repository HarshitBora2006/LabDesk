// Tests for the process model (PID, name, priority, burst, run-one-unit). Plain C++17.

#include "processstate.h"

#include <iostream>
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

void testProcessManager()
{
    LifecycleSimulator sim;
    std::string err;
    const int chrome = sim.addProcess("chrome.exe", 1, 3, &err);
    const int python = sim.addProcess("python.exe", 2, 2, &err);
    check(chrome == 1001 && python == 1002, "PIDs start at 1001 and increase");
    check(sim.addProcess("", 1, 3, &err) < 0 && sim.addProcess("x.exe", 100, 3, &err) < 0
              && sim.addProcess("x.exe", 1, 0, &err) < 0,
          "invalid name, priority or burst rejected");
    const SimulatedProcess* c = sim.find("1001");
    check(c && c->name == "chrome.exe" && c->state == ProcessState::New && c->remaining == 3 && c->arrival == 0,
          "new process: NEW, remaining = burst, arrival = clock");
    check(!sim.runTick(&err) && err.find("idle") != std::string::npos, "running a unit with an idle CPU is refused");
    sim.apply("1001", ProcessState::Ready);
    sim.apply("1001", ProcessState::Running);
    check(sim.runTick(&err) && sim.find("1001")->cpuTime == 1 && sim.find("1001")->remaining == 2, "one unit executed");
    sim.apply("1001", ProcessState::Waiting);  // blocks for I/O
    check(!sim.runTick(&err), "a WAITING process does not execute");
    sim.apply("1001", ProcessState::Ready);
    sim.apply("1001", ProcessState::Running);
    sim.runTick(&err);
    sim.runTick(&err);
    check(sim.find("1001")->state == ProcessState::Terminated && sim.find("1001")->remaining == 0
              && sim.find("1001")->cpuTime == 3,
          "process terminates automatically when its burst completes");
    check(sim.events().back().reason == "Burst complete", "automatic termination is logged");
    sim.apply("1002", ProcessState::Ready);
    sim.apply("1002", ProcessState::Running);
    check(sim.apply("1002", ProcessState::Terminated, &err) && sim.find("1002")->remaining == 2,
          "a running process can be terminated early; remaining time is kept for the record");
    check(sim.events().back().reason.find("before its burst finished") != std::string::npos,
          "early termination is logged as such");
    // Names copied from a schedule still work and get PIDs.
    check(sim.createProcess("R7", &err) && sim.find("R7")->pid == 1003, "processes copied from a schedule get PIDs");
    sim.reset();
    check(sim.addProcess("notepad.exe", 3, 1, &err) == 1001, "reset restarts PIDs at 1001");
}

}  // namespace

int main()
{
    std::cout << "LabDesk process model tests\n";
    testProcessManager();
    std::cout << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
