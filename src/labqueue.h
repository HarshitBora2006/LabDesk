#pragma once
// Lab queue: real arrival / waiting / burst data for computer requests.
//
// Analogy used throughout (state it in reports):
//   student request = job, lab computer = processor, session = execution.
//   arrival   = time the student requested a computer          (recorded)
//   start     = time a computer was assigned                   (recorded)
//   burst     = actual session length, end - start             (measured when the session ends)
//   waiting   = start - arrival;  turnaround = end - arrival;  response = waiting (non-preemptive)
//   priority  = from purpose: EXAM 1, ASSIGNMENT 2, PRACTICE 3 (lower number = higher priority)
//   expected  = length the student predicts when requesting; SJF must decide with
//               this estimate because the real burst is unknown until the session ends.
//
// Sessions are never interrupted, so all policies here are NON-preemptive.
// Pure C++17 (no Qt) so the decision logic can be unit-tested.

#include <optional>
#include <string>
#include <vector>

namespace labdesk::lab {

enum class Policy { FCFS, SJF, Priority };
enum class Purpose { Exam, Assignment, Practice };

inline constexpr int kMinExpectedMinutes = 5;
inline constexpr int kMaxExpectedMinutes = 480;

const char* policyCode(Policy policy);                        // "FCFS", "SJF", "PRIORITY"
std::optional<Policy> policyFromCode(const std::string& code);
std::string policyName(Policy policy);
std::string policyDescription(Policy policy);

const char* purposeCode(Purpose purpose);                     // "EXAM", "ASSIGNMENT", "PRACTICE"
std::optional<Purpose> purposeFromCode(const std::string& code);
std::string purposeName(Purpose purpose);
int priorityFor(Purpose purpose);                             // 1, 2, 3

// A waiting request as seen by the dispatcher. `arrival` is any monotonic
// time value (milliseconds since epoch in the app, seconds in the replay).
struct WaitingRequest {
    long long id = 0;
    long long arrival = 0;
    int expectedMinutes = 0;
    int priority = 3;
};

// Order in which the waiting requests will be served (indices into `waiting`).
// FCFS: earliest arrival. SJF: shortest expected length. PRIORITY: lowest
// priority number. Ties: earlier arrival, then lower request id.
std::vector<std::size_t> dispatchOrder(Policy policy, const std::vector<WaitingRequest>& waiting);
// Index of the request to serve next, or -1 when `waiting` is empty.
int selectNext(Policy policy, const std::vector<WaitingRequest>& waiting);

// ---- measured metrics from recorded timestamps (milliseconds since epoch)
struct MeasuredVisit {
    long long arrivalMs = 0;
    std::optional<long long> startMs;  // empty while still waiting / if cancelled
    std::optional<long long> endMs;    // empty while the session is running
    int expectedMinutes = 0;
};

struct VisitMetrics {
    bool started = false;
    bool finished = false;
    double waitingMinutes = 0;        // so far, if not started yet (relative to nowMs)
    double burstMinutes = 0;          // actual; only meaningful when finished
    double turnaroundMinutes = 0;     // only meaningful when finished
    double estimateErrorMinutes = 0;  // burst - expected; only meaningful when finished
};

VisitMetrics measure(const MeasuredVisit& visit, long long nowMs);

struct MeasuredSummary {
    int completed = 0;
    double averageWaiting = 0;
    double averageBurst = 0;
    double averageTurnaround = 0;
    double maximumWaiting = 0;
    double averageAbsoluteEstimateError = 0;
};
// Averages over FINISHED visits only (burst is unknown for the others).
MeasuredSummary summarise(const std::vector<MeasuredVisit>& visits);

// ---- what-if replay: recorded arrivals and actual bursts on N identical computers
struct ReplayJob {
    long long id = 0;
    long long arrivalSeconds = 0;
    long long burstSeconds = 1;
    int expectedMinutes = 0;  // used for SJF decisions, as in the real dispatcher
    int priority = 3;
};

struct ReplayResult {
    Policy policy = Policy::FCFS;
    int computers = 1;
    std::vector<long long> startSeconds;       // same order as the input jobs
    std::vector<long long> completionSeconds;
    double averageWaitingMinutes = 0;
    double averageTurnaroundMinutes = 0;
    double maximumWaitingMinutes = 0;
    long long finishSeconds = 0;               // last completion
};

// Throws std::invalid_argument if computers < 1 or a burst is not positive.
ReplayResult replay(Policy policy, const std::vector<ReplayJob>& jobs, int computers);

// ---- measured visits as a CPU-scheduling workload (CPU Scheduling page)
struct FinishedVisit {
    long long requestId = 0;
    long long arrivalMs = 0;
    long long startMs = 0;
    long long endMs = 0;
    int priority = 3;
};

enum class TimeUnit { Seconds, Minutes };

struct WorkloadRow {
    long long requestId = 0;
    int arrival = 0;                 // after the earliest arrival in the returned set, in the chosen unit
    int burst = 1;                   // actual session length in the chosen unit, rounded, at least 1
    int priority = 3;
    double actualBurstSeconds = 0;   // unrounded, to report how much rounding changed it
};

// Converts finished visits into scheduler processes: the `limit` most recent
// visits (by arrival), returned in arrival order (ties: request id). Arrival
// is relative to the earliest returned visit. Arrival and burst are rounded to
// the nearest unit (seconds or minutes); a burst is at least 1 unit.
// Real lab sessions last minutes or hours, so minutes keep the numbers
// readable; seconds keep very short (test) visits exact.
std::vector<WorkloadRow> toWorkload(std::vector<FinishedVisit> visits, std::size_t limit, TimeUnit unit);

// Relative change rounding made to the burst: |burst * unit - actual| / actual (0.10 = 10 %).
double roundingError(const WorkloadRow& row, TimeUnit unit);

}  // namespace labdesk::lab
