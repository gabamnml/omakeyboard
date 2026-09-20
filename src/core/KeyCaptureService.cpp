#include "KeyCaptureService.h"

#include "HyprlandReloadService.h"
#include "KeyCatalog.h"
#include "Paths.h"

#include <QKeySequence>
#include <Qt>

namespace omakeyboard {

namespace {
// A listening field that never times out would sit there swallowing keys, so
// it always has a deadline.
constexpr int kCaptureTimeoutMs = 15000;
// The tester is meant to be used for a while, so it listens for longer.
constexpr int kProbeTimeoutMs = 120000;
} // namespace

KeyCaptureService::KeyCaptureService(HyprlandReloadService *reload, QObject *parent)
    : QObject(parent)
    , m_reload(reload)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(kCaptureTimeoutMs);
    connect(&m_timeout, &QTimer::timeout, this, [this]() {
        if (!m_capturing)
            return;
        stopListening();
        setCapturing(false);
        emit captureFailed(QStringLiteral("Capture timed out."));
    });
}

KeyCaptureService::~KeyCaptureService() = default;

QString KeyCaptureService::hint() const
{
    if (!m_capturing)
        return {};
    if (m_mode == QStringLiteral("shortcut")) {
        return QStringLiteral("Press a combination. Anything Hyprland already has bound is "
                              "consumed before it reaches this window — pick those from the "
                              "list instead. Escape cancels.");
    }
    return QStringLiteral("Press a key. Escape cancels.");
}

void KeyCaptureService::setCapturing(bool capturing)
{
    if (m_capturing == capturing)
        return;
    m_capturing = capturing;
    emit capturingChanged();
}

void KeyCaptureService::start(const QString &mode)
{
    if (m_capturing)
        cancel();

    m_mode = mode.isEmpty() ? QStringLiteral("key") : mode;
    setCapturing(true);
    m_timeout.start(kCaptureTimeoutMs);
    emit capturingChanged();
}

void KeyCaptureService::stopListening()
{
    m_timeout.stop();
}

void KeyCaptureService::cancel()
{
    if (!m_capturing)
        return;
    stopListening();
    setCapturing(false);
}

void KeyCaptureService::startProbe()
{
    // Longer than a capture: the point is to sit here pressing keys.
    if (m_capturing)
        cancel();
    m_mode = QStringLiteral("probe");
    setCapturing(true);
    m_timeout.start(kProbeTimeoutMs);
    emit capturingChanged();
}

void KeyCaptureService::stopProbe()
{
    if (m_mode != QStringLiteral("probe"))
        return;
    cancel();
}

QVariantMap KeyCaptureService::describeKey(quint32 nativeScanCode, int qtKey, int modifiers,
                                           const QString &text) const
{
    const KeyCatalog &catalog = KeyCatalog::instance();
    const QString code = catalog.codeForNativeScanCode(nativeScanCode);

    // QKeySequence names the key the way a person would read it, and it is
    // what the *remapped* keyboard produced -- which is exactly the thing
    // being tested. A layer that works shows "Left" here, not "H".
    QString produced = QKeySequence(qtKey).toString(QKeySequence::NativeText);
    if (produced.isEmpty())
        produced = text;

    QVariantMap result{
        { QStringLiteral("keycode"), nativeScanCode },
        { QStringLiteral("code"), code },
        { QStringLiteral("codeLabel"), code.isEmpty() ? QString() : catalog.labelForCode(code) },
        { QStringLiteral("produced"), produced },
        { QStringLiteral("text"), text },
        { QStringLiteral("modifiers"), modifiersToOmarchy(modifiers) },
        { QStringLiteral("binding"), QString() },
    };

    // The spelling this key would need in a shortcut, when it has one.
    const QString token = qtKeyToBindToken(qtKey);
    if (!token.isEmpty()) {
        const QString mods = modifiersToOmarchy(modifiers);
        result[QStringLiteral("binding")] =
            mods.isEmpty() ? token : mods + QStringLiteral(" + ") + token;
    }

    return result;
}

QString KeyCaptureService::modifiersToOmarchy(int modifiers)
{
    // Order matters only for readability; the parser normalises it anyway.
    // Kept in Omarchy's own order: SUPER, then CTRL, then ALT, then SHIFT.
    QStringList parts;
    const auto flags = static_cast<Qt::KeyboardModifiers>(modifiers);
    if (flags.testFlag(Qt::MetaModifier))
        parts << QStringLiteral("SUPER");
    if (flags.testFlag(Qt::ControlModifier))
        parts << QStringLiteral("CTRL");
    if (flags.testFlag(Qt::AltModifier))
        parts << QStringLiteral("ALT");
    if (flags.testFlag(Qt::ShiftModifier))
        parts << QStringLiteral("SHIFT");
    return parts.join(QStringLiteral(" + "));
}

QString KeyCaptureService::qtKeyToBindToken(int qtKey)
{
    // Names Hyprland accepts. The awkward ones are the punctuation keysyms:
    // xkbcommon calls them "comma" and "period" in lower case, and Omarchy's
    // own bindings use exactly that spelling (see the note in
    // default/hypr/bindings/utilities.lua).
    switch (qtKey) {
    case Qt::Key_Return:
    case Qt::Key_Enter:      return QStringLiteral("RETURN");
    case Qt::Key_Space:      return QStringLiteral("SPACE");
    case Qt::Key_Tab:        return QStringLiteral("TAB");
    case Qt::Key_Backspace:  return QStringLiteral("BACKSPACE");
    case Qt::Key_Delete:     return QStringLiteral("Delete");
    case Qt::Key_Escape:     return QStringLiteral("ESCAPE");
    case Qt::Key_Home:       return QStringLiteral("HOME");
    case Qt::Key_End:        return QStringLiteral("END");
    case Qt::Key_PageUp:     return QStringLiteral("PRIOR");
    case Qt::Key_PageDown:   return QStringLiteral("NEXT");
    case Qt::Key_Left:       return QStringLiteral("LEFT");
    case Qt::Key_Right:      return QStringLiteral("RIGHT");
    case Qt::Key_Up:         return QStringLiteral("UP");
    case Qt::Key_Down:       return QStringLiteral("DOWN");
    case Qt::Key_Comma:      return QStringLiteral("comma");
    case Qt::Key_Period:     return QStringLiteral("period");
    case Qt::Key_Slash:      return QStringLiteral("slash");
    case Qt::Key_Minus:      return QStringLiteral("minus");
    case Qt::Key_Equal:      return QStringLiteral("equal");
    case Qt::Key_Semicolon:  return QStringLiteral("semicolon");
    case Qt::Key_Apostrophe: return QStringLiteral("apostrophe");
    case Qt::Key_Backslash:  return QStringLiteral("backslash");
    case Qt::Key_BracketLeft:  return QStringLiteral("bracketleft");
    case Qt::Key_BracketRight: return QStringLiteral("bracketright");
    case Qt::Key_QuoteLeft:  return QStringLiteral("grave");
    case Qt::Key_Print:      return QStringLiteral("PRINT");
    default:
        break;
    }

    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F35)
        return QStringLiteral("F%1").arg(qtKey - Qt::Key_F1 + 1);
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
        return QString(QChar(static_cast<char16_t>(qtKey)));
    if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9)
        return QString(QChar(static_cast<char16_t>(qtKey)));

    return {};
}

bool KeyCaptureService::handleKey(quint32 nativeScanCode, int qtKey, int modifiers)
{
    if (!m_capturing)
        return false;

    // Escape always cancels, and is never itself captured: a dialog you cannot
    // leave is worse than a mapping you cannot make.
    if (qtKey == Qt::Key_Escape) {
        cancel();
        return true;
    }

    // Probing reports and keeps listening; the UI reads the description
    // straight from describeKey().
    if (m_mode == QStringLiteral("probe")) {
        m_timeout.start(kProbeTimeoutMs);
        return true;
    }

    if (m_mode == QStringLiteral("shortcut")) {
        const QString token = qtKeyToBindToken(qtKey);
        if (token.isEmpty()) {
            // A bare modifier press: keep waiting for the rest of the chord
            // rather than rejecting it.
            return false;
        }

        const QString modifierText = modifiersToOmarchy(modifiers);
        const QString keys = modifierText.isEmpty()
            ? token
            : modifierText + QStringLiteral(" + ") + token;

        stopListening();
        setCapturing(false);
        emit shortcutCaptured(keys);
        return true;
    }

    // Physical key capture: the scan code identifies the key regardless of what
    // it currently types, which is exactly what a remap needs.
    const KeyCatalog &catalog = KeyCatalog::instance();
    const QString code = catalog.codeForNativeScanCode(nativeScanCode);
    if (code.isEmpty()) {
        emit captureFailed(QStringLiteral(
            "That key could not be identified (scan code %1). Pick it from the list instead.")
                               .arg(nativeScanCode));
        return false;
    }

    stopListening();
    setCapturing(false);
    emit keyCaptured(code, catalog.labelForCode(code));
    return true;
}

} // namespace omakeyboard
