#pragma once
#include <QImage>
#include <QJsonArray>
#include <QString>
#include <QVector>
#include <array>

struct VideoEffect {
    enum Type { Crop, Blur, Opacity, Zoom };
    Type type=Crop;
    bool enabled=true;
    // Crop: left/top/right/bottom percentages. Other effects use value[0].
    std::array<double,4> value{{0,0,0,0}};
    static VideoEffect make(Type type);
    QString name() const;
    bool valid() const;
    bool operator==(const VideoEffect &other) const {return type==other.type && enabled==other.enabled && value==other.value;}
};
namespace VideoEffects {
constexpr int maximumCount=16;
bool valid(const QVector<VideoEffect> &effects);
QJsonArray toJson(const QVector<VideoEffect> &effects);
bool fromJson(const QJsonArray &json,QVector<VideoEffect> &effects);
// Fit the source onto a transparent sequence canvas, then run the ordered stack.
QImage render(const QImage &source,QSize canvas,const QVector<VideoEffect> &effects);
// A comma-prefixed chain applied after fitting onto the same sequence canvas.
QString filters(QSize canvas,const QVector<VideoEffect> &effects);
}
