#pragma once

#include <QString>

namespace gui {

inline QString fmt_elapsed(qint64 ms)
{
    qint64 s = ms / 1000;
    return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

// Returns "M:SS / ~M:SS" (linear ETA) or "M:SS / …" before any step completes.
inline QString eta_text(qint64 total_ms, int steps_done, int steps_tot)
{
    const QString left = fmt_elapsed(total_ms);
    if (steps_done > 0 && steps_done < steps_tot) {
        const double eta_ms = static_cast<double>(total_ms) / steps_done
                              * (steps_tot - steps_done);
        return left + " / ~" + fmt_elapsed(static_cast<qint64>(eta_ms));
    }
    return left + " / …";
}

} // namespace gui
