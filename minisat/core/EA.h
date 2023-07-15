#ifndef EA_H
#define EA_H

#include <random>
#include <vector>

#include "minisat/core/Solver.h"

namespace Minisat {

class Instance;

class EvolutionaryAlgorithm {
   public:
    EvolutionaryAlgorithm(Solver& solver, std::vector<int> unusedVariables = {});
    ~EvolutionaryAlgorithm();

    void run(int numIterations, int seed = -1);

   private:
    Solver& solver;
    std::mt19937 gen;
    std::vector<int> unusedVariables;

    Instance initialize(int numVariables);
    double fitness(Instance& individual);
    void mutate(Instance& mutatedIndividual);
};

}  // namespace Minisat

#endif
