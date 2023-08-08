#include "minisat/core/EA.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <random>
#include <utility>
#include <vector>

namespace Minisat {

template <typename T>
std::ostream &operator<<(std::ostream &os, const std::vector<T> &myVector) {
    os << "[";
    bool first = true;
    for (const auto &element : myVector) {
        if (!first) os << ", ";
        os << element;
        first = false;
    }
    os << "]";
    return os;
}

template <typename T>
void printVector(const std::vector<T> &myVector, std::ostream &os = std::cout) {
    os << myVector << std::endl;
}

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
    int seed) {
    if (seed != -1) {
        gen.seed(seed);
    }

    std::cout << "Running EA for " << numIterations << " iterations..." << std::endl;
    std::cout << "instance size: " << instanceSize << std::endl;
    std::cout << "solver variables: " << solver.nVars() << std::endl;
    std::cout << "pool size: " << pool.size() << std::endl;
    std::cout << '\n';

    // Initial instance:
    Instance instance = initialize(instanceSize, std::move(pool));
    if (instance.pool.empty()) {
        std::cout << "Pool of variables is empty, cannot run!" << std::endl;
        return instance;
    }
    Fitness fit = calculateFitness(instance);
    std::cout << "Initial fitness " << fit.fitness
              << " (rho=" << fit.rho << ", hard=" << fit.hard << ")"
              << " for " << instance.numVariables() << " vars: "
              << instance
              << std::endl;

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

        Fitness mutatedFitness = calculateFitness(mutatedInstance);

        auto endTime = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);
        if (i <= 10 || (i < 1000 && i % 100 == 0) || (i < 10000 && i % 1000 == 0) || (i % 10000 == 0)) {
            std::cout << "[" << i << "/" << numIterations << "] "
                      << "Fitness " << mutatedFitness.fitness
                      << " (rho=" << mutatedFitness.rho
                      << ", hard=" << mutatedFitness.hard
                      << ") for " << mutatedInstance.numVariables() << " vars "
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
    }

    std::vector<int> bestVars = best.getVariables();
    std::cout << "Best fitness " << bestFitness.fitness
              << " (rho=" << bestFitness.rho
              << ", hard=" << bestFitness.hard
              << ") on iteration " << bestIteration
              << " with " << bestVars.size() << " variables: " << bestVars
              << std::endl;

    // Dump best to file
    std::ofstream outFile("backdoors.txt", std::ios::app);
    if (outFile.is_open()) {
        outFile << "Best fitness " << bestFitness.fitness
                << " (rho=" << bestFitness.rho
                << ", hard=" << bestFitness.hard
                << ") on iteration " << bestIteration
                << " with " << bestVars.size() << " variables: " << bestVars
                << std::endl;
        outFile.close();
    } else {
        std::cout << "Error opening the file." << std::endl;
    }

    // if (bestFitness.hard <= 16) {
    //     std::vector<std::vector<int>> cubes;
    //     uint64_t total_count;
    //     bool verb = false;
    //     // solver.gen_all_valid_assumptions_propcheck(vars, total_count, cubes, verb);
    //     solver.gen_all_valid_assumptions_tree(bestVars, total_count, cubes, (int) bestFitness.hard, verb);
    //     std::cout << "Hard tasks: " << bestFitness.hard << std::endl;
    //     for (auto&& cube : cubes) {
    //         std::cout << "[";
    //         for (size_t i = 0; i < cube.size(); ++i) {
    //             if (i > 0) std::cout << ",";
    //             std::cout<< cube[i];
    //         }
    //         std::cout << "]";
    //         std::cout << '\n';
    //     }
    //     std::cout << std::flush;
    // } else {
    //     std::cout << "Too many hard tasks (" << bestFitness.hard << ")" << std::endl;
    // }

    std::cout << "Cache hits: " << cache_hits << std::endl;
    std::cout << "Cache misses: " << cache_misses << std::endl;
    // std::cout << "Cached hits: " << cached_hits << std::endl;
    // std::cout << "Cached misses: " << cached_misses << std::endl;

    return best;
}

// Create an initial individual
Instance EvolutionaryAlgorithm::initialize(
    int instanceSize,
    std::vector<int> pool) {
    // std::vector<int> data(instanceSize, -1);
    // Instance instance(std::move(data), std::move(pool));

    std::uniform_real_distribution<double> dis(0.0, 1.0);
    std::uniform_int_distribution<size_t> dis_pool_index(0, pool.size() - 1);
    std::vector<int> data(instanceSize, -1);
    for (int i = 0; i < instanceSize; ++i) {
        while (data[i] == -1) {
            size_t j = dis_pool_index(gen);
            if (pool[j] != -1) {
                std::swap(data[i], pool[j]);
            }
        }
    }
    pool.erase(std::remove(pool.begin(), pool.end(), -1), pool.end());
    Instance instance(data, pool);

    return instance;
}

// Calculate the fitness value of the individual
Fitness EvolutionaryAlgorithm::calculateFitness(Instance &instance) {
    Fitness fitness{};
    if (!is_cached(instance, fitness)) {
        cache_misses++;
        if (instance._cached_fitness.has_value()) {
            cached_hits++;
        } else {
            cached_misses++;
        }

        // Delegate to instance for computing the fitness:
        fitness = instance.calculateFitness(solver);

        // Update global fitness cache:
        cache.emplace(instance.getVariables(), fitness);
    } else {
        // std::cout << "cached (global) fitness: " << fitness << std::endl;
        cache_hits++;
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
        if (dis(gen) < (1.0 / static_cast<double>(instance.size()))) {
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
    auto it = cache.find(instance.getVariables());
    if (it != cache.end()) {
        fitness = it->second;
        return true;
    }
    return false;
}

}  // namespace Minisat
