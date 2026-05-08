#ifndef TRANSLATOR_MANAGER_H
#define TRANSLATOR_MANAGER_H

#include <QObject>
#include <QTranslator>

class TranslatorManager : public QObject
{
    Q_OBJECT
public:
    static TranslatorManager& instance();

    bool loadTranslation(const QString& language); // language: "en", "zh"...
    void removeTranslation();

private:
    TranslatorManager() = default;
    QTranslator m_translator;
};

#endif // TRANSLATOR_MANAGER_H