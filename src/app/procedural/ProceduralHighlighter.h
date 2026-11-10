#pragma once

#include <QColor>
#include <QQuickTextDocument>
#include <QString>
#include <QSyntaxHighlighter>



// Syntax colouring for the Procedural workspace's code editor. Attach it to a TextArea's
// textDocument from QML; the colours come from theme tokens, set as properties.
class ProceduralHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT
    Q_PROPERTY(QQuickTextDocument *textDocument READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged)
    // "lua", "glsl", "json"; anything else leaves the text plain.
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QColor keywordColor MEMBER m_keyword NOTIFY colorsChanged)
    Q_PROPERTY(QColor typeColor MEMBER m_type NOTIFY colorsChanged)
    Q_PROPERTY(QColor builtinColor MEMBER m_builtin NOTIFY colorsChanged)
    Q_PROPERTY(QColor stringColor MEMBER m_string NOTIFY colorsChanged)
    Q_PROPERTY(QColor commentColor MEMBER m_comment NOTIFY colorsChanged)
    Q_PROPERTY(QColor numberColor MEMBER m_number NOTIFY colorsChanged)

public:
    explicit ProceduralHighlighter(QObject *parent = nullptr);

    QQuickTextDocument *textDocument() const { return m_quickDocument; }
    void setTextDocument(QQuickTextDocument *document);
    QString language() const { return m_language; }
    void setLanguage(const QString &language);

signals:
    void textDocumentChanged();
    void languageChanged();
    void colorsChanged();

protected:
    void highlightBlock(const QString &text) override;

private:
    enum class Lang { Plain, Lua, Glsl, Json };

    QQuickTextDocument *m_quickDocument = nullptr;
    QString m_language;
    Lang m_lang = Lang::Plain;
    QColor m_keyword, m_type, m_builtin, m_string, m_comment, m_number;
};
