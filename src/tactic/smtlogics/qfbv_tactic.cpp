/*++
Copyright (c) 2012 Microsoft Corporation

Module Name:

    qfbv_tactic.cpp

Abstract:

    Tactic for QF_BV based on bit-blasting

Author:

    Leonardo (leonardo) 2012-02-22

Notes:

--*/
#include "tactic/tactical.h"
#include "tactic/core/simplify_tactic.h"
#include "tactic/core/propagate_values_tactic.h"
#include "tactic/core/solve_eqs_tactic.h"
#include "tactic/core/elim_uncnstr_tactic.h"
#include "tactic/bv/bit_blaster_tactic.h"
#include "tactic/bv/bv1_blaster_tactic.h"
#include "tactic/bv/max_bv_sharing_tactic.h"
#include "tactic/bv/bv_size_reduction_tactic.h"
#include "tactic/aig/aig_tactic.h"
#include "sat/tactic/sat_tactic.h"
#include "sat/sat_solver/inc_sat_solver.h"
#include "ackermannization/ackermannize_bv_tactic.h"
#include "tactic/smtlogics/smt_tactic.h"

#define MEMLIMIT 300

static tactic * mk_qfbv_preamble(ast_manager& m, params_ref const& p) {

    params_ref solve_eq_p;
    // conservative gaussian elimination.
    solve_eq_p.set_uint("solve_eqs_max_occs", 2);

    params_ref simp2_p = p;
    simp2_p.set_bool("som", true);
    simp2_p.set_bool("pull_cheap_ite", true);
    simp2_p.set_bool("push_ite_bv", false);
    // simp2_p.set_bool("local_ctx", true);
    // simp2_p.set_uint("local_ctx_limit", 10000000);
    simp2_p.set_bool("flat", true); // required by som
    simp2_p.set_bool("hoist_mul", false); // required by som

    params_ref hoist_p;
    hoist_p.set_bool("hoist_mul", true);
    hoist_p.set_bool("som", false);

    return
        and_then(
            mk_simplify_tactic(m),
            mk_propagate_values_tactic(m), // [Lin] Propagate values using equalities of the form (= t v) where v is a value, and atoms t and (not t)
            using_params(mk_solve_eqs_tactic(m), solve_eq_p), // [Lin] solving equations and performing gaussian elimination.
            mk_elim_uncnstr_tactic(m), //  [Lin] Eliminated unconstrained variables.
            if_no_proofs(if_no_unsat_cores(mk_bv_size_reduction_tactic(m))), // [Lin] for example, -2 <= x <= 2 Then, x can be replaced by (concat m_util.mk_numeral(numeral(sign), 5) k) where k is a fresh bit-vector constant of size 3.
            using_params(mk_simplify_tactic(m), simp2_p), // [Lin] local contextual simplifications are performed. These simplifications are potentially very expensive. So, a threshold on the maximal number of nodes to be visited is used (the default value is 10million). Local context simplification contain rules such as (x != 0 or  y = x+1) ->  (x != 0 or y = 1)

            //
            // Z3 can solve a couple of extra benchmarks by using hoist_mul
            // but the timeout in SMT-COMP is too small.
            // Moreover, it impacted negatively some easy benchmarks.
            // We should decide later, if we keep it or not.
            //
            using_params(mk_simplify_tactic(m), hoist_p), // [Lin] a_b + a_c -> (b+c)*a to minimize the number of multipliers
            mk_max_bv_sharing_tactic(m), // [Lin] a + (b + c), a + (b + d) -> (a+b)+c, (a+b)+d. minimize the number of adders and multipliers by applying associativity and commutativity
            if_no_proofs(if_no_unsat_cores(mk_ackermannize_bv_tactic(m,p)))
            );
}

static tactic * main_p(tactic* t) {
    params_ref p;
    p.set_bool("elim_and", true);
    p.set_bool("push_ite_bv", true);
    p.set_bool("blast_distinct", true);
    return using_params(t, p);
}


static tactic * mk_qfbv_tactic(ast_manager& m, params_ref const & p, tactic* sat, tactic* smt) {

    params_ref local_ctx_p = p;
    local_ctx_p.set_bool("local_ctx", true);

    params_ref solver_p;
    solver_p.set_bool("preprocess", false); // preprocessor of smt::context is not needed.

    params_ref big_aig_p;
    big_aig_p.set_bool("aig_per_assertion", false);

    tactic* preamble_st = mk_qfbv_preamble(m, p);
    tactic * st = main_p(and_then(preamble_st,
                                  // If the user sets HI_DIV0=false, then the formula may contain uninterpreted function
                                  // symbols. In this case, we should not use the `sat', but instead `smt'. Alternatively,
                                  // the UFs can be eliminated by eager ackermannization in the preamble.
                                  cond(mk_is_qfbv_eq_probe(),
                                       and_then(mk_bv1_blaster_tactic(m), // [Lin] bit blast each bit, and solver by equations solver
                                                using_params(smt, solver_p)),
                                       cond(mk_is_qfbv_probe(),
                                            and_then(mk_bit_blaster_tactic(m),
                                                     when(mk_lt(mk_memory_probe(), mk_const_probe(MEMLIMIT)),
                                                          and_then(using_params(and_then(mk_simplify_tactic(m),
                                                                                         mk_solve_eqs_tactic(m)),
                                                                                local_ctx_p),
                                                                   if_no_proofs(cond(mk_produce_unsat_cores_probe(),
                                                                                     mk_aig_tactic(),
                                                                                     using_params(mk_aig_tactic(),
                                                                                                  big_aig_p))))),
                                                     sat),
					     //  mk_qfbv_sls_tactic(m),
                                            smt))));

    st->updt_params(p);
    return st;

}


tactic * mk_qfbv_tactic(ast_manager & m, params_ref const & p) {
//	return mk_qfbv_sls_tactic(m, p);
    tactic * new_sat = cond(mk_produce_proofs_probe(),
                            and_then(mk_simplify_tactic(m), mk_smt_tactic(m, p)),
                            mk_psat_tactic(m, p));
    return mk_qfbv_tactic(m, p, new_sat, mk_smt_tactic(m, p));

}
