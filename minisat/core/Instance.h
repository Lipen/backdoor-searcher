#ifndef INSTANCE_H
#define INSTANCE_H

#include <algorithm>
#include <iostream>
#include <optional>
#include <ostream>
#include <utility>
#include <vector>

#include "minisat/core/Fitness.h"
#include "minisat/core/Solver.h"

namespace Minisat {

class Solver;

struct Instance {
    std::vector<int> data;
    std::vector<int> pool;
    std::optional<Fitness> _cached_fitness;

    virtual ~Instance() = default;

    Instance(std::vector<int> data, std::vector<int> pool) : data(std::move(data)), pool(std::move(pool)) {}

    // Copy constructor
    Instance(const Instance &other) : data(other.data), pool(other.pool) {}

    // Copy assignment operator
    Instance &operator=(const Instance &other) {
        // Check for self-assignment
        if (this != &other) {
            data = other.data;  // copy
            pool = other.pool;  // copy
            _cached_fitness = std::nullopt;
        }
        return *this;
    }

    [[nodiscard]] int numVariables() const {
        int count = 0;
        for (int x : data) {
            if (x != -1) {
                count++;
            }
        }
        return count;
    }

    [[nodiscard]] std::vector<int> getVariables() const {
        std::vector<int> variables;
        for (int x : data) {
            if (x != -1) {
                // Note: variables in MiniSat are 0-based
                int var = (int)x;
                variables.push_back(var);
            }
        }
        std::sort(variables.begin(), variables.end());
        return variables;
    }

    [[nodiscard]] std::vector<bool> getBitmask(int numVars) const {
        std::vector<bool> bits(numVars);
        for (int var : data) {
            if (var != -1) {
                bits[var] = true;
            }
        }
        return bits;
    }

    Fitness calculateFitness(Solver &solver);

    int operator[](size_t index) const {
        return data[index];
    }

    int &operator[](size_t index) {
        return data[index];
    }

    [[nodiscard]] size_t size() const {
        return data.size();
    }

    using Iterator = std::vector<int>::iterator;
    using ConstIterator = std::vector<int>::const_iterator;

    Iterator begin() {
        return data.begin();
    }

    Iterator end() {
        return data.end();
    }

    [[nodiscard]] ConstIterator begin() const {
        return data.begin();
    }

    [[nodiscard]] ConstIterator end() const {
        return data.end();
    }

    // Streaming support
    friend std::ostream &operator<<(std::ostream &os, const Instance &instance) {
        std::vector<int> vars = instance.getVariables();
        os << "[";
        for (size_t i = 0; i < vars.size(); ++i) {
            if (i > 0) os << ",";
            os << vars[i];
        }
        os << "]";
        return os;
    }
};

}  // namespace Minisat

#endif
