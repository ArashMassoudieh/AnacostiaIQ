#include "RuntimeModeGuard.h"

#include <QDir>
#include <QSaveFile>
#include <QTextStream>

QString RuntimeModeGuard::stateDirectory()
{
    QString root = qEnvironmentVariable("XDG_STATE_HOME");
    if (root.isEmpty())
        root = QDir::homePath() + "/.local/state";

    const QString dir = root + "/anacostiaiq";
    QDir().mkpath(dir);
    return dir;
}

QString RuntimeModeGuard::modeFilePath()
{
    return stateDirectory() + "/mode";
}

QString RuntimeModeGuard::lockFilePath()
{
    return stateDirectory() + "/hardware.lock";
}

RuntimeModeGuard::RuntimeModeGuard(const QString &mode)
    : m_lock(lockFilePath())
{
    // A stale lock from a crash or hard power loss should be reclaimed
    // quickly after reboot. QLockFile also checks whether the recorded PID
    // is still alive before deciding a lock is active.
    m_lock.setStaleLockTime(10000);

    if (!m_lock.tryLock(0)) {
        m_error = QStringLiteral(
            "Another AnacostiaIQ process already owns the sensor hardware");
        return;
    }

    m_acquired = true;

    if (!saveMode(mode)) {
        m_error = QStringLiteral("Could not persist AnacostiaIQ runtime mode");
        m_lock.unlock();
        m_acquired = false;
    }
}

bool RuntimeModeGuard::saveMode(const QString &mode)
{
    if (mode != "gui" && mode != "headless")
        return false;

    QSaveFile file(modeFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QTextStream out(&file);
    out << mode << '\n';
    out.flush();

    return file.commit();
}
