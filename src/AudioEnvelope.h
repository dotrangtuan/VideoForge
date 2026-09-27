#pragma once
#include <QtGlobal>
#include <algorithm>
inline double clipAudioGain(double gain,qint64 fadeIn,qint64 fadeOut,qint64 duration,qint64 local) {
    if(local<0 || local>=duration) return 0;
    double envelope=gain;
    if(fadeIn>0) envelope*=std::clamp(double(local)/double(fadeIn),0.0,1.0);
    if(fadeOut>0) envelope*=std::clamp(double(duration-local)/double(fadeOut),0.0,1.0);
    return envelope;
}
