QT += core gui qml quick quickcontrols2 quickdialogs2 dbus network

CONFIG += c++20 release
TARGET = omakeyboard
TEMPLATE = app

HEADERS += \
    src/core/Types.h \
    src/core/Paths.h \
    src/core/Process.h \
    src/core/RuleSet.h \
    src/core/KeyCatalog.h \
    src/core/XkbOptionCatalog.h \
    src/core/KeymapService.h \
    src/core/DeviceService.h \
    src/core/LuaScanner.h \
    src/core/ActionCatalog.h \
    src/core/ConfigBackupService.h \
    src/core/OmarchyConfigParser.h \
    src/core/OmarchyConfigWriter.h \
    src/core/ConflictDetector.h \
    src/core/HyprlandReloadService.h \
    src/core/KeyCaptureService.h \
    src/core/ConfigRepository.h \
    src/core/ThemeService.h \
    src/engine/NativeEngine.h \
    src/models/RowListModel.h \
    src/models/KeyMappingModel.h \
    src/models/ShortcutModel.h \
    src/models/ActionCatalogModel.h

SOURCES += \
    src/main.cpp \
    src/core/Paths.cpp \
    src/core/Process.cpp \
    src/core/RuleSet.cpp \
    src/core/KeyCatalog.cpp \
    src/core/XkbOptionCatalog.cpp \
    src/core/KeymapService.cpp \
    src/core/DeviceService.cpp \
    src/core/LuaScanner.cpp \
    src/core/ActionCatalog.cpp \
    src/core/ConfigBackupService.cpp \
    src/core/OmarchyConfigParser.cpp \
    src/core/OmarchyConfigWriter.cpp \
    src/core/ConflictDetector.cpp \
    src/core/HyprlandReloadService.cpp \
    src/core/KeyCaptureService.cpp \
    src/core/ConfigRepository.cpp \
    src/core/ThemeService.cpp \
    src/engine/NativeEngine.cpp \
    src/models/RowListModel.cpp \
    src/models/KeyMappingModel.cpp \
    src/models/ShortcutModel.cpp \
    src/models/ActionCatalogModel.cpp

RESOURCES += src/resources.qrc

QMAKE_CXXFLAGS += -Wall -Wextra

# libxkbcommon is the XKB implementation Hyprland itself uses; linking it
# makes keysym validation exact rather than a guess.
LIBS += -lxkbcommon
