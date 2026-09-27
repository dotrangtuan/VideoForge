#pragma once
#include "TimelineClip.h"
#include <QJsonObject>
#include <array>
#include "SequenceSettings.h"
#include "ExportRange.h"

struct ProjectMedia { QString path; qint64 durationMs=0; };
struct ProjectState {
    bool backgroundMode = false;
    ExportRange exportRange;
    SequenceSettings sequence;
    QVector<TimelineClip> clips;
    QVector<ProjectMedia> media;
    std::array<bool,5> visible{true,true,true,true,true};
    std::array<bool,5> muted{false,false,false,false,false};
    qint64 positionMs=0;
    int zoom=25;
    bool snapping=true;
    QString recoveryOrigin;
};
namespace ProjectFile {
    QJsonObject toJson(const ProjectState &state,const QString &baseDirectory);
    bool fromJson(const QJsonObject &root,const QString &baseDirectory,ProjectState &state,QString *error=nullptr);
    bool save(const QString &path,const ProjectState &state,QString *error=nullptr);
    bool load(const QString &path,ProjectState &state,QString *error=nullptr);
    QStringList missingMedia(const ProjectState &state);
    void relink(ProjectState &state,const QString &oldPath,const QString &newPath,qint64 durationMs);
}
