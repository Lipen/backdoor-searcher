#ifndef EA_H
#define EA_H

#include <random>
#include <unordered_map>
#include <vector>
#include <set>

#include "minisat/core/Instance.h"
#include "minisat/core/Solver.h"

namespace Minisat {

class EvolutionaryAlgorithm {
   public:
    virtual ~EvolutionaryAlgorithm() = default;
    explicit EvolutionaryAlgorithm(Solver& solver, int seed = -1);

    std::set<int> unusedVariables;

    Instance run(int numIterations, int seed = -1);

   private:
    std::mt19937 gen;
    Solver& solver;
    std::unordered_map<std::vector<bool>, double> cache;

    Instance initialize(int numVariables);
    double calculateFitness(Instance& individual);
    void mutate(Instance& mutatedIndividual);

    bool is_cached(Instance& instance, double& fitness);
};

}  // namespace Minisat

#endif
