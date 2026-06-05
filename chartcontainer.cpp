/////////////////////////////////////////////////////////////
// CHARTCONTAINER.CPP - Chart Visualization Component
/////////////////////////////////////////////////////////////

#include "chartcontainer.h"

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
using namespace QtCharts;
#endif

// Constructor: Initialize with 10 random colors for chart series
ChartContainer::ChartContainer()
{
    for (int i = 0; i < 10; i++)
        colors.append(QColor(rand() % 256, rand() % 256, rand() % 256));
}

// Plot a single weather data series on the chart
void ChartContainer::plotWeatherData(const QVector<WeatherData>& weatherData, const QString& yAxisTitle) {
    // Clear existing series and axes from the chart
    chart->removeAllSeries();
    removeAllAxes();

    // Create a new line series for the data
    QLineSeries* series = new QLineSeries();
    double max_val = -1e6;

    // Add all data points to the series and track maximum value
    for (const auto& data : weatherData) {
        series->append(data.timestamp.toMSecsSinceEpoch(), data.value);
        max_val = std::max(data.value, max_val);
    }

    // Customize the line appearance (width and color)
    QPen pen = series->pen();
    pen.setWidth(4); // Set the desired line thickness
    pen.setColor(colors[0]);
    series->setPen(pen);

    // Add the series to the chart and configure it
    chart->addSeries(series);
    chart->setTitle(yAxisTitle);
    chart->setAnimationOptions(animated ? QChart::SeriesAnimations : QChart::NoAnimation);

    // Configure X-axis (time axis)
    QDateTimeAxis* axisX = new QDateTimeAxis();
    axisX->setTitleText("Time");
    axisX->setFormat("dd/MM HH:mm");
    QFont xAxisFont = axisX->labelsFont();
    xAxisFont.setPointSize(8); // Make font smaller
    axisX->setLabelsFont(xAxisFont);
    axisX->setTickCount(20);
    axisX->setLabelsAngle(90.0); // Rotate labels by 90 degrees
    chart->addAxis(axisX, Qt::AlignBottom);
    series->attachAxis(axisX);

    // Configure Y-axis (value axis)
    QValueAxis* axisY = new QValueAxis();
    axisY->setTitleText(yAxisTitle);
    axisY->setLabelFormat("%.1f");
    QFont yAxisFont = axisY->labelsFont();
    axisY->setRange(0, max_val);
    yAxisFont.setPointSize(8); // Make font smaller
    axisY->setLabelsFont(yAxisFont);
    chart->addAxis(axisY, Qt::AlignLeft);
    series->attachAxis(axisY);

    // Update the chart view with anti-aliasing for smooth rendering
    chartview->setChart(chart);
    chartview->setRenderHint(QPainter::Antialiasing);
}

// Plot multiple weather data series on the same chart (multi-line chart)
void ChartContainer::plotWeatherDataMap(const QMap<QString, QVector<WeatherData>>& weatherDataMap) {
    // Clear existing series and axes
    chart->removeAllSeries();
    removeAllAxes();
    chart->setAnimationOptions(animated ? QChart::SeriesAnimations : QChart::NoAnimation);

    // Find the time range across all series for a unified X-axis
    QDateTime minTime = QDateTime::currentDateTime();
    QDateTime maxTime = QDateTime::fromSecsSinceEpoch(0);

    for (auto it = weatherDataMap.begin(); it != weatherDataMap.end(); ++it) {
        const QVector<WeatherData>& weatherData = it.value();
        if (!weatherData.empty()) {
            QDateTime seriesStartTime = weatherData.front().timestamp;
            QDateTime seriesEndTime = weatherData.back().timestamp;

            if (seriesStartTime < minTime) {
                minTime = seriesStartTime;
            }
            if (seriesEndTime > maxTime) {
                maxTime = seriesEndTime;
            }
        }
    }

    // Create and configure the shared X-axis (time)
    QDateTimeAxis* axisX = new QDateTimeAxis();
    axisX->setTitleText("Time");
    axisX->setFormat("dd/MM HH:mm");
    axisX->setTickCount(10);
    QFont xAxisFont = axisX->labelsFont();
    xAxisFont.setPointSize(8); // Make font smaller
    axisX->setLabelsFont(xAxisFont);
    axisX->setLabelsAngle(90.0); // Rotate labels by 90 degrees
    axisX->setRange(minTime, maxTime);
    chart->addAxis(axisX, Qt::AlignBottom);

    // Create a separate series and Y-axis for each data type in the map
    int counter = 0;
    for (auto it = weatherDataMap.begin(); it != weatherDataMap.end(); ++it) {
        counter++;
        const QString& yAxisTitle = it.key();
        const QVector<WeatherData>& weatherData = it.value();

        // Create a line series for this data type
        QLineSeries* series = new QLineSeries();
        series->setName(yAxisTitle);

        // Add all data points to the series
        for (const auto& data : weatherData) {
            series->append(data.timestamp.toMSecsSinceEpoch(), data.value);
        }

        // Customize line appearance with unique color
        QPen pen = series->pen();
        pen.setWidth(4); // Set the desired line thickness
        pen.setColor(colors[counter]);
        series->setPen(pen);

        // Add the series to the chart and attach to X-axis
        chart->addSeries(series);
        series->attachAxis(axisX);

        // Create a dedicated Y-axis for this series
        QValueAxis* axisY = new QValueAxis();
        axisY->setTitleText(yAxisTitle);
        QFont yAxisFont = axisY->labelsFont();
        yAxisFont.setPointSize(8); // Make font smaller
        axisY->setLabelsFont(yAxisFont);
        axisY->setLabelFormat("%.1f");
        chart->addAxis(axisY, Qt::AlignLeft);
        series->attachAxis(axisY);
    }

    // Customize chart title font
    QFont titleFont = chart->titleFont();
    titleFont.setPointSize(10); // Make font smaller
    chart->setTitleFont(titleFont);

    // Update chart view with anti-aliasing
    chartview->setChart(chart);
    chartview->setRenderHint(QPainter::Antialiasing);
}

// Helper function to remove all axes from the chart
void ChartContainer::removeAllAxes() {
    // Get a list of all axes associated with the chart
    QList<QAbstractAxis*> axes = chart->axes();

    // Loop through each axis and remove it
    for (QAbstractAxis* axis : axes) {
        chart->removeAxis(axis);
    }
}
