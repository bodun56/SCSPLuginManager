#pragma once

#include <QString>
#include <QList>
#include <QPair>
#include <QSet>
#include <QByteArray>
#include <QSqlDatabase>

struct GameInfo {
    int id = -1;
    QString name;
    QString gameRoot;
    QString executablePath;
    QString iconPath;
    QByteArray iconBlob;
    int steamAppId = -1;
};

struct PluginInfo {
    int id = -1;
    int gameId = -1;
    QString fileBaseName;
    QString description;
    QString url;
    bool enabled = true;
    QString fullPath;
};

struct InstallResult {
    bool ok = false;
    QString error;
    int pluginId = -1;
    QString pluginBaseName;
    QStringList installedRelativeFiles;
};

class PluginManager
{
public:
    PluginManager();
    ~PluginManager();

    bool initDatabase(const QString &dbPath);
    bool isDatabaseOpen() const;

    // Игры
    QList<GameInfo> loadGames();
    bool saveGame(const GameInfo &game);
    bool removeGame(int gameId);
    bool gameExistsByRoot(const QString &root);

    // Плагины
    QList<PluginInfo> scanPlugins(const GameInfo &game);
    bool setPluginEnabled(const PluginInfo &plugin, bool enabled);
    bool deletePlugin(const PluginInfo &plugin, bool removeAuxiliaryFiles);
    bool updatePluginMeta(const PluginInfo &plugin);

    QString pluginsDirForGame(const GameInfo &game) const;

    // Установка
    InstallResult installFromMapping(const GameInfo &game,
                                     const QList<QPair<QString, QString>> &sourceToRelative,
                                     const QString &hintBaseName);

    QStringList pluginFiles(int pluginId) const;
    QStringList pluginFilesForDeletion(const PluginInfo &plugin) const;

private:
    QSqlDatabase m_db;
    bool ensureTables();
    int findPluginDbId(int gameId, const QString &fileBaseName);
    bool insertPluginRecord(int gameId, const QString &fileBaseName);
    bool setPluginFiles(int pluginId, const QStringList &relativePaths);
    QSet<QString> helperBaseNames() const;
};
