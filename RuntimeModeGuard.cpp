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
    // QLockFile records PID/host/application information. A lock left by a
    // crash or hard power loss is eligible for stale-lock cleanup; an active
    // GUI/headless owner is not removed.
    m_lock.setStaleLockTime(10000);

    if (!m_lock.tryLock(0)) {
        // Important for desktop autostart after a hard outage: there may be a
        // lock file on disk even though the old process no longer exists.
        // removeStaleLockFile() removes it only when QLockFile considers it
        // stale, then we retry ownership once.
        if (!m_lock.removeStaleLockFile() || !m_lock.tryLock(0)) {
            m_error = QStringLiteral(
                "Another AnacostiaIQ process already owns the sensor hardware");
            return;
        }
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
