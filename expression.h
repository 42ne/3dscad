#ifndef EXPRESSION_H
#define EXPRESSION_H

#include <QHash>
#include <QRandomGenerator>
#include <QString>
#include <QVector>
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
                if (atEnd())
                    return true;
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

            if (!parsePrimary())
                return false;

            // Postfix array/vector subscripts: a[i][j]...
            while (true) {
                skipSpaces();
                if (!consume(QLatin1Char('[')))
                    return true;
                if (!parseExpression())
                    return false;
                skipSpaces();
                if (!consume(QLatin1Char(']')))
                    return setError(QStringLiteral("Expected ']' at position %1.").arg(position() + 1));
            }
        }

        bool parsePrimary()
        {
            skipSpaces();

            if (consume(QLatin1Char('('))) {
                if (!parseExpression())
                    return false;
                skipSpaces();
                if (!consume(QLatin1Char(')')))
                    return setError(QStringLiteral("Expected ')' at position %1.").arg(position() + 1));
                return true;
            }

            if (consume(QLatin1Char('[')))
                return parseVectorOrRangeTail();

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

        // '[' already consumed. Accepts vector literal [a, b, ...], range [a:b],
        // range with step [a:step:b], and the empty vector [].
        bool parseVectorOrRangeTail()
        {
            skipSpaces();
            if (consume(QLatin1Char(']')))
                return true; // empty vector

            if (!parseExpression())
                return false;

            skipSpaces();
            if (consume(QLatin1Char(':'))) {
                if (!parseExpression())
                    return setError(QStringLiteral("Expected expression after ':' at position %1.").arg(position() + 1));
                skipSpaces();
                if (consume(QLatin1Char(':'))) {
                    if (!parseExpression())
                        return setError(QStringLiteral("Expected expression after ':' at position %1.").arg(position() + 1));
                    skipSpaces();
                }
                if (!consume(QLatin1Char(']')))
                    return setError(QStringLiteral("Expected ']' at position %1.").arg(position() + 1));
                return true;
            }

            while (consume(QLatin1Char(','))) {
                skipSpaces();
                if (consume(QLatin1Char(']'))) // trailing comma
                    return true;
                if (!parseExpression())
                    return false;
                skipSpaces();
            }

            if (!consume(QLatin1Char(']')))
                return setError(QStringLiteral("Expected ']' at position %1.").arg(position() + 1));
            return true;
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
    // A runtime value: either a scalar or a (possibly nested) vector.
    struct Value
    {
        bool isVector = false;
        qreal scalar = 0.0;
        QVector<Value> items; // valid when isVector

        Value() = default;
        explicit Value(qreal s) : scalar(s) {}

        static Value vector(QVector<Value> v)
        {
            Value x;
            x.isVector = true;
            x.items = std::move(v);
            return x;
        }

        // Collapse to a scalar (used by the scalar public API and by operators
        // that only have scalar semantics). A vector collapses to its first
        // element, recursively; an empty vector collapses to 0.
        qreal toScalar() const
        {
            if (!isVector)
                return scalar;
            return items.isEmpty() ? 0.0 : items.first().toScalar();
        }

        // OpenSCAD truthiness: non-empty vector is true; empty vector is false.
        bool truthy() const
        {
            return isVector ? !items.isEmpty() : (scalar != 0.0);
        }
    };

    static bool evaluate(const QString &text,
                         const QHash<QString, qreal> &variableValues,
                         qreal *value,
                         QString *errorMessage = nullptr,
                         const QHash<QString, FunctionDef> *functions = nullptr)
    {
        Value result;
        if (!evaluateValue(text, variableValues, &result, errorMessage, functions))
            return false;
        if (value)
            *value = result.toScalar();
        return true;
    }

    // Vector-aware evaluation. Returns the full Value (scalar or vector).
    static bool evaluateValue(const QString &text,
                              const QHash<QString, qreal> &variableValues,
                              Value *value,
                              QString *errorMessage = nullptr,
                              const QHash<QString, FunctionDef> *functions = nullptr)
    {
        Evaluator ev(text, variableValues, functions);
        Value result;
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
        using Value = ExpressionSyntax::Value;

        explicit Evaluator(QString text, const QHash<QString, qreal> &vars,
                           const QHash<QString, FunctionDef> *fns = nullptr)
            : m_text(std::move(text)), m_vars(vars), m_fns(fns)
        {
        }

        bool evalExpression(Value *result)
        {
            return evalTernary(result);
        }

        bool evalTernary(Value *result)
        {
            Value cond;
            if (!evalLogical(&cond))
                return false;

            skipSpaces();
            if (consume(QLatin1Char('?'))) {
                Value trueVal, falseVal;
                if (!evalExpression(&trueVal))
                    return setError(QStringLiteral("Expected expression after '?' at position %1.").arg(m_pos + 1));
                skipSpaces();
                if (!consume(QLatin1Char(':')))
                    return setError(QStringLiteral("Expected ':' after true branch at position %1.").arg(m_pos + 1));
                if (!evalTernary(&falseVal))
                    return setError(QStringLiteral("Expected expression after ':' at position %1.").arg(m_pos + 1));
                *result = cond.truthy() ? trueVal : falseVal;
                return true;
            }

            *result = cond;
            return true;
        }

        bool evalLogical(Value *result)
        {
            Value left;
            if (!evalComparison(&left))
                return false;

            while (true) {
                skipSpaces();
                if (consumeString(QStringLiteral("&&"))) {
                    Value right;
                    if (!evalComparison(&right))
                        return setError(QStringLiteral("Expected expression after '&&' at position %1.").arg(m_pos + 1));
                    left = Value((left.truthy() && right.truthy()) ? 1.0 : 0.0);
                } else if (consumeString(QStringLiteral("||"))) {
                    Value right;
                    if (!evalComparison(&right))
                        return setError(QStringLiteral("Expected expression after '||' at position %1.").arg(m_pos + 1));
                    left = Value((left.truthy() || right.truthy()) ? 1.0 : 0.0);
                } else {
                    break;
                }
            }

            *result = left;
            return true;
        }

        bool evalComparison(Value *result)
        {
            Value left;
            if (!evalAddSub(&left))
                return false;

            while (true) {
                skipSpaces();
                if (consumeString(QStringLiteral("=="))) {
                    Value right;
                    if (!evalAddSub(&right))
                        return setError(QStringLiteral("Expected expression after '==' at position %1.").arg(m_pos + 1));
                    left = Value(valuesEqual(left, right) ? 1.0 : 0.0);
                } else if (consumeString(QStringLiteral("!="))) {
                    Value right;
                    if (!evalAddSub(&right))
                        return setError(QStringLiteral("Expected expression after '!=' at position %1.").arg(m_pos + 1));
                    left = Value(valuesEqual(left, right) ? 0.0 : 1.0);
                } else if (consumeString(QStringLiteral("<="))) {
                    Value right;
                    if (!evalAddSub(&right))
                        return setError(QStringLiteral("Expected expression after '<=' at position %1.").arg(m_pos + 1));
                    left = Value((left.toScalar() <= right.toScalar()) ? 1.0 : 0.0);
                } else if (consumeString(QStringLiteral(">="))) {
                    Value right;
                    if (!evalAddSub(&right))
                        return setError(QStringLiteral("Expected expression after '>=' at position %1.").arg(m_pos + 1));
                    left = Value((left.toScalar() >= right.toScalar()) ? 1.0 : 0.0);
                } else if (consume(QLatin1Char('<'))) {
                    Value right;
                    if (!evalAddSub(&right))
                        return setError(QStringLiteral("Expected expression after '<' at position %1.").arg(m_pos + 1));
                    left = Value((left.toScalar() < right.toScalar()) ? 1.0 : 0.0);
                } else if (consume(QLatin1Char('>'))) {
                    Value right;
                    if (!evalAddSub(&right))
                        return setError(QStringLiteral("Expected expression after '>' at position %1.").arg(m_pos + 1));
                    left = Value((left.toScalar() > right.toScalar()) ? 1.0 : 0.0);
                } else {
                    break;
                }
            }

            *result = left;
            return true;
        }

        bool evalAddSub(Value *result)
        {
            Value left;
            if (!evalTerm(&left))
                return false;

            while (true) {
                skipSpaces();
                if (consume(QLatin1Char('+'))) {
                    Value right;
                    if (!evalTerm(&right))
                        return setError(QStringLiteral("Expected expression after '+' at position %1.").arg(m_pos + 1));
                    if (!combineAddSub(left, right, /*add*/ true, &left))
                        return false;
                } else if (consume(QLatin1Char('-'))) {
                    Value right;
                    if (!evalTerm(&right))
                        return setError(QStringLiteral("Expected expression after '-' at position %1.").arg(m_pos + 1));
                    if (!combineAddSub(left, right, /*add*/ false, &left))
                        return false;
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
        bool evalTerm(Value *result)
        {
            Value left;
            if (!evalFactor(&left))
                return false;

            while (true) {
                skipSpaces();
                if (consume(QLatin1Char('*'))) {
                    Value right;
                    if (!evalFactor(&right))
                        return setError(QStringLiteral("Expected expression after '*' at position %1.").arg(m_pos + 1));
                    if (!combineMul(left, right, &left))
                        return false;
                } else if (consume(QLatin1Char('/'))) {
                    Value right;
                    if (!evalFactor(&right))
                        return setError(QStringLiteral("Expected expression after '/' at position %1.").arg(m_pos + 1));
                    if (!combineDiv(left, right, &left))
                        return false;
                } else if (consume(QLatin1Char('%'))) {
                    Value right;
                    if (!evalFactor(&right))
                        return setError(QStringLiteral("Expected expression after '%%' at position %1.").arg(m_pos + 1));
                    if (right.toScalar() == 0.0)
                        return setError(QStringLiteral("Modulo by zero."));
                    left = Value(std::fmod(left.toScalar(), right.toScalar()));
                } else {
                    break;
                }
            }

            *result = left;
            return true;
        }

        bool evalFactor(Value *result)
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

            Value value;
            if (consume(QLatin1Char('('))) {
                if (!evalExpression(&value))
                    return false;
                skipSpaces();
                if (!consume(QLatin1Char(')')))
                    return setError(QStringLiteral("Expected ')' at position %1.").arg(m_pos + 1));
            } else if (m_pos < m_text.size() && m_text[m_pos] == QLatin1Char('[')) {
                ++m_pos; // consume '['
                if (!evalVectorOrRange(&value))
                    return false;
            } else {
                qreal num = 0.0;
                if (evalNumber(&num)) {
                    value = Value(num);
                } else {
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
                        qreal scalar = 0.0;
                        if (!resolveIdentifier(name, &scalar)) {
                            m_pos = savedPos;
                            return setError(QStringLiteral("Unknown variable '%1'.").arg(name));
                        }
                        value = Value(scalar);
                    }
                }
            }

            // Postfix array/vector subscript: expr[idx][idx2]...
            skipSpaces();
            while (!atEnd() && m_text[m_pos] == QLatin1Char('[')) {
                ++m_pos; // consume '['
                Value idxVal;
                if (!evalExpression(&idxVal))
                    return false;
                skipSpaces();
                if (atEnd() || m_text[m_pos] != QLatin1Char(']'))
                    return setError(QStringLiteral("Expected ']' at position %1.").arg(m_pos + 1));
                ++m_pos; // consume ']'

                const int idx = static_cast<int>(std::floor(idxVal.toScalar()));
                if (value.isVector && idx >= 0 && idx < value.items.size())
                    value = value.items[idx];
                else
                    value = Value(0.0); // out of range / non-vector → undef
                skipSpaces();
            }

            if (logicalNot)
                value = Value(value.truthy() ? 0.0 : 1.0);
            if (negate)
                value = negated(value);
            *result = value;
            return true;
        }

        // '[' already consumed. Parses a vector literal or a range and stores
        // the resulting Value (ranges are expanded into a vector).
        bool evalVectorOrRange(Value *result)
        {
            skipSpaces();
            if (consume(QLatin1Char(']'))) {
                *result = Value::vector({});
                return true;
            }

            Value first;
            if (!evalExpression(&first))
                return false;

            skipSpaces();
            if (consume(QLatin1Char(':'))) {
                // Range: [start:end] or [start:step:end]
                Value second;
                if (!evalExpression(&second))
                    return setError(QStringLiteral("Expected expression after ':' at position %1.").arg(m_pos + 1));
                qreal start = first.toScalar();
                qreal step = 1.0;
                qreal end = second.toScalar();
                skipSpaces();
                if (consume(QLatin1Char(':'))) {
                    Value third;
                    if (!evalExpression(&third))
                        return setError(QStringLiteral("Expected expression after ':' at position %1.").arg(m_pos + 1));
                    step = second.toScalar();
                    end = third.toScalar();
                    skipSpaces();
                }
                if (!consume(QLatin1Char(']')))
                    return setError(QStringLiteral("Expected ']' at position %1.").arg(m_pos + 1));
                return expandRange(start, step, end, result);
            }

            QVector<Value> elems;
            elems.append(first);
            while (consume(QLatin1Char(','))) {
                skipSpaces();
                if (consume(QLatin1Char(']'))) { // trailing comma
                    *result = Value::vector(elems);
                    return true;
                }
                Value next;
                if (!evalExpression(&next))
                    return false;
                elems.append(next);
                skipSpaces();
            }

            if (!consume(QLatin1Char(']')))
                return setError(QStringLiteral("Expected ']' at position %1.").arg(m_pos + 1));
            *result = Value::vector(elems);
            return true;
        }

        bool expandRange(qreal start, qreal step, qreal end, Value *result)
        {
            QVector<Value> elems;
            if (step == 0.0)
                return setError(QStringLiteral("Range step is zero."));

            // Guard against runaway expansion.
            const double span = (end - start) / step;
            if (span < 0.0) {
                *result = Value::vector({});
                return true;
            }
            const double count = std::floor(span + 1e-9) + 1.0;
            if (count > 1000000.0)
                return setError(QStringLiteral("Range is too large to expand."));

            if (step > 0.0) {
                for (qreal v = start; v <= end + 1e-9; v += step)
                    elems.append(Value(v));
            } else {
                for (qreal v = start; v >= end - 1e-9; v += step)
                    elems.append(Value(v));
            }
            *result = Value::vector(elems);
            return true;
        }

        static Value negated(const Value &v)
        {
            if (!v.isVector)
                return Value(-v.scalar);
            QVector<Value> out;
            out.reserve(v.items.size());
            for (const Value &item : v.items)
                out.append(negated(item));
            return Value::vector(out);
        }

        static bool valuesEqual(const Value &a, const Value &b)
        {
            if (a.isVector != b.isVector)
                return false;
            if (!a.isVector)
                return a.scalar == b.scalar;
            if (a.items.size() != b.items.size())
                return false;
            for (int i = 0; i < a.items.size(); ++i)
                if (!valuesEqual(a.items[i], b.items[i]))
                    return false;
            return true;
        }

        // Elementwise +/- for matching shapes; scalar +/- scalar otherwise.
        bool combineAddSub(const Value &a, const Value &b, bool add, Value *out)
        {
            if (!a.isVector && !b.isVector) {
                *out = Value(add ? a.scalar + b.scalar : a.scalar - b.scalar);
                return true;
            }
            if (a.isVector && b.isVector) {
                if (a.items.size() != b.items.size())
                    return setError(QStringLiteral("Vector length mismatch in %1.")
                                        .arg(add ? QStringLiteral("addition") : QStringLiteral("subtraction")));
                QVector<Value> r;
                r.reserve(a.items.size());
                for (int i = 0; i < a.items.size(); ++i) {
                    Value e;
                    if (!combineAddSub(a.items[i], b.items[i], add, &e))
                        return false;
                    r.append(e);
                }
                *out = Value::vector(r);
                return true;
            }
            return setError(QStringLiteral("Cannot add/subtract a scalar and a vector."));
        }

        // scalar*scalar; vector*scalar / scalar*vector (scale); vector*vector (dot).
        bool combineMul(const Value &a, const Value &b, Value *out)
        {
            if (!a.isVector && !b.isVector) {
                *out = Value(a.scalar * b.scalar);
                return true;
            }
            if (a.isVector && !b.isVector) {
                *out = scaled(a, b.scalar);
                return true;
            }
            if (!a.isVector && b.isVector) {
                *out = scaled(b, a.scalar);
                return true;
            }
            // vector * vector → dot product
            if (a.items.size() != b.items.size())
                return setError(QStringLiteral("Vector length mismatch in multiplication."));
            qreal sum = 0.0;
            for (int i = 0; i < a.items.size(); ++i)
                sum += a.items[i].toScalar() * b.items[i].toScalar();
            *out = Value(sum);
            return true;
        }

        bool combineDiv(const Value &a, const Value &b, Value *out)
        {
            if (b.isVector)
                return setError(QStringLiteral("Cannot divide by a vector."));
            if (b.scalar == 0.0)
                return setError(QStringLiteral("Division by zero."));
            if (a.isVector) {
                *out = scaled(a, 1.0 / b.scalar);
                return true;
            }
            *out = Value(a.scalar / b.scalar);
            return true;
        }

        static Value scaled(const Value &v, qreal factor)
        {
            if (!v.isVector)
                return Value(v.scalar * factor);
            QVector<Value> out;
            out.reserve(v.items.size());
            for (const Value &item : v.items)
                out.append(scaled(item, factor));
            return Value::vector(out);
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

        bool evalFunctionCall(const QString &name, Value *result)
        {
            QVector<Value> args;
            if (!evalArgumentList(args))
                return false;

            const qreal a0 = args.size() > 0 ? args[0].toScalar() : 0.0;
            const qreal a1 = args.size() > 1 ? args[1].toScalar() : 0.0;

            if (name.compare(QStringLiteral("sin"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("sin() expects 1 argument"));
                *result = Value(qSin(qDegreesToRadians(a0)));
                return true;
            }
            if (name.compare(QStringLiteral("cos"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("cos() expects 1 argument"));
                *result = Value(qCos(qDegreesToRadians(a0)));
                return true;
            }
            if (name.compare(QStringLiteral("tan"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("tan() expects 1 argument"));
                *result = Value(qTan(qDegreesToRadians(a0)));
                return true;
            }
            if (name.compare(QStringLiteral("asin"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("asin() expects 1 argument"));
                if (a0 < -1.0 || a0 > 1.0) return setError(QStringLiteral("asin() domain error"));
                *result = Value(qRadiansToDegrees(qAsin(a0)));
                return true;
            }
            if (name.compare(QStringLiteral("acos"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("acos() expects 1 argument"));
                if (a0 < -1.0 || a0 > 1.0) return setError(QStringLiteral("acos() domain error"));
                *result = Value(qRadiansToDegrees(qAcos(a0)));
                return true;
            }
            if (name.compare(QStringLiteral("atan"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("atan() expects 1 argument"));
                *result = Value(qRadiansToDegrees(qAtan(a0)));
                return true;
            }
            if (name.compare(QStringLiteral("atan2"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 2) return setError(QStringLiteral("atan2() expects 2 arguments"));
                *result = Value(qRadiansToDegrees(qAtan2(a0, a1)));
                return true;
            }
            if (name.compare(QStringLiteral("abs"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("abs() expects 1 argument"));
                *result = Value(qAbs(a0));
                return true;
            }
            if (name.compare(QStringLiteral("ceil"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("ceil() expects 1 argument"));
                *result = Value(qCeil(a0));
                return true;
            }
            if (name.compare(QStringLiteral("floor"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("floor() expects 1 argument"));
                *result = Value(qFloor(a0));
                return true;
            }
            if (name.compare(QStringLiteral("round"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("round() expects 1 argument"));
                *result = Value(static_cast<qreal>(qRound(a0)));
                return true;
            }
            if (name.compare(QStringLiteral("sign"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("sign() expects 1 argument"));
                *result = Value((a0 > 0.0) ? 1.0 : (a0 < 0.0) ? -1.0 : 0.0);
                return true;
            }
            if (name.compare(QStringLiteral("sqrt"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("sqrt() expects 1 argument"));
                if (a0 < 0.0) return setError(QStringLiteral("sqrt() of negative number"));
                *result = Value(qSqrt(a0));
                return true;
            }
            if (name.compare(QStringLiteral("pow"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 2) return setError(QStringLiteral("pow() expects 2 arguments"));
                *result = Value(qPow(a0, a1));
                return true;
            }
            if (name.compare(QStringLiteral("exp"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("exp() expects 1 argument"));
                *result = Value(qExp(a0));
                return true;
            }
            if (name.compare(QStringLiteral("log"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("log() expects 1 argument"));
                if (a0 <= 0.0) return setError(QStringLiteral("log() of non-positive number"));
                *result = Value(std::log10(a0)); // OpenSCAD log() is log base-10, not natural log
                return true;
            }
            if (name.compare(QStringLiteral("ln"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("ln() expects 1 argument"));
                if (a0 <= 0.0) return setError(QStringLiteral("ln() of non-positive number"));
                *result = Value(qLn(a0));
                return true;
            }
            if (name.compare(QStringLiteral("min"), Qt::CaseInsensitive) == 0) {
                QVector<qreal> flat = flattenToScalars(args);
                if (flat.isEmpty()) return setError(QStringLiteral("min() expects at least 1 argument"));
                *result = Value(*std::min_element(flat.begin(), flat.end()));
                return true;
            }
            if (name.compare(QStringLiteral("max"), Qt::CaseInsensitive) == 0) {
                QVector<qreal> flat = flattenToScalars(args);
                if (flat.isEmpty()) return setError(QStringLiteral("max() expects at least 1 argument"));
                *result = Value(*std::max_element(flat.begin(), flat.end()));
                return true;
            }
            if (name.compare(QStringLiteral("rands"), Qt::CaseInsensitive) == 0) {
                if (args.size() < 2) return setError(QStringLiteral("rands() expects at least 2 arguments"));
                const qreal lo = a0, hi = a1;
                if (hi <= lo) { *result = Value(lo); return true; }
                *result = Value(lo + QRandomGenerator::global()->generateDouble() * (hi - lo));
                return true;
            }
            if (name.compare(QStringLiteral("str"), Qt::CaseInsensitive) == 0) {
                // str() converts values to string — in our numeric evaluator,
                // return the first numeric arg (used mainly inside echo() which we ignore).
                *result = Value(args.isEmpty() ? 0.0 : a0);
                return true;
            }
            if (name.compare(QStringLiteral("concat"), Qt::CaseInsensitive) == 0) {
                // concat() flattens its arguments (each scalar or vector) into a single vector.
                QVector<Value> out;
                for (const Value &arg : args) {
                    if (arg.isVector)
                        out += arg.items;
                    else
                        out.append(arg);
                }
                *result = Value::vector(out);
                return true;
            }
            if (name.compare(QStringLiteral("lookup"), Qt::CaseInsensitive) == 0) {
                // lookup(key, table) — interpolation; return key as-is for preview.
                *result = Value(a0);
                return true;
            }
            if (name.compare(QStringLiteral("len"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("len() expects 1 argument"));
                *result = Value(args[0].isVector ? static_cast<qreal>(args[0].items.size()) : 0.0);
                return true;
            }
            if (name.compare(QStringLiteral("norm"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 1) return setError(QStringLiteral("norm() expects 1 argument"));
                const Value &v = args[0];
                if (!v.isVector) { *result = Value(qAbs(v.scalar)); return true; }
                qreal sum = 0.0;
                for (const Value &item : v.items) {
                    const qreal c = item.toScalar();
                    sum += c * c;
                }
                *result = Value(qSqrt(sum));
                return true;
            }
            if (name.compare(QStringLiteral("cross"), Qt::CaseInsensitive) == 0) {
                if (args.size() != 2) return setError(QStringLiteral("cross() expects 2 arguments"));
                const Value &a = args[0];
                const Value &b = args[1];
                if (!a.isVector || !b.isVector || a.items.size() != 3 || b.items.size() != 3)
                    return setError(QStringLiteral("cross() expects two 3-element vectors"));
                const qreal ax = a.items[0].toScalar(), ay = a.items[1].toScalar(), az = a.items[2].toScalar();
                const qreal bx = b.items[0].toScalar(), by = b.items[1].toScalar(), bz = b.items[2].toScalar();
                QVector<Value> r{ Value(ay * bz - az * by),
                                  Value(az * bx - ax * bz),
                                  Value(ax * by - ay * bx) };
                *result = Value::vector(r);
                return true;
            }
            if (m_fns) {
                auto it = m_fns->find(name);
                if (it != m_fns->end()) {
                    const FunctionDef &fn = it.value();
                    if (args.size() != fn.params.size())
                        return setError(QStringLiteral("Function '%1' expects %2 argument(s), got %3")
                                        .arg(name).arg(fn.params.size()).arg(args.size()));
                    // User functions operate in the scalar variable space.
                    QHash<QString, qreal> localVars = m_vars;
                    for (int i = 0; i < fn.params.size(); ++i)
                        localVars[fn.params[i]] = args[i].toScalar();
                    Value fnResult;
                    if (!ExpressionSyntax::evaluateValue(fn.body, localVars, &fnResult, nullptr, m_fns))
                        return setError(QStringLiteral("Error evaluating function '%1'").arg(name));
                    *result = fnResult;
                    return true;
                }
            }

            return setError(QStringLiteral("Unknown function '%1'.").arg(name));
        }

        static QVector<qreal> flattenToScalars(const QVector<Value> &args)
        {
            QVector<qreal> out;
            for (const Value &arg : args) {
                if (arg.isVector) {
                    for (const Value &item : arg.items)
                        out.append(item.toScalar());
                } else {
                    out.append(arg.scalar);
                }
            }
            return out;
        }

        bool evalArgumentList(QVector<Value> &args)
        {
            args.clear();
            skipSpaces();
            if (consume(QLatin1Char(')')))
                return true;

            Value first;
            if (!evalExpression(&first))
                return false;
            args.append(first);

            while (consume(QLatin1Char(','))) {
                skipSpaces();
                Value next;
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
