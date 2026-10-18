#pragma once
// Banker's Algorithm (deadlock AVOIDANCE) for the OS Management module.
// Pure C++17, independent of Qt, unit-tested in tests/os_extra_tests.cpp.
//
// State: n processes, m resource types.
//   available[j]      free instances of resource j
//   maximum[i][j]     most process i may ever hold of resource j
//   allocation[i][j]  what process i holds now
//   need = maximum - allocation
//
// Safe state: there is an order in which every process can obtain its
// remaining need and finish. Unsafe does NOT mean deadlocked yet; it means the
// system can no longer guarantee that a deadlock will be avoided.
//
// Deterministic rule: at each step the LOWEST-numbered process whose need fits
// the current work vector is chosen.

#include <string>
#include <vector>

namespace labdesk::os {

using Vector = std::vector<int>;
using Matrix = std::vector<Vector>;

struct BankersState {
    Vector available;
    Matrix maximum;
    Matrix allocation;
};

// Empty = valid. Checks dimensions, non-negative values and allocation <= maximum.
std::vector<std::string> validateBankers(const BankersState& state);

Matrix needMatrix(const BankersState& state);

struct SafetyStep {
    int process = 0;
    Vector workBefore;
    Vector workAfter;  // workBefore + allocation of the process (it finished and released)
};

struct SafetyResult {
    bool safe = false;
    std::vector<int> sequence;     // a safe sequence when safe
    std::vector<SafetyStep> steps;
    std::vector<int> unfinished;   // processes that could not be guaranteed to finish
};

SafetyResult checkSafety(const BankersState& state);

enum class RequestOutcome { Granted, MustWait, Denied, Invalid };

struct RequestResult {
    RequestOutcome outcome = RequestOutcome::Invalid;
    std::string reason;
    BankersState state;     // the new state if granted, otherwise the unchanged state
    SafetyResult safety;    // safety of the tentative state (Granted / Denied)
};

// Resource-request algorithm: Invalid if request > need, MustWait if
// request > available, otherwise pretend to allocate and keep it only if the
// resulting state is safe (Granted); else roll back (Denied).
RequestResult requestResources(const BankersState& state, int process, const Vector& request);

// Releases resources held by a process (e.g. it finished part of its work).
RequestResult releaseResources(const BankersState& state, int process, const Vector& release);

// Silberschatz, Operating System Concepts: 5 processes, resources A B C.
BankersState textbookBankersExample();

}  // namespace labdesk::os
