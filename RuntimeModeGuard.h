#ifndef RUNTIMEMODEGUARD_H
#define RUNTIMEMODEGUARD_H

#include <QLockFile>
#include <QString>

// Keeps GUI and headless from owning the Pi hardware simultaneously and
// records the mode that actually started, so reboot recovery restores what
// was really running rather than relying only on a switch script.
class RuntimeModeGuard
{
public:
    explicit RuntimeModeGuard(const QString &mode);

    bool acquired() const { return m_acquired; }
    QString errorString() const { return m_error; }

    static QString stateDirectory();
    static QString modeFilePath();
    static QString lockFilePath();

private:
    bool saveMode(const QString &mode);

    QLockFile m_lock;
    bool m_acquired = false;
    QString m_error;
};

#endif // RUNTIMEMODEGUARD_H
