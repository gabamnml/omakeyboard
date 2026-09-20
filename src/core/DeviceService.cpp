#include "DeviceService.h"

#include "HyprlandReloadService.h"
#include "Process.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>

namespace omakeyboard {

namespace {
constexpr const char *kHyprctl = "hyprctl";

// Fallback only, for when udev cannot be consulted. Hyprland's own synthetic
// keyboards are matched by the name it gives them, which is reliable; the rest
// of the list is guesswork and exists solely so a missing udev does not fill
// the interface with power buttons.
bool looksVirtual(const QString &name)
{
    static const QStringList markers{
        QStringLiteral("hl-virtual"), QStringLiteral("virtual-keyboard"),
        QStringLiteral("power-button"), QStringLiteral("sleep-button"),
        QStringLiteral("video-bus"), QStringLiteral("lid-switch"),
    };
    for (const QString &marker : markers)
        if (name.contains(marker))
            return true;
    return false;
}

// Hyprland lowercases a device name and turns spaces into dashes, so the same
// transformation applied to an evdev name identifies the node behind it.
QString hyprlandStyleName(const QString &evdevName)
{
    return evdevName.toLower().replace(QLatin1Char(' '), QLatin1Char('-'));
}
} // namespace

DeviceService::DeviceService(QObject *parent)
    : QObject(parent)
{
}

QString DeviceService::prettify(const QString &deviceName)
{
    // Hyprland lowercases and dash-joins names, so "Apple Inc. Apple Internal
    // Keyboard / Trackpad" arrives as
    // "apple-inc.-apple-internal-keyboard-/-trackpad". Undo enough of that to
    // be readable without pretending to recover the original spelling.
    QString text = deviceName;
    text.replace(QStringLiteral("-/-"), QStringLiteral(" / "));
    text.replace(QLatin1Char('-'), QLatin1Char(' '));

    // Vendors put their legal form in the product string and then repeat the
    // brand, which is how "Apple apple internal keyboard" happens.
    static const QStringList noise{
        QStringLiteral("inc."), QStringLiteral("inc"), QStringLiteral("corp."),
        QStringLiteral("corp"), QStringLiteral("ltd."), QStringLiteral("ltd"),
        QStringLiteral("co."), QStringLiteral("gmbh"), QStringLiteral("s.a."),
    };

    QStringList words;
    const QStringList parts = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const QString &part : parts) {
        if (noise.contains(part, Qt::CaseInsensitive))
            continue;
        // Drop a word the name has already used, which collapses the
        // brand-repeated-as-product stutter.
        bool seen = false;
        for (const QString &existing : words) {
            if (existing.compare(part, Qt::CaseInsensitive) == 0) {
                seen = true;
                break;
            }
        }
        if (!seen)
            words.append(part);
    }

    text = words.join(QLatin1Char(' ')).simplified();
    if (text.isEmpty())
        return deviceName;
    text[0] = text.at(0).toUpper();
    return text;
}

bool DeviceService::isBluetoothHidProxy(const QString &inputPath,
                                        const QStringList &bluetoothUsbPaths)
{
    if (inputPath.isEmpty())
        return false;
    for (const QString &usbPath : bluetoothUsbPaths) {
        if (usbPath.isEmpty())
            continue;
        // The input device sits under the same USB device as the adapter.
        if (inputPath.startsWith(usbPath + QLatin1Char('/')))
            return true;
    }
    return false;
}

// The USB devices that own a Bluetooth adapter, asked of the system rather
// than inferred from names or class codes.
static QStringList bluetoothUsbDevicePaths()
{
    QStringList paths;
    QDir bluetooth(QStringLiteral("/sys/class/bluetooth"));
    const QStringList adapters =
        bluetooth.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System);
    for (const QString &adapter : adapters) {
        const QString link = bluetooth.filePath(adapter) + QStringLiteral("/device");
        const QString resolved = QFileInfo(link).canonicalFilePath();
        if (resolved.isEmpty())
            continue;
        // The adapter is a USB *interface*; its parent is the USB device whose
        // other interfaces include the HID proxy.
        const QString usbDevice = QFileInfo(resolved).dir().absolutePath();
        if (!usbDevice.isEmpty() && !paths.contains(usbDevice))
            paths.append(usbDevice);
    }
    return paths;
}

bool DeviceService::refresh()
{
    const ProcessResult result = Process::run(
        QString::fromLatin1(kHyprctl), { QStringLiteral("devices"), QStringLiteral("-j") });
    if (!result.ok())
        return false;

    const QJsonDocument document = QJsonDocument::fromJson(result.stdOut.toUtf8());
    if (!document.isObject())
        return false;

    QVector<KeyboardDevice> keyboards;
    const QJsonArray entries =
        document.object().value(QStringLiteral("keyboards")).toArray();
    for (const QJsonValue &value : entries) {
        const QJsonObject entry = value.toObject();
        KeyboardDevice device;
        device.name = entry.value(QStringLiteral("name")).toString();
        if (device.name.isEmpty())
            continue;
        device.label = prettify(device.name);
        device.layout = entry.value(QStringLiteral("layout")).toString();
        device.variant = entry.value(QStringLiteral("variant")).toString();
        device.model = entry.value(QStringLiteral("model")).toString();
        device.options = entry.value(QStringLiteral("options")).toString();
        device.activeKeymap = entry.value(QStringLiteral("active_keymap")).toString();
        device.isMain = entry.value(QStringLiteral("main")).toBool();
        device.isVirtual = looksVirtual(device.name);
        keyboards.append(device);
    }

    m_keyboards = keyboards;
    resolveVerdicts();
    readEffectiveOptions();
    emit keyboardsChanged();
    return true;
}

void DeviceService::resolveVerdicts()
{
    // /proc/bus/input/devices lists every input device with its name and the
    // handlers it owns. Readable by any user, and one file instead of a scan.
    //
    // Read it whole rather than streaming: files under /proc report a size of
    // zero, and a QTextStream over one comes back empty.
    QFile file(QStringLiteral("/proc/bus/input/devices"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QStringList lines =
        QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
    file.close();

    struct Node {
        QString name;       // hyprland-style
        QString event;      // eventN
        QString transport;
    };
    QList<Node> nodes;
    QString pendingName;
    int pendingBus = -1;
    for (const QString &line : lines) {
        if (line.startsWith(QStringLiteral("I: Bus="))) {
            bool ok = false;
            pendingBus = line.mid(7, 4).toInt(&ok, 16);
            if (!ok)
                pendingBus = -1;
        } else if (line.startsWith(QStringLiteral("N: Name="))) {
            pendingName = line.section(QLatin1Char('='), 1).trimmed();
            if (pendingName.startsWith(QLatin1Char('"')) && pendingName.endsWith(QLatin1Char('"')))
                pendingName = pendingName.mid(1, pendingName.size() - 2);
        } else if (line.startsWith(QStringLiteral("H: Handlers=")) && !pendingName.isEmpty()) {
            static const QRegularExpression handler(QStringLiteral("\\b(event\\d+)\\b"));
            const auto match = handler.match(line);
            if (match.hasMatch())
                nodes.append({ hyprlandStyleName(pendingName), match.captured(1),
                               transportFor(pendingBus, pendingName) });
            pendingName.clear();
        }
    }

    // One udevadm call per node, cached: several Hyprland entries can share a
    // name, and one device can expose both a keyboard and a pointer node.
    QHash<QString, bool> nodeIsKeyboard;
    const auto askUdev = [&nodeIsKeyboard](const QString &node) -> bool {
        const auto cached = nodeIsKeyboard.constFind(node);
        if (cached != nodeIsKeyboard.constEnd())
            return *cached;

        const ProcessResult result = Process::run(
            QStringLiteral("udevadm"),
            { QStringLiteral("info"), QStringLiteral("--query=property"),
              QStringLiteral("--name=/dev/input/") + node },
            2000);
        const bool verdict =
            result.ok() && result.stdOut.contains(QStringLiteral("ID_INPUT_KEYBOARD=1"));
        nodeIsKeyboard.insert(node, verdict);
        return verdict;
    };

    if (!Process::isAvailable(QStringLiteral("udevadm")))
        return;

    const QStringList bluetoothUsb = bluetoothUsbDevicePaths();

    for (KeyboardDevice &device : m_keyboards) {
        bool found = false;
        bool isKeyboard = false;

        for (const auto &entry : nodes) {
            // Exact match, or Hyprland's duplicate suffix ("power-button-1").
            const bool sameName = entry.name == device.name
                || (device.name.startsWith(entry.name + QLatin1Char('-'))
                    && device.name.mid(entry.name.size() + 1).toInt() > 0);
            if (!sameName)
                continue;

            found = true;
            if (!askUdev(entry.event))
                continue;

            // udev calls it a keyboard, but a Bluetooth radio's boot-protocol
            // HID endpoint answers the same way and types nothing.
            const QString resolved = QFileInfo(
                QStringLiteral("/sys/class/input/") + entry.event).canonicalFilePath();
            if (isBluetoothHidProxy(resolved, bluetoothUsb))
                continue;

            isKeyboard = true;
            device.transport = entry.transport;
            break;
        }

        if (!found)
            device.verdict = KeyboardVerdict::Unknown;
        else
            device.verdict = isKeyboard ? KeyboardVerdict::Confirmed
                                        : KeyboardVerdict::Denied;
    }
}

QString DeviceService::transportFor(int bus, const QString &deviceName)
{
    // Values from linux/input.h.
    constexpr int kBusUsb = 0x03;
    constexpr int kBusBluetooth = 0x05;
    constexpr int kBusI8042 = 0x11;
    constexpr int kBusI2c = 0x18;
    constexpr int kBusHost = 0x19;
    constexpr int kBusSpi = 0x1c;

    if (bus == kBusBluetooth)
        return QStringLiteral("bluetooth");
    if (bus == kBusI8042 || bus == kBusI2c || bus == kBusHost || bus == kBusSpi)
        return QStringLiteral("internal");
    if (bus == kBusUsb) {
        return deviceName.contains(QStringLiteral("internal"), Qt::CaseInsensitive)
            ? QStringLiteral("internal")
            : QStringLiteral("usb");
    }
    return {};
}

void DeviceService::readEffectiveOptions()
{
    // `hyprctl getoption` is the only honest source for the values actually in
    // effect: Omarchy computes kb_options at load time from /etc/vconsole.conf,
    // so reading that file back would miss what it decided.
    // One parser for both callers: getting this wrong once is enough.
    const auto readOption = [](const QString &key) -> QString {
        const ProcessResult result = Process::run(
            QString::fromLatin1(kHyprctl), { QStringLiteral("getoption"), key });
        if (!result.ok())
            return {};
        return HyprlandReloadService::parseOptionValue(result.stdOut);
    };

    m_effectiveLayout = readOption(QStringLiteral("input:kb_layout"));
    m_effectiveVariant = readOption(QStringLiteral("input:kb_variant"));
    m_effectiveModel = readOption(QStringLiteral("input:kb_model"));
    m_effectiveOptions = readOption(QStringLiteral("input:kb_options"));

    // A main keyboard's own values win when present: a profile for that device
    // should inherit what that device is really using.
    if (const KeyboardDevice *main = mainKeyboard()) {
        if (!main->layout.isEmpty())
            m_effectiveLayout = main->layout;
        if (!main->options.isEmpty())
            m_effectiveOptions = main->options;
    }
}

QStringList DeviceService::effectiveOptions() const
{
    if (m_effectiveOptions.isEmpty())
        return {};
    return m_effectiveOptions.split(QLatin1Char(','), Qt::SkipEmptyParts);
}

const KeyboardDevice *DeviceService::find(const QString &name) const
{
    for (const KeyboardDevice &device : m_keyboards)
        if (device.name == name)
            return &device;
    return nullptr;
}

const KeyboardDevice *DeviceService::mainKeyboard() const
{
    // Prefer a real device over the virtual keyboard Hyprland marks as main
    // when an input method is running (fcitx5 shows up that way).
    for (const KeyboardDevice &device : m_keyboards)
        if (device.isMain && device.isConfigurable())
            return &device;
    for (const KeyboardDevice &device : m_keyboards)
        if (device.isConfigurable())
            return &device;
    return m_keyboards.isEmpty() ? nullptr : &m_keyboards.constFirst();
}

} // namespace omakeyboard
