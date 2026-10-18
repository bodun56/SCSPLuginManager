#include "steamgamedetector.h"

#include <QSettings>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTextStream>
#include <QDebug>

namespace {
constexpr int kEts2AppId = 227300;
constexpr int kAtsAppId  = 270880;
const QString kEts2Exe = "eurotrucks2.exe";
const QString kAtsExe  = "amtrucks.exe";
}

QString SteamGameDetector::findSteamRoot()
{
    // 1) HKCU\Software\Valve\Steam\SteamPath
    {
        QSettings reg("HKEY_CURRENT_USER\\Software\\Valve\\Steam", QSettings::NativeFormat);
        const QString p = reg.value("SteamPath").toString();
        if (!p.isEmpty() && QDir(p).exists())
            return QDir::cleanPath(p);
    }
    // 2) HKLM\SOFTWARE\WOW6432Node\Valve\Steam\InstallPath
    {
        QSettings reg("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Valve\\Steam", QSettings::NativeFormat);
        const QString p = reg.value("InstallPath").toString();
        if (!p.isEmpty() && QDir(p).exists())
            return QDir::cleanPath(p);
    }
    // 3) HKLM\SOFTWARE\Valve\Steam\InstallPath (32-bit)
    {
        QSettings reg("HKEY_LOCAL_MACHINE\\SOFTWARE\\Valve\\Steam", QSettings::NativeFormat);
        const QString p = reg.value("InstallPath").toString();
        if (!p.isEmpty() && QDir(p).exists())
            return QDir::cleanPath(p);
    }
    // 4) Эвристика: стандартные пути
    const QStringList candidates = {
        "C:/Program Files (x86)/Steam",
        "C:/Program Files/Steam",
        "D:/Steam",
        "D:/Program Files (x86)/Steam"
    };
    for (const QString &c : candidates) {
        if (QFileInfo::exists(c + "/steam.exe"))
            return QDir::cleanPath(c);
    }
    return {};
}

QStringList SteamGameDetector::parseVdfLibraryPaths(const QString &vdfPath)
{
    QStringList result;
    QFile f(vdfPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return result;

    const QString content = QString::fromUtf8(f.readAll());

    // Формат libraryfolders.vdf:
    // "libraryfolders" { "0" { "path" "C:\\Program Files (x86)\\Steam" ... } ... }
    // Ищем все пары "path" "..." внутри файла. Это устойчиво работает
    // для старого и нового формата VDF.
    QRegularExpression re(R"REGEX("path"\s+"([^"]+)")REGEX");
    auto it = re.globalMatch(content);
    while (it.hasNext()) {
        const auto m = it.next();
        QString p = m.captured(1);
        p.replace("\\\\", "/");
        if (QDir(p).exists() && !result.contains(p))
            result.append(QDir::cleanPath(p));
    }
    return result;
}

QStringList SteamGameDetector::steamLibraries()
{
    QStringList libs;
    const QString steamRoot = findSteamRoot();

    // Первая библиотека — сам Steam
    if (!steamRoot.isEmpty())
        libs.append(steamRoot);

    if (steamRoot.isEmpty())
        return libs;

    const QString vdf = steamRoot + "/steamapps/libraryfolders.vdf";
    for (const QString &p : parseVdfLibraryPaths(vdf)) {
        if (!libs.contains(p))
            libs.append(p);
    }
    return libs;
}

QString SteamGameDetector::parseAcfInstalldir(const QString &acfPath)
{
    QFile f(acfPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QString content = QString::fromUtf8(f.readAll());
    QRegularExpression re(R"REGEX("installdir"\s+"([^"]+)")REGEX");
    const auto m = re.match(content);
    if (m.hasMatch())
        return m.captured(1);
    return {};
}

SteamGameLocation SteamGameDetector::findGame(int appId, const QString &exeName)
{
    SteamGameLocation loc;
    loc.appId = appId;

    for (const QString &lib : steamLibraries()) {
        const QString acf = lib + QString("/steamapps/appmanifest_%1.acf").arg(appId);
        if (!QFileInfo::exists(acf))
            continue;

        const QString installdir = parseAcfInstalldir(acf);
        if (installdir.isEmpty())
            continue;

        const QString gameRoot = QDir(lib).filePath("steamapps/common/" + installdir);
        if (!QDir(gameRoot).exists())
            continue;

        const QString exe = QDir(gameRoot).filePath("bin/win_x64/" + exeName);
        loc.found = true;
        loc.installDir = QDir::cleanPath(gameRoot);
        loc.executable = QDir::cleanPath(exe);
        loc.libraryPath = QDir::cleanPath(lib);
        return loc;
    }
    return loc;
}

SteamGameLocation SteamGameDetector::findEts2()
{
    return findGame(kEts2AppId, kEts2Exe);
}

SteamGameLocation SteamGameDetector::findAts()
{
    return findGame(kAtsAppId, kAtsExe);
}
