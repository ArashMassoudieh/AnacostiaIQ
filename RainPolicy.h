/////////////////////////////////////////////////////////////
// RAINPOLICY.H - "Is rain coming?" decision, as a pure function
//
//  Split out of AnacostiaIQ so the rule that drives adaptive polling
//  can be reasoned about and tested on its own, with no GUI, no
//  network, and no wall clock.
//
//  The three outcomes are kept distinct on purpose. "Dry" and "I have
//  no usable forecast" both leave the probability unknown-or-zero, but
//  they must not lead to the same action: we only slow the sensors
//  down when we positively know it's dry.
/////////////////////////////////////////////////////////////

#ifndef RAINPOLICY_H
#define RAINPOLICY_H

#include "WeatherFetcher.h"   // WeatherData
#include <QVector>
#include <QDateTime>

namespace RainPolicy {

enum class Decision {
    Rain,            // a point in the window exceeds the threshold
    Dry,             // the window has points, none above the threshold
    NoDataInWindow   // empty forecast, or nothing lands in the window
};

// Forecast points stamped up to this long before 'now' still count:
// NOAA reports multi-hour bins stamped at the bin start, so the bin
// covering 'now' can carry an older timestamp.
constexpr int kStaleBinToleranceHours = 6;

// 'now' is injected rather than read from the system clock, so the
// decision is deterministic and testable.
Decision evaluate(const QVector<WeatherData> &precipProbability,
                  const QDateTime &now,
                  int lookaheadHours,
                  double thresholdPercent);

// For logs and the dashboard.
const char *toString(Decision d);

} // namespace RainPolicy

#endif // RAINPOLICY_H
