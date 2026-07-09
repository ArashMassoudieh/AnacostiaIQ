/////////////////////////////////////////////////////////////
// RAINPOLICY.CPP - "Is rain coming?" decision, as a pure function
/////////////////////////////////////////////////////////////

#include "RainPolicy.h"
#include <algorithm>

namespace RainPolicy {

Decision evaluate(const QVector<WeatherData> &precipProbability,
                  const QDateTime &now,
                  int lookaheadHours,
                  double thresholdPercent)
{
    if (lookaheadHours < 1)
        lookaheadHours = 1;

    const QDateTime horizon =
        now.addSecs(static_cast<qint64>(lookaheadHours) * 3600);
    const QDateTime floor =
        now.addSecs(-static_cast<qint64>(kStaleBinToleranceHours) * 3600);

    // Points outside [floor, horizon] are ignored: a high probability
    // from this morning shouldn't pin us to high frequency all day,
    // and one a week out shouldn't either.
    double maxProb = -1.0;
    for (const WeatherData &d : precipProbability) {
        if (d.timestamp < floor || d.timestamp > horizon)
            continue;
        maxProb = std::max(maxProb, d.value);
    }

    if (maxProb < 0.0)
        return Decision::NoDataInWindow;

    return (maxProb > thresholdPercent) ? Decision::Rain : Decision::Dry;
}

const char *toString(Decision d)
{
    switch (d) {
    case Decision::Rain:           return "rain expected";
    case Decision::Dry:            return "dry";
    case Decision::NoDataInWindow: return "no usable forecast";
    }
    return "unknown";
}

} // namespace RainPolicy
