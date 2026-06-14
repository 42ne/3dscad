#ifndef EXPRESSION_H
#define EXPRESSION_H

#include <QHash>
#include <QRandomGenerator>
#include <QString>
#include <QtMath>
#include <cmath>
#include <algorithm>
#include <utility>

class ExpressionSyntax
{
public:
    struct FunctionDef {
        QStringList params;
        QString body;
    };

    static bool validate(const QString &text, QString *errorMessage = nullptr)
    {
        Parser parser(text);
        if (!parser.parseExpression()) {
            if (errorMessage)
                *errorMessage = parser.error();
            return false;
        }

        parser.skipSpaces();
        if (!parser.atEnd()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Unexpected token at position %1.").arg(parser.position() + 1);
            return false;
        }

        return true;
    }

private:
    class Parser
    {
    public:
        explicit Parser(QString text)
            : m_text(std::move(text))
        {
        }

        bool parseExpression()
        {
            return parseTernary();
        }

        bool parseTernary()
        {
            if (!parseLogical())
                return false;

            skipSpaces();
            if (consume(QLatin1Char('?'))) {
                if (!parseExpression())
                    return setError(QStringLiteral("Expected expression after '?' at position %1.").arg(position() + 1));
                skipSpaces();
                if (!consume(QLatin1Char(':')))
                    return setError(QStringLiteral("Expected ':' after true branch at position %1.").arg(position() + 1));
                if (!parseTernary())
                    return setError(QStringLiteral("Expected expression after ':' at position %1.").arg(position() + 1));
            }

            return true;
        }

        bool parseLogical()
        {
            if (!parseComparison())
                return false;

            while (true) {
                skipSpaces();
                if (consumeString(QStringLiteral("&&"))) {
                    if (!parseComparison())
                        return setError(QStringLiteral("Expected expression after '&&' at position %1.").arg(position() + 1));
                } else if (consumeString(QStringLiteral("||"))) {
                    if (!parseComparison())
                        return setError(QStringLiteral("Expected expression after '||' at position %1.").arg(position() + 1));
                } else {
                    return true;
                }
            }
        }

        bool parseComparison()
        {
            if (!parseAddSub())
                return false;

            while (true) {
                skipSpaces();
                if (consumeString(QStringLiteral("=="))
                    || consumeString(QStringLiteral("!="))
                    || consumeString(QStringLiteral("<="))
                    || consumeString(QStringLiteral(">="))
                    || consume(QLatin1Char('<'))
                    || consume(QLatin1Char('>'))) {
                    if (!parseAddSub())
                        return setError(QStringLiteral("Expected expression after comparison at position %1.").arg(position() + 1));
                } else {
                    return true;
                }
            }
        }

        void skipSpaces()
        {
            while (!atEnd() && m_text[m_pos].isSpace())
                ++m_pos;
        }

        bool atEnd() const { return m_pos >= m_text.size(); }
        int position() const { return m_pos; }
        QString error() const { return m_error.isEmpty() ? QStringLiteral("Invalid expression.") : m_error; }

    private:
        bool parseAddSub()
        {
            if (!parseTerm())
                return false;

            while (true) {
                skipSpaces();
                if (!consume(QLatin1Char('+')) && !consume(QLatin1Char('-')))
                    return true;

                if (!parseTerm())
                    return setError(QStringLiteral("Expected expression after operator at position %1.").arg(position() + 1));
            }
        }

        bool parseTerm()
        {
            if (!parseFactor())
                return false;

            while (true) {
                skipSpaces();
                const QChar op = m_text[m_pos];
                if (op != QLatin1Char('*') && op != QLatin1Char('/') && op != QLatin1Char('%'))
                    return true;
                ++m_pos;

                if (!parseFactor())
                    return setError(QStringLiteral("Expected expression after operator at position %1.").arg(position() + 1));
            }
        }

        bool parseFactor()
        {
            skipSpaces();

            while (true) {
                if (consume(QLatin1Char('-'))) continue;
                if (consume(QLatin1Char('!'))) continue;
                consume(QLatin1Char('+')); // ignore unary +
                break;
            }

            skipSpaces();

            if (consume(QLatin1Char('('))) {
                if (!parseExpression())
                    return false;

                skipSpaces();
                if (!consume(QLatin1Char(')')))
                    return setError(QStringLiteral("Expected ')' at position %1.").arg(position() + 1));
                return true;
            }

            if (parseNumber())
                return true;

            // Function call or variable
            const int savedPos = m_pos;
            if (parseIdentifier()) {
                skipSpaces();
                if (consume(QLatin1Char('('))) {
                    // Function call — parse argument list
                    while (true) {
                        skipSpaces();
                        if (consume(QLatin1Char(')')))
                            return true;
                        if (!parseExpression())
                            return false;
                        skipSpaces();
                        if (consume(QLatin1Char(')')))
                            return true;
                        if (!consume(QLatin1Char(',')))
                            return setError(QStringLiteral("Expected ',' or ')' at position %1.").arg(position() + 1));
                    }
                }
                return true; // plain variable
            }

            m_pos = savedPos;
            return setError(QStringLiteral("Expected number, variable, or '(' at position %1.").arg(position() + 1));
        }

        bool parseNumber()
        {
            skipSpaces();
            const int start = m_pos;
            bool hasDigit = false;

            while (!atEnd() && m_text[m_pos].isDigit()) {
                hasDigit = true;
                ++m_pos;
            }

            if (!atEnd() && m_text[m_pos] == QLatin1Char('.')) {
                ++m_pos;
                while (!atEnd() && m_text[m_pos].isDigit()) {
                    hasDigit = true;
                    ++m_pos;
                }
            }

            if (!hasDigit) {
                m_pos = start;
                return false;
            }

            // Scientific notation (e.g. 1e5, 2.5e-3)
            if (!atEnd() && (m_text[m_pos] == QLatin1Char('e') || m_text[m_pos] == QLatin1Char('E'))) {
                const int save = m_pos;
                ++m_pos;
                if (!atEnd() && (m_text[m_pos] == QLatin1Char('+') || m_text[m_pos] == QLatin1Char('-')))
                    ++m_pos;
                if (atEnd() || !m_text[m_pos].isDigit())
                    m_pos = save; // not scientific notation, restore
                else {
                    while (!atEnd() && m_text[m_pos].isDigit())
                        ++m_pos;
                }
            }

            return true;
        }

        bool parseIdentifier()
        {
            skipSpaces();
            if (atEnd() || !(m_text[m_pos].isLetter() || m_text[m_pos] == QLatin1Char('_')
                             || m_text[m_pos] == QLatin1Char('$')))
                return false;

            ++m_pos;
            while (!atEnd() && (m_text[m_pos].isLetterOrNumber() || m_text[m_pos] == QLatin1Char('_')
                                || m_text[m_pos] == QLatin1Char('$')))
                ++m_pos;

            return true;
        }

        bool consume(QChar ch)
        {
            skipSpaces();
            if (atEnd() || m_text[m_pos] != ch)
                return false;

            ++m_pos;
            return true;
        }

        bool consumeString(const QString &str)
        {
            skipSpaces();
            if (m_text.mid(m_pos, str.size()) == str) {
                m_pos += str.size();
                return true;
            }
            return false;
        }

        bool setError(const QString &message)
        {
            if (m_error.isEmpty())
                m_error = message;
            return false;
        }

        QString m_text;
        int m_pos = 0;
        QString m_error;
    };

public:
    static bool evaluate(const QString &text,
                         const QHash<QString, qreal> &variableValues,
                         qreal *value,
                         QString *errorMessage = nullptr,
                         const QHash<QString, FunctionDef> *functions = nullptr)
    {
        Evaluator ev(text, variableValues, functions);
        qreal result = 0.0;
        if (!ev.evalExpression(&result)) {
            if (errorMessage)
                *errorMessage = ev.error();
            return false;
        }

        ev.skipSpaces();
        if (!ev.atEnd()) {
            if (errorMessage)
                *errorMessage = QStringLiteral("Unexpected token at position %1.").arg(ev.position() + 1);
            return false;
        }

        if (value)
            *value = result;
        return true;
    }

private:
    class Evaluator
    {
    public:
        explicit Evaluator(QString text, const QHash<QString, qreal> &vars,
                           const QHash<QString, FunctionDef> *fns = nullptr)
            : m_text(std::move(text)), m_vars(vars), m_fns(fns)
        {
        }

        bool evalExpression(qreal *result)
        {
            return evalTernary(result);
        }

        bool evalTernary(qreal *result)
        {
            qreal cond = 0.0;
            if (!evalLogical(&cond))
                return false;

            skipSpaces();
            if (consume(QLatin1Char('?'))) {
                qreal trueVal = 0.0, falseVal = 0.0;
                if (!evalExpression(&trueVal))
                    return setError(QStringLiteral("Expected expression after '?' at position %1.").arg(m_pos + 1));
                skipSpaces();
                if (!consume(QLatin1Char(':')))
                    return setError(QStringLiteral("Expected ':' after true branch at position %1.").arg(m_pos + 1));
                if (!evalTernary(&falseVal))
                    return setError(QStringLiteral("Expected expression after ':' at position %1.").arg(m_pos + 1));
                *result = (cond != 0.0) ? trueVal : falseVal;
                return true;
            }

            *result = cond;
            return true;
        }

        bool evalLogical(qreal *result)
        {
            qreal left = 0.0;
            if (!evalComparison(&left))
                return false;

            while (true) {
                skipSpaces();
                if (consumeString(QStringLiteral("&&"))) {
                    qreal right = 0.0;
                    if (!evalComparison(&right))
                        return setError(QStringLiteral("Expected expression after '&&' at position %1.").arg(m_pos + 1));
                    left = (left != 0.0 && right != 0.0) ? 1.0 : 0.0;
                } else if (consumeString(QStringLiteral("||"))) {
                    qreal right = 0.0;
                    if (!evalComparison(&right))
                        return setError(QStringLiteral("Expected expression after '||' at position %1.").arg(m_pos + 1));
                    left = (left != 0.0 || right != 0.0) ? 1.0 : 0.0;
                } else {
                    break;
                }
            }

            *result = left;
            return true;
        }

        bool evalComparison(qreal *result)
        {
            qreal left = 0.0;
            if (!evalAddSub(&left))
                return false;

            while (true) {
                skipSpaces();
                if (consumeString(QStringLiteral("=="))) {
                    qreal right = 0.0;
                    if (!evalAddSub(&right))
                        return setError(QStringLiteral("Expected expression after '==' at position %1.").arg(m_pos + 1));
                    left = (left == right) ? 1.0 : 0.0;
                } else if (consumeString(QStringLiteral("!="))) {
                    qreal right = 0.0;
                    if (!evalAddSub(&right))
                        return setError(QStringLiteral("Expected expression after '!=' at position %1.").arg(m_pos + 1));
                    left = (left != right) ? 1.0 : 0.0;
                } else if (consumeString(QStringLiteral("<="))) {
                    qreal right = 0.0;
                    if (!evalAddSub(&right))
                        return setError(QStringLiteral("Expected expression after '<=' at position %1.").arg(m_pos + 1));
                    left = (left <= right) ? 1.0 : 0.0;
                } else if (consumeString(QStringLiteral(">="))) {
                    qreal right = 0.0;
                    if (!evalAddSub(&right))
                        return setError(QStringLiteral("Expected expression after '>=' at position %1.").arg(m_pos + 1));
                    left = (left >= right) ? 1.0 : 0.0;
                } else if (consume(QLatin1Char('<'))) {
                    qreal right = 0.0;
                    if (!evalAddSub(&right))
                        return setError(QStringLiteral("Expected expression after '<' at position %1.").arg(m_pos + 1));
                    left = (left < right) ? 1.0 : 0.0;
                } else if (consume(QLatin1Char('>'))) {
                    qreal right = 0.0;
                    if (!evalAddSub(&right))
                        return setError(QStringLiteral("Expected expression after '>' at position %1.").arg(m_pos + 1));
                    left = (left > right) ? 1.0 : 0.0;
                } else {
                    break;
                }
            }

            *result = left;
            return true;
        }

        bool evalAddSub(qreal *result)
        {
            qreal left = 0.0;
            if (!evalTerm(&left))
                return false;

            while (true) {
                skipSpaces();
                if (consume(QLatin1Char('+'))) {
                    qreal right = 0.0;
                    if (!evalTerm(&right))
                        return setError(QStringLiteral("Expected expression after '+' at position %1.").arg(m_pos + 1));
                    left += right;
                } else if (consume(QLatin1Char('-'))) {
                    qreal right = 0.0;
                    if (!evalTerm(&right))
                        return setError(QStringLiteral("Expected expression after '-' at position %1.").arg(m_pos + 1));
                    left -= right;
                } else {
                    break;
                }
            }

            *result = left;
            return true;
        }

        void skipSpaces()
        {
            while (!atEnd() && m_text[m_pos].isSpace())
                ++m_pos;
        }

        bool atEnd() const { return m_pos >= m_text.size(); }
        int position() const { return m_pos; }
        QString error() const { return m_error.isEmpty() ? QStringLiteral("Invalid expression.") : m_error; }

    private:
        bool evalTerm(qreal *result)
        {
            qreal left = 0.0;
            if (!evalFactor(&left))
                return false;

            while (true) {
                skipSpaces();
                if (consume(QLatin1Char('*'))) {
                    qreal right = 0.0;
                    if (!evalFactor(&right))
                        return setError(QStringLiteral("Expected expression after '*' at position %1.").arg(m_pos + 1));
                    left *= right;
                } else if (consume(QLatin1Char('/'))) {
                    qreal right = 0.0;
                    if (!evalFactor(&right))
                        return setError(QStringLiteral("Expected expression after '/' at position %1.").arg(m_pos + 1));
                    if (right == 0.0)
                        return setError(QStringLiteral("Division by zero."));
                    left /= right;
                } else if (consume(QLatin1Char('%'))) {
                    qreal right = 0.0;
                    if (!evalFactor(&right))
                        return setError(QStringLiteral("Expected expression after '%%' at position %1.").arg(m_pos + 1));
                    if (right == 0.0)
                        return setError(QStringLiteral("Modulo by zero."));
                    left = std::fmod(left, right);
                } else {
                    break;
                }
            }

            *result = left;
            return true;
        }

        bool evalFactor(qreal *result)
        {
            skipSpaces();

            bool negate = false;
            bool logicalNot = false;
            while (true) {
                if (consume(QLatin1Char('-'))) {
                    negate = !negate;
                } else if (consume(QLatin1Char('!'))) {
                    logicalNot = !logicalNot;
                } else {
                    consume(QLatin1Char('+')); // ignore unary +
                    break;
                }
            }

            skipSpaces();

            qreal value = 0.0;
            if (consume(QLatin1Char('('))) {
                if (!evalExpression(&value))
                    return false;
                skipSpaces();
                if (!consume(QLatin1Char(')')))
                    return setError(QStringLiteral("Expected ')' at position %1.").arg(m_pos + 1));
            } else if (!evalNumber(&value)) {
                // Try identifier — could be variable, constant, or function call
                const int savedPos = m_pos;
                const int start = m_pos;
                if (atEnd() || !(m_text[m_pos].isLetter() || m_text[m_pos] == QLatin1Char('_')
                                 || m_text[m_pos] == QLatin1Char('$'))) {
                    m_pos = savedPos;
                    return setError(QStringLiteral("Expected number, variable, or '(' at position %1.").arg(m_pos + 1));
                }
                ++m_pos;
                while (!atEnd() && (m_text[m_pos].isLetterOrNumber() || m_text[m_pos] == QLatin1Char('_')
                                    || m_text[m_pos] == QLatin1Char('$')))
                    ++m_pos;
                const QString name = m_text.mid(start, m_pos - start);

                skipSpaces();
                if (consume(QLatin1Char('('))) {
                    if (!evalFunctionCall(name, &value))
                        return false;
                } else {
                    if (!resolveIdentifier(name, &value)) {
                        m_pos = savedPos;
                        return setError(QStringLiteral("Unknown variable '%1'.").arg(name));
                    }
                }
            }

            // Array/vector subscript: expr[idx] — returns 0 as placeholder
            skipSpaces();
            while (!atEnd() && m_text[m_pos] == QLatin1Char('[')) {
                ++m_pos; // consume '['
                qreal idx = 0.0;
                evalExpression(&idx); // consume index expression (result ignored)
                skipSpaces();
                if (atEnd() || m_text[m_pos] != QLatin1Char(']'))
                    return setError(QStringLiteral("Expected ']'"));
                ++m_pos; // consume ']'
                value = 0.0; // placeholder: array element access
                skipSpaces();
            }

            if (logicalNot)
                value = (value == 0.0) ? 1.0 : 0.0;
            if (negate)
                value = -value;
            *result = value;
            return true;
        }

        bool resolveIdentifier(const QString &name, qreal *result)
        {
            if (name.compare(QStringLiteral("PI"), Qt::CaseInsensitive) == 0
                || name.compare(QStringLiteral("PI"), Qt::CaseSensitive) == 0) {
                *result = M_PI;
                return true;
            }
            if (name.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0) {
                *result = 1.0;
                return true;
            }
            if (name.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0) {
                *result = 0.0;
                return true;
            }
            // OpenSCAD special variables — default values; can be overridden via m_vars
            if (name == QStringLiteral("$fn")) {
                *result = m_vars.value(name, 0.0);
                return true;
            }
            if (name == QStringLiteral("$fa")) {
                *result = m_vars.value(name, 12.0);
                return true;
            }
            if (name == QStringLiteral("$fs")) {
                *result = m_vars.value(name, 2.0);
                return true;
            }
            if (name == QStringLiteral("$t")) {
                *result = m_vars.value(name, 0.0); // animation time 0..1, default 0
                return true;
            }
            if (name.compare(QStringLiteral("undef"), Qt::CaseInsensitive) == 0) {
                *result = 0.0;
                return true;
            }
            if (!m_vars.contains(name))
                return false;
            *result = m_vars.value(name);
            return true;
        }

        bool evalFunctionCall(const QString &name, qreal *result)
        {
            QVector<qreal> args;
            if (!evalArgumentList(args))
                return false;

            const qreal a0 = args.size() > 0 ? args[0] : 0.0;
            const qreal a1 = args.size() > 1 ? args[1] : 0.0;

            if (name.compare(QStringLiteral("sin"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("sin() expects 1 argument"));
                *result = qSin(qDegreesToRadians(a0));
                return true;
            }
            if (name.compare(QStringLiteral("cos"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("cos() expects 1 argument"));
                *result = qCos(qDegreesToRadians(a0));
                return true;
            }
            if (name.compare(QStringLiteral("tan"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("tan() expects 1 argument"));
                *result = qTan(qDegreesToRadians(a0));
                return true;
            }
            if (name.compare(QStringLiteral("asin"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("asin() expects 1 argument"));
                if (a0 < -1.0 || a0 > 1.0) return setError(QStringLiteral("asin() domain error"));
                *result = qRadiansToDegrees(qAsin(a0));
                return true;
            }
            if (name.compare(QStringLiteral("acos"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("acos() expects 1 argument"));
                if (a0 < -1.0 || a0 > 1.0) return setError(QStringLiteral("acos() domain error"));
                *result = qRadiansToDegrees(qAcos(a0));
                return true;
            }
            if (name.compare(QStringLiteral("atan"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("atan() expects 1 argument"));
                *result = qRadiansToDegrees(qAtan(a0));
                return true;
            }
            if (name.compare(QStringLiteral("atan2"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 2) return setError(QStringLiteral("atan2() expects 2 arguments"));
                *result = qRadiansToDegrees(qAtan2(a0, a1));
                return true;
            }
            if (name.compare(QStringLiteral("abs"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("abs() expects 1 argument"));
                *result = qAbs(a0);
                return true;
            }
            if (name.compare(QStringLiteral("ceil"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("ceil() expects 1 argument"));
                *result = qCeil(a0);
                return true;
            }
            if (name.compare(QStringLiteral("floor"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("floor() expects 1 argument"));
                *result = qFloor(a0);
                return true;
            }
            if (name.compare(QStringLiteral("round"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("round() expects 1 argument"));
                *result = static_cast<qreal>(qRound(a0));
                return true;
            }
            if (name.compare(QStringLiteral("sign"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("sign() expects 1 argument"));
                *result = (a0 > 0.0) ? 1.0 : (a0 < 0.0) ? -1.0 : 0.0;
                return true;
            }
            if (name.compare(QStringLiteral("sqrt"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("sqrt() expects 1 argument"));
                if (a0 < 0.0) return setError(QStringLiteral("sqrt() of negative number"));
                *result = qSqrt(a0);
                return true;
            }
            if (name.compare(QStringLiteral("pow"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 2) return setError(QStringLiteral("pow() expects 2 arguments"));
                *result = qPow(a0, a1);
                return true;
            }
            if (name.compare(QStringLiteral("exp"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("exp() expects 1 argument"));
                *result = qExp(a0);
                return true;
            }
            if (name.compare(QStringLiteral("log"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("log() expects 1 argument"));
                if (a0 <= 0.0) return setError(QStringLiteral("log() of non-positive number"));
                *result = std::log10(a0); // OpenSCAD log() is log base-10, not natural log
                return true;
            }
            if (name.compare(QStringLiteral("ln"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("ln() expects 1 argument"));
                if (a0 <= 0.0) return setError(QStringLiteral("ln() of non-positive number"));
                *result = qLn(a0);
                return true;
            }
            if (name.compare(QStringLiteral("min"), Qt::CaseInsensitive) == 0) {
                if (args.isEmpty()) return setError(QStringLiteral("min() expects at least 1 argument"));
                *result = *std::min_element(args.begin(), args.end());
                return true;
            }
            if (name.compare(QStringLiteral("max"), Qt::CaseInsensitive) == 0) {
                if (args.isEmpty()) return setError(QStringLiteral("max() expects at least 1 argument"));
                *result = *std::max_element(args.begin(), args.end());
                return true;
            }
            if (name.compare(QStringLiteral("rands"), Qt::CaseInsensitive) == 0) {
                if (args.size() < 2) return setError(QStringLiteral("rands() expects at least 2 arguments"));
                const qreal lo = a0, hi = a1;
                if (hi <= lo) { *result = lo; return true; }
                *result = lo + QRandomGenerator::global()->generateDouble() * (hi - lo);
                return true;
            }
            if (name.compare(QStringLiteral("str"), Qt::CaseInsensitive) == 0) {
                // str() converts values to string — in our scalar evaluator,
                // return the first numeric arg (used mainly inside echo() which we ignore).
                *result = args.isEmpty() ? 0.0 : a0;
                return true;
            }
            if (name.compare(QStringLiteral("concat"), Qt::CaseInsensitive) == 0) {
                // concat() joins lists — in our scalar evaluator, return arg count.
                *result = static_cast<qreal>(args.size());
                return true;
            }
            if (name.compare(QStringLiteral("lookup"), Qt::CaseInsensitive) == 0) {
                // lookup(key, table) — interpolation; return key as-is for preview.
                *result = a0;
                return true;
            }
            if (name.compare(QStringLiteral("len"), Qt::CaseInsensitive) == 0) {
                // len(v) — array/vector length; return 0 as placeholder (arrays not tracked).
                *result = 0.0;
                return true;
            }
            if (name.compare(QStringLiteral("norm"), Qt::CaseInsensitive) == 0) {
                *result = 0.0;
                return true;
            }
            if (name.compare(QStringLiteral("cross"), Qt::CaseInsensitive) == 0) {
                *result = 0.0;
                return true;
            }
            if (m_fns) {
                auto it = m_fns->find(name);
                if (it != m_fns->end()) {
                    const FunctionDef &fn = it.value();
                    if (args.size() != fn.params.size())
                        return setError(QStringLiteral("Function '%1' expects %2 argument(s), got %3")
                                        .arg(name).arg(fn.params.size()).arg(args.size()));
                    QHash<QString, qreal> localVars = m_vars;
                    for (int i = 0; i < fn.params.size(); ++i)
                        localVars[fn.params[i]] = args[i];
                    qreal fnResult = 0.0;
                    if (!ExpressionSyntax::evaluate(fn.body, localVars, &fnResult, nullptr, m_fns))
                        return setError(QStringLiteral("Error evaluating function '%1'").arg(name));
                    *result = fnResult;
                    return true;
                }
            }

            return setError(QStringLiteral("Unknown function '%1'.").arg(name));
        }

        bool evalArgumentList(QVector<qreal> &args)
        {
            args.clear();
            skipSpaces();
            if (consume(QLatin1Char(')')))
                return true;

            qreal first = 0.0;
            if (!evalExpression(&first))
                return false;
            args.append(first);

            while (consume(QLatin1Char(','))) {
                skipSpaces();
                qreal next = 0.0;
                if (!evalExpression(&next))
                    return false;
                args.append(next);
            }

            skipSpaces();
            if (!consume(QLatin1Char(')')))
                return setError(QStringLiteral("Expected ')' after function arguments at position %1.").arg(m_pos + 1));
            return true;
        }

        bool evalNumber(qreal *result)
        {
            skipSpaces();
            const int start = m_pos;
            bool hasDigit = false;

            while (!atEnd() && m_text[m_pos].isDigit()) {
                hasDigit = true;
                ++m_pos;
            }

            if (!atEnd() && m_text[m_pos] == QLatin1Char('.')) {
                ++m_pos;
                while (!atEnd() && m_text[m_pos].isDigit()) {
                    hasDigit = true;
                    ++m_pos;
                }
            }

            if (!hasDigit) {
                m_pos = start;
                return false;
            }

            // Scientific notation (e.g. 1e5, 2.5e-3)
            if (!atEnd() && (m_text[m_pos] == QLatin1Char('e') || m_text[m_pos] == QLatin1Char('E'))) {
                const int save = m_pos;
                ++m_pos;
                if (!atEnd() && (m_text[m_pos] == QLatin1Char('+') || m_text[m_pos] == QLatin1Char('-')))
                    ++m_pos;
                if (atEnd() || !m_text[m_pos].isDigit())
                    m_pos = save;
                else {
                    while (!atEnd() && m_text[m_pos].isDigit())
                        ++m_pos;
                }
            }

            bool ok = false;
            *result = m_text.mid(start, m_pos - start).toDouble(&ok);
            return ok;
        }

        bool consume(QChar ch)
        {
            skipSpaces();
            if (atEnd() || m_text[m_pos] != ch)
                return false;

            ++m_pos;
            return true;
        }

        bool consumeString(const QString &str)
        {
            skipSpaces();
            if (m_text.mid(m_pos, str.size()) == str) {
                m_pos += str.size();
                return true;
            }
            return false;
        }

        bool setError(const QString &message)
        {
            if (m_error.isEmpty())
                m_error = message;
            return false;
        }

        QString m_text;
        int m_pos = 0;
        QString m_error;
        const QHash<QString, qreal> &m_vars;
        const QHash<QString, FunctionDef> *m_fns = nullptr;
    };
};

#endif
