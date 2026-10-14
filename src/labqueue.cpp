#include "labqueue.h"

#include <algorithm>
#include <numeric>
#include <set>
#include <stdexcept>
#include <tuple>

namespace labdesk::lab {

const char* policyCode(Policy policy)
{
    switch (policy) {
    case Policy::FCFS: return "FCFS";
    case Policy::SJF: return "SJF";
    case Policy::Priority: return "PRIORITY";
    }
    return "FCFS";
}

std::optional<Policy> policyFromCode(const std::string& code)
{
    for (Policy p : {Policy::FCFS, Policy::SJF, Policy::Priority})
        if (code == policyCode(p))
            return p;
    return std::nullopt;
}

std::string policyName(Policy policy)
{
    switch (policy) {
    case Policy::FCFS: return "First Come First Serve";
    case Policy::SJF: return "Shortest Job First (by expected length)";
    case Policy::Priority: return "Priority (by purpose)";
    }
    return {};
}

std::string policyDescription(Policy policy)
{
    switch (policy) {
    case Policy::FCFS:
        return "The student who has waited longest gets the next free computer. Fair and predictable, but a short "
               "exam check may wait behind long practice sessions.";
    case Policy::SJF:
        return "The student who expects the shortest session gets the next free computer. This lowers the average "
               "waiting time, but relies on honest estimates and can make long requests wait a long time.";
    case Policy::Priority:
        return "Exam requests go first, then assignments, then practice; ties go to the earlier request. Urgent work "
               "is served quickly, but practice requests can wait a long time when the lab is busy.";
    }
    return {};
}

const char* purposeCode(Purpose purpose)
{
    switch (purpose) {
    case Purpose::Exam: return "EXAM";
    case Purpose::Assignment: return "ASSIGNMENT";
    case Purpose::Practice: return "PRACTICE";
    }
    return "PRACTICE";
}

std::optional<Purpose> purposeFromCode(const std::string& code)
{
    for (Purpose p : {Purpose::Exam, Purpose::Assignment, Purpose::Practice})
        if (code == purposeCode(p))
            return p;
    return std::nullopt;
}

std::string purposeName(Purpose purpose)
{
    switch (purpose) {
    case Purpose::Exam: return "Exam";
    case Purpose::Assignment: return "Assignment";
    case Purpose::Practice: return "Practice";
    }
    return {};
}

int priorityFor(Purpose purpose)
{
    switch (purpose) {
    case Purpose::Exam: return 1;
    case Purpose::Assignment: return 2;
    case Purpose::Practice: return 3;
    }
    return 3;
}

namespace {
bool servedBefore(Policy policy, const WaitingRequest& a, const WaitingRequest& b)
{
    switch (policy) {
    case Policy::SJF:
        return std::make_tuple(a.expectedMinutes, a.arrival, a.id) < std::make_tuple(b.expectedMinutes, b.arrival, b.id);
    case Policy::Priority:
        return std::make_tuple(a.priority, a.arrival, a.id) < std::make_tuple(b.priority, b.arrival, b.id);
    case Policy::FCFS:
        break;
    }
    return std::make_tuple(a.arrival, a.id) < std::make_tuple(b.arrival, b.id);
}
}  // namespace

std::vector<std::size_t> dispatchOrder(Policy policy, const std::vector<WaitingRequest>& waiting)
{
    std::vector<std::size_t> order(waiting.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::sort(order.begin(), order.end(),
              [&](std::size_t a, std::size_t b) { return servedBefore(policy, waiting[a], waiting[b]); });
    return order;
}

int selectNext(Policy policy, const std::vector<WaitingRequest>& waiting)
{
    if (waiting.empty())
        return -1;
    return static_cast<int>(dispatchOrder(policy, waiting).front());
}

VisitMetrics measure(const MeasuredVisit& v, long long nowMs)
{
    constexpr double msPerMinute = 60000.0;
    VisitMetrics m;
    m.started = v.startMs.has_value();
    m.finished = m.started && v.endMs.has_value();
    const long long waitedUntil = m.started ? *v.startMs : nowMs;
    m.waitingMinutes = std::max(0LL, waitedUntil - v.arrivalMs) / msPerMinute;
    if (m.finished) {
        m.burstMinutes = std::max(0LL, *v.endMs - *v.startMs) / msPerMinute;
        m.turnaroundMinutes = std::max(0LL, *v.endMs - v.arrivalMs) / msPerMinute;
        m.estimateErrorMinutes = m.burstMinutes - v.expectedMinutes;
    }
    return m;
}

MeasuredSummary summarise(const std::vector<MeasuredVisit>& visits)
{
    MeasuredSummary s;
    double wt = 0, bt = 0, tat = 0, err = 0;
    for (const MeasuredVisit& v : visits) {
        const VisitMetrics m = measure(v, 0);
        if (!m.finished)
            continue;
        ++s.completed;
        wt += m.waitingMinutes;
        bt += m.burstMinutes;
        tat += m.turnaroundMinutes;
        err += m.estimateErrorMinutes < 0 ? -m.estimateErrorMinutes : m.estimateErrorMinutes;
        s.maximumWaiting = std::max(s.maximumWaiting, m.waitingMinutes);
    }
    if (s.completed > 0) {
        s.averageWaiting = wt / s.completed;
        s.averageBurst = bt / s.completed;
        s.averageTurnaround = tat / s.completed;
        s.averageAbsoluteEstimateError = err / s.completed;
    }
    return s;
}

ReplayResult replay(Policy policy, const std::vector<ReplayJob>& jobs, int computers)
{
    if (computers < 1)
        throw std::invalid_argument("At least one computer is needed for the replay.");
    for (const ReplayJob& j : jobs)
        if (j.burstSeconds <= 0)
            throw std::invalid_argument("Every replayed visit needs a positive duration.");

    const std::size_t n = jobs.size();
    ReplayResult r;
    r.policy = policy;
    r.computers = computers;
    r.startSeconds.assign(n, 0);
    r.completionSeconds.assign(n, 0);
    if (n == 0)
        return r;

    std::vector<std::size_t> byArrival(n);
    std::iota(byArrival.begin(), byArrival.end(), std::size_t{0});
    std::stable_sort(byArrival.begin(), byArrival.end(), [&](std::size_t a, std::size_t b) {
        return std::make_tuple(jobs[a].arrivalSeconds, jobs[a].id) < std::make_tuple(jobs[b].arrivalSeconds, jobs[b].id);
    });

    std::vector<std::size_t> waiting;        // job indices
    std::multiset<long long> busyUntil;      // completion times of running visits
    std::size_t nextArrival = 0;
    std::size_t assigned = 0;
    long long time = jobs[byArrival[0]].arrivalSeconds;

    while (assigned < n) {
        // Free computers whose visit has finished, admit everyone who has arrived.
        while (!busyUntil.empty() && *busyUntil.begin() <= time)
            busyUntil.erase(busyUntil.begin());
        while (nextArrival < n && jobs[byArrival[nextArrival]].arrivalSeconds <= time)
            waiting.push_back(byArrival[nextArrival++]);

        // Fill free computers using the same rule as the live dispatcher.
        while (static_cast<int>(busyUntil.size()) < computers && !waiting.empty()) {
            std::vector<WaitingRequest> view;
            view.reserve(waiting.size());
            for (std::size_t j : waiting)
                view.push_back({jobs[j].id, jobs[j].arrivalSeconds, jobs[j].expectedMinutes, jobs[j].priority});
            const int pick = selectNext(policy, view);
            const std::size_t j = waiting[static_cast<std::size_t>(pick)];
            waiting.erase(waiting.begin() + pick);
            r.startSeconds[j] = time;
            r.completionSeconds[j] = time + jobs[j].burstSeconds;
            busyUntil.insert(r.completionSeconds[j]);
            ++assigned;
        }
        if (assigned == n)
            break;

        // Jump to the next moment something can change.
        long long next = -1;
        if (nextArrival < n)
            next = jobs[byArrival[nextArrival]].arrivalSeconds;
        if (!busyUntil.empty() && !waiting.empty())
            next = (next < 0) ? *busyUntil.begin() : std::min(next, *busyUntil.begin());
        if (next < 0)
            next = *busyUntil.begin();  // only running visits left before the next arrival
        time = std::max(time, next);
    }

    double wt = 0, tat = 0;
    for (std::size_t j = 0; j < n; ++j) {
        const double w = (r.startSeconds[j] - jobs[j].arrivalSeconds) / 60.0;
        wt += w;
        tat += (r.completionSeconds[j] - jobs[j].arrivalSeconds) / 60.0;
        r.maximumWaitingMinutes = std::max(r.maximumWaitingMinutes, w);
        r.finishSeconds = std::max(r.finishSeconds, r.completionSeconds[j]);
    }
    r.averageWaitingMinutes = wt / static_cast<double>(n);
    r.averageTurnaroundMinutes = tat / static_cast<double>(n);
    return r;
}

namespace {
long long unitMs(TimeUnit unit) { return unit == TimeUnit::Minutes ? 60000LL : 1000LL; }
}  // namespace

std::vector<WorkloadRow> toWorkload(std::vector<FinishedVisit> visits, std::size_t limit, TimeUnit unit)
{
    auto earlier = [](const FinishedVisit& a, const FinishedVisit& b) {
        return std::make_tuple(a.arrivalMs, a.requestId) < std::make_tuple(b.arrivalMs, b.requestId);
    };
    std::sort(visits.begin(), visits.end(), earlier);
    if (visits.size() > limit)
        visits.erase(visits.begin(), visits.end() - static_cast<std::ptrdiff_t>(limit));  // keep the most recent
    const long long step = unitMs(unit);
    auto roundToUnit = [step](long long ms) { return static_cast<int>((std::max(0LL, ms) + step / 2) / step); };
    std::vector<WorkloadRow> rows;
    if (visits.empty())
        return rows;
    const long long origin = visits.front().arrivalMs;
    for (const FinishedVisit& v : visits) {
        WorkloadRow row;
        row.requestId = v.requestId;
        row.arrival = roundToUnit(v.arrivalMs - origin);
        row.burst = std::max(1, roundToUnit(v.endMs - v.startMs));
        row.priority = v.priority;
        row.actualBurstSeconds = std::max(0LL, v.endMs - v.startMs) / 1000.0;
        rows.push_back(row);
    }
    return rows;
}

double roundingError(const WorkloadRow& row, TimeUnit unit)
{
    const double unitSeconds = unitMs(unit) / 1000.0;
    const double actual = std::max(row.actualBurstSeconds, 0.001);  // avoid dividing by zero
    const double used = row.burst * unitSeconds;
    return (used > actual ? used - actual : actual - used) / actual;
}

}  // namespace labdesk::lab
