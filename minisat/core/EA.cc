#include "minisat/core/EA.h"

#include <algorithm>
#include <functional>
#include <iostream>
#include <random>
#include <vector>

#include "minisat/core/Solver.h"

namespace Minisat {

class Instance {
    std::vector<int> data;

   public:
    double _cached_fitness = -1;

    Instance(std::vector<int> data) : data(std::move(data)) {}
    ~Instance() {}

    int operator[](size_t index) const {
        return data[index];
    }
    int& operator[](size_t index) {
        return data[index];
    }

    size_t size() const {
        return data.size();
    }

    // Iterator support
    class Iterator {
        std::vector<int>::iterator iter;

       public:
        explicit Iterator(std::vector<int>::iterator iter) : iter(iter) {}

        int operator*() const {
            return *iter;
        }

        Iterator& operator++() {
            ++iter;
            return *this;
        }

        bool operator!=(const Iterator& other) const {
            return iter != other.iter;
        }
    };

    Iterator begin() {
        return Iterator(data.begin());
    }

    Iterator end() {
        return Iterator(data.end());
    }
};

EvolutionaryAlgorithm::EvolutionaryAlgorithm(Solver& solver, std::vector<int> unusedVariables)
    : solver(solver), unusedVariables(unusedVariables) {
        std::cout << "EA created" << std::endl;
    }

EvolutionaryAlgorithm::~EvolutionaryAlgorithm() {}

// Create an initial individual
Instance EvolutionaryAlgorithm::initialize(int numVariables) {
    std::vector<int> data(numVariables);
    // TODO: fill random if necessary
    Instance instance(data);
    return instance;
}

// Calculate the fitness value of the individual
double EvolutionaryAlgorithm::fitness(Instance& individual) {
    if (individual._cached_fitness != -1) {
        // TODO: cache globally
        return individual._cached_fitness;
    }

    std::vector<int> activeVariables;
    for (int i = 0; i < (int)individual.size(); ++i) {
        if (individual[i]) {
            // Note: variables are 0-based, so we are just passing `i` as a variable
            activeVariables.push_back(i);
        }
    }

    std::vector<std::vector<int>> hardCubes;
    uint64_t totalCount = 0;
    solver.gen_all_valid_assumptions_propcheck(activeVariables, totalCount, hardCubes);
    // hardCubes now contains all the hard tasks (each task is a vector of literals (cube))

    int numUnknowns = hardCubes.size();  //|H|
    int size = activeVariables.size();   // |B|
    int numValuations = 1 << size;       // 2^|B|
    // `rho` is the proportion of "easy" tasks:
    double rho = 1.0 - (static_cast<double>(numUnknowns) / static_cast<double>(numValuations));

    //! fitness = log2( rho * 2^size + (1-rho) * 2^const )
    double magic_number = 1 << 20;
    double fitness = std::log2(rho * std::pow(2, size) + (1 - rho) * magic_number);

    individual._cached_fitness = fitness;
    return fitness;
}

// Mutate the individual by flipping bits
void EvolutionaryAlgorithm::mutate(Instance& mutatedIndividual) {
    std::uniform_real_distribution<double> dis(0.0, 1.0);

    for (int i = 0; i < (int)mutatedIndividual.size(); ++i) {
        // Check if the variable is unused
        if (std::find(unusedVariables.begin(), unusedVariables.end(), i) != unusedVariables.end()) {
            continue;  // Skip unused variables
        }

        // Flip the bit with probability 1/n
        if (dis(gen) < (1.0 / solver.nVars())) {
            mutatedIndividual[i] = 1 - mutatedIndividual[i];
        }
    }
}

// Perform the evolutionary algorithm
void EvolutionaryAlgorithm::run(int numIterations, int seed) {
    if (seed == -1) {
        std::random_device rd;
        seed = rd();
    }
    gen.seed(seed);

    std::cout << "Running EA for " << numIterations << " iterations with seed " << seed << "..." << std::endl;

    int numVariables = solver.nVars();
    Instance instance = initialize(numVariables);
    Instance bestInstance = instance;
    double bestFitness = fitness(bestInstance);

    std::cout << "Initial fitness: " << bestFitness << std::endl;
    std::cout << "Initial instance: ";
    for (int bit : bestInstance) {
        std::cout << bit;
    }

    for (int i = 1; i <= numIterations; ++i) {
        std::cout << "Iteration #" << i << std::endl;

        Instance mutatedInstance = instance;  // copy
        mutate(mutatedInstance);
        double mutatedFitness = fitness(mutatedInstance);

        if (mutatedFitness <= bestFitness) {
            bestInstance = mutatedInstance;
            bestFitness = mutatedFitness;
        }

        instance = mutatedInstance; /// Note: this is (1,1), not (1+1)
    }

    std::cout << "Best fitness: " << bestFitness << std::endl;
    std::cout << "Best instance: ";
    for (int bit : bestInstance) {
        std::cout << bit;
    }
    std::cout << std::endl;
}

}  // namespace Minisat
