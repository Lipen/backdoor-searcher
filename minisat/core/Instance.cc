#include "Instance.h"

std::ostream& operator<<(std::ostream& os, const Instance& instance) {
    for (auto bit : instance) {
        os << bit;
    }
    return os;
}
