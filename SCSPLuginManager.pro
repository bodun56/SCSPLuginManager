QT += core gui widgets sql
QT += core-private

CONFIG += c++20
CONFIG += windows
CONFIG += lrelease embed_translations

TARGET = SCSPLuginManager
TEMPLATE = app

SOURCES += \
    src/main.cpp \
    src/steamgamedetector.cpp \
    src/pluginmanager.cpp \
    src/mainwindow.cpp

HEADERS += \
    src/steamgamedetector.h \
    src/pluginmanager.h \
    src/mainwindow.h

# Иконка самого exe (проводник / панель задач / свойства файла)
RC_FILE = app.rc

# Иконка окна и любые другие ресурсы Qt
RESOURCES += \
    resources.qrc

TRANSLATIONS += \
    translations/scs_en.ts

win32 {
    QMAKE_CXXFLAGS += -finput-charset=UTF-8 -fexec-charset=UTF-8
    QMAKE_CXXFLAGS += -Wall -Wextra
}
