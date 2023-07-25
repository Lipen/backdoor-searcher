#include "minisat/core/EA.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <random>
#include <utility>
#include <vector>

namespace Minisat {

EvolutionaryAlgorithm::EvolutionaryAlgorithm(Solver &solver, int seed) : solver(solver) {
    if (seed != -1) {
        gen.seed(seed);
    }
}

// Run the evolutionary algorithm
Instance EvolutionaryAlgorithm::run(
    int numIterations,
    int instanceSize,
    std::vector<int> pool,
    int seed
) {
    if (seed != -1) {
        gen.seed(seed);
    }

    std::cout << "Running EA for " << numIterations << " iterations..." << std::endl;

    std::cout << "instance size: " << instanceSize << std::endl;
    std::cout << "solver variables: " << solver.nVars() << std::endl;

    std::cout << '\n';

    // Initial instance:
    Instance instance = initialize(instanceSize, std::move(pool));
    if (instance.pool.empty()) {
        std::cout << "Pool of variables is empty, cannot run!" << std::endl;
        return instance;
    }
    Fitness fit = calculateFitness(instance);
    std::cout << "Initial instance with " << instance.numVariables() << " vars: " << instance << std::endl;
    std::cout << "Initial fitness: " << fit.fitness << " (rho=" << fit.rho << ")" << std::endl;

    int bestIteration = 0;
    Instance best = instance;
    Fitness bestFitness = fit;

    for (int i = 1; i <= numIterations; ++i) {
        // if (i <= 10 || i % 100 == 0) {
        //     std::cout << "\n=== Iteration #" << i << std::endl;
        // }

        auto startTime = std::chrono::high_resolution_clock::now();

        Instance mutatedInstance = instance;  // copy
        mutate(mutatedInstance);
        // Fitness _ignore;
        // while (is_cached(mutatedInstance, _ignore)) {
        //     // std::cout << "in cache, mutating again..." << std::endl;
        //     mutate(mutatedInstance);
        // }
        // std::cout << "Mutated instance: " << mutatedInstance << std::endl;
        // std::vector<int> mutatedVars = mutatedInstance.getVariables();
        // std::cout << "Mutated variables (total " << mutatedVars.size() << "): [";
        // for (size_t i = 0; i < mutatedVars.size(); ++i) {
        //     if (i > 0) std::cout << ", ";
        //     std::cout << mutatedVars[i];
        // }
        // std::cout << "]" << std::endl;

        Fitness mutatedFitness = calculateFitness(mutatedInstance);
        // std::cout << "Mutated fitness: " << mutatedFitness << std::endl;

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        if (i <= 10 || (i < 1000 && i % 100 == 0) || (i < 10000 && i % 1000 == 0) || (i % 10000 == 0)) {
            std::cout << "[" << i << "/" << numIterations << "] "
                      << "Fitness " << mutatedFitness.fitness
                      << " (rho=" << mutatedFitness.rho << ")"
                      << " for " << mutatedInstance.numVariables() << " vars "
                      << mutatedInstance << " in " << duration.count() << " ms"
                      << std::endl;
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

        // if (fit.rho >= 0.999) {
        //     std::cout << "Found rho >= 0.999 on iteration " << i << std::endl;
        //     break;
        // }
    }

    // std::cout << std::endl;
    // std::cout << "Best iteration: " << bestIteration << std::endl;
    // std::cout << "Best fitness: " << bestFitness << std::endl;
    // std::cout << "Best instance: " << best << std::endl;
    std::vector<int> bestVars = best.getVariables();
    std::cout << "Best fitness " << bestFitness.fitness << " (rho=" << bestFitness.rho << ")"
              << " on iteration " << bestIteration << " with " << bestVars.size() << " variables: [";
    for (size_t i = 0; i < bestVars.size(); ++i) {
        if (i > 0) std::cout << ", ";
        std::cout << bestVars[i];
    }
    std::cout << "]" << std::endl;
    return best;
}

// Create an initial individual
Instance EvolutionaryAlgorithm::initialize(int instanceSize, std::vector<int> pool) { // NOLINT(readability-convert-member-functions-to-static)
    std::vector<int> data(instanceSize, -1);
    Instance instance(std::move(data), std::move(pool));

    // std::uniform_real_distribution<double> dis(0.0, 1.0);
    // std::uniform_int_distribution<size_t> dis_pool_index(0, pool.size() - 1);
    // std::vector<int> data(instanceSize, -1);
    // for (int i = 0; i < instanceSize; ++i) {
    //     while (data[i] == -1) {
    //         size_t j = dis_pool_index(gen);
    //         if (pool[j] != -1) {
    //             std::swap(data[i], pool[j]);
    //         }
    //     }
    // }
    // pool.erase(std::remove(pool.begin(), pool.end(), -1), pool.end());
    // Instance instance(data, pool);

    return instance;
}

// Calculate the fitness value of the individual
Fitness EvolutionaryAlgorithm::calculateFitness(Instance &instance) {
    Fitness fitness{};
    if (!is_cached(instance, fitness)) {
        // Delegate to instance for computing the fitness:
        fitness = instance.calculateFitness(solver);

        // Update global fitness cache:
        cache.emplace(instance.getBitmask(solver.nVars()), fitness);
    } else {
        // std::cout << "cached (global) fitness: " << fitness << std::endl;
    }

    // Update instance's local cache:
    instance._cached_fitness = std::make_optional(fitness);

    return fitness;
}

// Mutate the individual by flipping bits
void EvolutionaryAlgorithm::mutate(Instance &instance) {
    std::uniform_real_distribution<double> dis(0.0, 1.0);
    std::uniform_int_distribution<size_t> dis_index(0, instance.pool.size() - 1);

    for (size_t i = 0; i < instance.size(); ++i) {
        if (dis(gen) < (1.0 / static_cast<double>(instance.pool.size()))) {
            size_t j = dis_index(gen);
            std::swap(instance[i], instance.pool[j]);
        }
    }

    // while (instance.numVariables() < 16) {
    //     // std::cout << "numVariables = " << instance.numVariables() << ", swapping..." << std::endl;
    //     for (size_t i = 0; i < instance.size(); ++i) {
    //         if (instance[i] == -1) {
    //             auto it = std::find_if(instance.pool.begin(), instance.pool.end(), [](int value) {
    //                 return value != -1;
    //             });
    //             size_t j = std::distance(instance.pool.begin(), it);
    //             std::swap(instance[i], instance.pool[j]);
    //             break;
    //         }
    //     }
    // }
}

bool EvolutionaryAlgorithm::is_cached(const Instance &instance, Fitness &fitness) const {
    auto it = cache.find(instance.getBitmask(solver.nVars()));
    if (it != cache.end()) {
        fitness = it->second;
        return true;
    }
    return false;
}

}  // namespace Minisat
