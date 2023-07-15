#include "minisat/core/EA.h"

#include <algorithm>
#include <iostream>

namespace Minisat {

EvolutionaryAlgorithm::EvolutionaryAlgorithm(Solver& solver, int seed)
    : solver(solver) {
    if (seed != -1) {
        gen.seed(seed);
    }
}

// Run the evolutionary algorithm
Instance EvolutionaryAlgorithm::run(int numIterations, int seed) {
    if (seed != -1) {
        gen.seed(seed);
    }

    std::cout << "Running EA for " << numIterations << " iterations with seed " << seed << "..." << std::endl;

    std::cout << "solver variables: " << solver.nVars() << std::endl;
    std::cout << "unused variables: " << unusedVariables.size() << std::endl;

    std::cout << std::endl;

    int numVariables = solver.nVars();
    Instance instance = initialize(numVariables);
    double fit = fitness(instance);
    std::cout << "Initial instance: " << instance << std::endl;
    std::cout << "Initial fitness: " << fit << std::endl;

    int bestIteration = 0;
    Instance best = instance;
    double bestFitness = fit;

    for (int i = 1; i <= numIterations; ++i) {
        std::cout << std::endl;
        std::cout << "== Iteration #" << i << std::endl;

        Instance mutatedInstance(instance);  // copy
        mutate(mutatedInstance);
        double _ignore;
        while (is_cached(mutatedInstance, _ignore)) {
            std::cout << "mutating again..." << std::endl;
            mutate(mutatedInstance);
        }
        std::cout << "Mutated instance: " << mutatedInstance << std::endl;

        double mutatedFitness = fitness(mutatedInstance);
        std::cout << "Mutated fitness = " << mutatedFitness << std::endl;

        // Update the best
        if (mutatedFitness < bestFitness) {
            bestIteration = i;
            best = mutatedInstance;
            bestFitness = mutatedFitness;
        }

        // (1+1) strategy: replace 'current' instance if mutated is not worse
        if (mutatedFitness <= fit) {
            instance = mutatedInstance;
            fit = mutatedFitness;
        }
    }

    std::cout << std::endl;
    std::cout << "Best iteration: " << bestIteration << std::endl;
    std::cout << "Best fitness: " << bestFitness << std::endl;
    std::cout << "Best instance: " << best << std::endl;
    return best;
}

// Create an initial individual
Instance EvolutionaryAlgorithm::initialize(int numVariables) {
    std::vector<bool> data(numVariables);  // initially filled with `false`
    // TODO: fill random if necessary
    Instance instance(data);
    return instance;
}

// Calculate the fitness value of the individual
double EvolutionaryAlgorithm::fitness(Instance& instance) {
    double fitness;
    if (!is_cached(instance, fitness)) {
        if (instance._cached_fitness != -1) {
            fitness = instance._cached_fitness;
            std::cout << "cached fitness: " << fitness << std::endl;
        } else {
            std::cout << "computing fitness" << std::endl;

            std::vector<int> variables = instance.variables();
            std::cout << "variables: " << variables.size() << std::endl;

            if (variables.empty()) {
                return std::numeric_limits<double>::max();
            }

            std::vector<std::vector<int>> cubes;
            uint64_t total_count;
            bool verb = false;
            solver.gen_all_valid_assumptions_propcheck(variables, total_count, cubes, verb);

            int numValuations = 1 << variables.size();  // 2^|B|
            // `rho` is the proportion of "easy" tasks:
            double rho = static_cast<double>(total_count) / static_cast<double>(numValuations);
            std::cout << "rho = " << rho << std::endl;

            //! fitness = log2( rho * 2^size + (1-rho) * 2^const )
            int size = variables.size();
            double magic_number = 1 << 20;
            fitness = std::log2(rho * std::pow(2, size) + (1 - rho) * magic_number);
        }

        cache.emplace(instance.data, fitness);
    }

    if (instance._cached_fitness != -1) {
        assert(instance._cached_fitness == fitness);
    }

    instance._cached_fitness = fitness;
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

bool EvolutionaryAlgorithm::is_cached(Instance& instance, double& fitness) {
    auto it = cache.find(instance.data);
    if (it != cache.end()) {
        fitness = it->second;
        return true;
    }
    return false;
}

}  // namespace Minisat
