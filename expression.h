#ifndef EXPRESSION_H
#define EXPRESSION_H

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
};

#endif
