// Standalone sanity test for ExpressionSyntax vector support.
// Build: g++ -std=gnu++1z -I<Qt include dirs> exprtest.cpp -lQt5Core
#include "../expression.h"
#include <QHash>
#include <cstdio>

static int g_fail = 0;

static void check(const QString &expr, qreal expected, const QHash<QString, qreal> &vars = {})
{
    qreal v = 0.0;
    QString err;
    bool ok = ExpressionSyntax::evaluate(expr, vars, &v, &err);
    const bool pass = ok && std::abs(v - expected) < 1e-6;
    if (!pass) {
        ++g_fail;
        std::printf("FAIL  %-28s -> ok=%d val=%g err=%s (expected %g)\n",
                    expr.toUtf8().constData(), ok, v, err.toUtf8().constData(), expected);
    } else {
        std::printf("ok    %-28s = %g\n", expr.toUtf8().constData(), v);
    }
}

static void checkFail(const QString &expr, const QHash<QString, qreal> &vars = {})
{
    qreal v = 0.0;
    QString err;
    bool ok = ExpressionSyntax::evaluate(expr, vars, &v, &err);
    if (ok) {
        ++g_fail;
        std::printf("FAIL  %-28s -> expected error, got %g\n", expr.toUtf8().constData(), v);
    } else {
        std::printf("ok    %-28s -> error as expected (%s)\n",
                    expr.toUtf8().constData(), err.toUtf8().constData());
    }
}

int main()
{
    // Scalars still work
    check("1 + 2 * 3", 7);
    check("(1+2)*3", 9);
    check("-5 + 3", -2);
    check("log(1000)", 3);          // base-10
    check("ln(1)", 0);
    check("max(1,5,3)", 5);
    check("min(4,2,8)", 2);
    check("sqrt(16)", 4);
    check("abs(-7)", 7);
    check("3 < 5 ? 10 : 20", 10);

    // Vector literals + subscript
    check("[10,20,30][0]", 10);
    check("[10,20,30][2]", 30);
    check("[10,20,30][5]", 0);       // out of range -> 0
    check("[[1,2],[3,4]][1][0]", 3); // nested subscript

    // len / norm / cross (as scalar-producing)
    check("len([1,2,3,4])", 4);
    check("len([])", 0);
    check("norm([3,4])", 5);
    check("norm([0,0,0])", 0);
    check("cross([1,0,0],[0,1,0])[2]", 1);
    check("cross([1,0,0],[0,1,0])[0]", 0);

    // min/max over a single vector arg
    check("max([1,9,4])", 9);
    check("min([5,2,8])", 2);

    // Vector arithmetic collapsing to scalar via subscript
    check("([1,2,3]+[10,20,30])[1]", 22);
    check("([1,2,3]*2)[2]", 6);
    check("([2,4,6]/2)[0]", 1);
    check("([1,2,3]*[4,5,6])", 32);   // dot product 4+10+18

    // Range expansion
    check("len([0:2:10])", 6);        // 0,2,4,6,8,10
    check("[0:2:10][3]", 6);
    check("len([5:8])", 4);           // 5,6,7,8

    // concat
    check("len(concat([1,2],[3,4,5]))", 5);
    check("concat([1,2],[3,4])[2]", 3);

    // Errors
    checkFail("[1,2] + [1,2,3]");     // length mismatch
    checkFail("1 / 0");

    std::printf("\n%s (%d failures)\n", g_fail == 0 ? "ALL PASS" : "FAILURES", g_fail);
    return g_fail == 0 ? 0 : 1;
}
