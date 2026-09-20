#pragma once

#include <QObject>
#include <QVariantMap>
#include <QString>
#include <QTimer>

namespace omakeyboard {

class HyprlandReloadService;

// "Press a key" capture, and the key tester.
//
// There is no keyboard grab, because on Hyprland there is nothing to grab
// with. Two routes were tried and both are dead ends:
//
//   * the shortcuts-inhibit protocol, which a client would use to ask the
//     compositor to stop resolving bindings: Qt's Wayland backend does not
//     implement it (no zwp_keyboard_shortcuts_inhibit symbols in
//     libQt6WaylandClient);
//   * entering an empty Hyprland submap, which does suspend every binding --
//     but a submap also stops keys being delivered to the focused client at
//     all, verified against 0.56.2, so the window ends up seeing less rather
//     than more.
//
// So capture reads what the compositor forwards. A combination Hyprland has
// bound is consumed before it arrives and cannot be captured; the interface
// says so and offers the key list instead. Physical keys are almost never
// bound on their own, so capturing a remap's source key -- the common case for
// a keyboard app -- works.
class KeyCaptureService : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool capturing READ isCapturing NOTIFY capturingChanged)
    Q_PROPERTY(QString hint READ hint NOTIFY capturingChanged)

public:
    explicit KeyCaptureService(HyprlandReloadService *reload, QObject *parent = nullptr);
    ~KeyCaptureService() override;

    bool isCapturing() const { return m_capturing; }
    QString hint() const;

    // mode: "key" captures a physical key for remapping; "shortcut" captures a
    // modifier combination for a binding.
    Q_INVOKABLE void start(const QString &mode);
    Q_INVOKABLE void cancel();

    // Called from the QML key handler. nativeScanCode comes from the key event;
    // modifiers is Qt::KeyboardModifiers as an int.
    Q_INVOKABLE bool handleKey(quint32 nativeScanCode, int qtKey, int modifiers);

    // --- key tester ---
    // Probing reports every key instead of stopping at the first one, which is
    // how a remap or a layer can be confirmed to have actually taken effect.
    Q_INVOKABLE void startProbe();
    Q_INVOKABLE void stopProbe();
    // What the system reports for a key, as a map the tester renders directly.
    Q_INVOKABLE QVariantMap describeKey(quint32 nativeScanCode, int qtKey, int modifiers,
                                        const QString &text) const;

    // Qt modifiers to Omarchy's o.bind spelling, e.g. "SUPER + SHIFT".
    static QString modifiersToOmarchy(int modifiers);
    // A Qt key to the keysym name Hyprland expects in a binding.
    static QString qtKeyToBindToken(int qtKey);

signals:
    void capturingChanged();
    // A physical key was captured: XKB keycode name, plus a readable label.
    void keyCaptured(const QString &code, const QString &label);
    // A combination was captured, in o.bind spelling.
    void shortcutCaptured(const QString &keys);
    void captureFailed(const QString &reason);

private:
    void setCapturing(bool capturing);
    void stopListening();

    HyprlandReloadService *m_reload = nullptr;
    QTimer m_timeout;
    QString m_mode;
    bool m_capturing = false;
};

} // namespace omakeyboard
