#ifndef EA_H
#define EA_H

#include <random>
#include <set>
#include <unordered_map>
#include <vector>

#include "minisat/core/Fitness.h"
#include "minisat/core/Instance.h"
#include "minisat/core/Solver.h"

namespace Minisat {

class EvolutionaryAlgorithm {
   public:
    virtual ~EvolutionaryAlgorithm() = default;
    explicit EvolutionaryAlgorithm(Solver& solver, int seed = -1);

    std::set<int> unusedVariables;

    Instance run(int numIterations, int instanceSize, int seed = -1);

   private:
    std::mt19937 gen;
    Solver& solver;
    std::unordered_map<std::vector<bool>, Fitness> cache;

    Instance initialize(int numVariables);
    Fitness calculateFitness(Instance& individual);
    void mutate(Instance& mutatedIndividual);

    bool is_cached(Instance& instance, Fitness& fitness);
};

}  // namespace Minisat

#endif
