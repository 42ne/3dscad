#ifndef EXPRESSION_H
#define EXPRESSION_H

#include <QHash>
#include <QString>
#include <utility>

class ExpressionSyntax
{
public:
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

        void skipSpaces()
        {
            while (!atEnd() && m_text[m_pos].isSpace())
                ++m_pos;
        }

        bool atEnd() const { return m_pos >= m_text.size(); }
        int position() const { return m_pos; }
        QString error() const { return m_error.isEmpty() ? QStringLiteral("Invalid expression.") : m_error; }

    private:
        bool parseTerm()
        {
            if (!parseFactor())
                return false;

            while (true) {
                skipSpaces();
                if (!consume(QLatin1Char('*')) && !consume(QLatin1Char('/')))
                    return true;

                if (!parseFactor())
                    return setError(QStringLiteral("Expected expression after operator at position %1.").arg(position() + 1));
            }
        }

        bool parseFactor()
        {
            skipSpaces();

            consume(QLatin1Char('+')) || consume(QLatin1Char('-'));
            skipSpaces();

            if (consume(QLatin1Char('('))) {
                if (!parseExpression())
                    return false;

                skipSpaces();
                if (!consume(QLatin1Char(')')))
                    return setError(QStringLiteral("Expected ')' at position %1.").arg(position() + 1));
                return true;
            }

            return parseNumber() || parseIdentifier() || setError(QStringLiteral("Expected number, variable, or '(' at position %1.").arg(position() + 1));
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

            return true;
        }

        bool parseIdentifier()
        {
            skipSpaces();
            if (atEnd() || !(m_text[m_pos].isLetter() || m_text[m_pos] == QLatin1Char('_')))
                return false;

            ++m_pos;
            while (!atEnd() && (m_text[m_pos].isLetterOrNumber() || m_text[m_pos] == QLatin1Char('_')))
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
    // Evaluates expression with operator precedence and variable lookup.
    // Returns false if the expression is invalid or references an unknown variable.
    static bool evaluate(const QString &text,
                         const QHash<QString, qreal> &variableValues,
                         qreal *value,
                         QString *errorMessage = nullptr)
    {
        Evaluator ev(text, variableValues);
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
        explicit Evaluator(QString text, const QHash<QString, qreal> &vars)
            : m_text(std::move(text)), m_vars(vars)
        {
        }

        bool evalExpression(qreal *result)
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
            if (consume(QLatin1Char('-')))
                negate = true;
            else
                consume(QLatin1Char('+'));

            skipSpaces();

            qreal value = 0.0;
            if (consume(QLatin1Char('('))) {
                if (!evalExpression(&value))
                    return false;
                skipSpaces();
                if (!consume(QLatin1Char(')')))
                    return setError(QStringLiteral("Expected ')' at position %1.").arg(m_pos + 1));
            } else if (!evalNumber(&value) && !evalIdentifier(&value)) {
                return setError(QStringLiteral("Expected number, variable, or '(' at position %1.").arg(m_pos + 1));
            }

            *result = negate ? -value : value;
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

            bool ok = false;
            *result = m_text.mid(start, m_pos - start).toDouble(&ok);
            return ok;
        }

        bool evalIdentifier(qreal *result)
        {
            skipSpaces();
            if (atEnd() || !(m_text[m_pos].isLetter() || m_text[m_pos] == QLatin1Char('_')))
                return false;

            const int start = m_pos;
            ++m_pos;
            while (!atEnd() && (m_text[m_pos].isLetterOrNumber() || m_text[m_pos] == QLatin1Char('_')))
                ++m_pos;

            const QString name = m_text.mid(start, m_pos - start);
            if (!m_vars.contains(name))
                return setError(QStringLiteral("Unknown variable '%1'.").arg(name));

            *result = m_vars.value(name);
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
    };
};

#endif
