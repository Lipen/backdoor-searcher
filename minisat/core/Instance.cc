#include "Instance.h"

namespace Minisat {

std::ostream& operator<<(std::ostream& os, const Instance& instance) {
    for (bool bit : instance) {
        os << bit;
    }
    return os;
}

}  // namespace Minisat
