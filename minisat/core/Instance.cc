#include "Instance.h"

namespace Minisat {

std::ostream& operator<<(std::ostream& os, const Instance& instance) {
    for (auto bit : instance) {
        os << bit;
    }
    return os;
}

}  // namespace Minisat
