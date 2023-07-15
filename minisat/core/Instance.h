#ifndef INSTANCE_HPP
#define INSTANCE_HPP

#include <ostream>
#include <vector>

struct Instance {
    std::vector<bool> data;
    double _cached_fitness = -1;

    Instance(std::vector<bool> data) : data(std::move(data)) {}
    ~Instance() {}

    Instance(const Instance& other) : data(other.data) {}

    std::vector<int> variables() {
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

#endif
