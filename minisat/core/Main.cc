/*****************************************************************************************[Main.cc]
Copyright (c) 2003-2006, Niklas Een, Niklas Sorensson
Copyright (c) 2007-2010, Niklas Sorensson

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
associated documentation files (the "Software"), to deal in the Software without restriction,
including without limitation the rights to use, copy, modify, merge, publish, distribute,
sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or
substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT
NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT
OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
**************************************************************************************************/

#include <errno.h>
#include <signal.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>
#include <set>
#include <string>
#include <sstream>

#include "minisat/core/Dimacs.h"
#include "minisat/core/EA.h"
#include "minisat/core/OutOfMemoryException.h"
#include "minisat/core/Solver.h"
#include "minisat/utils/Options.h"
#include "minisat/utils/ParseUtils.h"
#include "minisat/utils/System.h"

using namespace Minisat;

//=================================================================================================

void printStats(Solver &solver) {
    double cpu_time = cpuTime();
#ifndef __MINGW32__
    double mem_used = memUsedPeak();
#endif

#ifndef PRIi64
#ifdef __MINGW32__
#define PRIi64 "I64i"
#else
#error PRIi64 not defined
#endif
#endif

    fprintf(stderr, "restarts              : %" PRIu64 "\n", solver.starts);
    fprintf(stderr, "conflicts             : %-12" PRIu64 "   (%.0f /sec)\n", solver.conflicts,
            solver.conflicts / cpu_time);
    fprintf(stderr, "decisions             : %-12" PRIu64 "   (%4.2f %% random) (%.0f /sec)\n", solver.decisions,
            (float)solver.rnd_decisions * 100 / (float)solver.decisions, solver.decisions / cpu_time);
    fprintf(stderr, "propagations          : %-12" PRIu64 "   (%.0f /sec)\n", solver.propagations,
            solver.propagations / cpu_time);
    fprintf(stderr, "conflict literals     : %-12" PRIu64 "   (%4.2f %% deleted)\n", solver.tot_literals,
            (solver.max_literals - solver.tot_literals) * 100 / (double)solver.max_literals);
#ifndef __MINGW32__
    if (mem_used != 0) fprintf(stderr, "Memory used           : %.2f MB\n", mem_used);
#endif
    fprintf(stderr, "CPU time              : %g s\n", cpu_time);
}

static Solver *solver;
#if !(defined(__MINGW32__) || defined(_MSC_VER))
// Terminate by notifying the solver and back out gracefully. This is mainly to have a test-case
// for this feature of the Solver as it may take longer than an immediate call to '_exit()'.
static void SIGINT_interrupt(int) { solver->interrupt(); }

// Note that '_exit()' rather than 'exit()' has to be used. The reason is that 'exit()' calls
// destructors and may cause deadlocks if a malloc/free function happens to be running (these
// functions are guarded by locks for multithreaded use).
static void SIGINT_exit(int) {
    fprintf(stderr, "\n");
    fprintf(stderr, "*** INTERRUPTED ***\n");
    if (solver->verbosity > 0) {
        printStats(*solver);
        fprintf(stderr, "\n");
        fprintf(stderr, "*** INTERRUPTED ***\n");
    }
    _exit(1);
}
#endif

std::vector<int> parse_comma_separated_intervals(const std::string& input) {
    std::vector<int> result;
    std::istringstream iss(input);
    std::string part;
    while (std::getline(iss, part, ',')) {
        std::istringstream range_ss(part);
        std::string range_part;
        std::vector<std::string> range_parts;
        while (std::getline(range_ss, range_part, '-')) {
            range_parts.push_back(range_part);
        }
        if (range_parts.size() == 2) {
            int start = std::stoi(range_parts[0]);
            int end = std::stoi(range_parts[1]);
            if (start <= end) {
                for (int i = start; i <= end; ++i) {
                    result.push_back(i);
                }
            } else {
                for (int i = start; i >= end; --i) {
                    result.push_back(i);
                }
            }
        } else {
            int single = std::stoi(range_parts[0]);
            result.push_back(single);
        }
    }
    return result;
}

//=================================================================================================
// Main:

int main(int argc, char **argv) {
    try {
        setUsageHelp(
            "USAGE: %s [options] <input-file> <result-output-file>\n\n  where input may be either in plain or gzipped DIMACS.\n");
        // fprintf(stderr, "This is MiniSat 2.0 beta\n");

#if defined(__linux__) && !defined(__ANDROID__)
        fpu_control_t oldcw, newcw;
        _FPU_GETCW(oldcw);
        newcw = (oldcw & ~_FPU_EXTENDED) | _FPU_DOUBLE;
        _FPU_SETCW(newcw);
        fprintf(stderr, "WARNING: for repeatability, setting FPU to use double precision\n");
#endif
        // Extra options:
        //
        IntOption verb("MAIN", "verb", "Verbosity level (0=silent, 1=some, 2=more).",
                       1, IntRange(0, 2));
        IntOption cpu_lim("MAIN", "cpu-lim", "Limit on CPU time allowed in seconds.\n",
                          INT32_MAX, IntRange(0, INT32_MAX));
        IntOption mem_lim("MAIN", "mem-lim", "Limit on memory usage in megabytes.\n",
                          INT32_MAX, IntRange(0, INT32_MAX));
        IntOption ea_seed("EA", "ea-seed", "Seed for EA.\n",
                          42, IntRange(0, INT32_MAX));
        IntOption ea_num_runs("EA", "ea-num-runs", "Number of EA runs.\n",
                              1, IntRange(0, INT32_MAX));
        IntOption ea_num_iterations("EA", "ea-num-iters", "Number of EA iterations in each run.\n",
                                    1000, IntRange(0, INT32_MAX));
        IntOption ea_instance_size("EA", "ea-instance-size", "Instance size in EA.\n",
                                   10, IntRange(1, INT32_MAX));
        StringOption ea_vars("EA", "ea-vars", "Comma-separated list of non-negative 0-based variable indices to use for EA.");
        StringOption ea_bans("EA", "ea-bans", "Comma-separated list of non-negative 0-based variable indices to ban in EA.");
        StringOption ea_output_path("EA", "ea-output-path", "Output file with backdoors found by EA. Each line contains the best backdoor for each EA run.\n", "backdoors.txt");

        parseOptions(argc, argv, true);

        Solver S;
        double initial_time = cpuTime();

        S.verbosity = verb;

        solver = &S;
        // Use signal handlers that forcibly quit until the solver will be able to respond to
        // interrupts:
#if !(defined(__MINGW32__) || defined(_MSC_VER))
        signal(SIGINT, SIGINT_exit);
        signal(SIGXCPU, SIGINT_exit);

        // Set limit on CPU-time:
        if (cpu_lim != INT32_MAX) {
            rlimit rl;
            getrlimit(RLIMIT_CPU, &rl);
            if (rl.rlim_max == RLIM_INFINITY || (rlim_t)cpu_lim < rl.rlim_max) {
                rl.rlim_cur = cpu_lim;
                if (setrlimit(RLIMIT_CPU, &rl) == -1)
                    fprintf(stderr, "WARNING! Could not set resource limit: CPU-time.\n");
            }
        }

        // Set limit on virtual memory:
        if (mem_lim != INT32_MAX) {
            rlim_t new_mem_lim = (rlim_t)mem_lim * 1024 * 1024;
            rlimit rl;
            getrlimit(RLIMIT_AS, &rl);
            if (rl.rlim_max == RLIM_INFINITY || new_mem_lim < rl.rlim_max) {
                rl.rlim_cur = new_mem_lim;
                if (setrlimit(RLIMIT_AS, &rl) == -1)
                    fprintf(stderr, "WARNING! Could not set resource limit: Virtual memory.\n");
            }
        }
#endif

        if (argc == 1)
            fprintf(stderr, "Reading from standard input... Use '--help' for help.\n");

        FILE *in = (argc == 1) ? fdopen(0, "rb") : fopen(argv[1], "rb");
        if (in == NULL)
            fprintf(stderr, "ERROR! Could not open file: %s\n", argc == 1 ? "<stdin>" : argv[1]), exit(1);

        if (S.verbosity > 0) {
            fprintf(stderr, "============================[ Problem Statistics ]=============================\n");
            fprintf(stderr, "|                                                                             |\n");
        }

        parse_DIMACS(in, S);
        fclose(in);
        FILE *res = argc >= 3 ? fopen(argv[2], "wb") : stdout;

        if (S.verbosity > 0) {
            fprintf(stderr, "|  Number of variables:  %12d                                         |\n", S.nVars());
            fprintf(stderr, "|  Number of clauses:    %12d                                         |\n", S.nClauses());
        }

        double parsed_time = cpuTime();
        if (S.verbosity > 0) {
            fprintf(stderr, "|  Parse time:           %12.2f s                                       |\n",
                    parsed_time - initial_time);
            fprintf(stderr, "|                                                                             |\n");
        }

        // Change to signal-handlers that will only notify the solver and allow it to terminate
        // voluntarily:
// #if !(defined(__MINGW32__) || defined(_MSC_VER))
//         signal(SIGINT, SIGINT_interrupt);
//         signal(SIGXCPU, SIGINT_interrupt);
// #endif

        if (!S.simplify()) {
            if (res != NULL) fprintf(res, "UNSAT\n"), fclose(res);
            if (S.verbosity > 0) {
                fprintf(stderr,
                        "===============================================================================\n");
                fprintf(stderr, "Solved by unit propagation\n");
                printStats(S);
                fprintf(stderr, "\n");
            }
            fprintf(stderr, "UNSATISFIABLE\n");
            exit(20);
        }

        if (1) {
            // Truncate the "backdoors" file beforehand:
            std::ofstream outFile((const char *)ea_output_path, std::ios::out | std::ios::trunc);
            if (outFile.is_open()) {
                outFile.close();
            } else {
                std::cerr << "Error opening the file." << std::endl;
                return 1;
            }

            if (1) {
                auto startTime = std::chrono::high_resolution_clock::now();
                EvolutionaryAlgorithm ea(S, ea_seed);

                // Determine holes in the original CNF:
                std::vector<bool> hole(S.nVars(), true);
                for (ClauseIterator it = S.clausesBegin(); it != S.clausesEnd(); ++it) {
                    const Clause& c = *it;
                    for (int i = 0; i < c.size(); ++i) {
                        Var v = var(c[i]);
                        hole[v] = false;
                    }
                }

                // Ban the variables passed via '-ea-bans' option:
                std::vector<bool> banned(S.nVars(), false);
                if (ea_bans != NULL) {
                    std::vector<int> vars = parse_comma_separated_intervals((const char*) ea_bans);
                    for (Var v : vars) {
                        banned[v] = true;
                    }
                }

                // Note: variables in MiniSat are 0-based!

                std::set<int> possible_vars;
                if (ea_vars != NULL) {
                    std::vector<int> vars = parse_comma_separated_intervals((const char*) ea_vars);
                    std::copy(vars.begin(), vars.end(), std::inserter(possible_vars, possible_vars.end()));
                } else {
                    for (Var v = 0; v < S.nVars(); ++v) {
                        possible_vars.insert(v);
                    }
                }
                // std::cout << "Possible vars: " << possible_vars.size() << std::endl;

                std::vector<Var> pool;

                for (Var v : possible_vars) {
                    // Skip the "holes":
                    if (hole[v] && S.value(v) == l_Undef) {
                        if (S.verbosity > 1) {
                            std::cout << "Skipping hole " << v << std::endl;
                        }
                        continue;
                    }

                    // Skip banned variables:
                    if (banned[v]) {
                        if (S.verbosity > 1) {
                            std::cout << "Skipping banned variable " << v << std::endl;
                        }
                        continue;
                    }

                    // Skip already assigned variables:
                    if (S.value(v) != l_Undef) {
                        if (S.verbosity > 1) {
                            std::cout << "Skipping variable " << v
                                      << " already assigned to "
                                      << (S.value(v).isTrue() ? "TRUE" : "FALSE")
                                      << std::endl;
                        }
                        continue;
                    }

                    // Add suitable variable to the pool:
                    pool.push_back(v);
                }

                std::sort(pool.begin(), pool.end());
                if (S.verbosity > 0) {
                    std::cout << "Pool size: " << pool.size() << std::endl;
                }

                // Run EA
                std::cout << "\n=== [" << 1 << "/" << ea_num_runs << "]"
                          << " -------------------------------------\n\n";
                Instance best = ea.run(ea_num_iterations, ea_instance_size, pool, (const char *)ea_output_path);

                for (int i = 2; i <= ea_num_runs; ++i) {
                    // Forbid already used variables:
                    // std::vector<int> vars = best.getVariables();
                    // std::sort(vars.begin(), vars.end());
                    // std::vector<int> difference;
                    // std::set_difference(pool.begin(), pool.end(),
                    //                     vars.begin(), vars.end(),
                    //                     std::back_inserter(difference));
                    // pool = difference;

                    // Another run of EA
                    std::cout << "\n=== [" << i << "/" << ea_num_runs << "]"
                              << " -------------------------------------\n\n";
                    best = ea.run(ea_num_iterations, ea_instance_size, pool, (const char *)ea_output_path);
                }

                auto endTime = std::chrono::high_resolution_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
                std::cout << "\nDone " << ea_num_runs << " EA runs"
                          << " in " << duration / 1000.0 << " s"
                          << std::endl;

                if (S.verbosity > 0) {
                    fprintf(stderr, "\n");
                    printStats(S);
                }

            } else {
                // ------------------------------------------------------

                EvolutionaryAlgorithm ea(S, ea_seed);
                S.ea = &ea;

                // ------------------------------------------------------

                vec<Lit> dummy;
                lbool ret = S.solveLimited(dummy);
                if (S.verbosity > 0) {
                    printStats(S);
                    fprintf(stderr, "\n");
                }
                fprintf(stderr, ret == l_True ? "SATISFIABLE\n" : ret == l_False ? "UNSATISFIABLE\n"
                                                                                 : "INDETERMINATE\n");
                if (res != NULL) {
                    if (ret == l_True) {
                        fprintf(res, "SAT\n");
                        for (int i = 0; i < S.nVars(); i++)
                            if (S.model[i] != l_Undef)
                                fprintf(res, "%s%s%d", (i == 0) ? "" : " ", (S.model[i] == l_True) ? "" : "-", i + 1);
                        fprintf(res, " 0\n");
                    } else if (ret == l_False)
                        fprintf(res, "UNSAT\n");
                    else
                        fprintf(res, "INDET\n");
                    fclose(res);
                }

#ifdef NDEBUG
                exit(ret == l_True ? 10 : ret == l_False ? 20
                                                         : 0);  // (faster than "return", which will invoke the destructor for 'Solver')
#else
                return (ret == l_True ? 10 : ret == l_False ? 20
                                                            : 0);
#endif
            }
        } else {
            std::vector<int> vars;
            for (int i = 0; i < 24; ++i) {
                vars.push_back(i);
            }
            std::reverse(vars.begin(), vars.end());

            std::vector<std::vector<int>> cubes;  // hard tasks
            uint64_t total_count;                 // number of hard tasks
            S.gen_all_valid_assumptions_tree(vars, total_count, cubes, 0, true);

            std::cout << "hard tasks: " << cubes.size() << std::endl;
        }

    } catch (OutOfMemoryException &) {
        fprintf(stderr, "===============================================================================\n");
        fprintf(stderr, "INDETERMINATE\n");
        exit(0);
    }
}
