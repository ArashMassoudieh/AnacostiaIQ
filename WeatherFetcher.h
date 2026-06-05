/////////////////////////////////////////////////////////////
// WEATHERFETCHER.H - Weather API client (NOAA or Open-Meteo)
//
//  Source is selectable at runtime (from config). Both backends
//  return the same QVector<WeatherData> so the rest of the app is
//  unchanged. Open-Meteo gives hourly values; NOAA gives multi-hour
//  bins.
/////////////////////////////////////////////////////////////

#ifndef WEATHERFETCHER_H
#define WEATHERFETCHER_H

#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QString>
#include <QVector>

// Weather data type requested
enum class datatype {
    ProbabilityofPrecipitation,
    Temperature,
    PrecipitationAmount,
    RelativeHumidity
};

// Which backend to fetch from
enum class WeatherSource {
    NOAA,
    OpenMeteo
};

// One time-series data point (unchanged from the original struct so
// the charts, DB writer, and app code keep working).
struct WeatherData {
    QDateTime timestamp;  // Time of the prediction
    double    value;      // Value of the measurement
};

class WeatherFetcher : public QObject {
    Q_OBJECT

public:
    explicit WeatherFetcher(QObject *parent = nullptr);

    // ── Source / location configuration ────────────────────
    void setSource(WeatherSource source) { m_source = source; }
    WeatherSource source() const { return m_source; }

    // Parse "noaa" / "openmeteo" (case-insensitive). Unknown -> OpenMeteo.
    void setSourceFromString(const QString &s);

    void setLocation(double latitude, double longitude) {
        m_lat = latitude; m_lon = longitude;
    }
    void setNoaaGrid(const QString &office, int gridX, int gridY) {
        m_office = office; m_gridX = gridX; m_gridY = gridY;
    }

    // ── Fetch ──────────────────────────────────────────────
    // Returns a time series for the requested variable from the
    // currently selected source. Empty on error (see lastError()).
    QVector<WeatherData> getWeatherPrediction(datatype type);

    QString lastError() const { return m_lastError; }

private:
    QVector<WeatherData> fetchNOAA(datatype type);
    QVector<WeatherData> fetchOpenMeteo(datatype type);
    QByteArray httpGetBlocking(const QString &url);

    QNetworkAccessManager *manager;
    WeatherSource m_source = WeatherSource::OpenMeteo;

    // Open-Meteo location
    double m_lat = 38.98;
    double m_lon = -77.10;

    // NOAA grid
    QString m_office = "LWX";
    int m_gridX = 97;
    int m_gridY = 71;

    QString m_lastError;
};

// Helper to sum values over a number of days (unchanged signature).
double calculateCumulativeValue(const QVector<WeatherData>& weatherData, int days);

#endif // WEATHERFETCHER_H
