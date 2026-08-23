#ifndef WILDPALMS_RUNTIME_PALMRUNRESULT_H
#define WILDPALMS_RUNTIME_PALMRUNRESULT_H

#include <QHash>
#include <QString>
#include <QStringList>
#include <QDateTime>

namespace WildPalms::Runtime {

struct PalmRunResult {
    struct PluginStats {
        int created   = 0;
        int updated   = 0;
        int deleted   = 0;
        int unchanged = 0;
        int conflicts = 0;
        int errors    = 0;
    };

    bool                          success = true;
    // Shakedown F13: a user-cancelled run is not an error. UI consumers
    // use this to pick a neutral tone instead of "finished with errors".
    bool                          cancelled = false;
    QString                       errorMessage;
    QStringList                   logLines;
    QHash<QString, PluginStats>   perPluginStats;
    QDateTime                     startTime;
    QDateTime                     endTime;

    qint64 durationMs() const {
        if (!startTime.isValid() || !endTime.isValid()) return 0;
        return startTime.msecsTo(endTime);
    }
};

}  // namespace WildPalms::Runtime

Q_DECLARE_METATYPE(WildPalms::Runtime::PalmRunResult)

#endif
