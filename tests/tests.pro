QT += core gui testlib dbus network

CONFIG += c++20 console testcase
CONFIG -= app_bundle
TARGET = tst_omakeyboard
TEMPLATE = app

SRC = $$PWD/../src

INCLUDEPATH += $$SRC

HEADERS += \
    $$SRC/core/Types.h \
    $$SRC/core/Paths.h \
    $$SRC/core/Process.h \
    $$SRC/core/RuleSet.h \
    $$SRC/core/KeyCatalog.h \
    $$SRC/core/XkbOptionCatalog.h \
    $$SRC/core/KeymapService.h \
    $$SRC/core/DeviceService.h \
    $$SRC/core/LuaScanner.h \
    $$SRC/core/ActionCatalog.h \
    $$SRC/core/ConfigBackupService.h \
    $$SRC/core/OmarchyConfigParser.h \
    $$SRC/core/OmarchyConfigWriter.h \
    $$SRC/core/ConfigRepository.h \
    $$SRC/core/ConflictDetector.h \
    $$SRC/core/HyprlandReloadService.h \
    $$SRC/core/KeyCaptureService.h \
    $$SRC/engine/NativeEngine.h \
    $$SRC/models/RowListModel.h

SOURCES += \
    tst_omakeyboard.cpp \
    $$SRC/core/Paths.cpp \
    $$SRC/core/Process.cpp \
    $$SRC/core/RuleSet.cpp \
    $$SRC/core/KeyCatalog.cpp \
    $$SRC/core/XkbOptionCatalog.cpp \
    $$SRC/core/KeymapService.cpp \
    $$SRC/core/DeviceService.cpp \
    $$SRC/core/LuaScanner.cpp \
    $$SRC/core/ActionCatalog.cpp \
    $$SRC/core/ConfigBackupService.cpp \
    $$SRC/core/OmarchyConfigParser.cpp \
    $$SRC/core/OmarchyConfigWriter.cpp \
    $$SRC/core/ConfigRepository.cpp \
    $$SRC/core/ConflictDetector.cpp \
    $$SRC/core/HyprlandReloadService.cpp \
    $$SRC/core/KeyCaptureService.cpp \
    $$SRC/engine/NativeEngine.cpp \
    $$SRC/models/RowListModel.cpp

# libxkbcommon is the XKB implementation Hyprland itself uses; linking it
# makes keysym validation exact rather than a guess.
LIBS += -lxkbcommon
