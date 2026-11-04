#include "translator.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>

bool JsonTranslator::loadLanguage(const QString &language)
{
    m_strings.clear();
    m_language.clear();
    QFile file(QStringLiteral(":/i18n/%1.json").arg(language));
    if (language.isEmpty() || !file.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject object = QJsonDocument::fromJson(file.readAll()).object();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        m_strings.insert(it.key(), it.value().toString());
    m_language = language;
    return !m_strings.isEmpty();
}

QString JsonTranslator::translate(const char *context, const char *sourceText,
                                  const char *disambiguation, int n) const
{
    Q_UNUSED(context);
    Q_UNUSED(disambiguation);
    Q_UNUSED(n);
    // A null string tells Qt to keep the English source text.
    return m_strings.value(QString::fromUtf8(sourceText));
}

QString JsonTranslator::resolveLanguage(const QString &setting)
{
    const QString value = setting.trimmed();
    if (!value.isEmpty() && value != QLatin1String("auto"))
        return value;
    // ru_RU -> ru; "C" and "POSIX" mean English.
    const QString name = QLocale::system().name();
    return name.section(QLatin1Char('_'), 0, 0);
}
