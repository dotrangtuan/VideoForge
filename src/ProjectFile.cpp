#include "ProjectFile.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QSet>
#include <QDateTime>
#include <cmath>
#include <algorithm>

namespace {
constexpr qint64 maxTime=86400000; // Bound imported state before widget geometry/math.
bool fail(QString *error,const QString &message) { if(error) *error=message;return false; }
QString resolve(const QString &base,const QString &path) {
    return QDir::cleanPath(QFileInfo(path).isAbsolute() ? path : QDir(base).absoluteFilePath(path));
}
bool number(const QJsonObject &o,const char *key,double min,double max,double &v) {
    auto value=o.value(QLatin1String(key));
    if(!value.isDouble()) return false;
    v=value.toDouble();return std::isfinite(v) && v>=min && v<=max;
}
bool integer(const QJsonObject &o,const char *key,qint64 min,qint64 max,qint64 &v) {
    double n=0;if(!number(o,key,double(min),double(max),n) || std::floor(n)!=n)return false;
    v=qint64(n);return true;
}
}
QJsonObject ProjectFile::toJson(const ProjectState &state,const QString &baseDirectory) {
    QDir base(baseDirectory);
    auto relative=[&](const QString &p) {return base.relativeFilePath(QFileInfo(p).absoluteFilePath());};
    QJsonArray clips, media, tracks;
    for(const auto &c:state.clips) {
        clips.append(QJsonObject{
        {"id",c.id},{"name",c.name},{"path",relative(c.filePath)},{"track",c.track},
        {"startMs",c.startMs},{"sourceDurationMs",c.sourceDurationMs},{"inMs",c.sourceInMs},{"outMs",c.sourceOutMs},
        {"image",c.image},{"audioOnly",c.audioOnly},{"muted",c.muted},{"role",c.role},{"gain",c.gain},{"fadeInMs",c.fadeInMs},{"fadeOutMs",c.fadeOutMs},{"effects",VideoEffects::toJson(c.effects)}});
    }
    for(const auto &m:state.media) media.append(QJsonObject{{"path",relative(m.path)},{"durationMs",m.durationMs}});
    for(int t=0;t<5;++t) tracks.append(QJsonObject{{"visible",state.visible[t]},{"muted",state.muted[t]}});
    QJsonObject root{{"format","VideoForgeProject"},{"version",4},{"clips",clips},{"media",media},{"tracks",tracks},
        {"positionMs",state.positionMs},{"zoom",state.zoom},{"snapping",state.snapping},
        {"recoveryOrigin",state.recoveryOrigin},{"savedUtc",QDateTime::currentDateTimeUtc().toString(Qt::ISODate)}};
    root["sequence"]=QJsonObject{{"width",state.sequence.width},{"height",state.sequence.height},{"fpsNum",state.sequence.fpsNum},{"fpsDen",state.sequence.fpsDen}};
    root["backgroundMode"] = state.backgroundMode;
    root["exportRange"] = QJsonObject{{"enabled",state.exportRange.enabled},{"inMs",state.exportRange.inMs},{"outMs",state.exportRange.outMs}};
    return root;
}
bool ProjectFile::save(const QString &path,const ProjectState &state,QString *error) {
    qint64 duration=0;for(const auto &c:state.clips)duration=qMax(duration,c.startMs+c.durationMs());
    if(!state.exportRange.valid(duration))return fail(error,"Invalid export In/Out range.");
    if(!state.sequence.valid())return fail(error,"Invalid sequence settings.");
    for(const auto &c:state.clips)if(!VideoEffects::valid(c.effects) || (c.audioOnly && !c.effects.isEmpty()))return fail(error,"Invalid video effects.");
    const auto root=toJson(state,QFileInfo(path).absolutePath());
    QSaveFile file(path); // Atomic replace; an interrupted save leaves the old file intact.
    if(!file.open(QIODevice::WriteOnly)) return fail(error,file.errorString());
    const auto bytes=QJsonDocument(root).toJson();
    if(bytes.size()>16*1024*1024)return fail(error,"Project is too large (maximum 16 MB).");
    if(file.write(bytes)!=bytes.size() || !file.commit()) return fail(error,file.errorString());
    return true;
}
bool ProjectFile::load(const QString &path,ProjectState &state,QString *error) {
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))return fail(error,file.errorString());
    if(file.size()>16*1024*1024)return fail(error,"Project is too large (maximum 16 MB).");
    QJsonParseError parse;
    auto doc=QJsonDocument::fromJson(file.readAll(),&parse);
    if(parse.error!=QJsonParseError::NoError || !doc.isObject())return fail(error,"Invalid project JSON: "+parse.errorString());
    return fromJson(doc.object(),QFileInfo(path).absolutePath(),state,error);
}
bool ProjectFile::fromJson(const QJsonObject &root,const QString &base,ProjectState &state,QString *error) {
    qint64 version;
    if(root["format"]!="VideoForgeProject" || !integer(root,"version",1,4,version))return fail(error,"Unsupported project format/version.");
    if(!root["clips"].isArray() || !root["media"].isArray() || !root["tracks"].isArray())return fail(error,"Missing project collections.");
    ProjectState candidate;
    if(version>=2) {
        if(!root["sequence"].isObject())return fail(error,"Missing sequence settings.");
        auto sequence=root["sequence"].toObject();qint64 w,h,n,d;
        if(!integer(sequence,"width",64,4096,w) || !integer(sequence,"height",64,4096,h)
            || !integer(sequence,"fpsNum",1,60000,n) || !integer(sequence,"fpsDen",1,1001,d))return fail(error,"Invalid sequence dimensions/timebase.");
        candidate.sequence={int(w),int(h),int(n),int(d)};
        if(!candidate.sequence.valid())return fail(error,"Use even frame dimensions and a supported timebase.");
    } // Version 1 projects inherit their original 1280x720, 30/1 output.

    const auto array=root["clips"].toArray();
    if(array.size()>10000 || root["media"].toArray().size()>10000)return fail(error,"Project exceeds 10000 clips/media entries.");
    QSet<int> ids;
    for(const auto &value:array) {
        if(!value.isObject())return fail(error,"Invalid clip entry.");
        auto o=value.toObject();TimelineClip c;qint64 id,track;
        if(version>=4) {
            qint64 role;
            if(!integer(o,"role",0,2,role) || !o["muted"].isBool())return fail(error,"Invalid clip role or mute state.");
            c.role=int(role);c.muted=o["muted"].toBool();
        }
        if(!integer(o,"id",1,1000000000,id) || !integer(o,"track",0,4,track)
            || !integer(o,"startMs",0,maxTime,c.startMs) || !integer(o,"sourceDurationMs",120,maxTime,c.sourceDurationMs)
            || !integer(o,"inMs",0,c.sourceDurationMs,c.sourceInMs) || !integer(o,"outMs",0,c.sourceDurationMs,c.sourceOutMs)
            || c.durationMs()<(c.role==TimelineClip::Background?1:120) || c.startMs+c.durationMs()>maxTime
            || !number(o,"gain",0,2,c.gain) || !integer(o,"fadeInMs",0,c.durationMs(),c.fadeInMs)
            || !integer(o,"fadeOutMs",0,c.durationMs(),c.fadeOutMs))return fail(error,"Invalid clip timing, track or audio settings.");
        c.id=int(id);c.track=int(track);
        if(ids.contains(c.id))return fail(error,"Duplicate clip ID.");ids.insert(c.id);
        if(!o["image"].isBool() || !o["audioOnly"].isBool() || !o["path"].isString() || o["path"].toString().isEmpty())return fail(error,"Invalid clip media metadata.");
        c.image=o["image"].toBool();c.audioOnly=o["audioOnly"].toBool();
        if((c.image && c.audioOnly) || (c.image && c.track>2) || (c.audioOnly && c.track<3))return fail(error,"Incompatible clip track.");
        c.filePath=resolve(base,o["path"].toString());c.name=o["name"].toString(QFileInfo(c.filePath).fileName());
        if(version>=3 && (!o["effects"].isArray() || !VideoEffects::fromJson(o["effects"].toArray(),c.effects)
            || (c.audioOnly && !c.effects.isEmpty())))return fail(error,"Invalid video effect stack.");
        candidate.clips<<c;
    }
    auto sorted=candidate.clips;
    std::sort(sorted.begin(),sorted.end(),[](const auto &a,const auto &b) {return a.track==b.track ? a.startMs<b.startMs : a.track<b.track;});
    for(int i=1;i<sorted.size();++i) if(sorted[i].track==sorted[i-1].track && sorted[i].startMs<sorted[i-1].startMs+sorted[i-1].durationMs())return fail(error,"Overlapping clips on one track.");
    for(const auto &v:root["media"].toArray()) {
        const auto o=v.toObject();ProjectMedia m;
        if(!o["path"].isString() || o["path"].toString().isEmpty() || !integer(o,"durationMs",120,maxTime,m.durationMs))return fail(error,"Invalid media entry.");
        m.path=resolve(base,o["path"].toString());candidate.media<<m;
    }
    const auto tracks=root["tracks"].toArray();
    if(tracks.size()!=5)return fail(error,"Expected five tracks.");
    for(int i=0;i<5;++i) {
        auto o=tracks[i].toObject();if(!o["visible"].isBool() || !o["muted"].isBool())return fail(error,"Invalid track settings.");
        candidate.visible[i]=o["visible"].toBool();candidate.muted[i]=o["muted"].toBool();
    }
    qint64 zoom;
    if(!integer(root,"positionMs",0,maxTime,candidate.positionMs) || !integer(root,"zoom",25,220,zoom) || !root["snapping"].isBool())return fail(error,"Invalid timeline view settings.");
    candidate.zoom=int(zoom);candidate.snapping=root["snapping"].toBool();candidate.recoveryOrigin=root["recoveryOrigin"].toString();
    if(version>=4) {
        if(!root["backgroundMode"].isBool() || !root["exportRange"].isObject())return fail(error,"Missing editing mode or export range.");
        candidate.backgroundMode=root["backgroundMode"].toBool();
        const auto range=root["exportRange"].toObject();
        if(!range["enabled"].isBool() || !integer(range,"inMs",0,maxTime,candidate.exportRange.inMs)
            || !integer(range,"outMs",0,maxTime,candidate.exportRange.outMs))return fail(error,"Invalid export In/Out range.");
        candidate.exportRange.enabled=range["enabled"].toBool();
        qint64 duration=0;for(const auto &c:candidate.clips)duration=qMax(duration,c.startMs+c.durationMs());
        if(!candidate.exportRange.valid(duration))return fail(error,"Export range must satisfy 0 <= In < Out <= sequence end.");
    }
    state=candidate;return true; // Never partially mutate the destination on failure.
}
QStringList ProjectFile::missingMedia(const ProjectState &state) {
    QSet<QString> missing;
    for(const auto &m:state.media) if(!QFileInfo(m.path).isFile())missing.insert(m.path);
    for(const auto &c:state.clips) if(!QFileInfo(c.filePath).isFile())missing.insert(c.filePath);
    auto result=missing.values();result.sort();return result;
}
void ProjectFile::relink(ProjectState &state,const QString &oldPath,const QString &newPath,qint64 durationMs) {
    const QString full=QFileInfo(newPath).absoluteFilePath();
    for(auto &m:state.media) if(m.path==oldPath) {m.path=full;m.durationMs=durationMs;}
    for(auto &c:state.clips) if(c.filePath==oldPath) {c.filePath=full;c.sourceDurationMs=durationMs;c.name=QFileInfo(full).fileName();}
}
