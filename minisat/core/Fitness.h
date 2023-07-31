#ifndef FITNESS_H
#define FITNESS_H

#include <cstdint>

namespace Minisat {

struct Fitness {
    double fitness;
    double rho;
    uint64_t hard;

    bool operator<(const Fitness& other) const {
        return fitness < other.fitness;
    }

    bool operator==(const Fitness& other) const {
        return fitness == other.fitness;
    }

    bool operator!=(const Fitness& other) const {
        return !(*this == other);
    }

    bool operator>(const Fitness& other) const {
        return other < *this;
    }

    bool operator>=(const Fitness& other) const {
        return !(*this < other);
    }

    bool operator<=(const Fitness& other) const {
        return !(*this > other);
    }
};

}  // namespace Minisat

#endif
