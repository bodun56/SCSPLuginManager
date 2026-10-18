#pragma once

#include <QString>
#include <QList>

struct SteamGameLocation {
    bool found = false;
    QString installDir;   // корень игры
    QString executable;   // путь к exe
    QString libraryPath;  // библиотека Steam
    int appId = -1;
};

class SteamGameDetector
{
public:
    // Ищет Steam в реестре Windows и возвращает корень Steam (где steam.exe)
    static QString findSteamRoot();

    // Читает libraryfolders.vdf и возвращает список путей библиотек
    static QStringList steamLibraries();

    // Ищет установленную игру по App ID.
    // exeName — имя исполняемого файла внутри bin/win_x64 (например eurotrucks2.exe)
    static SteamGameLocation findGame(int appId, const QString &exeName);

    // Удобные обёртки для ETS2 / ATS
    static SteamGameLocation findEts2();
    static SteamGameLocation findAts();

private:
    static QString parseAcfInstalldir(const QString &acfPath);
    static QStringList parseVdfLibraryPaths(const QString &vdfPath);
};
