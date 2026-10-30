#pragma once
#include <string>
#include <vector>

#include "core/Rules.hpp"

namespace pf {

struct ScalingPoint {
    int threads = 0;
    double runsPerSec = 0;
    double evalsPerSec = 0;
};

struct BenchmarkResult {
    double evalsPerSecSingle = 0;  // evaluator alone, one thread, random 7-card hands
    std::vector<ScalingPoint> scaling;  // full simulation, 6 random hands preflop
    std::string summary;  // one line for the console
};

// Takes roughly secondsPerPoint * (1 + number of thread counts tried).
BenchmarkResult runBenchmark(const RuleSet& rules, double secondsPerPoint = 0.25);

}  // namespace pf
