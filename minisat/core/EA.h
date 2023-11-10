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

class Solver;

struct Instance;

struct VectorHasher {
    template <typename T>
    std::size_t operator()(const std::vector<T> &vec) const {
        std::size_t seed = vec.size();
        for (const auto &value : vec) {
            seed ^= std::hash<T>{}(value) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
        }
        return seed;
    }
};

class EvolutionaryAlgorithm {
   public:
    virtual ~EvolutionaryAlgorithm() = default;

    explicit EvolutionaryAlgorithm(Solver &solver, int seed = -1);

    Instance run(int numIterations, int instanceSize, std::vector<int> pool, const char* backdoor_path, int seed = -1);

    std::mt19937 gen;
    Solver &solver;
    std::unordered_map<std::vector<int>, Fitness, VectorHasher> cache;
    int cache_hits = 0;
    int cache_misses = 0;
    int cached_hits = 0;
    int cached_misses = 0;

   private:
    Instance initialize(int numVariables, std::vector<int> pool);

    Fitness calculateFitness(Instance &individual);

    void mutate(Instance &mutatedIndividual);

    bool is_cached(const Instance &instance, Fitness &fitness) const;
};

}  // namespace Minisat

#endif
