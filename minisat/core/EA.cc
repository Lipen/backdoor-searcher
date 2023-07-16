#include "minisat/core/EA.h"

#include <algorithm>
#include <chrono>
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

    std::cout << "Running EA for " << numIterations << " iterations..." << '\n';

    std::cout << "solver variables: " << solver.nVars() << '\n';
    std::cout << "unused variables: " << unusedVariables.size() << '\n';

    std::cout << '\n';

    int numVariables = solver.nVars();
    Instance instance = initialize(numVariables);
    double fit = calculateFitness(instance);
    // std::cout << "Initial instance: " << instance << '\n';
    std::vector<int> vars = instance.getVariables();
    std::cout << "Initial variables (total " << vars.size() << "): [";
    for (size_t i = 0; i < vars.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << vars[i];
    }
    std::cout << "]" << '\n';
    std::cout << "Initial fitness: " << fit << '\n';

    int bestIteration = 0;
    Instance best = instance;
    double bestFitness = fit;

    for (int i = 1; i <= numIterations; ++i) {
        // std::cout << "\n=== Iteration #" << i << '\n';

        Instance mutatedInstance = instance;  // copy
        mutate(mutatedInstance);
        while (1) {
            double _ignore;
            if (is_cached(mutatedInstance, _ignore)) {
                // std::cout << "in cache, mutating again..." << '\n';
                // mutatedInstance = instance;
                mutate(mutatedInstance);
                continue;
            }

            std::vector<int> vars = mutatedInstance.getVariables();
            if (vars.empty()) {
                // std::cout << "empty, mutating again..." << '\n';
                // mutatedInstance = instance;
                mutate(mutatedInstance);
                continue;
            }
            // if (vars.size() > 24) {
            //     // std::cout << "too much vars(" << vars.size() << "), mutating again..." << '\n';
            //     // mutatedInstance = instance;
            //     mutate(mutatedInstance);
            //     continue;
            // }

            break;
        }
        // std::cout << "Mutated instance: " << mutatedInstance << '\n';
        std::vector<int> mutatedVars = mutatedInstance.getVariables();
        // std::cout << "Mutated variables (total " << mutatedVars.size() << "): [";
        // for (size_t i = 0; i < mutatedVars.size(); ++i) {
        //     if (i > 0) std::cout << ", ";
        //     std::cout << mutatedVars[i];
        // }
        // std::cout << "]" << '\n';

        auto startTime = std::chrono::high_resolution_clock::now();
        double mutatedFitness = calculateFitness(mutatedInstance);
        // std::cout << "Mutated fitness: " << mutatedFitness << '\n';
        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        std::cout << "[" << i << "/" << numIterations << "] Calculated fitness (" << mutatedFitness << ") for " << mutatedInstance.numVariables() << " variables in " << duration.count() << " milliseconds" << '\n';

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

        // (1,1) strategy: replace 'current' instance with mutated in any case
        // instance = mutatedInstance;
        // fit = mutatedFitness;
    }

    // std::cout << '\n';
    // std::cout << "Best iteration: " << bestIteration << '\n';
    // std::cout << "Best fitness: " << bestFitness << '\n';
    // std::cout << "Best instance: " << best << '\n';
    std::vector<int> bestVars = best.getVariables();
    std::cout << "Best fitness " << bestFitness << " on iteration " << bestIteration << " with variables (total " << bestVars.size() << "): [";
    for (size_t i = 0; i < bestVars.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << bestVars[i];
    }
    std::cout << "]" << '\n';
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
double EvolutionaryAlgorithm::calculateFitness(Instance& instance) {
    double fitness;
    if (!is_cached(instance, fitness)) {
        // Delegate to instance for computing the fitness:
        fitness = instance.calculateFitness(solver);

        // Update global fitness cache:
        cache.emplace(instance.data, fitness);
    } else {
        // std::cout << "cached (global) fitness: " << fitness << '\n';
    }

    // Update instance's local cache:
    instance._cached_fitness = fitness;

    return fitness;
}

// Mutate the individual by flipping bits
void EvolutionaryAlgorithm::mutate(Instance& instance) {
    std::uniform_real_distribution<double> dis(0.0, 1.0);

    // Track the set size
    int counter = instance.numVariables();

    for (size_t i = 0; i < instance.size(); ++i) {
        // Check if the variable is unused
        if (unusedVariables.find(i) != unusedVariables.end()) {
            assert(instance[i] == false);
            continue;  // Skip unused variables
        }

        // // Flip the bit with probability 1/n
        // if (dis(gen) < (1.0 / solver.nVars())) {
        //     instance[i] = ! instance[i];
        // }

        if (instance[i]) {
            if (counter > 24) {
                instance[i] = false;
                counter--;
            }
        } else {
            // Flip the bit with probability 1/n
            if (dis(gen) < (1.0 / (instance.size() - unusedVariables.size() - counter))) {
                instance[i] = true;
                counter++;
            }
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
