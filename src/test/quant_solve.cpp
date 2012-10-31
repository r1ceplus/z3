#include "ast.h"
#include "front_end_params.h"
#include "qe.h"
#include "arith_decl_plugin.h"
#include "ast_pp.h"
#include "lbool.h"
#include <sstream>
#include "expr_replacer.h"
#include "smt_solver.h"
#include "reg_decl_plugins.h"
#include "expr_abstract.h"
#include "model_smt2_pp.h"
#include "smt2parser.h"
#include "var_subst.h"

static void validate_quant_solution(ast_manager& m, expr* fml, expr* guard, qe::def_vector const& defs) {
    // verify:
    //    new_fml => fml[t/x]
    scoped_ptr<expr_replacer> rep = mk_expr_simp_replacer(m);
    app_ref_vector xs(m);
    expr_substitution sub(m);
    for (unsigned i = 0; i < defs.size(); ++i) {
        xs.push_back(m.mk_const(defs.var(i)));
        sub.insert(xs.back(), defs.def(i));
    }
    rep->set_substitution(&sub);
    expr_ref fml1(fml, m);
    (*rep)(fml1);
    expr_ref tmp(m);
    tmp = m.mk_not(m.mk_implies(guard, fml1));
    front_end_params fp;
    smt::solver solver(m, fp);
    solver.assert_expr(tmp);
    lbool res = solver.check();
    //SASSERT(res == l_false);
    if (res != l_false) {
        std::cout << "Validation failed: " << res << "\n";
        std::cout << mk_pp(tmp, m) << "\n";
        model_ref model;
        solver.get_model(model);
        model_smt2_pp(std::cout, m, *model, 0);
        fatal_error(0);
    }
}


#if 0
static void validate_quant_solutions(app* x, expr* fml, expr_ref_vector& guards) {
    return;
    // quant_elim option got removed...
    // verify:
    //    fml <=> guard_1 \/ guard_2 \/ ... 
    ast_manager& m = guards.get_manager();
    expr_ref tmp(m), fml2(m);
    tmp = m.mk_or(guards.size(), guards.c_ptr());
    expr* _x = x;
    std::cout << mk_pp(fml, m) << "\n";
    expr_abstract(m, 0, 1, &_x, fml, fml2);
    std::cout << mk_pp(fml2, m) << "\n";
    symbol name(x->get_decl()->get_name());
    sort* s = m.get_sort(x);
    fml2 = m.mk_exists(1, &s, &name, fml2);
    std::cout << mk_pp(fml2, m) << "\n";
    tmp = m.mk_not(m.mk_iff(fml2, tmp));
    std::cout << mk_pp(tmp, m) << "\n";
    front_end_params fp;
    smt::solver solver(m, fp);
    solver.assert_expr(tmp);
    lbool res = solver.check();
    std::cout << "checked\n";
    SASSERT(res == l_false);
    if (res != l_false) {
        std::cout << res << "\n";
        fatal_error(0);
    }
}
#endif


static void test_quant_solver(ast_manager& m, unsigned sz, app*const* xs, expr* fml) {
    front_end_params params;
    qe::expr_quant_elim qe(m, params);
    qe::guarded_defs defs(m);
    bool success = qe.solve_for_vars(sz, xs, fml, defs);
    std::cout << "------------------------\n";
    std::cout << mk_pp(fml, m) << "\n";
    if (success) {        
        defs.display(std::cout);
        for (unsigned i = 0; i < defs.size(); ++i) {     
            validate_quant_solution(m, fml, defs.guard(i), defs.defs(i));
        }
    }
    else {
        std::cout << "failed\n";
    }
}

static expr_ref parse_fml(ast_manager& m, char const* str) {
    expr_ref result(m);
    front_end_params fp;
    cmd_context ctx(&fp, false, &m);
    ctx.set_ignore_check(true);
    std::ostringstream buffer;
    buffer << "(declare-const x Int)\n"
           << "(declare-const y Int)\n"
           << "(declare-const z Int)\n"
           << "(declare-const a Int)\n"
           << "(declare-const b Int)\n"
           << "(declare-datatypes () ((IList (nil) (cons (car Int) (cdr IList)))))\n"
           << "(declare-const l1 IList)\n"
           << "(declare-const l2 IList)\n"
           << "(declare-datatypes () ((Cell (null) (cell (car Cell) (cdr Cell)))))\n"
           << "(declare-const c1 Cell)\n"
           << "(declare-const c2 Cell)\n"
           << "(declare-const c3 Cell)\n"
           << "(declare-datatypes () ((Tuple (tuple (first Int) (second Bool) (third Real)))))\n"
           << "(declare-const t1 Tuple)\n"
           << "(declare-const t2 Tuple)\n"
           << "(declare-const t3 Tuple)\n"
           << "(assert " << str << ")\n";
    std::istringstream is(buffer.str());
    VERIFY(parse_smt2_commands(ctx, is));
    SASSERT(ctx.begin_assertions() != ctx.end_assertions());
    result = *ctx.begin_assertions();
    return result;
}


static void parse_fml(char const* str, app_ref_vector& vars, expr_ref& fml) {
    ast_manager& m = fml.get_manager();
    fml = parse_fml(m, str);
    if (is_exists(fml)) {
        quantifier* q = to_quantifier(fml);
        for (unsigned i = 0; i < q->get_num_decls(); ++i) {
            vars.push_back(m.mk_const(q->get_decl_name(i), q->get_decl_sort(i)));
        }
        fml = q->get_expr();
        var_subst vs(m, true);
        vs(fml, vars.size(), (expr*const*)vars.c_ptr(), fml);
    }
}

static void test_quant_solver(ast_manager& m, app* x, char const* str) {
    expr_ref fml = parse_fml(m, str);
    test_quant_solver(m, 1, &x, fml);
}

static void test_quant_solver(ast_manager& m, unsigned sz, app*const* xs, char const* str) {
    expr_ref fml = parse_fml(m, str);
    test_quant_solver(m, sz, xs, fml);
}

static void test_quant_solver(ast_manager& m, char const* str) {
    expr_ref fml(m);
    app_ref_vector vars(m);
    parse_fml(str, vars, fml);
    test_quant_solver(m, vars.size(), vars.c_ptr(), fml);
}


static void test_quant_solve1() {
    ast_manager m;
    arith_util ar(m);
    reg_decl_plugins(m);
    sort* i = ar.mk_int();
    app_ref xr(m.mk_const(symbol("x"),i), m);
    app_ref yr(m.mk_const(symbol("y"),i), m);
    app* x = xr.get();
    app* y = yr.get();
    app* xy[2] = { x, y };


    test_quant_solver(m, x, "(and (<= x y) (= (mod x 2) 0))");
    test_quant_solver(m, x, "(and (<= (* 2 x) y) (= (mod x 2) 0))");
    test_quant_solver(m, x, "(and (>= x y) (= (mod x 2) 0))");
    test_quant_solver(m, x, "(and (>= (* 2 x) y) (= (mod x 2) 0))");
    test_quant_solver(m, x, "(and (<= (* 2 x) y) (>= x z) (= (mod x 2) 0))");
    test_quant_solver(m, x, "(and (<= (* 2 x) y) (>= (* 3 x) z) (= (mod x 2) 0))");
    test_quant_solver(m, x, "(>= (* 2 x) a)");
    test_quant_solver(m, x, "(<= (* 2 x) a)");
    test_quant_solver(m, x, "(< (* 2 x) a)");
    test_quant_solver(m, x, "(= (* 2 x) a)");
    test_quant_solver(m, x, "(< (* 2 x) a)");
    test_quant_solver(m, x, "(> (* 2 x) a)");
    test_quant_solver(m, x, "(and (<= a x) (<= (* 2 x) b))");
    test_quant_solver(m, x, "(and (<= a x) (<= x b))");
    test_quant_solver(m, x, "(and (<= (* 2 a) x) (<= x b))");
    test_quant_solver(m, x, "(and (<= (* 2 a) x) (<= (* 2 x) b))");
    test_quant_solver(m, x, "(and (<= a x) (<= (* 3 x) b))");
    test_quant_solver(m, x, "(and (<= (* 3 a) x) (<= x b))");
    test_quant_solver(m, x, "(and (<= (* 3 a) x) (<= (* 3 x) b))");
    test_quant_solver(m, x, "(and (< a (* 3 x)) (< (* 3 x) b))");    
    test_quant_solver(m, x, "(< (* 3 x) a)");
    test_quant_solver(m, x, "(= (* 3 x) a)");
    test_quant_solver(m, x, "(< (* 3 x) a)");
    test_quant_solver(m, x, "(> (* 3 x) a)");
    test_quant_solver(m, x, "(<= (* 3 x) a)");
    test_quant_solver(m, x, "(>= (* 3 x) a)");
    test_quant_solver(m, x, "(<= (* 2 x) a)");
    test_quant_solver(m, x, "(or (= (* 2 x) y) (= (+ (* 2 x) 1) y))");
    test_quant_solver(m, x, "(= x a)");
    test_quant_solver(m, x, "(< x a)");
    test_quant_solver(m, x, "(> x a)");
    test_quant_solver(m, x, "(and (> x a) (< x b))");
    test_quant_solver(m, x, "(and (> x a) (< x b))");
    test_quant_solver(m, x, "(<= x a)");
    test_quant_solver(m, x, "(>= x a)");
    test_quant_solver(m, x, "(and (<= (* 2 x) y) (= (mod x 2) 0))");
    test_quant_solver(m, x, "(= (* 2 x) y)");
    test_quant_solver(m, x, "(or (< x 0) (> x 1))");
    test_quant_solver(m, x, "(or (< x y) (> x y))");
    test_quant_solver(m, x, "(= x y)");
    test_quant_solver(m, x, "(<= x y)");
    test_quant_solver(m, x, "(>= x y)");
    test_quant_solver(m, x, "(and (<= (+ x y) 0) (<= (+ x z) 0))");
    test_quant_solver(m, x, "(and (<= (+ x y) 0) (<= (+ (* 2 x) z) 0))");
    test_quant_solver(m, x, "(and (<= (+ (* 3 x) y) 0) (<= (+ (* 2 x) z) 0))");
    test_quant_solver(m, x, "(and (>= x y) (>= x z))");
    test_quant_solver(m, x, "(< x y)");
    test_quant_solver(m, x, "(> x y)");
    test_quant_solver(m, 2, xy, "(and (<= (- (* 2 y) b) (+ (* 3 x) a)) (<= (- (* 2 x) a) (+ (* 4 y) b)))");

    test_quant_solver(m, "(exists ((c Cell)) (= c null))");
    test_quant_solver(m, "(exists ((c Cell)) (= c (cell null c1)))");
    //TBD:
    //test_quant_solver(m, "(exists ((c Cell)) (= (cell c c) c1))");
    //test_quant_solver(m, "(exists ((c Cell)) (not (= c null)))");
}


void tst_quant_solve() {

    test_quant_solve1();   

    memory::finalize();
#ifdef _WINDOWS
    _CrtDumpMemoryLeaks();
#endif
    exit(0);
}

