/////////////////////////////////////////////////////////////
// CHARTCONTAINER.H - Chart Container Class Header
/////////////////////////////////////////////////////////////

#ifndef CHARTCONTAINER_H
#define CHARTCONTAINER_H
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QDateTimeAxis>
#include <QMap>

#include "WeatherFetcher.h"

// Qt5 keeps charts classes in the QtCharts namespace; Qt6 moved
// them to the global namespace. Pull them in for Qt5 so the
// unqualified type names below work on both.
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
using namespace QtCharts;
#endif

// Class for managing chart visualization of weather data
class ChartContainer
{
public:
    ChartContainer();

    // Plot a single weather data series
    void plotWeatherData(const QVector<WeatherData>& weatherData, const QString& yAxisTitle);

    // Plot multiple weather data series on the same chart
    void plotWeatherDataMap(const QMap<QString, QVector<WeatherData>>& weatherDataMap);

    // Getter for the chart view widget
    QChartView* GetChartView() { return chartview; }

    // Enable/disable chart animations (disable for rapid updates)
    void setAnimated(bool enabled) { animated = enabled; }

private:
    QChart* chart = new QChart();                              // The chart object
    QChartView* chartview = new QChartView();                  // Widget to display the chart
    void removeAllAxes();                                      // Helper to clear all axes
    QVector<QColor> colors;                                    // Color palette for series
    bool animated = true;                                      // Animation toggle
};

#endif // CHARTCONTAINER_H
