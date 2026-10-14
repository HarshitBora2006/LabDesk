// Unit tests for the lab queue: dispatch order, measured metrics and the
// what-if replay. Plain C++17, no Qt. Expected values are hand-calculated.

#include "labqueue.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace labdesk::lab;

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

void testCodesAndPriorities()
{
    check(priorityFor(Purpose::Exam) == 1 && priorityFor(Purpose::Assignment) == 2
              && priorityFor(Purpose::Practice) == 3,
          "purpose priorities: exam 1, assignment 2, practice 3");
    for (Policy p : {Policy::FCFS, Policy::SJF, Policy::Priority})
        check(policyFromCode(policyCode(p)) == p, std::string("policy code round trip ") + policyCode(p));
    for (Purpose p : {Purpose::Exam, Purpose::Assignment, Purpose::Practice})
        check(purposeFromCode(purposeCode(p)) == p, std::string("purpose code round trip ") + purposeCode(p));
    check(!policyFromCode("RR").has_value() && !purposeFromCode("GAMING").has_value(), "unknown codes rejected");
}

void testDispatchOrder()
{
    // A: arrived first, 90 min, practice.  B: 30 min, assignment.  C: 60 min, exam.
    const std::vector<WaitingRequest> q = {{1, 0, 90, 3}, {2, 10, 30, 2}, {3, 20, 60, 1}};
    check(selectNext(Policy::FCFS, q) == 0, "FCFS serves the earliest arrival (A)");
    check(selectNext(Policy::SJF, q) == 1, "SJF serves the shortest expected length (B)");
    check(selectNext(Policy::Priority, q) == 2, "Priority serves the exam request (C)");
    check(dispatchOrder(Policy::SJF, q) == std::vector<std::size_t>({1, 2, 0}), "SJF queue order B, C, A");
    check(dispatchOrder(Policy::Priority, q) == std::vector<std::size_t>({2, 1, 0}), "Priority queue order C, B, A");
    // Ties: equal expected length / equal priority -> earlier arrival, then lower id.
    const std::vector<WaitingRequest> ties = {{7, 50, 30, 2}, {5, 50, 30, 2}, {9, 40, 30, 2}};
    check(dispatchOrder(Policy::SJF, ties) == std::vector<std::size_t>({2, 1, 0}), "SJF ties: arrival, then id");
    check(dispatchOrder(Policy::Priority, ties) == std::vector<std::size_t>({2, 1, 0}), "Priority ties: arrival, then id");
    check(selectNext(Policy::FCFS, {}) == -1, "empty queue: nothing to serve");
}

void testMeasure()
{
    // Requested at 0, started at 10 min, ended at 60 min, expected 45 min.
    MeasuredVisit v;
    v.arrivalMs = 0;
    v.startMs = 10 * 60000LL;
    v.endMs = 60 * 60000LL;
    v.expectedMinutes = 45;
    const VisitMetrics m = measure(v, 0);
    check(m.started && m.finished, "finished visit");
    check(near(m.waitingMinutes, 10) && near(m.burstMinutes, 50) && near(m.turnaroundMinutes, 60),
          "waiting 10, burst 50, turnaround 60 minutes");
    check(near(m.estimateErrorMinutes, 5), "estimate error +5 minutes (used 50, expected 45)");

    MeasuredVisit waiting;
    waiting.arrivalMs = 0;
    waiting.expectedMinutes = 30;
    const VisitMetrics w = measure(waiting, 7 * 60000LL);
    check(!w.started && near(w.waitingMinutes, 7), "still waiting: 7 minutes so far");

    MeasuredVisit running = v;
    running.endMs.reset();
    const VisitMetrics r = measure(running, 0);
    check(r.started && !r.finished && near(r.waitingMinutes, 10), "running visit: waiting known, burst not yet");

    const MeasuredSummary s = summarise({v, waiting, running});
    check(s.completed == 1 && near(s.averageBurst, 50) && near(s.averageWaiting, 10),
          "summary averages only finished visits");
    check(summarise({}).completed == 0, "empty summary");
}

std::vector<ReplayJob> threeJobs()
{
    // Times in minutes, converted to seconds.  J1 (0, 60, exp 60, pr 3)
    // J2 (10, 30, exp 30, pr 1)  J3 (20, 10, exp 10, pr 2)
    return {{1, 0, 60 * 60, 60, 3}, {2, 10 * 60, 30 * 60, 30, 1}, {3, 20 * 60, 10 * 60, 10, 2}};
}

void testReplayOneComputer()
{
    const auto jobs = threeJobs();
    // FCFS: J1 0-60, J2 60-90, J3 90-100. Waits 0, 50, 70 -> 40. TAT 60, 80, 80 -> 73.33
    const ReplayResult f = replay(Policy::FCFS, jobs, 1);
    check(f.startSeconds == std::vector<long long>({0, 3600, 5400}), "replay FCFS starts 0, 60, 90 min");
    check(near(f.averageWaitingMinutes, 40) && near(f.averageTurnaroundMinutes, 220.0 / 3), "replay FCFS averages");
    check(near(f.maximumWaitingMinutes, 70) && f.finishSeconds == 6000, "replay FCFS max wait 70, finish 100 min");
    // SJF: at 60 J3 (10) beats J2 (30): J3 60-70, J2 70-100. Waits 0, 60, 40 -> 33.33
    const ReplayResult s = replay(Policy::SJF, jobs, 1);
    check(s.startSeconds == std::vector<long long>({0, 4200, 3600}), "replay SJF runs J3 before J2");
    check(near(s.averageWaitingMinutes, 100.0 / 3), "replay SJF average waiting 33.33");
    // Priority: J2 (priority 1) before J3 (priority 2): same as FCFS here -> 40
    const ReplayResult p = replay(Policy::Priority, jobs, 1);
    check(p.startSeconds == std::vector<long long>({0, 3600, 5400}), "replay Priority runs J2 (exam) first");
    check(near(p.averageWaitingMinutes, 40), "replay Priority average waiting 40");
}

void testReplayTwoComputers()
{
    // FCFS on 2 computers: J1 c1 0-60, J2 c2 10-40, J3 waits until 40 -> 40-50. Waits 0, 0, 20 -> 6.67
    const ReplayResult r = replay(Policy::FCFS, threeJobs(), 2);
    check(r.startSeconds == std::vector<long long>({0, 600, 2400}), "replay 2 computers: J3 starts at 40 min");
    check(near(r.averageWaitingMinutes, 20.0 / 3), "replay 2 computers average waiting 6.67");
    const ReplayResult many = replay(Policy::SJF, threeJobs(), 5);
    check(near(many.averageWaitingMinutes, 0), "enough computers: nobody waits");
}

void testSjfUsesEstimateNotActual()
{
    // At t=60 J2 (expected 5, actually 30) and J3 (expected 50, actually 10) wait.
    // A real dispatcher only knows the estimate, so SJF must pick J2.
    const std::vector<ReplayJob> jobs = {{1, 0, 3600, 60, 3}, {2, 600, 1800, 5, 3}, {3, 1200, 600, 50, 3}};
    const ReplayResult r = replay(Policy::SJF, jobs, 1);
    check(r.startSeconds[1] == 3600 && r.startSeconds[2] == 5400, "SJF decides on the expected length, not the actual");
}

void testReplayValidation()
{
    bool threw = false;
    try {
        replay(Policy::FCFS, threeJobs(), 0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "replay rejects 0 computers");
    threw = false;
    try {
        replay(Policy::FCFS, {{1, 0, 0, 10, 3}}, 1);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "replay rejects a zero-length visit");
    check(replay(Policy::FCFS, {}, 3).finishSeconds == 0, "replay of no visits");
}

void testReplayProperties()
{
    std::mt19937 rng(20261014);
    std::uniform_int_distribution<int> count(1, 25), arrive(0, 300), length(1, 120), expect(5, 120), pr(1, 3),
        pcs(1, 6);
    for (int trial = 0; trial < 300; ++trial) {
        std::vector<ReplayJob> jobs;
        const int n = count(rng);
        for (int i = 0; i < n; ++i)
            jobs.push_back({i + 1, arrive(rng) * 60LL, length(rng) * 60LL, expect(rng), pr(rng)});
        const int computers = pcs(rng);
        for (Policy policy : {Policy::FCFS, Policy::SJF, Policy::Priority}) {
            const ReplayResult r = replay(policy, jobs, computers);
            bool ok = true;
            std::vector<std::pair<long long, int>> sweep;
            for (int i = 0; i < n; ++i) {
                ok &= r.startSeconds[i] >= jobs[i].arrivalSeconds;
                ok &= r.completionSeconds[i] == r.startSeconds[i] + jobs[i].burstSeconds;
                sweep.push_back({r.startSeconds[i], +1});
                sweep.push_back({r.completionSeconds[i], -1});
            }
            std::sort(sweep.begin(), sweep.end());  // ends (-1) sort before starts at the same instant
            int running = 0;
            for (const auto& e : sweep) {
                running += e.second;
                ok &= running <= computers;
            }
            // Work-conserving: if a visit waited, every computer was busy when it arrived.
            for (int i = 0; i < n && ok; ++i) {
                if (r.startSeconds[i] == jobs[i].arrivalSeconds)
                    continue;
                int busy = 0;
                for (int k = 0; k < n; ++k)
                    if (r.startSeconds[k] <= jobs[i].arrivalSeconds && r.completionSeconds[k] > jobs[i].arrivalSeconds)
                        ++busy;
                ok &= busy == computers;
            }
            if (!ok) {
                check(false, "replay property trial " + std::to_string(trial) + " " + policyCode(policy));
                return;
            }
        }
    }
    check(true, "replay properties (300 random labs x 3 policies): no early start, never more visits than "
                "computers, nobody waits while a computer is free");
}

void testToWorkload()
{
    // Arrivals at t0, t0 + 10 min, t0 + 18 min 30 s, t0 + 20 min (given out of order).
    const long long t0 = 1'000'000'000'000LL;
    std::vector<FinishedVisit> visits = {
        {3, t0 + 600000, t0 + 600000, t0 + 600000 + 61 * 60000, 3},   // 61 min (a real session)
        {1, t0, t0, t0 + 147000, 1},                                   // 2 min 27 s
        {2, t0 + 1110000, t0 + 1110000, t0 + 1114400, 3},              // 4.4 s (a quick test)
        {4, t0 + 1200000, t0 + 1200000, t0 + 1200000 + 7200000, 2},   // 2 hours
    };
    // Minutes: readable numbers for real sessions.
    const auto m = toWorkload(visits, 100, TimeUnit::Minutes);
    check(m.size() == 4 && m[0].requestId == 1 && m[1].requestId == 3 && m[2].requestId == 2 && m[3].requestId == 4,
          "workload rows in arrival order");
    check(m[0].arrival == 0 && m[1].arrival == 10 && m[2].arrival == 19 && m[3].arrival == 20,
          "minutes: arrival rounded to the nearest minute (18.5 -> 19)");
    check(m[1].burst == 61 && m[3].burst == 120, "minutes: real sessions stay readable (61, 120)");
    check(m[0].burst == 2 && m[2].burst == 1, "minutes: 2.45 -> 2, 4.4 s -> 1 (minimum one unit)");
    check(roundingError(m[1], TimeUnit::Minutes) < 0.01 && roundingError(m[3], TimeUnit::Minutes) < 1e-9,
          "minutes: rounding error of real sessions is negligible (<1 %)");
    check(roundingError(m[0], TimeUnit::Minutes) > 0.10 && roundingError(m[2], TimeUnit::Minutes) > 10.0,
          "minutes: short test visits are reported as distorted (>10 %)");
    // Seconds: exact for short visits.
    const auto s = toWorkload(visits, 100, TimeUnit::Seconds);
    check(s[0].burst == 147 && s[2].burst == 4 && s[2].arrival == 1110 && s[3].burst == 7200,
          "seconds: exact values (147, 4, arrival 1110, 7200)");
    check(roundingError(s[0], TimeUnit::Seconds) < 1e-9 && roundingError(s[2], TimeUnit::Seconds) < 0.10,
          "seconds: rounding error within 10 % even for a 4.4 s visit");
    check(s[0].priority == 1 && s[2].priority == 3, "priority copied from the request");
    const auto latest = toWorkload(visits, 2, TimeUnit::Seconds);
    check(latest.size() == 2 && latest[0].requestId == 2 && latest[0].arrival == 0 && latest[1].arrival == 90,
          "limit keeps the most recent visits and re-bases arrival on the earliest kept");
    check(toWorkload({}, 10, TimeUnit::Minutes).empty(), "no finished visits: empty workload");
}

}  // namespace

int main()
{
    std::cout << "LabDesk lab queue tests\n";
    testCodesAndPriorities();
    testDispatchOrder();
    testMeasure();
    testReplayOneComputer();
    testReplayTwoComputers();
    testSjfUsesEstimateNotActual();
    testReplayValidation();
    testReplayProperties();
    testToWorkload();
    std::cout << g_passed << " passed, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
