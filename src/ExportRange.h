#pragma once
#include <QtGlobal>

// Out is exclusive. Zero/zero is allowed only for an empty sequence.
struct ExportRange {
    bool enabled = false;
    qint64 inMs = 0;
    qint64 outMs = 0;
    bool valid(qint64 duration) const {
        return inMs >= 0 && outMs >= inMs && outMs <= duration
            && (!enabled || (duration == 0 ? inMs == 0 && outMs == 0 : outMs > inMs));
    }
};
