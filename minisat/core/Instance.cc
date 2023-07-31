#include "minisat/core/Instance.h"

#include "minisat/core/Fitness.h"

namespace Minisat {

Fitness Instance::calculateFitness(Solver &solver) {
    if (_cached_fitness.has_value()) {
        // std::cout << "cached fitness: " << _cached_fitness << std::endl;
        return _cached_fitness.value();
    } else {
        // std::cout << "computing fitness" << std::endl;

        std::vector<int> vars = getVariables();
        // std::cout << "variables: " << vars.size() << std::endl;

        if (vars.empty()) {
            double rho = 0.0;
            double fitness = std::numeric_limits<double>::max();
            uint64_t hard = 1 << vars.size();
            return Fitness{fitness, rho, hard};
        }

        if (0) {
            bool verb = !true;

            std::cout << "- PROPCHECK" << std::endl;
            std::vector<std::vector<int>> cubes_pc;
            uint64_t total_count_pc;
            solver.gen_all_valid_assumptions_propcheck(vars, total_count_pc, cubes_pc, verb);

            std::cout << "- TREE" << std::endl;
            std::vector<std::vector<int>> cubes_tree;
            uint64_t total_count_tree;
            solver.gen_all_valid_assumptions_tree(vars, total_count_tree, cubes_tree, 0, verb);

            if (total_count_pc != total_count_tree) {
                std::cout << "total_count_pc = " << total_count_pc << std::endl;
                std::cout << "total_count_tree = " << total_count_tree << std::endl;
                std::cout << "= MISMATCH" << std::endl;
                exit(42);
            } else {
                std::cout << "total_count_pc = " << total_count_pc << std::endl;
                std::cout << "total_count_tree = " << total_count_tree << std::endl;
                std::cout << "MATCH" << std::endl;
            }
        }

        std::vector<std::vector<int>> cubes; // hard tasks
        uint64_t total_count; // number of hard tasks
        bool verb = false;
        // solver.gen_all_valid_assumptions_propcheck(vars, total_count, cubes, verb);
        solver.gen_all_valid_assumptions_tree(vars, total_count, cubes, 0, verb);

        double omega = 20;
        double magic = std::pow(2.0, omega);
        double normalizedSize = static_cast<double>(vars.size()) / static_cast<double>(pool.size());
        int numValuations = 1 << vars.size();  // 2^|B|
        // `rho` is the proportion of "easy" tasks:
        double rho = 1 - static_cast<double>(total_count) / static_cast<double>(numValuations);
        // std::cout << "rho = " << rho << std::endl;
        // std::cout << "normalized size = " << normalizedSize << std::endl;

        //! fitness = log2( rho * 2^size + (1-rho) * 2^const )
        // double fitness = std::log2(rho * numValuations + (1 - rho) * magic);
        // fitness = rho*2^size + 2^omega - rho*2^omega
        // fitness = rho*2^size - rho*2^omega
        // fitness = rho*(2^size - 2^omega)

        // fixed
        // double fitness = std::log2((1 - rho) * numValuations + (1 - rho) * magic);
        // fitness = (1-rho)*(2^size + 2^omega)

        // num hard only
        // double fitness = std::log2((1 - rho) * numValuations);

        // normalized
        // double fitness = std::log2((1 - rho) * normalizedSize);

        // multiply (1-rho) and size
        // double fitness = std::log2(1 + (1 - rho) * vars.size());

        // mutliply, square
        // double fitness = std::log2(1 + (1 - rho) * vars.size() * vars.size());

        // multiply (1-rho) and number of hard tasks
        // double fitness = std::log2(1 + (1 - rho) * static_cast<double>(total_count));

        // double fitness = std::log2(1 + (1 - rho));
        double fitness = (1 - rho);

        return Fitness{fitness, rho, total_count};
    }
}

}
