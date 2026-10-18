#include "bankers.h"

namespace labdesk::os {

namespace {
std::string vectorText(const Vector& v)
{
    std::string s = "(";
    for (std::size_t i = 0; i < v.size(); ++i)
        s += (i ? ", " : "") + std::to_string(v[i]);
    return s + ")";
}

bool fits(const Vector& a, const Vector& b)  // a <= b component-wise
{
    for (std::size_t j = 0; j < a.size(); ++j)
        if (a[j] > b[j])
            return false;
    return true;
}
}  // namespace

std::vector<std::string> validateBankers(const BankersState& s)
{
    std::vector<std::string> problems;
    const std::size_t m = s.available.size();
    if (m == 0)
        problems.emplace_back("Define at least one resource type.");
    if (s.maximum.empty())
        problems.emplace_back("Define at least one process.");
    if (s.maximum.size() != s.allocation.size())
        problems.emplace_back("Maximum and allocation must have the same number of processes.");
    for (std::size_t j = 0; j < m; ++j)
        if (s.available[j] < 0)
            problems.push_back("Available resource " + std::to_string(j) + " is negative.");
    for (std::size_t i = 0; i < s.maximum.size() && i < s.allocation.size(); ++i) {
        const std::string p = "P" + std::to_string(i) + ": ";
        if (s.maximum[i].size() != m || s.allocation[i].size() != m) {
            problems.push_back(p + "every row needs exactly " + std::to_string(m) + " values.");
            continue;
        }
        for (std::size_t j = 0; j < m; ++j) {
            if (s.maximum[i][j] < 0 || s.allocation[i][j] < 0)
                problems.push_back(p + "values cannot be negative.");
            else if (s.allocation[i][j] > s.maximum[i][j])
                problems.push_back(p + "allocation of resource " + std::to_string(j) + " exceeds its maximum demand.");
        }
    }
    return problems;
}

Matrix needMatrix(const BankersState& s)
{
    Matrix need = s.maximum;
    for (std::size_t i = 0; i < need.size(); ++i)
        for (std::size_t j = 0; j < need[i].size(); ++j)
            need[i][j] -= s.allocation[i][j];
    return need;
}

SafetyResult checkSafety(const BankersState& s)
{
    SafetyResult r;
    const Matrix need = needMatrix(s);
    const std::size_t n = s.maximum.size();
    Vector work = s.available;
    std::vector<bool> finished(n, false);
    for (std::size_t round = 0; round < n; ++round) {
        bool progressed = false;
        for (std::size_t i = 0; i < n; ++i) {
            if (finished[i] || !fits(need[i], work))
                continue;
            SafetyStep step;
            step.process = static_cast<int>(i);
            step.workBefore = work;
            for (std::size_t j = 0; j < work.size(); ++j)
                work[j] += s.allocation[i][j];
            step.workAfter = work;
            r.steps.push_back(step);
            r.sequence.push_back(static_cast<int>(i));
            finished[i] = true;
            progressed = true;
            break;  // restart from the lowest index (deterministic rule)
        }
        if (!progressed)
            break;
    }
    for (std::size_t i = 0; i < n; ++i)
        if (!finished[i])
            r.unfinished.push_back(static_cast<int>(i));
    r.safe = r.unfinished.empty();
    return r;
}

RequestResult requestResources(const BankersState& s, int process, const Vector& request)
{
    RequestResult r;
    r.state = s;
    const auto problems = validateBankers(s);
    if (!problems.empty()) {
        r.reason = problems.front();
        return r;
    }
    if (process < 0 || process >= static_cast<int>(s.maximum.size())) {
        r.reason = "Choose an existing process.";
        return r;
    }
    if (request.size() != s.available.size()) {
        r.reason = "The request needs one value per resource type.";
        return r;
    }
    for (int v : request)
        if (v < 0) {
            r.reason = "Requested amounts cannot be negative.";
            return r;
        }
    const Matrix need = needMatrix(s);
    const std::size_t i = static_cast<std::size_t>(process);
    const std::string p = "P" + std::to_string(process);
    if (!fits(request, need[i])) {
        r.reason = p + " asked for " + vectorText(request) + " but its remaining need is only " + vectorText(need[i])
                   + ": a process may not exceed its declared maximum.";
        return r;
    }
    if (!fits(request, s.available)) {
        r.outcome = RequestOutcome::MustWait;
        r.reason = p + " must wait: only " + vectorText(s.available) + " is available.";
        return r;
    }
    BankersState tentative = s;
    for (std::size_t j = 0; j < request.size(); ++j) {
        tentative.available[j] -= request[j];
        tentative.allocation[i][j] += request[j];
    }
    r.safety = checkSafety(tentative);
    if (r.safety.safe) {
        r.outcome = RequestOutcome::Granted;
        r.state = tentative;
        r.reason = p + "'s request " + vectorText(request) + " is granted: the new state is safe.";
    } else {
        r.outcome = RequestOutcome::Denied;
        r.reason = p + "'s request " + vectorText(request) + " is denied and rolled back: granting it would leave an "
                   "unsafe state (not necessarily a deadlock, but no longer guaranteed to avoid one). "
                   + p + " waits.";
    }
    return r;
}

RequestResult releaseResources(const BankersState& s, int process, const Vector& release)
{
    RequestResult r;
    r.state = s;
    if (process < 0 || process >= static_cast<int>(s.maximum.size()) || release.size() != s.available.size()) {
        r.reason = "Choose a process and give one value per resource type.";
        return r;
    }
    const std::size_t i = static_cast<std::size_t>(process);
    for (std::size_t j = 0; j < release.size(); ++j) {
        if (release[j] < 0 || release[j] > s.allocation[i][j]) {
            r.reason = "P" + std::to_string(process) + " cannot release more than it holds " + vectorText(s.allocation[i]) + ".";
            return r;
        }
    }
    for (std::size_t j = 0; j < release.size(); ++j) {
        r.state.available[j] += release[j];
        r.state.allocation[i][j] -= release[j];
    }
    r.outcome = RequestOutcome::Granted;
    r.safety = checkSafety(r.state);
    r.reason = "P" + std::to_string(process) + " released " + vectorText(release) + ".";
    return r;
}

BankersState textbookBankersExample()
{
    BankersState s;
    s.available = {3, 3, 2};
    s.allocation = {{0, 1, 0}, {2, 0, 0}, {3, 0, 2}, {2, 1, 1}, {0, 0, 2}};
    s.maximum = {{7, 5, 3}, {3, 2, 2}, {9, 0, 2}, {2, 2, 2}, {4, 3, 3}};
    return s;
}

}  // namespace labdesk::os
