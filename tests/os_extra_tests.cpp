// Tests for the OS Management additions: Banker's Algorithm and the extended
// process simulator (PID, name, priority, burst, run-one-unit). Plain C++17.

#include "bankers.h"
#include "processstate.h"

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

// Independent checker: does following `sequence` really let every process finish?
bool sequenceWorks(const BankersState& s, const std::vector<int>& sequence)
{
    if (sequence.size() != s.maximum.size())
        return false;
    const Matrix need = needMatrix(s);
    Vector work = s.available;
    std::vector<bool> done(s.maximum.size(), false);
    for (int p : sequence) {
        if (p < 0 || p >= static_cast<int>(done.size()) || done[p])
            return false;
        for (std::size_t j = 0; j < work.size(); ++j)
            if (need[p][j] > work[j])
                return false;
        for (std::size_t j = 0; j < work.size(); ++j)
            work[j] += s.allocation[p][j];
        done[p] = true;
    }
    return true;
}

// Brute force: is there ANY order that lets everyone finish? (small n only)
bool anySafeOrder(const BankersState& s, Vector work, std::vector<bool>& done)
{
    bool all = true;
    const Matrix need = needMatrix(s);
    for (std::size_t i = 0; i < done.size(); ++i) {
        if (done[i])
            continue;
        all = false;
        bool fits = true;
        for (std::size_t j = 0; j < work.size(); ++j)
            fits &= need[i][j] <= work[j];
        if (!fits)
            continue;
        Vector next = work;
        for (std::size_t j = 0; j < work.size(); ++j)
            next[j] += s.allocation[i][j];
        done[i] = true;
        if (anySafeOrder(s, next, done)) {
            done[i] = false;
            return true;
        }
        done[i] = false;
    }
    return all;
}

void testBankersTextbook()
{
    const BankersState s = textbookBankersExample();
    check(validateBankers(s).empty(), "textbook state is valid");
    const Matrix need = needMatrix(s);
    check(need[0] == Vector({7, 4, 3}) && need[1] == Vector({1, 2, 2}) && need[2] == Vector({6, 0, 0})
              && need[3] == Vector({0, 1, 1}) && need[4] == Vector({4, 3, 1}),
          "need = max - allocation (textbook values)");
    // Lowest index first, by hand: work (3,3,2): P1 -> (5,3,2); P3 -> (7,4,3); P0 -> (7,5,3);
    // P2 -> (10,5,5); P4 -> (10,5,7).
    const SafetyResult r = checkSafety(s);
    check(r.safe, "textbook state is SAFE");
    check(r.sequence == std::vector<int>({1, 3, 0, 2, 4}), "safe sequence P1, P3, P0, P2, P4");
    check(r.steps.back().workAfter == Vector({10, 5, 7}), "final work = total resources (10, 5, 7)");
    check(sequenceWorks(s, r.sequence), "independent check: the sequence really lets every process finish");

    // Textbook request 1: P1 asks (1,0,2) -> granted (state stays safe).
    const RequestResult g = requestResources(s, 1, {1, 0, 2});
    check(g.outcome == RequestOutcome::Granted, "P1 request (1,0,2) granted");
    check(g.state.available == Vector({2, 3, 0}) && g.state.allocation[1] == Vector({3, 0, 2}),
          "after grant: available (2,3,0), P1 holds (3,0,2)");
    // Then P4 asks (3,3,0): more than available (2,3,0) -> must wait.
    check(requestResources(g.state, 4, {3, 3, 0}).outcome == RequestOutcome::MustWait, "P4 (3,3,0) must wait");
    // Then P0 asks (0,2,0): available, but the result is unsafe -> denied, rolled back.
    const RequestResult d = requestResources(g.state, 0, {0, 2, 0});
    check(d.outcome == RequestOutcome::Denied && d.state.available == g.state.available,
          "P0 (0,2,0) denied as unsafe and rolled back");
    check(requestResources(s, 1, {2, 0, 0}).outcome == RequestOutcome::Invalid,
          "request above the remaining need is invalid");
}

void testBankersValidationAndRelease()
{
    BankersState bad = textbookBankersExample();
    bad.allocation[0] = {8, 1, 0};
    check(!validateBankers(bad).empty(), "allocation above maximum rejected");
    bad = textbookBankersExample();
    bad.maximum[2] = {9, 0};
    check(!validateBankers(bad).empty(), "row with wrong length rejected");
    const BankersState s = textbookBankersExample();
    const RequestResult rel = releaseResources(s, 2, {3, 0, 2});
    check(rel.outcome == RequestOutcome::Granted && rel.state.available == Vector({6, 3, 4}), "release returns resources");
    check(releaseResources(s, 0, {1, 0, 0}).outcome == RequestOutcome::Invalid, "cannot release more than held");
    // Unsafe example: P0 needs 2 more of the only resource, P1 needs 2 more, 1 available.
    BankersState u;
    u.available = {1};
    u.maximum = {{3}, {3}};
    u.allocation = {{1}, {1}};
    const SafetyResult ur = checkSafety(u);
    check(!ur.safe && ur.unfinished.size() == 2, "unsafe state detected; both processes unfinished");
}

void testBankersRandom()
{
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> np(1, 5), nr(1, 3), v(0, 6);
    for (int trial = 0; trial < 500; ++trial) {
        BankersState s;
        const int n = np(rng), m = nr(rng);
        s.available.resize(m);
        for (int& a : s.available)
            a = v(rng);
        for (int i = 0; i < n; ++i) {
            Vector max(m), alloc(m);
            for (int j = 0; j < m; ++j) {
                max[j] = v(rng);
                alloc[j] = std::uniform_int_distribution<int>(0, max[j])(rng);
            }
            s.maximum.push_back(max);
            s.allocation.push_back(alloc);
        }
        const SafetyResult r = checkSafety(s);
        std::vector<bool> done(n, false);
        const bool truth = anySafeOrder(s, s.available, done);
        if (r.safe != truth || (r.safe && !sequenceWorks(s, r.sequence))) {
            check(false, "random Banker's trial " + std::to_string(trial));
            return;
        }
    }
    check(true, "500 random states: safe exactly when a brute-force search finds a safe order");
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
    std::cout << "LabDesk OS module tests (Banker's Algorithm, process management)\n";
    testBankersTextbook();
    testBankersValidationAndRelease();
    testBankersRandom();
    testProcessManager();
    std::cout << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
