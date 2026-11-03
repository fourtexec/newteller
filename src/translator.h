#pragma once

#include <QHash>
#include <QString>
#include <QTranslator>

// The shell's translations: the QML wraps its texts in qsTr() (English in
// the source), and this translator answers from a JSON dictionary compiled
// into the resources (:/i18n/<language>.json, English text -> translation).
// Plain JSON needs no Qt Linguist tools at build time on any distribution.
class JsonTranslator : public QTranslator {
    Q_OBJECT
public:
    using QTranslator::QTranslator;

    // Loads :/i18n/<language>.json; false (and empty) when there is none.
    bool loadLanguage(const QString &language);
    QString language() const { return m_language; }

    QString translate(const char *context, const char *sourceText, const char *disambiguation = nullptr,
                      int n = -1) const override;
    bool isEmpty() const override { return m_strings.isEmpty(); }

    // The language to use for a shell.language value: "auto" follows the
    // system locale (LANG / LC_MESSAGES), anything else is taken as is.
    static QString resolveLanguage(const QString &setting);

private:
    QHash<QString, QString> m_strings;
    QString m_language;
};
