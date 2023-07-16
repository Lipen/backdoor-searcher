#include "minisat/core/EA.h"

#include <algorithm>
#include <chrono>
#include <iostream>

namespace Minisat {

EvolutionaryAlgorithm::EvolutionaryAlgorithm(Solver& solver, int seed) : solver(solver) {
    if (seed != -1) {
        gen.seed(seed);
    }
}

// Run the evolutionary algorithm
Instance EvolutionaryAlgorithm::run(int numIterations, int instanceSize, int seed) {
    if (seed != -1) {
        gen.seed(seed);
    }

    std::cout << "Running EA for " << numIterations << " iterations..." << '\n';

    std::cout << "instance size: " << instanceSize << '\n';
    std::cout << "solver variables: " << solver.nVars() << '\n';
    std::cout << "unused variables: " << unusedVariables.size() << '\n';

    std::vector<int> pool;
    for (int i = 0; i < solver.nVars(); ++i) {
        if (unusedVariables.find(i) == unusedVariables.end()) {
            // Note: variables are 0-based
            pool.push_back(i);
        }
    }
    std::cout << "pool size: " << pool.size() << '\n';
    if (pool.empty()) {
        std::cout << "Pool of variables is empty, cannot continue!" << std::endl;
        return Instance(instanceSize);
    }

    std::cout << '\n';

    Instance instance = initialize(instanceSize);
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
        mutate(mutatedInstance, pool);
        double _ignore;
        while (is_cached(mutatedInstance, _ignore)) {
            // std::cout << "in cache, mutating again..." << '\n';
            mutate(mutatedInstance, pool);
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
        if (i < 10 || i % 1000 == 0 || true) {
            std::cout << "[" << i << "/" << numIterations << "] Calculated fitness (" << mutatedFitness << ") for " << mutatedInstance.numVariables() << " variables in " << duration.count() << " milliseconds" << '\n';
        }

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
Instance EvolutionaryAlgorithm::initialize(int instanceSize) {
    Instance instance(instanceSize);
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
void EvolutionaryAlgorithm::mutate(Instance& instance, std::vector<int>& pool) {
    assert(!pool.empty());

    std::uniform_real_distribution<double> dis(0.0, 1.0);
    std::uniform_int_distribution<size_t> dis_index(0, pool.size() - 1);

    for (size_t i = 0; i < instance.size(); ++i) {
        if (dis(gen) < (1.0 / (instance.size()))) {
            size_t j = dis_index(gen);
            std::swap(instance[i], pool[j]);
        }
    }

    while (instance.numVariables() < 16) {
        // std::cout << "numVariables = " << instance.numVariables() << ", swapping..." << std::endl;
        for (size_t i = 0; i < instance.size(); ++i) {
            if (instance[i] == -1) {
                auto it = std::find_if(pool.begin(), pool.end(), [](int value) {
                    return value != -1;
                });
                size_t j = std::distance(pool.begin(), it);
                std::swap(instance[i], pool[j]);
                break;
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
