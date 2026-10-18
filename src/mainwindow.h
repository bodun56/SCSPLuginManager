#pragma once

#include <QMainWindow>
#include <QList>
#include "pluginmanager.h"

class QListWidget;
class QListWidgetItem;
class QTableWidget;
class QTableWidgetItem;
class QPushButton;
class QLineEdit;
class QLabel;
class QSplitter;
class QMenu;
class QAction;
class QActionGroup;
class QTranslator;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void changeEvent(QEvent *e) override;

private slots:
    void autoDetectSteamGames();
    void onAddGameManually();
    void onRemoveGame();
    void onGameSelected(int row);
    void onRefreshPlugins();
    void onPluginItemChanged(QTableWidgetItem *item);
    void onDeletePlugin();
    void onMetaEdited();
    void onLaunchGame();
    void onChangeIcon();

    void onOpenPluginsFolder();
    void onInstallPluginFromFiles();
    void onInstallPluginFromZip();
    void onInstallPluginFromFolder();

    void onLanguageSelected(QAction *action);

private:
    void setupUi();
    void setupLanguageMenu();
    void retranslateUi();
    void applyLanguage(const QString &lang);
    void reloadGameList();
    void populatePluginTable(const QList<PluginInfo> &plugins);
    GameInfo currentGame() const;
    PluginInfo pluginAtRow(int row) const;
    QIcon gameIcon(const GameInfo &g) const;
    void addDetectedGame(const QString &displayName, const QString &root,
                         const QString &exe, int appId);

    PluginManager m_manager;
    QList<GameInfo> m_games;
    QList<PluginInfo> m_currentPlugins;
    int m_currentGameRow = -1;
    bool m_updating = false;

    QTranslator *m_translator = nullptr;

    QLabel *m_gamesLabel = nullptr;
    QListWidget *m_gameList = nullptr;
    QPushButton *m_addGameBtn = nullptr;
    QPushButton *m_removeGameBtn = nullptr;
    QPushButton *m_refreshBtn = nullptr;
    QPushButton *m_openPluginsFolderBtn = nullptr;
    QPushButton *m_installPluginBtn = nullptr;
    QPushButton *m_launchBtn = nullptr;
    QPushButton *m_changeIconBtn = nullptr;
    QPushButton *m_deletePluginBtn = nullptr;
    QLabel *m_gameTitle = nullptr;
    QLabel *m_gamePath = nullptr;
    QTableWidget *m_table = nullptr;
    QLineEdit *m_descEdit = nullptr;
    QLineEdit *m_urlEdit = nullptr;
    QLabel *m_descLabel = nullptr;
    QLabel *m_urlLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QSplitter *m_splitter = nullptr;

    QMenu *m_installMenu = nullptr;
    QAction *m_actionInstallFiles = nullptr;
    QAction *m_actionInstallZip = nullptr;
    QAction *m_actionInstallFolder = nullptr;

    QMenu *m_languageMenu = nullptr;
    QActionGroup *m_languageGroup = nullptr;
    QAction *m_actionRu = nullptr;
    QAction *m_actionEn = nullptr;
};
