#include "pluginmanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QSet>
#include <QDebug>
#include <algorithm>

PluginManager::PluginManager() = default;

PluginManager::~PluginManager()
{
    if (m_db.isOpen())
        m_db.close();
}

bool PluginManager::initDatabase(const QString &dbPath)
{
    if (QSqlDatabase::contains("scs_plugins"))
        m_db = QSqlDatabase::database("scs_plugins");
    else
        m_db = QSqlDatabase::addDatabase("QSQLITE", "scs_plugins");

    m_db.setDatabaseName(dbPath);
    if (!m_db.open()) {
        qDebug() << "DB open error:" << m_db.lastError().text();
        return false;
    }
    return ensureTables();
}

bool PluginManager::isDatabaseOpen() const { return m_db.isOpen(); }

bool PluginManager::ensureTables()
{
    QSqlQuery pragma(m_db);
    pragma.exec("PRAGMA foreign_keys = ON");

    QSqlQuery q(m_db);

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS games (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            game_root TEXT NOT NULL UNIQUE,
            executable_path TEXT DEFAULT '',
            icon_path TEXT DEFAULT '',
            icon_blob BLOB,
            steam_app_id INTEGER DEFAULT -1
        )
    )")) {
        qDebug() << "create games:" << q.lastError().text();
        return false;
    }

    const QStringList cols = {
        "ALTER TABLE games ADD COLUMN executable_path TEXT DEFAULT ''",
        "ALTER TABLE games ADD COLUMN icon_path TEXT DEFAULT ''",
        "ALTER TABLE games ADD COLUMN icon_blob BLOB",
        "ALTER TABLE games ADD COLUMN steam_app_id INTEGER DEFAULT -1"
    };
    for (const QString &sql : cols) {
        QSqlQuery a(m_db);
        a.exec(sql);
    }

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS plugins (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            game_id INTEGER NOT NULL,
            file_base_name TEXT NOT NULL,
            description TEXT DEFAULT '',
            url TEXT DEFAULT '',
            UNIQUE(game_id, file_base_name),
            FOREIGN KEY(game_id) REFERENCES games(id) ON DELETE CASCADE
        )
    )")) {
        qDebug() << "create plugins:" << q.lastError().text();
        return false;
    }

    if (!q.exec(R"(
        CREATE TABLE IF NOT EXISTS plugin_files (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            plugin_id INTEGER NOT NULL,
            relative_path TEXT NOT NULL,
            UNIQUE(plugin_id, relative_path),
            FOREIGN KEY(plugin_id) REFERENCES plugins(id) ON DELETE CASCADE
        )
    )")) {
        qDebug() << "create plugin_files:" << q.lastError().text();
        return false;
    }

    return true;
}

QList<GameInfo> PluginManager::loadGames()
{
    QList<GameInfo> result;
    QSqlQuery q(m_db);
    if (!q.exec("SELECT id, name, game_root, executable_path, icon_path, icon_blob, steam_app_id "
                "FROM games ORDER BY name")) {
        qDebug() << "loadGames:" << q.lastError().text();
        return result;
    }
    while (q.next()) {
        GameInfo g;
        g.id = q.value(0).toInt();
        g.name = q.value(1).toString();
        g.gameRoot = q.value(2).toString();
        g.executablePath = q.value(3).toString();
        g.iconPath = q.value(4).toString();
        g.iconBlob = q.value(5).toByteArray();
        g.steamAppId = q.value(6).toInt();
        result.append(g);
    }
    return result;
}

bool PluginManager::saveGame(const GameInfo &game)
{
    QSqlQuery q(m_db);
    if (game.id < 0) {
        q.prepare("INSERT INTO games (name, game_root, executable_path, icon_path, icon_blob, steam_app_id) "
                  "VALUES (?, ?, ?, ?, ?, ?)");
    } else {
        q.prepare("UPDATE games SET name=?, game_root=?, executable_path=?, icon_path=?, icon_blob=?, steam_app_id=? "
                  "WHERE id=?");
    }
    q.addBindValue(game.name);
    q.addBindValue(game.gameRoot);
    q.addBindValue(game.executablePath);
    q.addBindValue(game.iconPath);
    q.addBindValue(game.iconBlob.isNull() ? QVariant(QMetaType(QMetaType::QByteArray)) : QVariant(game.iconBlob));
    q.addBindValue(game.steamAppId);
    if (game.id >= 0)
        q.addBindValue(game.id);

    if (!q.exec()) {
        qDebug() << "saveGame:" << q.lastError().text();
        return false;
    }
    return true;
}

bool PluginManager::removeGame(int gameId)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM games WHERE id = ?");
    q.addBindValue(gameId);
    return q.exec();
}

bool PluginManager::gameExistsByRoot(const QString &root)
{
    QSqlQuery q(m_db);
    q.prepare("SELECT 1 FROM games WHERE game_root = ? LIMIT 1");
    q.addBindValue(QDir::cleanPath(root));
    if (q.exec() && q.next())
        return true;
    return false;
}

QString PluginManager::pluginsDirForGame(const GameInfo &game) const
{
    return QDir(game.gameRoot).filePath("bin/win_x64/plugins");
}

int PluginManager::findPluginDbId(int gameId, const QString &fileBaseName)
{
    QSqlQuery q(m_db);
    q.prepare("SELECT id FROM plugins WHERE game_id=? AND file_base_name=?");
    q.addBindValue(gameId);
    q.addBindValue(fileBaseName);
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return -1;
}

bool PluginManager::insertPluginRecord(int gameId, const QString &fileBaseName)
{
    QSqlQuery q(m_db);
    q.prepare("INSERT OR IGNORE INTO plugins (game_id, file_base_name) VALUES (?, ?)");
    q.addBindValue(gameId);
    q.addBindValue(fileBaseName);
    return q.exec();
}

QSet<QString> PluginManager::helperBaseNames() const
{
    QSet<QString> result;
    QSqlQuery q(m_db);
    if (!q.exec("SELECT relative_path FROM plugin_files"))
        return result;

    while (q.next()) {
        const QString rel = q.value(0).toString();
        const QString fname = QFileInfo(rel).fileName();
        if (fname.endsWith(".dll.disable", Qt::CaseInsensitive))
            result.insert(fname.left(fname.length() - 12));
        else if (fname.endsWith(".dll", Qt::CaseInsensitive))
            result.insert(fname.left(fname.length() - 4));
    }
    return result;
}

QList<PluginInfo> PluginManager::scanPlugins(const GameInfo &game)
{
    QList<PluginInfo> result;
    const QString pluginsDir = pluginsDirForGame(game);
    QDir dir(pluginsDir);

    if (!dir.exists() && !QDir().mkpath(pluginsDir)) {
        qDebug() << "Cannot create plugins dir:" << pluginsDir;
        return result;
    }

    dir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
    dir.setSorting(QDir::Name);

    const QFileInfoList entries = dir.entryInfoList();
    for (const QFileInfo &fi : entries) {
        const QString fileName = fi.fileName();
        bool enabled = false;
        QString baseName;

        if (fileName.endsWith(".dll.disable", Qt::CaseInsensitive)) {
            enabled = false;
            baseName = fileName.left(fileName.length() - 12);
        } else if (fileName.endsWith(".dll", Qt::CaseInsensitive)) {
            enabled = true;
            baseName = fileName.left(fileName.length() - 4);
        } else {
            continue;
        }

        if (baseName.isEmpty())
            continue;

        int dbId = findPluginDbId(game.id, baseName);
        if (dbId < 0) {
            insertPluginRecord(game.id, baseName);
            dbId = findPluginDbId(game.id, baseName);
        }

        PluginInfo p;
        p.id = dbId;
        p.gameId = game.id;
        p.fileBaseName = baseName;
        p.enabled = enabled;
        p.fullPath = fi.absoluteFilePath();

        QSqlQuery q(m_db);
        q.prepare("SELECT description, url FROM plugins WHERE id=?");
        q.addBindValue(dbId);
        if (q.exec() && q.next()) {
            p.description = q.value(0).toString();
            p.url = q.value(1).toString();
        }
        result.append(p);
    }

    // Скрываем helper-DLL, зарегистрированные как вспомогательные файлы другого плагина
    const QSet<QString> helpers = helperBaseNames();
    if (!helpers.isEmpty()) {
        QList<PluginInfo> filtered;
        filtered.reserve(result.size());
        for (const PluginInfo &p : result) {
            if (!helpers.contains(p.fileBaseName))
                filtered.append(p);
        }
        return filtered;
    }
    return result;
}

bool PluginManager::setPluginEnabled(const PluginInfo &plugin, bool enabled)
{
    const QString currentPath = plugin.fullPath;
    QString targetPath;

    if (enabled) {
        if (!currentPath.endsWith(".dll.disable", Qt::CaseInsensitive))
            return false;
        targetPath = currentPath.left(currentPath.length() - 8);
    } else {
        if (!currentPath.endsWith(".dll", Qt::CaseInsensitive) ||
            currentPath.endsWith(".dll.disable", Qt::CaseInsensitive))
            return false;
        targetPath = currentPath + ".disable";
    }

    if (QFile::exists(targetPath))
        return false;

    return QFile::rename(currentPath, targetPath);
}

QStringList PluginManager::pluginFiles(int pluginId) const
{
    QStringList result;
    QSqlQuery q(m_db);
    q.prepare("SELECT relative_path FROM plugin_files WHERE plugin_id = ? ORDER BY relative_path");
    q.addBindValue(pluginId);
    if (q.exec()) {
        while (q.next())
            result.append(q.value(0).toString());
    }
    return result;
}

bool PluginManager::setPluginFiles(int pluginId, const QStringList &relativePaths)
{
    QSqlQuery del(m_db);
    del.prepare("DELETE FROM plugin_files WHERE plugin_id = ?");
    del.addBindValue(pluginId);
    if (!del.exec())
        return false;

    for (const QString &rel : relativePaths) {
        QSqlQuery ins(m_db);
        ins.prepare("INSERT OR IGNORE INTO plugin_files (plugin_id, relative_path) VALUES (?, ?)");
        ins.addBindValue(pluginId);
        ins.addBindValue(rel);
        if (!ins.exec())
            return false;
    }
    return true;
}

QStringList PluginManager::pluginFilesForDeletion(const PluginInfo &plugin) const
{
    QStringList result;
    if (plugin.id < 0)
        return result;

    // Главный файл в его текущем состоянии (.dll или .dll.disable)
    result.append(QFileInfo(plugin.fullPath).fileName());

    const QStringList aux = pluginFiles(plugin.id);
    if (aux.isEmpty())
        return result;

    // Файлы, которые числятся ещё за каким-то другим плагином — не трогаем
    QSet<QString> shared;
    QSqlQuery q(m_db);
    q.prepare("SELECT DISTINCT relative_path FROM plugin_files WHERE plugin_id != ?");
    q.addBindValue(plugin.id);
    if (q.exec()) {
        while (q.next())
            shared.insert(q.value(0).toString());
    }

    for (const QString &a : aux) {
        if (!shared.contains(a))
            result.append(a);
    }
    return result;
}

bool PluginManager::deletePlugin(const PluginInfo &plugin, bool removeAuxiliaryFiles)
{
    if (plugin.id < 0)
        return false;

    const QString pluginsDir = QFileInfo(plugin.fullPath).absolutePath();

    QStringList toRemove;
    toRemove.append(QFileInfo(plugin.fullPath).fileName());

    if (removeAuxiliaryFiles) {
        const QStringList all = pluginFilesForDeletion(plugin);
        for (int i = 1; i < all.size(); ++i)
            toRemove.append(all.at(i));
    }

    for (const QString &rel : toRemove) {
        const QString path = QDir(pluginsDir).filePath(rel);
        if (QFile::exists(path) && !QFile::remove(path)) {
            qWarning() << "Cannot remove file:" << path;
            return false;
        }
    }

    QSqlQuery q(m_db);
    q.prepare("DELETE FROM plugins WHERE id = ?");
    q.addBindValue(plugin.id);
    return q.exec();
}

bool PluginManager::updatePluginMeta(const PluginInfo &plugin)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE plugins SET description=?, url=? WHERE id=?");
    q.addBindValue(plugin.description);
    q.addBindValue(plugin.url);
    q.addBindValue(plugin.id);
    return q.exec();
}

InstallResult
PluginManager::installFromMapping(const GameInfo &game,
                                  const QList<QPair<QString, QString>> &sourceToRelative,
                                  const QString &hintBaseName)
{
    InstallResult res;
    if (game.id < 0) {
        res.error = "Игра не выбрана.";
        return res;
    }
    if (sourceToRelative.isEmpty()) {
        res.error = "Нечего устанавливать.";
        return res;
    }

    const QString pluginsDir = pluginsDirForGame(game);
    if (!QDir().mkpath(pluginsDir)) {
        res.error = "Не удалось создать папку плагинов:\n" + pluginsDir;
        return res;
    }

    // 1. Копирование файлов
    QStringList installedRel;
    installedRel.reserve(sourceToRelative.size());
    for (const auto &pair : sourceToRelative) {
        const QString dst = QDir(pluginsDir).filePath(pair.second);
        QDir().mkpath(QFileInfo(dst).absolutePath());

        if (QFile::exists(dst) && !QFile::remove(dst)) {
            res.error = "Не удалось перезаписать файл:\n" + pair.second;
            return res;
        }
        if (!QFile::copy(pair.first, dst)) {
            res.error = "Не удалось скопировать файл:\n" + pair.second;
            return res;
        }
        installedRel.append(pair.second);
    }

    // 2. Собираем имена DLL
    QStringList dllBaseNames;
    for (const QString &rel : installedRel) {
        const QString fname = QFileInfo(rel).fileName();
        if (fname.endsWith(".dll", Qt::CaseInsensitive))
            dllBaseNames.append(fname.left(fname.length() - 4));
    }

    if (dllBaseNames.isEmpty()) {
        res.error = "В установленных файлах нет ни одного .dll —\n"
                    "это не похоже на плагин.";
        return res;
    }

    // 3. Главный DLL — по совпадению с hint, иначе первый по алфавиту
    QString primary;
    for (const QString &b : dllBaseNames) {
        if (b.compare(hintBaseName, Qt::CaseInsensitive) == 0) {
            primary = b;
            break;
        }
    }
    if (primary.isEmpty()) {
        QStringList sorted = dllBaseNames;
        std::sort(sorted.begin(), sorted.end(), [](const QString &a, const QString &b){
            return a.compare(b, Qt::CaseInsensitive) < 0;
        });
        primary = sorted.first();
    }

    // 4. Регистрируем только главный DLL как плагин
    int id = findPluginDbId(game.id, primary);
    if (id < 0) {
        insertPluginRecord(game.id, primary);
        id = findPluginDbId(game.id, primary);
    }
    if (id < 0) {
        res.error = "Не удалось зарегистрировать плагин в БД.";
        return res;
    }
    res.pluginId = id;

    // 5. Вспомогательные файлы — всё, кроме главного DLL
    const QString primaryDll = primary + ".dll";
    QStringList aux;
    for (const QString &rel : installedRel) {
        if (QFileInfo(rel).fileName().compare(primaryDll, Qt::CaseInsensitive) == 0)
            continue;
        aux.append(rel);
    }
    setPluginFiles(res.pluginId, aux);

    res.ok = true;
    res.pluginBaseName = primary;
    res.installedRelativeFiles = installedRel;
    return res;
}
