#ifndef EA_H
#define EA_H

#include <random>
#include <set>
#include <unordered_map>
#include <vector>

#include "minisat/core/Instance.h"
#include "minisat/core/Solver.h"

namespace Minisat {

struct VectorHasher {
    template <typename T>
    std::size_t operator()(const std::vector<T>& vec) const {
        std::size_t seed = vec.size();
        for (const auto& value : vec) {
            seed ^= std::hash<T>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

class EvolutionaryAlgorithm {
   public:
    virtual ~EvolutionaryAlgorithm() = default;
    explicit EvolutionaryAlgorithm(Solver& solver, int seed = -1);

    std::set<int> unusedVariables;

    Instance run(int numIterations, int instanceSize, int seed = -1);

   private:
    std::mt19937 gen;
    Solver& solver;
    std::unordered_map<std::vector<int>, double, VectorHasher> cache;

    Instance initialize(int numVariables);
    double calculateFitness(Instance& individual);
    void mutate(Instance& mutatedIndividual, std::vector<int>& pool);

    bool is_cached(Instance& instance, double& fitness);
};

}  // namespace Minisat

#endif
