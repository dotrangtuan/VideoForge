#include "VideoEffects.h"
#include <QJsonObject>
#include <QPainter>
#include <QStringList>
#include <cmath>
#include <algorithm>

VideoEffect VideoEffect::make(Type type) {
    VideoEffect effect;effect.type=type;
    if(type==Blur)effect.value[0]=1;
    if(type==Opacity || type==Zoom)effect.value[0]=100;
    return effect;
}
QString VideoEffect::name() const {
    switch(type){case Crop:return "Crop";case Blur:return "Blur";case Opacity:return "Opacity";case Zoom:return "Zoom";}
    return {};
}
bool VideoEffect::valid() const {
    for(double v:value)if(!std::isfinite(v) || v<0)return false;
    if(type==Crop)return value[0]+value[2]<=99 && value[1]+value[3]<=99;
    if(value[1]!=0 || value[2]!=0 || value[3]!=0)return false;
    switch(type){case Blur:return value[0]<=5;case Opacity:return value[0]<=100;case Zoom:return value[0]>=10 && value[0]<=400;default:return false;}
}
namespace {
const QStringList typeNames{"crop","blur","opacity","zoom"};
// All geometry is rounded once from normalized values at the target resolution.
QRect cropRect(QSize size,const VideoEffect &effect) {
    const auto &v=effect.value;
    int left=qRound(size.width()*v[0]/100),right=qRound(size.width()*v[2]/100);
    int top=qRound(size.height()*v[1]/100),bottom=qRound(size.height()*v[3]/100);
    left=qMin(left,size.width()-1);top=qMin(top,size.height()-1);
    return QRect(left,top,qMax(1,size.width()-left-right),qMax(1,size.height()-top-bottom));
}
QSize zoomSize(QSize size,double scale) {
    // For enlargement crop first, avoiding huge intermediate images at 4K.
    const double factor=scale>=100 ? 100/scale : scale/100;
    return QSize(qMax(1,qRound(size.width()*factor)),qMax(1,qRound(size.height()*factor)));
}
int radius(QSize size,double amount) {return qRound(qMin(size.width(),size.height())*amount/100);}
int reflect(int position,int length) {
    if(position<0)return -position-1;
    if(position>=length)return 2*length-position-1;
    return position;
}
QImage boxBlur(const QImage &source,int radius) {
    if(radius==0)return source;
    // Two sliding-window passes in premultiplied RGBA preserve transparent edges.
    QImage input=source.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int width=input.width(),height=input.height(),count=2*radius+1;
    QImage temp(input.size(),input.format()),result(input.size(),input.format());
    for(int pass=0;pass<2;++pass) {
        const QImage &src=pass==0 ? input : temp;QImage &dst=pass==0 ? temp : result;
        const int lines=pass==0 ? height : width,length=pass==0 ? width : height;
        for(int line=0;line<lines;++line) {
            auto pixel=[&](int pos){int x=pass==0?reflect(pos,length):line,y=pass==0?line:reflect(pos,length);return reinterpret_cast<const QRgb*>(src.constScanLine(y))[x];};
            int sum[4]={0,0,0,0};
            auto add=[&](QRgb p,int sign){sum[0]+=sign*qRed(p);sum[1]+=sign*qGreen(p);sum[2]+=sign*qBlue(p);sum[3]+=sign*qAlpha(p);};
            for(int pos=-radius;pos<=radius;++pos)add(pixel(pos),1);
            for(int pos=0;pos<length;++pos) {
                int x=pass==0?pos:line,y=pass==0?line:pos;
                reinterpret_cast<QRgb*>(dst.scanLine(y))[x]=qRgba((sum[0]+count/2)/count,(sum[1]+count/2)/count,(sum[2]+count/2)/count,(sum[3]+count/2)/count);
                add(pixel(pos-radius),-1);add(pixel(pos+radius+1),1);
            }
        }
    }
    return result;
}
}
bool VideoEffects::valid(const QVector<VideoEffect> &effects) {
    if(effects.size()>maximumCount)return false;
    for(const auto &effect:effects)if(!effect.valid())return false;
    return true;
}
QJsonArray VideoEffects::toJson(const QVector<VideoEffect> &effects) {
    QJsonArray result;
    for(const auto &effect:effects) {
        QJsonArray values;for(double v:effect.value)values.append(v);
        result.append(QJsonObject{{"type",typeNames[int(effect.type)]},{"enabled",effect.enabled},{"values",values}});
    }
    return result;
}
bool VideoEffects::fromJson(const QJsonArray &json,QVector<VideoEffect> &effects) {
    if(json.size()>maximumCount)return false;
    QVector<VideoEffect> candidate;
    for(const auto &entry:json) {
        if(!entry.isObject())return false;
        const auto object=entry.toObject();const int type=typeNames.indexOf(object["type"].toString());
        if(type<0 || !object["enabled"].isBool() || !object["values"].isArray())return false;
        const auto values=object["values"].toArray();if(values.size()!=4)return false;
        VideoEffect effect;effect.type=VideoEffect::Type(type);effect.enabled=object["enabled"].toBool();
        for(int i=0;i<4;++i){if(!values[i].isDouble())return false;effect.value[i]=values[i].toDouble();}
        if(!effect.valid())return false;candidate.append(effect);
    }
    effects=candidate;return true;
}
QImage VideoEffects::render(const QImage &source,QSize canvas,const QVector<VideoEffect> &effects) {
    if(!canvas.isValid() || source.isNull() || !valid(effects))return {};
    QImage image(canvas,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
    const QSize fit=source.size().scaled(canvas,Qt::KeepAspectRatio);
    {QPainter painter(&image);painter.setRenderHint(QPainter::SmoothPixmapTransform);painter.drawImage(QRect(QPoint((canvas.width()-fit.width())/2,(canvas.height()-fit.height())/2),fit),source);}
    for(const auto &effect:effects) {
        if(!effect.enabled)continue;
        if(effect.type==VideoEffect::Crop) {
            const QRect keep=cropRect(canvas,effect);
            QImage cropped(canvas,image.format());cropped.fill(Qt::transparent);
            {QPainter painter(&cropped);painter.drawImage(keep.topLeft(),image.copy(keep));}
            image=cropped;
        } else if(effect.type==VideoEffect::Opacity) {
            const double opacity=effect.value[0]/100;
            for(int y=0;y<image.height();++y) {
                auto line=reinterpret_cast<QRgb*>(image.scanLine(y));
                for(int x=0;x<image.width();++x){const QRgb p=line[x];line[x]=qRgba(qRound(qRed(p)*opacity),qRound(qGreen(p)*opacity),qRound(qBlue(p)*opacity),qRound(qAlpha(p)*opacity));}
            }
        } else if(effect.type==VideoEffect::Blur) image=boxBlur(image,radius(canvas,effect.value[0]));
        else if(effect.type==VideoEffect::Zoom && effect.value[0]!=100) {
            const QSize size=zoomSize(canvas,effect.value[0]);
            if(effect.value[0]>100)image=image.copy(QRect(QPoint((canvas.width()-size.width())/2,(canvas.height()-size.height())/2),size)).scaled(canvas,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
            else {
                QImage scaled(canvas,image.format());scaled.fill(Qt::transparent);
                {QPainter painter(&scaled);painter.drawImage(QPoint((canvas.width()-size.width())/2,(canvas.height()-size.height())/2),image.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation));}
                image=scaled;
            }
        }
    }
    return image;
}
QString VideoEffects::filters(QSize canvas,const QVector<VideoEffect> &effects) {
    QString result;
    for(const auto &effect:effects) {
        if(!effect.enabled)continue;
        if(effect.type==VideoEffect::Crop) {
            const QRect r=cropRect(canvas,effect);
            if(r==QRect(QPoint(0,0),canvas))continue;
            result+=QString(",format=rgba,crop=%1:%2:%3:%4:exact=1,pad=%5:%6:%3:%4:color=black@0")
                .arg(r.width()).arg(r.height()).arg(r.x()).arg(r.y()).arg(canvas.width()).arg(canvas.height());
        } else if(effect.type==VideoEffect::Opacity && effect.value[0]!=100.0)result+=",format=rgba,lut=a='val*"+QString::number(effect.value[0]/100,'f',8)+"'";
        else if(effect.type==VideoEffect::Blur) {
            const int r=radius(canvas,effect.value[0]);
            if(r>0)result+=QString(",format=gbrap,premultiply=inplace=1,boxblur=%1:1:%1:1:%1:1,unpremultiply=inplace=1,format=rgba").arg(r);
        } else if(effect.type==VideoEffect::Zoom && effect.value[0]!=100) {
            const QSize size=zoomSize(canvas,effect.value[0]);
            if(effect.value[0]>100)result+=QString(",format=rgba,crop=%1:%2:%3:%4:exact=1,format=gbrap,premultiply=inplace=1,scale=%5:%6:flags=bilinear,unpremultiply=inplace=1,format=rgba")
                .arg(size.width()).arg(size.height()).arg((canvas.width()-size.width())/2).arg((canvas.height()-size.height())/2).arg(canvas.width()).arg(canvas.height());
            else result+=QString(",format=gbrap,premultiply=inplace=1,scale=%1:%2:flags=bilinear,unpremultiply=inplace=1,format=rgba,pad=%3:%4:%5:%6:color=black@0")
                .arg(size.width()).arg(size.height()).arg(canvas.width()).arg(canvas.height()).arg((canvas.width()-size.width())/2).arg((canvas.height()-size.height())/2);
        }
    }
    return result;
}
