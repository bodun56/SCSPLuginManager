#include "mainwindow.h"
#include "steamgamedetector.h"

#include <QApplication>
#include <QAction>
#include <QActionGroup>
#include <QBuffer>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTranslator>
#include <QUrl>
#include <QVBoxLayout>
#include <QPixmap>

#include <QtCore/private/qzipreader_p.h>

namespace {

void collectFolderMapping(const QString &srcDir, const QString &relPrefix,
                          QList<QPair<QString, QString>> &out)
{
    QDir src(srcDir);
    if (!src.exists())
        return;

    const QFileInfoList entries =
        src.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QFileInfo &fi : entries) {
        const QString rel = relPrefix.isEmpty()
        ? fi.fileName()
        : relPrefix + "/" + fi.fileName();
        if (fi.isDir())
            collectFolderMapping(fi.absoluteFilePath(), rel, out);
        else
            out.append({fi.absoluteFilePath(), rel});
    }
}

bool loadEnglish(QTranslator *tr)
{
    const QStringList candidates = {
        ":/i18n/scs_en.qm",
        ":/i18n/translations/scs_en.qm",
        QCoreApplication::applicationDirPath() + "/translations/scs_en.qm"
    };
    for (const QString &p : candidates) {
        if (tr->load(p))
            return true;
    }
    return false;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    // 1. Загружаем сохранённый язык ДО построения UI
    QSettings settings;
    QString lang = settings.value("language").toString();
    if (lang.isEmpty()) {
        lang = QLocale::system().name().startsWith("en") ? "en" : "ru";
        settings.setValue("language", lang);
    }
    applyLanguage(lang);

    // 2. Строим UI (tr() уже возвращает нужный язык)
    setupUi();
    setupLanguageMenu();

    // 3. Инициализация БД
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    const QString dbPath = dataDir + "/scs_plugin_manager.db";

    if (!m_manager.initDatabase(dbPath)) {
        QMessageBox::critical(this, tr("Ошибка БД"),
                              tr("Не удалось открыть базу данных:\n%1").arg(dbPath));
    }

    autoDetectSteamGames();
    reloadGameList();
}

MainWindow::~MainWindow() = default;

void MainWindow::changeEvent(QEvent *e)
{
    if (e->type() == QEvent::LanguageChange)
        retranslateUi();
    QMainWindow::changeEvent(e);
}

void MainWindow::applyLanguage(const QString &lang)
{
    if (m_translator) {
        qApp->removeTranslator(m_translator);
        delete m_translator;
        m_translator = nullptr;
    }

    if (lang == "en") {
        m_translator = new QTranslator(this);
        if (loadEnglish(m_translator)) {
            qApp->installTranslator(m_translator);
        } else {
            delete m_translator;
            m_translator = nullptr;
        }
    }
    // Для "ru" переводчик не нужен — исходный язык русский
}

void MainWindow::onLanguageSelected(QAction *action)
{
    if (!action)
        return;
    const QString lang = action->data().toString();
    if (lang.isEmpty())
        return;

    QSettings settings;
    settings.setValue("language", lang);

    // installTranslator / removeTranslator сами сгенерируют QEvent::LanguageChange
    applyLanguage(lang);
}

void MainWindow::setupUi()
{
    setWindowTitle("SCS Plugin Manager");
    resize(1100, 700);

    auto *central = new QWidget(this);
    setCentralWidget(central);

    m_splitter = new QSplitter(Qt::Horizontal, central);

    // ---------- Левая панель ----------
    auto *leftPanel = new QWidget();
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(4, 4, 4, 4);

    m_gamesLabel = new QLabel();
    m_gamesLabel->setText("<b>" + tr("Игры") + "</b>");
    leftLayout->addWidget(m_gamesLabel);

    m_gameList = new QListWidget();
    m_gameList->setIconSize(QSize(48, 48));
    m_gameList->setSpacing(4);
    m_gameList->setSelectionMode(QAbstractItemView::SingleSelection);
    leftLayout->addWidget(m_gameList, 1);

    auto *gameBtnRow = new QHBoxLayout();
    m_addGameBtn = new QPushButton(tr("Добавить..."));
    m_removeGameBtn = new QPushButton(tr("Удалить..."));
    gameBtnRow->addWidget(m_addGameBtn);
    gameBtnRow->addWidget(m_removeGameBtn);
    leftLayout->addLayout(gameBtnRow);

    leftPanel->setMinimumWidth(260);
    leftPanel->setMaximumWidth(420);

    // ---------- Правая панель ----------
    auto *rightPanel = new QWidget();
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(4, 4, 4, 4);

    m_gameTitle = new QLabel("<b>" + tr("Игра не выбрана") + "</b>");
    m_gameTitle->setStyleSheet("font-size: 16px;");
    rightLayout->addWidget(m_gameTitle);

    m_gamePath = new QLabel();
    m_gamePath->setStyleSheet("color: #666;");
    m_gamePath->setTextInteractionFlags(Qt::TextSelectableByMouse);
    rightLayout->addWidget(m_gamePath);

    auto *btnRow = new QHBoxLayout();
    m_refreshBtn = new QPushButton(tr("Обновить плагины"));
    m_openPluginsFolderBtn = new QPushButton(tr("Открыть папку плагинов"));
    m_installPluginBtn = new QPushButton(tr("Установить плагин..."));
    m_launchBtn = new QPushButton(tr("Запустить игру"));
    m_changeIconBtn = new QPushButton(tr("Сменить иконку..."));

    m_installMenu = new QMenu(m_installPluginBtn);
    m_actionInstallFiles = m_installMenu->addAction(
        tr("Из файлов..."), this, &MainWindow::onInstallPluginFromFiles);
    m_actionInstallZip = m_installMenu->addAction(
        tr("Из ZIP-архива..."), this, &MainWindow::onInstallPluginFromZip);
    m_actionInstallFolder = m_installMenu->addAction(
        tr("Из папки..."), this, &MainWindow::onInstallPluginFromFolder);
    m_installPluginBtn->setMenu(m_installMenu);

    btnRow->addWidget(m_refreshBtn);
    btnRow->addWidget(m_openPluginsFolderBtn);
    btnRow->addWidget(m_installPluginBtn);
    btnRow->addWidget(m_launchBtn);
    btnRow->addWidget(m_changeIconBtn);
    btnRow->addStretch();
    rightLayout->addLayout(btnRow);

    m_table = new QTableWidget();
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(
        {tr("Включён"), tr("Имя плагина"), tr("Описание"), tr("URL")});
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    rightLayout->addWidget(m_table, 1);

    auto *bottomLayout = new QHBoxLayout();
    auto *metaForm = new QFormLayout();
    m_descEdit = new QLineEdit();
    m_descEdit->setPlaceholderText(tr("Краткое описание плагина"));
    m_urlEdit = new QLineEdit();
    m_urlEdit->setPlaceholderText(QStringLiteral("https://..."));
    m_descLabel = new QLabel(tr("Описание:"));
    m_urlLabel = new QLabel(tr("URL:"));
    metaForm->addRow(m_descLabel, m_descEdit);
    metaForm->addRow(m_urlLabel, m_urlEdit);
    bottomLayout->addLayout(metaForm, 1);

    m_deletePluginBtn = new QPushButton(tr("Удалить плагин"));
    m_deletePluginBtn->setStyleSheet("QPushButton { color: #b00020; font-weight: bold; }");
    bottomLayout->addWidget(m_deletePluginBtn, 0, Qt::AlignBottom);
    rightLayout->addLayout(bottomLayout);

    m_statusLabel = new QLabel(tr("Готово"));
    rightLayout->addWidget(m_statusLabel);

    m_splitter->addWidget(leftPanel);
    m_splitter->addWidget(rightPanel);
    m_splitter->setStretchFactor(0, 0);
    m_splitter->setStretchFactor(1, 1);
    m_splitter->setSizes({300, 800});

    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->addWidget(m_splitter);

    // ---------- Связи ----------
    connect(m_addGameBtn, &QPushButton::clicked, this, &MainWindow::onAddGameManually);
    connect(m_removeGameBtn, &QPushButton::clicked, this, &MainWindow::onRemoveGame);
    connect(m_gameList, &QListWidget::currentRowChanged, this, &MainWindow::onGameSelected);
    connect(m_refreshBtn, &QPushButton::clicked, this, &MainWindow::onRefreshPlugins);
    connect(m_openPluginsFolderBtn, &QPushButton::clicked, this, &MainWindow::onOpenPluginsFolder);
    connect(m_table, &QTableWidget::itemChanged, this, &MainWindow::onPluginItemChanged);
    connect(m_deletePluginBtn, &QPushButton::clicked, this, &MainWindow::onDeletePlugin);
    connect(m_descEdit, &QLineEdit::editingFinished, this, &MainWindow::onMetaEdited);
    connect(m_urlEdit, &QLineEdit::editingFinished, this, &MainWindow::onMetaEdited);
    connect(m_launchBtn, &QPushButton::clicked, this, &MainWindow::onLaunchGame);
    connect(m_changeIconBtn, &QPushButton::clicked, this, &MainWindow::onChangeIcon);

    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]{
        const int row = m_table->currentRow();
        if (row < 0) return;
        const PluginInfo p = pluginAtRow(row);
        m_updating = true;
        m_descEdit->setText(p.description);
        m_urlEdit->setText(p.url);
        m_updating = false;
    });
}

void MainWindow::setupLanguageMenu()
{
    m_languageMenu = menuBar()->addMenu(tr("Язык"));

    m_languageGroup = new QActionGroup(this);
    m_languageGroup->setExclusive(true);

    // Названия языков не переводятся — это родные названия
    m_actionRu = m_languageMenu->addAction(QString::fromUtf8("Русский"));
    m_actionRu->setCheckable(true);
    m_actionRu->setData("ru");
    m_languageGroup->addAction(m_actionRu);

    m_actionEn = m_languageMenu->addAction(QString::fromUtf8("English"));
    m_actionEn->setCheckable(true);
    m_actionEn->setData("en");
    m_languageGroup->addAction(m_actionEn);

    connect(m_languageGroup, &QActionGroup::triggered,
            this, &MainWindow::onLanguageSelected);

    QSettings settings;
    const QString current = settings.value("language", "ru").toString();
    if (current == "en")
        m_actionEn->setChecked(true);
    else
        m_actionRu->setChecked(true);
}

void MainWindow::retranslateUi()
{
    // Меню
    m_languageMenu->setTitle(tr("Язык"));

    // Левая панель
    m_gamesLabel->setText("<b>" + tr("Игры") + "</b>");
    m_addGameBtn->setText(tr("Добавить..."));
    m_removeGameBtn->setText(tr("Удалить..."));

    // Кнопки правой панели
    m_refreshBtn->setText(tr("Обновить плагины"));
    m_openPluginsFolderBtn->setText(tr("Открыть папку плагинов"));
    m_installPluginBtn->setText(tr("Установить плагин..."));
    m_launchBtn->setText(tr("Запустить игру"));
    m_changeIconBtn->setText(tr("Сменить иконку..."));
    m_deletePluginBtn->setText(tr("Удалить плагин"));

    // Меню установки
    m_actionInstallFiles->setText(tr("Из файлов..."));
    m_actionInstallZip->setText(tr("Из ZIP-архива..."));
    m_actionInstallFolder->setText(tr("Из папки..."));

    // Таблица
    m_table->setHorizontalHeaderLabels(
        {tr("Включён"), tr("Имя плагина"), tr("Описание"), tr("URL")});

    // Форма метаданных
    m_descLabel->setText(tr("Описание:"));
    m_urlLabel->setText(tr("URL:"));
    m_descEdit->setPlaceholderText(tr("Краткое описание плагина"));

    // Заголовок игры
    if (m_currentGameRow >= 0 && m_currentGameRow < m_games.size())
        m_gameTitle->setText("<b>" + m_games.at(m_currentGameRow).name + "</b>");
    else
        m_gameTitle->setText("<b>" + tr("Игра не выбрана") + "</b>");
}

QIcon MainWindow::gameIcon(const GameInfo &g) const
{
    if (!g.iconPath.isEmpty() && QFileInfo::exists(g.iconPath))
        return QIcon(g.iconPath);

    if (!g.iconBlob.isEmpty()) {
        QPixmap pm;
        if (pm.loadFromData(g.iconBlob, "PNG"))
            return QIcon(pm);
    }

    if (!g.executablePath.isEmpty() && QFileInfo::exists(g.executablePath)) {
        QFileIconProvider provider;
        return provider.icon(QFileInfo(g.executablePath));
    }
    return QIcon();
}

void MainWindow::addDetectedGame(const QString &displayName, const QString &root,
                                 const QString &exe, int appId)
{
    if (m_manager.gameExistsByRoot(root))
        return;

    GameInfo g;
    g.name = displayName;
    g.gameRoot = QDir::cleanPath(root);
    g.executablePath = QDir::cleanPath(exe);
    g.steamAppId = appId;

    if (QFileInfo::exists(exe)) {
        QFileIconProvider provider;
        const QPixmap pm = provider.icon(QFileInfo(exe)).pixmap(64, 64);
        if (!pm.isNull()) {
            QByteArray bytes;
            QBuffer buf(&bytes);
            buf.open(QIODevice::WriteOnly);
            pm.save(&buf, "PNG");
            g.iconBlob = bytes;
        }
    }

    m_manager.saveGame(g);
}

void MainWindow::autoDetectSteamGames()
{
    m_statusLabel->setText(tr("Поиск игр в Steam..."));

    const SteamGameLocation ets2 = SteamGameDetector::findEts2();
    if (ets2.found)
        addDetectedGame("Euro Truck Simulator 2", ets2.installDir, ets2.executable, ets2.appId);

    const SteamGameLocation ats = SteamGameDetector::findAts();
    if (ats.found)
        addDetectedGame("American Truck Simulator", ats.installDir, ats.executable, ats.appId);

    if (!ets2.found && !ats.found)
        m_statusLabel->setText(tr("Игры Steam не найдены — добавьте игру вручную."));
    else
        m_statusLabel->setText(tr("Готово"));
}

void MainWindow::onAddGameManually()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("Добавить игру"), tr("Название игры:"),
        QLineEdit::Normal, "", &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    const QString root = QFileDialog::getExistingDirectory(
        this, tr("Выберите корневую папку игры (где лежит bin\\win_x64)"));
    if (root.isEmpty())
        return;

    if (m_manager.gameExistsByRoot(root)) {
        QMessageBox::information(this, tr("Игра уже добавлена"),
                                 tr("Эта папка уже присутствует в списке."));
        return;
    }

    GameInfo g;
    g.name = name.trimmed();
    g.gameRoot = QDir::cleanPath(root);
    g.steamAppId = -1;

    const QString ets2Exe = QDir(root).filePath("bin/win_x64/eurotrucks2.exe");
    const QString atsExe  = QDir(root).filePath("bin/win_x64/amtrucks.exe");
    if (QFileInfo::exists(ets2Exe))
        g.executablePath = ets2Exe;
    else if (QFileInfo::exists(atsExe))
        g.executablePath = atsExe;

    if (!g.executablePath.isEmpty()) {
        QFileIconProvider provider;
        const QPixmap pm = provider.icon(QFileInfo(g.executablePath)).pixmap(64, 64);
        if (!pm.isNull()) {
            QByteArray bytes;
            QBuffer buf(&bytes);
            buf.open(QIODevice::WriteOnly);
            pm.save(&buf, "PNG");
            g.iconBlob = bytes;
        }
    }

    if (!m_manager.saveGame(g)) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить игру."));
        return;
    }

    reloadGameList();
    m_statusLabel->setText(tr("Игра добавлена: %1").arg(g.name));
}

void MainWindow::onRemoveGame()
{
    const int row = m_currentGameRow;
    if (row < 0 || row >= m_games.size()) {
        QMessageBox::information(this, tr("Удаление игры"),
                                 tr("Сначала выберите игру в левой панели."));
        return;
    }

    const GameInfo g = m_games.at(row);

    QString msg = tr("Удалить игру «%1» из списка?").arg(g.name);
    msg += "\n\n" + tr("Игра будет удалена только из базы данных программы.\n"
                       "Все файлы игры и плагинов на диске останутся нетронутыми.");
    msg += "\n\n" + tr("Продолжить?");

    const auto answer = QMessageBox::question(
        this, tr("Удаление игры"), msg,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (answer != QMessageBox::Yes)
        return;

    if (!m_manager.removeGame(g.id)) {
        QMessageBox::warning(this, tr("Ошибка"),
                             tr("Не удалось удалить игру из базы данных."));
        return;
    }

    m_currentGameRow = -1;
    reloadGameList();
    m_statusLabel->setText(tr("Игра удалена из списка: %1").arg(g.name));
}

void MainWindow::reloadGameList()
{
    m_updating = true;
    m_games = m_manager.loadGames();
    m_gameList->clear();

    for (const GameInfo &g : m_games) {
        auto *item = new QListWidgetItem(gameIcon(g), g.name + "\n" + g.gameRoot);
        item->setToolTip(g.gameRoot);
        m_gameList->addItem(item);
    }
    m_updating = false;

    if (!m_games.isEmpty()) {
        int row = m_currentGameRow;
        if (row < 0 || row >= m_games.size())
            row = 0;
        m_gameList->setCurrentRow(row);
        onGameSelected(row);
    } else {
        m_currentGameRow = -1;
        m_table->setRowCount(0);
        m_currentPlugins.clear();
        m_gameTitle->setText("<b>" + tr("Игра не выбрана") + "</b>");
        m_gamePath->clear();
        m_launchBtn->setEnabled(false);
        m_changeIconBtn->setEnabled(false);
        m_openPluginsFolderBtn->setEnabled(false);
        m_installPluginBtn->setEnabled(false);
        m_deletePluginBtn->setEnabled(false);
    }
}

GameInfo MainWindow::currentGame() const
{
    if (m_currentGameRow < 0 || m_currentGameRow >= m_games.size())
        return {};
    return m_games.at(m_currentGameRow);
}

void MainWindow::onGameSelected(int row)
{
    if (m_updating || row < 0 || row >= m_games.size())
        return;

    m_currentGameRow = row;
    const GameInfo g = m_games.at(row);

    m_gameTitle->setText("<b>" + g.name + "</b>");
    m_gamePath->setText(g.gameRoot);

    m_launchBtn->setEnabled(!g.executablePath.isEmpty() && QFileInfo::exists(g.executablePath));
    m_changeIconBtn->setEnabled(true);
    m_openPluginsFolderBtn->setEnabled(true);
    m_installPluginBtn->setEnabled(true);
    m_deletePluginBtn->setEnabled(true);

    onRefreshPlugins();
}

void MainWindow::onRefreshPlugins()
{
    const GameInfo game = currentGame();
    if (game.id < 0) {
        m_table->setRowCount(0);
        m_currentPlugins.clear();
        return;
    }

    m_currentPlugins = m_manager.scanPlugins(game);
    populatePluginTable(m_currentPlugins);

    m_statusLabel->setText(
        tr("Плагинов: %1  |  Папка: %2")
            .arg(m_currentPlugins.size())
            .arg(m_manager.pluginsDirForGame(game)));
}

void MainWindow::populatePluginTable(const QList<PluginInfo> &plugins)
{
    m_updating = true;
    m_table->setRowCount(plugins.size());

    for (int i = 0; i < plugins.size(); ++i) {
        const PluginInfo &p = plugins.at(i);

        auto *check = new QTableWidgetItem();
        check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        check->setCheckState(p.enabled ? Qt::Checked : Qt::Unchecked);
        check->setData(Qt::UserRole, i);
        m_table->setItem(i, 0, check);

        auto *name = new QTableWidgetItem(p.fileBaseName);
        name->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        name->setData(Qt::UserRole, i);
        m_table->setItem(i, 1, name);

        auto *desc = new QTableWidgetItem(p.description);
        desc->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        desc->setData(Qt::UserRole, i);
        m_table->setItem(i, 2, desc);

        auto *url = new QTableWidgetItem(p.url);
        url->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        url->setData(Qt::UserRole, i);
        m_table->setItem(i, 3, url);
    }

    m_table->resizeColumnsToContents();
    m_updating = false;
}

PluginInfo MainWindow::pluginAtRow(int row) const
{
    if (row < 0 || row >= m_table->rowCount())
        return {};
    auto *item = m_table->item(row, 0);
    if (!item)
        return {};
    const int idx = item->data(Qt::UserRole).toInt();
    if (idx < 0 || idx >= m_currentPlugins.size())
        return {};
    return m_currentPlugins.at(idx);
}

void MainWindow::onPluginItemChanged(QTableWidgetItem *item)
{
    if (m_updating || !item || item->column() != 0)
        return;

    const int row = item->row();
    const PluginInfo p = pluginAtRow(row);
    if (p.id < 0)
        return;

    const bool wantEnabled = (item->checkState() == Qt::Checked);

    m_updating = true;
    item->setCheckState(p.enabled ? Qt::Checked : Qt::Unchecked);
    m_updating = false;

    if (!m_manager.setPluginEnabled(p, wantEnabled)) {
        QMessageBox::warning(this, tr("Ошибка"),
                             tr("Не удалось %1 плагин:\n%2")
                                 .arg(wantEnabled ? tr("включить") : tr("отключить"), p.fileBaseName));
        return;
    }

    onRefreshPlugins();
    m_statusLabel->setText(
        tr("Плагин '%1' %2")
            .arg(p.fileBaseName, wantEnabled ? tr("включён") : tr("отключён")));
}

void MainWindow::onDeletePlugin()
{
    const int row = m_table->currentRow();
    if (row < 0) {
        QMessageBox::information(this, tr("Удаление"), tr("Выберите плагин в списке."));
        return;
    }

    const PluginInfo p = pluginAtRow(row);
    if (p.id < 0)
        return;

    const QStringList files = m_manager.pluginFilesForDeletion(p);

    QString msg = tr("Удалить плагин «%1»?").arg(p.fileBaseName);
    if (files.size() > 1) {
        QStringList fileLines;
        const int maxShow = 20;
        for (int i = 0; i < files.size() && i < maxShow; ++i)
            fileLines << "  • " + files.at(i);
        if (files.size() > maxShow)
            fileLines << tr("  ... и ещё %1 файлов").arg(files.size() - maxShow);

        msg += "\n\n" + tr("Будут удалены следующие файлы (%1):").arg(files.size())
               + "\n" + fileLines.join("\n")
               + "\n\n" + tr("Удаление безвозвратное. Продолжить?");
    } else {
        msg += "\n\n" + tr("Файл плагина будет удалён безвозвратно.");
    }

    const auto answer = QMessageBox::question(
        this, tr("Подтверждение"), msg,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
        return;

    if (!m_manager.deletePlugin(p, true)) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось удалить плагин."));
        return;
    }

    onRefreshPlugins();
    m_statusLabel->setText(tr("Плагин удалён: %1").arg(p.fileBaseName));
}

void MainWindow::onMetaEdited()
{
    if (m_updating)
        return;

    const int row = m_table->currentRow();
    if (row < 0)
        return;

    PluginInfo p = pluginAtRow(row);
    if (p.id < 0)
        return;

    p.description = m_descEdit->text().trimmed();
    p.url = m_urlEdit->text().trimmed();

    if (!m_manager.updatePluginMeta(p)) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить описание/URL."));
        return;
    }

    m_updating = true;
    if (auto *d = m_table->item(row, 2)) d->setText(p.description);
    if (auto *u = m_table->item(row, 3)) u->setText(p.url);
    m_updating = false;

    m_statusLabel->setText(tr("Метаданные сохранены"));
}

void MainWindow::onLaunchGame()
{
    const GameInfo g = currentGame();
    if (g.id < 0 || g.executablePath.isEmpty() || !QFileInfo::exists(g.executablePath)) {
        QMessageBox::warning(this, tr("Запуск игры"),
                             tr("Не удалось найти исполняемый файл игры."));
        return;
    }

    if (!QProcess::startDetached(g.executablePath, {}, QFileInfo(g.executablePath).absolutePath())) {
        QMessageBox::warning(this, tr("Запуск игры"),
                             tr("Не удалось запустить:\n%1").arg(g.executablePath));
        return;
    }
    m_statusLabel->setText(tr("Игра запущена: %1").arg(g.name));
}

void MainWindow::onChangeIcon()
{
    const int row = m_currentGameRow;
    if (row < 0 || row >= m_games.size())
        return;

    GameInfo g = m_games.at(row);
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Выберите иконку"), QString(),
        tr("Изображения (*.png *.ico *.jpg *.jpeg *.bmp)"));
    if (path.isEmpty())
        return;

    QPixmap pm(path);
    if (pm.isNull()) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось загрузить изображение."));
        return;
    }

    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    pm.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&buf, "PNG");

    g.iconPath = path;
    g.iconBlob = bytes;

    if (!m_manager.saveGame(g)) {
        QMessageBox::warning(this, tr("Ошибка"), tr("Не удалось сохранить иконку."));
        return;
    }

    reloadGameList();
    m_statusLabel->setText(tr("Иконка обновлена"));
}

void MainWindow::onOpenPluginsFolder()
{
    const GameInfo game = currentGame();
    if (game.id < 0) {
        QMessageBox::information(this, tr("Открыть папку"),
                                 tr("Сначала выберите игру в левой панели."));
        return;
    }

    const QString pluginsDir = m_manager.pluginsDirForGame(game);
    if (!QDir().mkpath(pluginsDir)) {
        QMessageBox::warning(this, tr("Ошибка"),
                             tr("Не удалось создать/открыть папку плагинов:\n%1").arg(pluginsDir));
        return;
    }

#ifdef Q_OS_WIN
    QProcess::startDetached("explorer.exe", {QDir::toNativeSeparators(pluginsDir)});
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(pluginsDir));
#endif
}

void MainWindow::onInstallPluginFromFiles()
{
    const GameInfo game = currentGame();
    if (game.id < 0) {
        QMessageBox::information(this, tr("Установка плагина"),
                                 tr("Сначала выберите игру в левой панели."));
        return;
    }

    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Выберите файлы плагина"), QString(),
        tr("Все файлы (*.*)"));
    if (files.isEmpty())
        return;

    QList<QPair<QString, QString>> mapping;
    for (const QString &src : files)
        mapping.append({src, QFileInfo(src).fileName()});

    const auto res = m_manager.installFromMapping(game, mapping, QString());
    if (!res.ok) {
        QMessageBox::warning(this, tr("Ошибка установки"), res.error);
        return;
    }

    onRefreshPlugins();
    m_statusLabel->setText(
        tr("Установлен плагин «%1» (%2 файлов)")
            .arg(res.pluginBaseName)
            .arg(res.installedRelativeFiles.size()));
}

void MainWindow::onInstallPluginFromZip()
{
    const GameInfo game = currentGame();
    if (game.id < 0) {
        QMessageBox::information(this, tr("Установка плагина"),
                                 tr("Сначала выберите игру в левой панели."));
        return;
    }

    const QString zipPath = QFileDialog::getOpenFileName(
        this, tr("Выберите ZIP-архив с плагином"), QString(),
        tr("ZIP-архивы (*.zip)"));
    if (zipPath.isEmpty())
        return;

    QTemporaryDir tmpDir;
    if (!tmpDir.isValid()) {
        QMessageBox::warning(this, tr("Ошибка"),
                             tr("Не удалось создать временную папку."));
        return;
    }

    QZipReader reader(zipPath);
    if (!reader.isReadable()) {
        QMessageBox::warning(this, tr("Ошибка"),
                             tr("Не удалось открыть ZIP-архив:\n%1").arg(zipPath));
        return;
    }
    if (!reader.extractAll(tmpDir.path())) {
        QMessageBox::warning(this, tr("Ошибка"),
                             tr("Не удалось распаковать ZIP-архив."));
        return;
    }

    QDir tmp(tmpDir.path());
    const QFileInfoList entries =
        tmp.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);

    QString sourceDir = tmpDir.path();
    if (entries.size() == 1 && entries.first().isDir())
        sourceDir = entries.first().absoluteFilePath();

    QList<QPair<QString, QString>> mapping;
    collectFolderMapping(sourceDir, QString(), mapping);

    const QString hint = QFileInfo(zipPath).completeBaseName();
    const auto res = m_manager.installFromMapping(game, mapping, hint);
    if (!res.ok) {
        QMessageBox::warning(this, tr("Ошибка установки"), res.error);
        return;
    }

    onRefreshPlugins();
    m_statusLabel->setText(
        tr("Установлен плагин «%1» из ZIP (%2 файлов)")
            .arg(res.pluginBaseName)
            .arg(res.installedRelativeFiles.size()));
}

void MainWindow::onInstallPluginFromFolder()
{
    const GameInfo game = currentGame();
    if (game.id < 0) {
        QMessageBox::information(this, tr("Установка плагина"),
                                 tr("Сначала выберите игру в левой панели."));
        return;
    }

    const QString srcDir = QFileDialog::getExistingDirectory(
        this, tr("Выберите папку с плагином (её содержимое будет скопировано)"));
    if (srcDir.isEmpty())
        return;

    QList<QPair<QString, QString>> mapping;
    collectFolderMapping(srcDir, QString(), mapping);

    const QString hint = QFileInfo(srcDir).fileName();
    const auto res = m_manager.installFromMapping(game, mapping, hint);
    if (!res.ok) {
        QMessageBox::warning(this, tr("Ошибка установки"), res.error);
        return;
    }

    onRefreshPlugins();
    m_statusLabel->setText(
        tr("Установлен плагин «%1» (%2 файлов)")
            .arg(res.pluginBaseName)
            .arg(res.installedRelativeFiles.size()));
}
