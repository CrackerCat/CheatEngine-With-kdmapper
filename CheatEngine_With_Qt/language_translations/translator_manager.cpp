#include "translator_manager.h"
#include <QCoreApplication>
#include <QDir>
#include <QDebug>

TranslatorManager& TranslatorManager::instance()
{
    static TranslatorManager inst;
    return inst;
}

bool TranslatorManager::loadTranslation(const QString& language)
{
    removeTranslation();
    // 从可执行文件同目录下的 Translate 文件夹加载 .qm
    QString qmPath = QCoreApplication::applicationDirPath() + "/language_Translate/Translation_" + language + ".qm";
    if (m_translator.load(qmPath)) {
        QCoreApplication::installTranslator(&m_translator);
        return true;
    }
    qWarning() << "Failed to load translation file:" << qmPath;
    return false;
}

void TranslatorManager::removeTranslation()
{
    QCoreApplication::removeTranslator(&m_translator);
}