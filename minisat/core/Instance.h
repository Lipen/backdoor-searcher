#ifndef INSTANCE_HPP
#define INSTANCE_HPP

#include <algorithm>
#include <iostream>
#include <ostream>
#include <vector>

#include "minisat/core/Solver.h"

namespace Minisat {

struct Instance {
    std::vector<bool> data;
    double _cached_fitness = -1;

    Instance(std::vector<bool> data) : data(std::move(data)) {}
    ~Instance() {}

    // Copy constructor
    Instance(const Instance& other) : data(other.data) {}

    // Copy assignment operator
    Instance& operator=(const Instance& other) {
        if (this != &other) {  // Check for self-assignment
            data = other.data;
            _cached_fitness = -1;
        }
        return *this;
    }

    int numVariables() {
        return std::count(begin(), end(), true);
    }

    std::vector<int> getVariables() {
        std::vector<int> variables;
        for (size_t i = 0; i < size(); ++i) {
            if ((*this)[i]) {
                // Note: variables in MiniSat are 0-based, so we are just using `i` as a variable
                int var = (int)i;
                variables.push_back(var);
            }
        }
        return variables;
    }

    double calculateFitness(Solver& solver) {
        if (_cached_fitness != -1) {
            // std::cout << "cached fitness: " << _cached_fitness << std::endl;
            return _cached_fitness;
        } else {
            // std::cout << "computing fitness" << std::endl;

            std::vector<int> vars = getVariables();
            // std::cout << "variables: " << vars.size() << std::endl;

            double omega = 20;
            double magic = std::pow(2.0, omega);

            if (vars.empty()) {
                // return std::numeric_limits<double>::max();
                return std::log2(magic);
            }

            std::vector<std::vector<int>> cubes;
            uint64_t total_count;
            bool verb = !true;
            // solver.gen_all_valid_assumptions_propcheck(vars, total_count, cubes, verb);
            solver.gen_all_valid_assumptions_rc2(vars, total_count, cubes, 0, verb);

            int numValuations = 1 << vars.size();  // 2^|B|
            // `rho` is the proportion of "easy" tasks:
            double rho = 1 - static_cast<double>(total_count) / static_cast<double>(numValuations);
            // std::cout << "rho = " << rho << std::endl;

            //! fitness = log2( rho * 2^size + (1-rho) * 2^const )
            double fitness = std::log2(rho * numValuations + (1 - rho) * magic);

            return fitness;
        }
    }

    bool operator[](size_t index) const {
        return data[index];
    }
    std::vector<bool>::reference operator[](size_t index) {
        return data[index];
    }

    size_t size() const {
        return data.size();
    }

    using Iterator = std::vector<bool>::iterator;
    using ConstIterator = std::vector<bool>::const_iterator;

    Iterator begin() {
        return data.begin();
    }
    Iterator end() {
        return data.end();
    }

    ConstIterator begin() const {
        return data.begin();
    }
    ConstIterator end() const {
        return data.end();
    }

    // Streaming support
    friend std::ostream& operator<<(std::ostream& os, const Instance& instance);
};

}  // namespace Minisat

#endif
