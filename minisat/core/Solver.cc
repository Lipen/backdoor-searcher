/***************************************************************************************[Solver.cc]
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

#include "minisat/core/Solver.h"

#include <math.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#include "minisat/core/EA.h"
#include "minisat/mtl/Sort.h"

using namespace Minisat;

//=================================================================================================
// Options:

static const char* _cat = "CORE";

static DoubleOption opt_var_decay(_cat, "var-decay", "The variable activity decay factor", 0.95, DoubleRange(0, false, 1, false));
static DoubleOption opt_clause_decay(_cat, "cla-decay", "The clause activity decay factor", 0.999, DoubleRange(0, false, 1, false));
static DoubleOption opt_random_var_freq(_cat, "rnd-freq", "The frequency with which the decision heuristic tries to choose a random variable", 0, DoubleRange(0, true, 1, true));
static DoubleOption opt_random_seed(_cat, "rnd-seed", "Used by the random variable selection", 91648253, DoubleRange(0, false, HUGE_VAL, false));
static IntOption opt_ccmin_mode(_cat, "ccmin-mode", "Controls conflict clause minimization (0=none, 1=basic, 2=deep)", 2, IntRange(0, 2));
static IntOption opt_phase_saving(_cat, "phase-saving", "Controls the level of phase saving (0=none, 1=limited, 2=full)", 2, IntRange(0, 2));
static BoolOption opt_rnd_init_act(_cat, "rnd-init", "Randomize the initial activity", false);
static BoolOption opt_luby_restart(_cat, "luby", "Use the Luby restart sequence", true);
static IntOption opt_restart_first(_cat, "rfirst", "The base restart interval", 100, IntRange(1, INT32_MAX));
static DoubleOption opt_restart_inc(_cat, "rinc", "Restart interval increase factor", 2, DoubleRange(1, false, HUGE_VAL, false));
static DoubleOption opt_garbage_frac(_cat, "gc-frac", "The fraction of wasted memory allowed before a garbage collection is triggered", 0.20, DoubleRange(0, false, HUGE_VAL, false));

//=================================================================================================
// Constructor/Destructor:

Solver::Solver() :

                   // Parameters (user settable):
                   //
                   verbosity(0),
                   var_decay(opt_var_decay),
                   clause_decay(opt_clause_decay),
                   random_var_freq(opt_random_var_freq),
                   random_seed(opt_random_seed),
                   luby_restart(opt_luby_restart),
                   ccmin_mode(opt_ccmin_mode),
                   phase_saving(opt_phase_saving),
                   rnd_pol(false),
                   rnd_init_act(opt_rnd_init_act),
                   garbage_frac(opt_garbage_frac),
                   restart_first(opt_restart_first),
                   restart_inc(opt_restart_inc)

                   // Parameters (the rest):
                   //
                   ,
                   learntsize_factor((double)1 / (double)3),
                   learntsize_inc(1.1)

                   // Parameters (experimental):
                   //
                   ,
                   learntsize_adjust_start_confl(100),
                   learntsize_adjust_inc(1.5)

                   // Statistics: (formerly in 'SolverStats')
                   //
                   ,
                   solves(0),
                   starts(0),
                   decisions(0),
                   rnd_decisions(0),
                   propagations(0),
                   conflicts(0),
                   dec_vars(0),
                   clauses_literals(0),
                   learnts_literals(0),
                   max_literals(0),
                   tot_literals(0)

                   ,
                   ok(true),
                   cla_inc(1),
                   var_inc(1),
                   watches(WatcherDeleted(ca)),
                   qhead(0),
                   simpDB_assigns(-1),
                   simpDB_props(0),
                   order_heap(VarOrderLt(activity)),
                   progress_estimate(0),
                   remove_satisfied(true),

                   // Resource constraints:
                   //
                   conflict_budget(-1),
                   propagation_budget(-1),
                   asynch_interrupt(false) {}

Solver::~Solver() {
}

//=================================================================================================
// Minor methods:

// Creates a new SAT variable in the solver. If 'decision' is cleared, variable will not be
// used as a decision variable (NOTE! This has effects on the meaning of a SATISFIABLE result).
//
Var Solver::newVar(bool sign, bool dvar) {
    int v = nVars();
    watches.init(mkLit(v, false));
    watches.init(mkLit(v, true));
    assigns.push(l_Undef);
    vardata.push(mkVarData(CRef_Undef, 0));
    // activity .push(0);
    activity.push(rnd_init_act ? drand(random_seed) * 0.00001 : 0);
    seen.push(0);
    polarity.push(sign);
    decision.push();
    trail.capacity(v + 1);
    setDecisionVar(v, dvar);
    return v;
}

bool Solver::addClause_(vec<Lit>& ps) {
    assert(decisionLevel() == 0);
    if (!ok) return false;

    // Check if clause is satisfied and remove false/duplicate literals:

    // We can skip sorting small clauses:
    // * 1 is always sorted!
    // * 2 is not always sorted, but for the unique-like loop below,
    //   it is good enough.
    if (ps.size() > 2) {
        sort(ps);
    }
    Lit p = lit_Undef;
    auto i = ps.begin();
    auto j = ps.begin();
    auto end = ps.end();
    while (i != end) {
        if (value(*i) == l_True || *i == ~p) {
            return true;
        } else if (value(*i) != l_False && *i != p) {
            *j = p = *i;
            ++j;
        }
        ++i;
    }
    ps.truncate(j);

    if (ps.empty()) {
        return ok = false;
    } else if (ps.size() == 1) {
        uncheckedEnqueue(ps[0]);
        return ok = (propagate() == CRef_Undef);
    } else {
        CRef cr = ca.alloc(ps, false);
        clauses.push(cr);
        attachClause(cr);
    }

    return true;
}

void Solver::attachClause(CRef cr) {
    const Clause& c = ca[cr];
    assert(c.size() > 1);
    watches[~c[0]].push(Watcher(cr, c[1]));
    watches[~c[1]].push(Watcher(cr, c[0]));
    if (c.learnt())
        learnts_literals += c.size();
    else
        clauses_literals += c.size();
}

void Solver::detachClause(CRef cr, bool strict) {
    const Clause& c = ca[cr];
    assert(c.size() > 1);

    if (strict) {
        remove(watches[~c[0]], Watcher(cr, c[1]));
        remove(watches[~c[1]], Watcher(cr, c[0]));
    } else {
        // Lazy detaching: (NOTE! Must clean all watcher lists before garbage collecting this clause)
        watches.smudge(~c[0]);
        watches.smudge(~c[1]);
    }

    if (c.learnt())
        learnts_literals -= c.size();
    else
        clauses_literals -= c.size();
}

void Solver::removeClause(CRef cr) {
    Clause& c = ca[cr];
    detachClause(cr);
    // Don't leave pointers to free'd memory!
    if (locked(c)) vardata[var(c[0])].reason = CRef_Undef;
    c.mark(1);
    ca.free(cr);
}

bool Solver::satisfied(const Clause& c) const {
    for (int i = 0; i < c.size(); i++)
        if (value(c[i]) == l_True)
            return true;
    return false;
}

// Revert to the state at given level (keeping all assignment at 'level' but not beyond).
//
void Solver::cancelUntil(int level) {
    if (decisionLevel() > level) {
        for (int c = trail.size() - 1; c >= trail_lim[level]; c--) {
            Var x = var(trail[c]);
            assigns[x] = l_Undef;
            if (phase_saving > 1 || ((phase_saving == 1) && c > trail_lim.last()))
                polarity[x] = sign(trail[c]);
            insertVarOrder(x);
        }
        qhead = trail_lim[level];
        trail.shrink(trail.size() - trail_lim[level]);
        trail_lim.shrink(trail_lim.size() - level);
    }
}

//=================================================================================================
// Major methods:

Lit Solver::pickBranchLit() {
    Var next = var_Undef;

    // Random decision:
    if (drand(random_seed) < random_var_freq && !order_heap.empty()) {
        next = order_heap[irand(random_seed, order_heap.size())];
        if (value(next) == l_Undef && decision[next])
            rnd_decisions++;
    }

    // Activity based decision:
    while (next == var_Undef || value(next) != l_Undef || !decision[next])
        if (order_heap.empty()) {
            next = var_Undef;
            break;
        } else
            next = order_heap.removeMin();

    return next == var_Undef ? lit_Undef : mkLit(next, rnd_pol ? drand(random_seed) < 0.5 : polarity[next]);
}

/*_________________________________________________________________________________________________
|
|  analyze : (confl : Clause*) (out_learnt : vec<Lit>&) (out_btlevel : int&)  ->  [void]
|
|  Description:
|    Analyze conflict and produce a reason clause.
|
|    Pre-conditions:
|      * 'out_learnt' is assumed to be cleared.
|      * Current decision level must be greater than root level.
|
|    Post-conditions:
|      * 'out_learnt[0]' is the asserting literal at level 'out_btlevel'.
|      * If out_learnt.size() > 1 then 'out_learnt[1]' has the greatest decision level of the
|        rest of literals. There may be others from the same level though.
|
|________________________________________________________________________________________________@*/
void Solver::analyze(CRef confl, vec<Lit>& out_learnt, int& out_btlevel) {
    int pathC = 0;
    Lit p = lit_Undef;

    // Generate conflict clause:
    //
    out_learnt.push();  // (leave room for the asserting literal)
    int index = trail.size() - 1;

    do {
        assert(confl != CRef_Undef);  // (otherwise should be UIP)
        Clause& c = ca[confl];

        if (c.learnt())
            claBumpActivity(c);

        for (int j = (p == lit_Undef) ? 0 : 1; j < c.size(); j++) {
            Lit q = c[j];

            if (!seen[var(q)] && level(var(q)) > 0) {
                varBumpActivity(var(q));
                seen[var(q)] = 1;
                if (level(var(q)) >= decisionLevel())
                    pathC++;
                else
                    out_learnt.push(q);
            }
        }

        // Select next clause to look at:
        while (!seen[var(trail[index--])])
            ;
        p = trail[index + 1];
        confl = reason(var(p));
        seen[var(p)] = 0;
        pathC--;

    } while (pathC > 0);
    out_learnt[0] = ~p;

    // Simplify conflict clause:
    //
    int i, j;
    out_learnt.copyTo(analyze_toclear);
    if (ccmin_mode == 2) {
        uint32_t abstract_level = 0;
        for (i = 1; i < out_learnt.size(); i++)
            abstract_level |= abstractLevel(var(out_learnt[i]));  // (maintain an abstraction of levels involved in conflict)

        for (i = j = 1; i < out_learnt.size(); i++)
            if (reason(var(out_learnt[i])) == CRef_Undef || !litRedundant(out_learnt[i], abstract_level))
                out_learnt[j++] = out_learnt[i];

    } else if (ccmin_mode == 1) {
        for (i = j = 1; i < out_learnt.size(); i++) {
            Var x = var(out_learnt[i]);

            if (reason(x) == CRef_Undef)
                out_learnt[j++] = out_learnt[i];
            else {
                Clause& c = ca[reason(var(out_learnt[i]))];
                for (int k = 1; k < c.size(); k++)
                    if (!seen[var(c[k])] && level(var(c[k])) > 0) {
                        out_learnt[j++] = out_learnt[i];
                        break;
                    }
            }
        }
    } else
        i = j = out_learnt.size();

    max_literals += out_learnt.size();
    out_learnt.shrink(i - j);
    tot_literals += out_learnt.size();

    // Find correct backtrack level:
    //
    if (out_learnt.size() == 1)
        out_btlevel = 0;
    else {
        int max_idx = 1;
        int min_lvl = level(var(out_learnt[1]));
        // Find the first literal assigned at the next-highest level:
        for (int idx = 2; idx < out_learnt.size(); idx++) {
            if (level(var(out_learnt[idx])) > min_lvl) {
                min_lvl = level(var(out_learnt[idx]));
                max_idx = idx;
            }
        }
        // Swap-in this literal at index 1:
        Lit temp = out_learnt[max_idx];
        out_learnt[max_idx] = out_learnt[1];
        out_learnt[1] = temp;
        out_btlevel = min_lvl;
    }

    for (auto const& elem : analyze_toclear) {
        seen[var(elem)] = 0;
    }
}

// Check if 'p' can be removed. 'abstract_levels' is used to abort early if the algorithm is
// visiting literals at levels that cannot be removed later.
bool Solver::litRedundant(Lit p, uint32_t abstract_levels) {
    analyze_stack.clear();
    analyze_stack.push(p);
    int top = analyze_toclear.size();
    while (analyze_stack.size() > 0) {
        assert(reason(var(analyze_stack.last())) != CRef_Undef);
        Clause& c = ca[reason(var(analyze_stack.last()))];
        analyze_stack.pop();

        for (int i = 1; i < c.size(); i++) {
            Lit cp = c[i];
            if (!seen[var(cp)] && level(var(cp)) > 0) {
                if (reason(var(cp)) != CRef_Undef && (abstractLevel(var(cp)) & abstract_levels) != 0) {
                    seen[var(cp)] = 1;
                    analyze_stack.push(cp);
                    analyze_toclear.push(cp);
                } else {
                    for (int j = top; j < analyze_toclear.size(); j++)
                        seen[var(analyze_toclear[j])] = 0;
                    analyze_toclear.shrink(analyze_toclear.size() - top);
                    return false;
                }
            }
        }
    }

    return true;
}

/*_________________________________________________________________________________________________
|
|  analyzeFinal : (p : Lit)  ->  [void]
|
|  Description:
|    Specialized analysis procedure to express the final conflict in terms of assumptions.
|    Calculates the (possibly empty) set of assumptions that led to the assignment of 'p', and
|    stores the result in 'out_conflict'.
|________________________________________________________________________________________________@*/
void Solver::analyzeFinal(Lit p, vec<Lit>& out_conflict) {
    out_conflict.clear();
    out_conflict.push(p);

    if (decisionLevel() == 0)
        return;

    seen[var(p)] = 1;

    for (int i = trail.size() - 1; i >= trail_lim[0]; i--) {
        Var x = var(trail[i]);
        if (seen[x]) {
            if (reason(x) == CRef_Undef) {
                assert(level(x) > 0);
                out_conflict.push(~trail[i]);
            } else {
                Clause& c = ca[reason(x)];
                for (int j = 1; j < c.size(); j++)
                    if (level(var(c[j])) > 0)
                        seen[var(c[j])] = 1;
            }
            seen[x] = 0;
        }
    }

    seen[var(p)] = 0;
}

void Solver::uncheckedEnqueue(Lit p, CRef from) {
    assert(value(p) == l_Undef);
    assigns[var(p)] = lbool(!sign(p));
    vardata[var(p)] = mkVarData(from, decisionLevel());
    trail.push_(p);
}

/*_________________________________________________________________________________________________
|
|  propagate : [void]  ->  [Clause*]
|
|  Description:
|    Propagates all enqueued facts. If a conflict arises, the conflicting clause is returned,
|    otherwise CRef_Undef.
|
|    Post-conditions:
|      * the propagation queue is empty, even if there was a conflict.
|________________________________________________________________________________________________@*/
CRef Solver::propagate() {
    CRef confl = CRef_Undef;
    int num_props = 0;
    watches.cleanAll();

    while (qhead < trail.size()) {
        Lit p = trail[qhead++];  // 'p' is enqueued fact to propagate.
        vec<Watcher>& ws = watches[p];
        Watcher *i, *j, *end;
        num_props++;

        for (i = j = (Watcher*)ws, end = i + ws.size(); i != end;) {
            // Try to avoid inspecting the clause:
            Lit blocker = i->blocker;
            if (value(blocker) == l_True) {
                *j++ = *i++;
                continue;
            }

            // Make sure the false literal is data[1]:
            CRef cr = i->cref;
            Clause& c = ca[cr];
            Lit false_lit = ~p;
            if (c[0] == false_lit)
                c[0] = c[1], c[1] = false_lit;
            assert(c[1] == false_lit);
            i++;

            // If 0th watch is true, then clause is already satisfied.
            Lit first = c[0];
            Watcher w = Watcher(cr, first);
            if (first != blocker && value(first) == l_True) {
                *j++ = w;
                continue;
            }

            // Look for new watch:
            for (int k = 2; k < c.size(); k++)
                if (value(c[k]) != l_False) {
                    c[1] = c[k];
                    c[k] = false_lit;
                    watches[~c[1]].push(w);
                    goto NextClause;
                }

            // Did not find watch -- clause is unit under assignment:
            *j++ = w;
            if (value(first) == l_False) {
                confl = cr;
                qhead = trail.size();
                // Copy the remaining watches:
                while (i < end)
                    *j++ = *i++;
            } else
                uncheckedEnqueue(first, cr);

        NextClause:;
        }
        ws.truncate(j);
    }
    propagations += num_props;
    simpDB_props -= num_props;

    return confl;
}

/*_________________________________________________________________________________________________
|
|  reduceDB : ()  ->  [void]
|
|  Description:
|    Remove half of the learnt clauses, minus the clauses locked by the current assignment. Locked
|    clauses are clauses that are reason to some assignment. Binary clauses are never removed.
|________________________________________________________________________________________________@*/
namespace {
struct reduceDB_lt {
    ClauseAllocator& ca;
    reduceDB_lt(ClauseAllocator& ca_) : ca(ca_) {}
    bool operator()(CRef x, CRef y) {
        return ca[x].size() > 2 && (ca[y].size() == 2 || ca[x].activity() < ca[y].activity());
    }
};
}  // namespace
void Solver::reduceDB() {
    int i, j;
    double extra_lim = cla_inc / learnts.size();  // Remove any clause below this activity

    sort(learnts, reduceDB_lt(ca));
    // Don't delete binary or locked clauses. From the rest, delete clauses from the first half
    // and clauses with activity smaller than 'extra_lim':
    for (i = j = 0; i < learnts.size(); i++) {
        Clause& c = ca[learnts[i]];
        if (c.size() > 2 && !locked(c) && (i < learnts.size() / 2 || c.activity() < extra_lim))
            removeClause(learnts[i]);
        else
            learnts[j++] = learnts[i];
    }
    learnts.shrink(i - j);
    checkGarbage();
}

void Solver::removeSatisfied(vec<CRef>& cs) {
    auto i = cs.begin();
    auto j = cs.begin();
    auto end = cs.end();
    while (i != end) {
        Clause& c = ca[*i];
        if (satisfied(c)) {
            removeClause(*i);
        } else {
            *j = *i;
            ++j;
        }
        ++i;
    }
    cs.truncate(j);
}

void Solver::rebuildOrderHeap() {
    vec<Var> vs;
    for (Var v = 0; v < nVars(); v++)
        if (decision[v] && value(v) == l_Undef)
            vs.push(v);
    order_heap.build(vs);
}

/*_________________________________________________________________________________________________
|
|  simplify : [void]  ->  [bool]
|
|  Description:
|    Simplify the clause database according to the current top-level assigment. Currently, the only
|    thing done here is the removal of satisfied clauses, but more things can be put here.
|________________________________________________________________________________________________@*/
bool Solver::simplify() {
    assert(decisionLevel() == 0);

    if (!ok || propagate() != CRef_Undef)
        return ok = false;

    if (nAssigns() == simpDB_assigns || (simpDB_props > 0))
        return true;

    // Remove satisfied clauses:
    removeSatisfied(learnts);
    if (remove_satisfied)  // Can be turned off.
        removeSatisfied(clauses);
    checkGarbage();
    rebuildOrderHeap();

    simpDB_assigns = nAssigns();
    simpDB_props = clauses_literals + learnts_literals;  // (shouldn't depend on stats really, but it will do for now)

    return true;
}

/*_________________________________________________________________________________________________
|
|  search : (nof_conflicts : int) (params : const SearchParams&)  ->  [lbool]
|
|  Description:
|    Search for a model the specified number of conflicts.
|    NOTE! Use negative value for 'nof_conflicts' indicate infinity.
|
|  Output:
|    'l_True' if a partial assigment that is consistent with respect to the clauseset is found. If
|    all variables are decision variables, this means that the clause set is satisfiable. 'l_False'
|    if the clause set is unsatisfiable. 'l_Undef' if the bound on number of conflicts is reached.
|________________________________________________________________________________________________@*/
lbool Solver::search(int nof_conflicts) {
    assert(ok);
    int backtrack_level;
    int conflictC = 0;
    vec<Lit> learnt_clause;
    starts++;

    for (;;) {
        CRef confl = propagate();
        if (confl != CRef_Undef) {
            // CONFLICT
            conflicts++;
            conflictC++;
            if (decisionLevel() == 0) return l_False;

            learnt_clause.clear();
            analyze(confl, learnt_clause, backtrack_level);
            cancelUntil(backtrack_level);

            if (learnt_clause.size() == 1) {
                uncheckedEnqueue(learnt_clause[0]);
            } else {
                CRef cr = ca.alloc(learnt_clause, true);
                learnts.push(cr);
                attachClause(cr);
                claBumpActivity(ca[cr]);
                uncheckedEnqueue(learnt_clause[0], cr);
            }

            varDecayActivity();
            claDecayActivity();

            if (--learntsize_adjust_cnt == 0) {
                learntsize_adjust_confl *= learntsize_adjust_inc;
                learntsize_adjust_cnt = (int)learntsize_adjust_confl;
                max_learnts *= learntsize_inc;

                if (verbosity >= 1)
                    fprintf(stderr, "| %9d | %7d %8d %8d | %8d %8d %6.0f | %6.3f %% |\n",
                            (int)conflicts,
                            (int)dec_vars - (trail_lim.empty() ? trail.size() : trail_lim[0]), nClauses(), (int)clauses_literals,
                            (int)max_learnts, nLearnts(), (double)learnts_literals / nLearnts(), progressEstimate() * 100);
            }

        } else {
            // NO CONFLICT
            if (nof_conflicts >= 0 && (conflictC >= nof_conflicts || !withinBudget())) {
                // Reached bound on number of conflicts:
                progress_estimate = progressEstimate();
                cancelUntil(0);
                return l_Undef;
            }

            // Simplify the set of problem clauses:
            if (decisionLevel() == 0 && !simplify())
                return l_False;

            if (learnts.size() - nAssigns() >= max_learnts)
                // Reduce the set of learnt clauses:
                reduceDB();

            Lit next = lit_Undef;
            while (decisionLevel() < assumptions.size()) {
                // Perform user provided assumption:
                Lit p = assumptions[decisionLevel()];
                if (value(p) == l_True) {
                    // Dummy decision level:
                    newDecisionLevel();
                } else if (value(p) == l_False) {
                    analyzeFinal(~p, conflict);
                    return l_False;
                } else {
                    next = p;
                    break;
                }
            }

            if (next == lit_Undef) {
                // New variable decision:
                decisions++;
                next = pickBranchLit();

                if (next == lit_Undef)
                    // Model found:
                    return l_True;
            }

            // Increase decision level and enqueue 'next'
            newDecisionLevel();
            uncheckedEnqueue(next);
        }
    }
}

double Solver::progressEstimate() const {
    double progress = 0;
    double F = 1.0 / nVars();

    for (int i = 0; i <= decisionLevel(); i++) {
        int beg = i == 0 ? 0 : trail_lim[i - 1];
        int end = i == decisionLevel() ? trail.size() : trail_lim[i];
        progress += pow(F, i) * (end - beg);
    }

    return progress / nVars();
}

/*
  Finite subsequences of the Luby-sequence:

  0: 1
  1: 1 1 2
  2: 1 1 2 1 1 2 4
  3: 1 1 2 1 1 2 4 1 1 2 1 1 2 4 8
  ...


 */

static double luby(double y, int x) {
    // Find the finite subsequence that contains index 'x', and the
    // size of that subsequence:
    int size, seq;
    for (size = 1, seq = 0; size < x + 1; seq++, size = 2 * size + 1)
        ;

    while (size - 1 != x) {
        size = (size - 1) >> 1;
        seq--;
        x = x % size;
    }

    return pow(y, seq);
}

// NOTE: assumptions passed in member-variable 'assumptions'.
lbool Solver::solve_() {
    model.clear();
    conflict.clear();
    if (!ok) return l_False;

    solves++;

    max_learnts = nClauses() * learntsize_factor;
    learntsize_adjust_confl = learntsize_adjust_start_confl;
    learntsize_adjust_cnt = (int)learntsize_adjust_confl;
    lbool status = l_Undef;

    if (verbosity >= 1) {
        fprintf(stderr, "============================[ Search Statistics ]==============================\n");
        fprintf(stderr, "| Conflicts |          ORIGINAL         |          LEARNT          | Progress |\n");
        fprintf(stderr, "|           |    Vars  Clauses Literals |    Limit  Clauses Lit/Cl |          |\n");
        fprintf(stderr, "===============================================================================\n");
    }

    auto startTime = std::chrono::steady_clock::now();
    bool first = true;
    int runNumber = 0;

    // Search:
    int curr_restarts = 0;
    while (status == l_Undef) {
        // Run EA on restart:
        if (ea) {
            auto currentTime = std::chrono::steady_clock::now();
            auto elapsedTime = std::chrono::duration_cast<std::chrono::seconds>(currentTime - startTime).count();
            if (first || elapsedTime > 5 * 60) {
                first = false;
                cancelUntil(0);

                std::vector<int> pool;
                pool.reserve(nVars());
                for (int i = 0; i < nVars(); ++i) {
                    if (value(i) == l_Undef) {
                        pool.push_back(i);
                    }
                }

                std::ofstream outFile("backdoors.txt", std::ios::app);
                if (outFile.is_open()) {
                    outFile << "---" << std::endl;
                } else {
                    std::cout << "Error opening the file." << std::endl;
                }
                outFile.close();

                std::string learntFilename = "learnts-" + std::to_string(runNumber) + ".txt";
                std::cout << "Dumping " << learnts.size() << " learnts to '" << learntFilename << "'" << std::endl;
                std::ofstream learntFile(learntFilename, std::ios::out | std::ios::trunc);
                for (CRef ref : learnts) {
                    Clause& learnt = ca[ref];
                    for (int i = 0; i < learnt.size(); ++i) {
                        int v = var(learnt[i]) + 1;
                        if (sign(learnt[i])) learntFile << '-';
                        learntFile << v << ' ';
                    }
                    learntFile << "0\n";
                }
                learntFile.close();

                std::cout << "Running EA multiple times. runNumber = " << runNumber << std::endl;
                runNumber++;
                ea->cache.clear();
                for (int i = 0; i < 100; ++i) {
                    ea->run(1000, 10, pool, "backdoor.txt");
                }

                startTime = currentTime;
            }
        }

        double rest_base = luby_restart ? luby(restart_inc, curr_restarts) : pow(restart_inc, curr_restarts);
        status = search(static_cast<int>(rest_base * restart_first));
        if (!withinBudget()) break;
        curr_restarts++;
    }

    if (verbosity >= 1)
        fprintf(stderr, "===============================================================================\n");

    if (status == l_True) {
        // Extend & copy model:
        model.growTo(nVars());
        for (int i = 0; i < nVars(); i++) model[i] = value(i);
    } else if (status == l_False && conflict.size() == 0)
        ok = false;

    cancelUntil(0);
    return status;
}

//=================================================================================================
// Writing CNF to DIMACS:
//
// FIXME: this needs to be rewritten completely.

static Var mapVar(Var x, vec<Var>& map, Var& max) {
    if (map.size() <= x || map[x] == -1) {
        map.growTo(x + 1, -1);
        map[x] = max++;
    }
    return map[x];
}

void Solver::toDimacs(FILE* f, Clause& c, vec<Var>& map, Var& max) {
    if (satisfied(c)) return;

    for (int i = 0; i < c.size(); i++)
        if (value(c[i]) != l_False)
            fprintf(f, "%s%d ", sign(c[i]) ? "-" : "", mapVar(var(c[i]), map, max) + 1);
    fprintf(f, "0\n");
}

void Solver::toDimacs(const char* file, const vec<Lit>& assumps) {
    FILE* f = fopen(file, "wr");
    if (f == NULL)
        fprintf(stderr, "could not open file %s\n", file), exit(1);
    toDimacs(f, assumps);
    fclose(f);
}

void Solver::toDimacs(FILE* f, const vec<Lit>&) {
    // Handle case when solver is in contradictory state:
    if (!ok) {
        fprintf(f, "p cnf 1 2\n1 0\n-1 0\n");
        return;
    }

    vec<Var> map;
    Var max = 0;

    // Cannot use removeClauses here because it is not safe
    // to deallocate them at this point. Could be improved.
    int cnt = 0;
    for (auto const& clause : clauses) {
        if (!satisfied(ca[clause])) {
            ++cnt;
        }
    }

    for (auto const& clause : clauses) {
        if (!satisfied(ca[clause])) {
            Clause& c = ca[clause];
            for (int j = 0; j < c.size(); j++)
                if (value(c[j]) != l_False)
                    mapVar(var(c[j]), map, max);
        }
    }

    // Assumptions are added as unit clauses:
    cnt += assumptions.size();

    fprintf(f, "p cnf %d %d\n", max, cnt);

    for (auto const& assump : assumptions) {
        assert(value(assump) != l_False);
        fprintf(f, "%s%d 0\n", sign(assump) ? "-" : "", mapVar(var(assump), map, max) + 1);
    }

    for (auto const& cl : clauses) {
        toDimacs(f, ca[cl], map, max);
    }

    if (verbosity > 0)
        fprintf(stderr, "Wrote %d clauses with %d variables.\n", cnt, max);
}

//=================================================================================================
// Garbage Collection methods:

void Solver::relocAll(ClauseAllocator& to) {
    // All watchers:
    //
    // for (int i = 0; i < watches.size(); i++)
    watches.cleanAll();
    for (int v = 0; v < nVars(); v++)
        for (int s = 0; s < 2; s++) {
            Lit p = mkLit(v, s);
            vec<Watcher>& ws = watches[p];
            for (auto& w : ws) {
                ca.reloc(w.cref, to);
            }
        }

    // All reasons:
    //
    for (auto const& t : trail) {
        Var v = var(t);

        if (reason(v) != CRef_Undef && (ca[reason(v)].reloced() || locked(ca[reason(v)])))
            ca.reloc(vardata[v].reason, to);
    }

    // All learnt:
    //
    for (auto& learnt : learnts) {
        ca.reloc(learnt, to);
    }

    // All original:
    //
    for (auto& cl : clauses) {
        ca.reloc(cl, to);
    }
}

void Solver::garbageCollect() {
    // Initialize the next region to a size corresponding to the estimated utilization degree. This
    // is not precise but should avoid some unnecessary reallocations for the new region:
    ClauseAllocator to(ca.size() - ca.wasted());

    relocAll(to);
    if (verbosity >= 2)
        fprintf(stderr, "|  Garbage collection:   %12d bytes => %12d bytes             |\n",
                ca.size() * ClauseAllocator::Unit_Size, to.size() * ClauseAllocator::Unit_Size);
    to.moveTo(ca);
}

bool Solver::prop_check(const vec<Lit>& assumps, vec<Lit>& prop, int psaving) {
    prop.clear();

    if (!ok)
        return false;

    bool st = true;
    int level = decisionLevel();
    CRef confl = CRef_Undef;

    // dealing with phase saving
    int psaving_copy = phase_saving;
    phase_saving = psaving;

    // propagate each assumption at a new decision level
    for (int i = 0; st && confl == CRef_Undef && i < assumps.size(); ++i) {
        Lit p = assumps[i];

        if (value(p) == l_False)
            st = false;
        else if (value(p) != l_True) {
            newDecisionLevel();
            uncheckedEnqueue(p);
            confl = propagate();
        }
    }

    // copying the result
    if (decisionLevel() > level) {
        for (int c = trail_lim[level]; c < trail.size(); ++c)
            prop.push(trail[c]);

        // if there is a conflict, pushing
        // the conflicting literal as well
        // here we may choose a wrong literal
        // in Glucose if the clause is binary!
        if (confl != CRef_Undef)
            prop.push(ca[confl][0]);

        // backtracking
        cancelUntil(level);
    }

    // restoring phase saving
    phase_saving = psaving_copy;

    return st && confl == CRef_Undef;
}

bool Solver::gen_all_valid_assumptions_propcheck(
    std::vector<int> d_set,
    uint64_t& total_count,
    std::vector<std::vector<int>>& vector_of_assumptions,
    bool verb) {
    vector_of_assumptions.clear();
    total_count = 0;
    int checked_points = 0;

    if (verb) {
        std::cout << "c checking backdoor: ";
        for (int j = 0; j < d_set.size(); j++) {
            std::cout << d_set[j] + 1 << ' ';
        }
        std::cout << '\n';
    }

    int d_size = d_set.size();

    vec<Lit> assumps;
    std::vector<int> aux(d_set.size());
    for (int i = 0; i < d_set.size(); i++) {
        aux[i] = 0;
        assumps.push(~mkLit(d_set[i]));
    }

    bool flag = true;
    while (flag == true) {
        checked_points++;
        for (int j = 0; j < d_size; j++) {
            if (aux[j] == 0) {
                assumps[j] = ~mkLit(d_set[j]);
            } else {
                assumps[j] = mkLit(d_set[j]);
            }
        }

        vec<Lit> prop;
        bool b = prop_check(assumps, prop);
        cancelUntil(0);
        if (b == true) {
            vector_of_assumptions.push_back(aux);
            total_count++;
            if (verb) {
                std::cout << "c valid vector of assumptions: ";
                for (int j = 0; j < aux.size(); j++) {
                    std::cout << aux[j] << ' ';
                }
                std::cout << '\n';
            }
        }

        int g = aux.size() - 1;

        while ((aux[g] == 1) && (g >= 0)) {
            g--;
        }
        if (g == -1) {
            // time to break;
            flag = false;
            break;
        }

        assert(aux[g] == 0);
        aux[g] = 1;
        // move to the next binary number
        if (g < d_size) {
            g++;
            while (g < d_size) {
                aux[g] = 0;
                g++;
            }
        }
    }
    cancelUntil(0);
    if (verb) {
        std::cout << "c Checked " << checked_points << " points, " << total_count << " valid" << '\n';
    }
    return true;
}

bool Solver::gen_all_valid_assumptions_tree(
    std::vector<int> variables,
    uint64_t& total_count,
    std::vector<std::vector<int>>& vector_of_assumptions,
    int limit,
    bool verb) {
    // 'variables' - vector of variables (backdoor)
    // 'total_count' - number of found hard tasks
    // 'vector_of_assumptions' - vector of hard tasks (no more than 'limit')

    assert(variables.size() < 64);

    if (verb) {
        std::cerr << "c checking backdoor: ";
        for (size_t j = 0; j < variables.size(); j++) {
            std::cerr << variables[j] + 1 << ' ';
        }
        std::cerr << '\n';
    }

    assert(ok);
    cancelUntil(0);

    assumptions.clear();
    for (size_t i = 0; i < variables.size(); i++) {
        assumptions.push(mkLit(variables[i], false));
    }

    std::vector<int> cube(variables.size(), 0);  // signs
    uint64_t total_checked = 0;                  // number of 'propagate' calls
    total_count = 0;                             // number of found valid cubes
    vector_of_assumptions.clear();               // valid cubes (hard subtasks)

    if (variables.size() == 0) {
        return true;
    }

    // State machine:
    //  state = 0 -- Descending
    //  state = 1 -- Ascending
    //  state = 2 -- Propagating
    //
    int state = 0;

    while (1) {
        if (verb) {
            std::cerr << "cube = ";
            for (size_t j = 0; j < variables.size(); j++) {
                std::cerr << cube[j] << ' ';
            }
            std::cerr << ", level = " << decisionLevel() << ", state = " << state << std::endl;
        }

        assert(decisionLevel() <= variables.size());

        if (state == 0) {
            // Descending.

            if (decisionLevel() == variables.size()) {
                if (verb) {
                    std::cerr << "c found valid vector of assumptions: ";
                    for (int j = 0; j < decisionLevel(); j++) {
                        std::cerr << (var(assumptions[j]) + 1) * (-2 * sign(assumptions[j]) + 1) << ' ';
                    }
                    std::cerr << '\n';
                }
                if (vector_of_assumptions.size() < limit) {
                    vector_of_assumptions.push_back(cube);
                }
                total_count++;
                state = 1;  // state = Ascending
            } else {
                while (decisionLevel() < variables.size()) {
                    newDecisionLevel();
                    Lit p = assumptions[decisionLevel() - 1];
                    if (value(p) == l_True) {
                        // `p` is already True.
                        // do nothing
                    } else if (value(p) == l_False) {
                        // if (verb) {
                        //     std::cerr << "c propagated a different value for assumptions: ";
                        //     for (int j = 0; j < decisionLevel(); j++) {
                        //         std::cerr << cube[j] << ' ';
                        //     }
                        //     std::cerr << '\n';
                        // }
                        state = 1;  // state = Ascending
                        break;
                    } else if (value(p) == l_Undef) {
                        uncheckedEnqueue(p);
                        state = 2;  // state = Propagating
                        break;
                    } else {
                        // Bad value.
                        exit(1);
                    }
                }
            }

        } else if (state == 1) {
            // Ascending.

            assert(decisionLevel() > 0);

            int i = decisionLevel();  // 1-based index
            while (i > 0 && cube[i - 1]) {
                i--;
            }
            if (i == 0) {
                // Finish.
                break;
            }

            assert(cube[i - 1] == 0);
            cube[i - 1] = 1;
            for (int j = i; j < variables.size(); j++) {
                cube[j] = 0;
            }
            // if (verb) {
            //     std::cerr << "c next cube: ";
            //     for (int j = 0; j < variables.size(); j++) {
            //         std::cerr << cube[j] << ' ';
            //     }
            //     std::cerr << '\n';
            // }

            // Modify assumptions:
            for (int j = i; j <= variables.size(); j++) {
                assumptions[j - 1] = mkLit(variables[j - 1], cube[j - 1]);
            }

            // Backtrack before i-th level:
            cancelUntil(i - 1);

            // Switch state:
            state = 0;  // state = Descending

        } else if (state == 2) {
            // Propagating.

            CRef confl = propagate();
            total_checked++;
            if (confl != CRef_Undef) {
                // CONFLICT

                // if (verb) {
                //     std::cerr<<"c conflict derived for assumptions: ";
                //     for (int j = 0; j < decisionLevel(); j++) {
                //         std::cerr << (var(assumptions[j]) + 1) * (-2 * sign(assumptions[j]) + 1) << ' ';
                //     }
                //     std::cerr << '\n';
                // }

                state = 1;  // state = Ascending
            } else {
                // NO CONFLICT

                // if (verb) {
                //     std::cerr<<"c no conflict for assumptions: ";
                //     for (int j = 0; j < decisionLevel(); j++) {
                //         std::cerr << (var(assumptions[j]) + 1) * (-2 * sign(assumptions[j]) + 1) << ' ';
                //     }
                //     std::cerr << '\n';
                // }

                state = 0;  // state = Descending
            }

        } else {
            std::cerr << "Bad state: " << state << std::endl;
            exit(1);
            break;
        }
    }

    cancelUntil(0);
    if (verb) {
        std::cout << "c Checked: " << total_checked << ", found valid: " << total_count << '\n';
    }
    assumptions.clear();
    return true;
}
