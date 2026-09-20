#pragma once

#include <QObject>
#include <QString>
#include <QVector>

namespace omakeyboard {

// Whether the system considers a device an actual keyboard. Hyprland reports
// power buttons, lid switches and video buses in its keyboard list too, so the
// question has to be asked somewhere else.
enum class KeyboardVerdict {
    Unknown = 0,   // no evdev node matched, so nothing is known
    Confirmed,     // udev says ID_INPUT_KEYBOARD=1
    Denied,        // the node was found and udev says it is not a keyboard
};

struct KeyboardDevice {
    QString name;          // hyprctl's own name; the key hl.device{} matches on
    QString label;         // prettified for display
    QString layout;
    QString variant;
    QString model;
    QString options;       // comma-joined kb_options currently in effect
    QString activeKeymap;
    bool isMain = false;
    bool isVirtual = false;
    KeyboardVerdict verdict = KeyboardVerdict::Unknown;
    // How it is attached: "internal", "usb", "bluetooth" or "" when unknown.
    QString transport;

    // Worth offering as something to configure. Denied devices are hidden;
    // an unknown verdict is shown, because hiding a real keyboard is worse
    // than showing a power button.
    bool isConfigurable() const
    {
        if (verdict == KeyboardVerdict::Confirmed)
            return true;
        if (verdict == KeyboardVerdict::Denied)
            return false;
        return !isVirtual;
    }
};

// Reads the keyboards Hyprland currently has, so profiles can be attached to a
// real device rather than a name the user typed.
class DeviceService : public QObject {
    Q_OBJECT

public:
    explicit DeviceService(QObject *parent = nullptr);

    // Refresh from `hyprctl devices -j`. Returns false when Hyprland is not
    // reachable, which is the normal case in unit tests.
    bool refresh();

    const QVector<KeyboardDevice> &keyboards() const { return m_keyboards; }
    const KeyboardDevice *find(const QString &name) const;
    const KeyboardDevice *mainKeyboard() const;

    // Effective values Hyprland reports, used as the fallback RMLVO when a
    // profile pins nothing.
    QString effectiveLayout() const { return m_effectiveLayout; }
    QString effectiveVariant() const { return m_effectiveVariant; }
    QString effectiveModel() const { return m_effectiveModel; }
    QStringList effectiveOptions() const;

    static QString prettify(const QString &deviceName);

    // A Bluetooth radio exposes USB HID boot-protocol endpoints so a paired
    // keyboard works before the OS Bluetooth stack loads. Under a running
    // Linux desktop those endpoints produce nothing -- bluez gives a paired
    // keyboard its own uhid device instead -- so they must not be offered as
    // keyboards to configure.
    //
    // Pure so it can be tested without sysfs: `inputPath` is the resolved
    // sysfs path of the input device, `bluetoothUsbPaths` the USB devices that
    // own a Bluetooth adapter.
    // The kernel's bus number for an input device, read as a transport. A
    // laptop's own keyboard often sits on USB internally (every Apple one
    // does), so the kernel's name for it is consulted too.
    static QString transportFor(int bus, const QString &deviceName);

    static bool isBluetoothHidProxy(const QString &inputPath,
                                    const QStringList &bluetoothUsbPaths);

signals:
    void keyboardsChanged();

private:
    void readEffectiveOptions();
    // Ask udev what each device actually is. Names are matched by undoing
    // Hyprland's lowercase-and-dash munging.
    void resolveVerdicts();

    QVector<KeyboardDevice> m_keyboards;
    QString m_effectiveLayout;
    QString m_effectiveVariant;
    QString m_effectiveModel;
    QString m_effectiveOptions;
};

} // namespace omakeyboard
