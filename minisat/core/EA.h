#ifndef EA_H
#define EA_H

#include <random>
#include <set>
#include <unordered_map>
#include <vector>

#include "minisat/core/Solver.h"
#include "minisat/core/Fitness.h"
#include "minisat/core/Instance.h"

namespace Minisat {

class Solver;
struct Instance;

class EvolutionaryAlgorithm {
public:
    virtual ~EvolutionaryAlgorithm() = default;

    explicit EvolutionaryAlgorithm(Solver &solver, int seed = -1);

    Instance run(int numIterations, int instanceSize, std::vector<int> pool, int seed = -1);

private:
    std::mt19937 gen;
    Solver &solver;
    std::unordered_map<std::vector<bool>, Fitness> cache;

    Instance initialize(int numVariables, std::vector<int> pool);

    Fitness calculateFitness(Instance &individual);

    void mutate(Instance &mutatedIndividual);

    bool is_cached(const Instance &instance, Fitness &fitness) const;
};

}  // namespace Minisat

#endif
