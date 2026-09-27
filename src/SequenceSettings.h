#pragma once
#include <QSize>
#include <QString>
#include <array>
#include <algorithm>

struct SequenceRate { int numerator; int denominator; const char *label; };
struct SequenceSettings {
    // Preserve the output of projects from 0.5 and earlier.
    int width=1280;
    int height=720;
    int fpsNum=30;
    int fpsDen=1;
    static const std::array<SequenceRate,8> &rates() {
        static const std::array<SequenceRate,8> values{{
            {24000,1001,"23.976"},{24,1,"24"},{25,1,"25"},{30000,1001,"29.97"},
            {30,1,"30"},{50,1,"50"},{60000,1001,"59.94"},{60,1,"60"}}};
        return values;
    }
    bool valid() const {
        if(width<64 || height<64 || width>4096 || height>4096 || width%2 || height%2)return false;
        for(const auto &rate:rates())if(fpsNum==rate.numerator && fpsDen==rate.denominator)return true;
        return false;
    }
    QString rateString() const {return QString("%1/%2").arg(fpsNum).arg(fpsDen);}
    QString rateLabel() const {
        for(const auto &rate:rates())if(fpsNum==rate.numerator && fpsDen==rate.denominator)return QString::fromLatin1(rate.label);
        return rateString();
    }
    QString description() const {return QString("%1 x %2 | %3 fps").arg(width).arg(height).arg(rateLabel());}
    QSize frameSize() const {return QSize(width,height);}
    QSize monitorSize() const {
        const QSize fit=frameSize().scaled(640,640,Qt::KeepAspectRatio);
        return QSize(qMax(2,fit.width() & ~1),qMax(2,fit.height() & ~1));
    }
    // Use the rational rate for every calculation, never repeatedly add a
    // rounded frame duration (which would accumulate drift at fractional fps).
    qint64 frameToMs(qint64 frame) const {return (qMax<qint64>(0,frame)*1000*fpsDen+fpsNum/2)/fpsNum;}
    qint64 nearestFrame(qint64 ms) const {return (qMax<qint64>(0,ms)*fpsNum+500*fpsDen)/(1000*fpsDen);}
    int nominalFps() const {return (fpsNum+fpsDen/2)/fpsDen;}
    // Non-drop-frame display: frame numbering uses the nominal rate, while
    // frame positions still use the exact rational sequence rate.
    QString timecodeForFrame(qint64 frame) const {
        frame=qMax<qint64>(0,frame);
        const int rate=nominalFps();const qint64 seconds=frame/rate;
        return QString("%1:%2:%3:%4")
            .arg(seconds/3600,2,10,QLatin1Char('0'))
            .arg(seconds/60%60,2,10,QLatin1Char('0'))
            .arg(seconds%60,2,10,QLatin1Char('0'))
            .arg(frame%rate,2,10,QLatin1Char('0'));
    }
    QString timecode(qint64 ms) const {return timecodeForFrame(nearestFrame(ms));}
    qint64 frameAt(qint64 ms) const {return (qMax<qint64>(0,ms)*fpsNum)/(1000*fpsDen);}
    qint64 frameCount(qint64 duration) const {return (qMax<qint64>(0,duration)*fpsNum+1000*fpsDen-1)/(1000*fpsDen);}
    qint64 step(qint64 ms,int direction,qint64 duration) const {
        if(duration<=0)return 0;
        if(ms>=duration)return direction<0 ? qMin(duration-1,frameToMs(frameCount(duration)-1)) : duration;
        qint64 frame=nearestFrame(ms);
        const qint64 boundary=frameToMs(frame);
        if(direction>0 && boundary<=ms)++frame;
        if(direction<0 && boundary>=ms)--frame;
        frame=std::clamp<qint64>(frame,0,qMax<qint64>(0,frameCount(duration)-1));
        return qMin(duration-1,frameToMs(frame));
    }
    bool operator==(const SequenceSettings &other) const {
        return width==other.width && height==other.height && fpsNum==other.fpsNum && fpsDen==other.fpsDen;
    }
};
