#ifndef EXPRESSIONEVALUATOR_H
#define EXPRESSIONEVALUATOR_H

#include <QMap>
#include <QString>

// Small, deterministic mathematical expression evaluator for dashboard
// derived series. It intentionally does not execute JavaScript or expose
// application objects, which keeps the same implementation usable in the
// desktop and Qt WebAssembly builds without adding QtQml/QJSEngine.
//
// Supported operators: + - * / ^, < <= > >= == !=, && ||, unary + - !
// Supported functions: sqrt, pow, tan, sin, cos, abs, min, max, exp, log
// Constants/variables are supplied by the caller; pi is built in.
class ExpressionEvaluator
{
public:
    static bool evaluate(const QString &expression,
                         const QMap<QString, double> &variables,
                         double *result,
                         QString *error = nullptr);
};

#endif // EXPRESSIONEVALUATOR_H
