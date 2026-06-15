/////////////////////////////////////////////////////////////
// SENSOR.H - Abstract sensor interface
//
//  Base class for every sensor in the system. Subclasses
//  implement the hardware-specific parts; the rest of the app
//  only ever talks to a Sensor*.
//
//  Reading contract:
//    measure() returns the current value, or a negative number
//    (-1) to signal "no valid reading" — no GPIO, sensor
//    missing/not responding, or out of range. Use isValid() to
//    test a reading, or isAvailable() to test the sensor.
//
//  Metadata (id + unit) lives on the base class so the
//  monitoring loop can iterate over a list of Sensor* and push
//  each reading to the cloud DB without knowing the concrete type.
/////////////////////////////////////////////////////////////

#ifndef SENSOR_H
#define SENSOR_H

#include <QObject>
#include <QString>
#include <QDebug>

class Sensor : public QObject {
    Q_OBJECT

public:
    // id   — stable identifier sent to the DB as "sensor_id"
    // unit — measurement unit sent to the DB (e.g. "cm", "%", "C")
    // name — human-friendly label for the UI (defaults to id)
    explicit Sensor(const QString &id, const QString &unit,
                    const QString &name = QString(),
                    QObject *parent = nullptr)
        : QObject(parent),
          m_id(id),
          m_unit(unit),
          m_name(name.isEmpty() ? id : name) {}

    ~Sensor() override = default;

    // ── Interface every sensor must implement ──────────────

    // Bring up the hardware. Return false if unavailable (no
    // GPIO on this machine, or hardware init failed). Implementations
    // call setAvailable() to record the result. The app keeps
    // running either way.
    virtual bool initialize() = 0;

    // Take a single reading. Return -1 for any invalid reading
    // (unavailable, not responding, out of range).
    virtual double measure() = 0;

    // ── Common behaviour provided by the base class ────────

    virtual void cleanup() {}   // override only if there's something to release

    bool    isAvailable() const { return m_available; }
    QString id() const          { return m_id; }
    QString unit() const        { return m_unit; }
    QString displayName() const { return m_name; }

    // Per-sensor polling interval (seconds). 0 means "not set" — the
    // app falls back to the global app.pollIntervalSeconds. Set from
    // config.json by the sensor factory. Lets fast-changing variables
    // poll more often than slow ones.
    int  pollIntervalSeconds() const     { return m_intervalSeconds; }
    void setPollIntervalSeconds(int s)   { m_intervalSeconds = s; }

    // Optional display full-scale (e.g. a depth sensor's total length,
    // in its reported unit). 0 means "not applicable / unknown" — the
    // UI should fall back to a default. Subclasses set it via
    // setFullScale() if meaningful.
    double fullScale() const { return m_fullScale; }

    // Centralizes the "-1 means invalid" convention.
    static bool isValid(double reading) { return reading >= 0.0; }

protected:
    // Subclasses call this from initialize() (and may toggle it
    // on a persistent read failure if they want isAvailable() to
    // reflect that).
    void setAvailable(bool a) { m_available = a; }
    void setFullScale(double fs) { m_fullScale = fs; }

private:
    QString m_id;
    QString m_unit;
    QString m_name;
    bool    m_available = false;
    double  m_fullScale = 0.0;
    int     m_intervalSeconds = 0;   // 0 = use app-level default
};

#endif // SENSOR_H
