#include "ExpressionEvaluator.h"

#include <QtMath>
#include <cmath>

namespace {
class Parser {
public:
    Parser(const QString &text, const QMap<QString,double> &vars)
        : s(text), variables(vars) {}

    bool run(double *out, QString *error) {
        pos = 0; ok = true; message.clear();
        const double v = parseOr();
        skip();
        if (ok && pos != s.size()) fail(QString("Unexpected token at position %1").arg(pos));
        if (ok && !std::isfinite(v)) fail("Expression produced a non-finite value");
        if (!ok) {
            if (error) *error = message;
            return false;
        }
        if (out) *out = v;
        return true;
    }

private:
    QString s;
    const QMap<QString,double> &variables;
    int pos = 0;
    bool ok = true;
    QString message;

    void skip() { while (pos < s.size() && s[pos].isSpace()) ++pos; }
    void fail(const QString &m) { if (ok) { ok = false; message = m; } }
    bool take(const QString &token) {
        skip();
        if (s.midRef(pos, token.size()) == token) { pos += token.size(); return true; }
        return false;
    }

    double parseOr() {
        double a = parseAnd();
        while (ok && take("||")) { const double b = parseAnd(); a = (a != 0.0 || b != 0.0) ? 1.0 : 0.0; }
        return a;
    }
    double parseAnd() {
        double a = parseComparison();
        while (ok && take("&&")) { const double b = parseComparison(); a = (a != 0.0 && b != 0.0) ? 1.0 : 0.0; }
        return a;
    }
    double parseComparison() {
        double a = parseAdd();
        for (;;) {
            if (take("<=")) { double b=parseAdd(); a=a<=b; }
            else if (take(">=")) { double b=parseAdd(); a=a>=b; }
            else if (take("==")) { double b=parseAdd(); a=a==b; }
            else if (take("!=")) { double b=parseAdd(); a=a!=b; }
            else if (take("<")) { double b=parseAdd(); a=a<b; }
            else if (take(">")) { double b=parseAdd(); a=a>b; }
            else break;
        }
        return a;
    }
    double parseAdd() {
        double a = parseMul();
        for (;;) {
            if (take("+")) a += parseMul();
            else if (take("-")) a -= parseMul();
            else break;
        }
        return a;
    }
    double parseMul() {
        double a = parsePower();
        for (;;) {
            if (take("*")) a *= parsePower();
            else if (take("/")) {
                const double b = parsePower();
                if (b == 0.0) { fail("Division by zero"); return 0.0; }
                a /= b;
            } else break;
        }
        return a;
    }
    double parsePower() {
        double a = parseUnary();
        if (take("^")) a = std::pow(a, parsePower());
        return a;
    }
    double parseUnary() {
        if (take("+")) return parseUnary();
        if (take("-")) return -parseUnary();
        if (take("!")) return parseUnary() == 0.0 ? 1.0 : 0.0;
        return parsePrimary();
    }

    QString identifier() {
        skip();
        const int start = pos;
        if (pos < s.size() && (s[pos].isLetter() || s[pos] == '_')) {
            ++pos;
            while (pos < s.size() && (s[pos].isLetterOrNumber() || s[pos] == '_')) ++pos;
        }
        return s.mid(start, pos-start);
    }

    double parsePrimary() {
        skip();
        if (take("(")) {
            const double v = parseOr();
            if (!take(")")) fail("Missing ')'");
            return v;
        }

        if (pos < s.size() && (s[pos].isDigit() || s[pos] == '.')) {
            const int start = pos;
            bool seenE = false;
            while (pos < s.size()) {
                const QChar c=s[pos];
                if (c.isDigit() || c=='.') ++pos;
                else if ((c=='e'||c=='E') && !seenE) { seenE=true; ++pos; if (pos<s.size() && (s[pos]=='+'||s[pos]=='-')) ++pos; }
                else break;
            }
            bool converted=false;
            const double v=s.mid(start,pos-start).toDouble(&converted);
            if (!converted) fail("Invalid number");
            return v;
        }

        const QString id = identifier();
        if (id.isEmpty()) { fail(QString("Expected value at position %1").arg(pos)); return 0.0; }
        if (id == "pi") return M_PI;
        if (id == "true") return 1.0;
        if (id == "false") return 0.0;

        if (take("(")) {
            QList<double> args;
            skip();
            if (!take(")")) {
                for (;;) {
                    args.append(parseOr());
                    if (take(")")) break;
                    if (!take(",")) { fail(QString("Expected ',' in %1()").arg(id)); return 0.0; }
                }
            }
            return call(id,args);
        }

        if (!variables.contains(id)) { fail(QString("Unknown variable '%1'").arg(id)); return 0.0; }
        return variables.value(id);
    }

    double call(const QString &name, const QList<double> &a) {
        auto one=[&](auto fn)->double { if(a.size()!=1){fail(name+"() expects 1 argument");return 0;} return fn(a[0]); };
        auto two=[&](auto fn)->double { if(a.size()!=2){fail(name+"() expects 2 arguments");return 0;} return fn(a[0],a[1]); };
        if (name=="sqrt") return one([](double x){return std::sqrt(x);});
        if (name=="tan")  return one([](double x){return std::tan(x);});
        if (name=="sin")  return one([](double x){return std::sin(x);});
        if (name=="cos")  return one([](double x){return std::cos(x);});
        if (name=="abs")  return one([](double x){return std::fabs(x);});
        if (name=="exp")  return one([](double x){return std::exp(x);});
        if (name=="log")  return one([](double x){return std::log(x);});
        if (name=="pow")  return two([](double x,double y){return std::pow(x,y);});
        if (name=="min")  return two([](double x,double y){return qMin(x,y);});
        if (name=="max")  return two([](double x,double y){return qMax(x,y);});
        fail(QString("Unknown function '%1'").arg(name));
        return 0.0;
    }
};
}

bool ExpressionEvaluator::evaluate(const QString &expression,
                                   const QMap<QString,double> &variables,
                                   double *result, QString *error)
{
    Parser p(expression, variables);
    return p.run(result, error);
}
