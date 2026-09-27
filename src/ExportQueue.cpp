#include "ExportQueue.h"
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStandardPaths>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <QRegularExpression>
#include <algorithm>

namespace {
bool fail(QString *error,const QString &message){if(error)*error=message;return false;}
QString pathKey(const QString &path) {
    QFileInfo info(path);QString result=info.canonicalFilePath();
    if(result.isEmpty()){auto directory=info.absoluteDir();QString parent=directory.canonicalPath();result=QDir(parent.isEmpty()?directory.absolutePath():parent).filePath(info.fileName());}
    result=QDir::cleanPath(result);
#ifdef Q_OS_WIN
    result=result.toCaseFolded();
#endif
    return result;
}
}
QString ExportQueue::defaultStorePath() {
    QString folder=qEnvironmentVariable("VIDEOFORGE_QUEUE_DIR");
    if(folder.isEmpty())folder=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/export-queue";
    return QDir(folder).absoluteFilePath("queue.json");
}
QString ExportQueue::statusName(ExportJob::Status status) {
    switch(status){case ExportJob::Pending:return "Pending";case ExportJob::Running:return "Running";case ExportJob::Completed:return "Completed";case ExportJob::Failed:return "Failed";case ExportJob::Cancelled:return "Cancelled";case ExportJob::Interrupted:return "Interrupted";}
    return "Unknown";
}
ExportQueue::ExportQueue(const QString &storePath,QObject *parent):QObject(parent),m_path(QFileInfo(storePath).absoluteFilePath()),m_exporter(this) {
    if(!QDir().mkpath(QFileInfo(m_path).absolutePath())){m_error="Cannot create the export queue folder.";return;}
    m_lock=std::make_unique<QLockFile>(m_path+".lock");m_lock->setStaleLockTime(0);
    if(!m_lock->tryLock()){m_error="Another editor owns this export queue. Close it and reopen this editor.";return;}
    if(!load(&m_error))return; // Never overwrite an unreadable queue with an empty one.
    m_ready=true;
    connect(&m_exporter,&FFmpegExporter::finished,this,&ExportQueue::finished);
    connect(&m_exporter,&FFmpegExporter::logMessage,this,[this](const QString &text){if(m_active<0)return;auto &job=m_jobs[m_active];job.log=(job.log+text+"\n").right(16384);});
    connect(&m_exporter,&FFmpegExporter::progressChanged,this,[this](int percent,qint64,qint64,double speed,qint64 elapsed,qint64 remaining){
        if(m_active<0)return;auto &job=m_jobs[m_active];job.percent=percent;job.speed=speed;job.elapsedMs=elapsed;job.remainingMs=remaining;emit changed();
    });
}
ExportQueue::~ExportQueue(){shutdown();m_exporter.disconnect();}
QVector<FFmpegExporter::ClipSegment> ExportQueue::segmentsFor(const ProjectState &state) {
    QVector<FFmpegExporter::ClipSegment> result;
    for(const auto &clip:state.clips) {
        FFmpegExporter::ClipSegment segment;segment.inputPath=clip.filePath;segment.effects=clip.effects;
        segment.gain=clip.gain;segment.fadeInMs=clip.fadeInMs;segment.fadeOutMs=clip.fadeOutMs;
        segment.track=clip.track;segment.startMs=clip.startMs;segment.image=clip.image;
        segment.inMs=clip.sourceInMs;segment.outMs=clip.sourceOutMs;
        segment.visible=state.visible[clip.track];segment.muted=state.muted[clip.track] || clip.muted;result.append(segment);
    }
    return result;
}
bool ExportQueue::save(const QVector<ExportJob> &jobs,QString *error) const {
    QJsonArray entries;
    for(const auto &job:jobs)entries.append(QJsonObject{{"id",job.id},{"name",job.name},{"output",job.outputPath},
        {"hardware",job.hardware},{"status",int(job.status)},{"percent",job.percent},{"message",job.message},{"log",job.log},
        {"snapshot",ProjectFile::toJson(job.snapshot,QFileInfo(m_path).absolutePath())}});
    auto bytes=QJsonDocument(QJsonObject{{"format","VideoForgeExportQueue"},{"version",1},{"jobs",entries}}).toJson(QJsonDocument::Compact);
    if(bytes.size()>128*1024*1024)return fail(error,"Queue exceeds 128 MB. Remove old jobs before adding more.");
    QSaveFile file(m_path);if(!file.open(QIODevice::WriteOnly))return fail(error,file.errorString());
    if(file.write(bytes)!=bytes.size() || !file.commit())return fail(error,file.errorString());return true;
}
bool ExportQueue::load(QString *error) {
    if(!QFileInfo::exists(m_path))return true;
    QFile file(m_path);if(!file.open(QIODevice::ReadOnly))return fail(error,file.errorString());
    if(file.size()>128*1024*1024)return fail(error,"Queue file exceeds 128 MB.");
    QJsonParseError parse;auto document=QJsonDocument::fromJson(file.readAll(),&parse);
    auto root=document.object();
    if(parse.error!=QJsonParseError::NoError || root["format"]!="VideoForgeExportQueue" || root["version"]!=1 || !root["jobs"].isArray())return fail(error,"Invalid queue file. Preserve it, move it aside and restart to create a new queue.");
    const auto entries=root["jobs"].toArray();if(entries.size()>100)return fail(error,"Queue exceeds 100 jobs.");
    QVector<ExportJob> candidate;QSet<QString> ids,outputs;
    for(const auto &entry:entries) {
        auto object=entry.toObject();ExportJob job;job.id=object["id"].toString();job.name=object["name"].toString();job.outputPath=object["output"].toString();
        const auto status=object["status"],percent=object["percent"];
        if(QUuid(job.id).isNull() || ids.contains(job.id) || job.name.size()>256 || !QFileInfo(job.outputPath).isAbsolute()
            || !job.outputPath.endsWith(".mp4",Qt::CaseInsensitive) || outputs.contains(pathKey(job.outputPath))
            || !object["hardware"].isBool() || !status.isDouble() || status.toDouble()!=status.toInt(-1) || status.toInt(-1)<0 || status.toInt()>5
            || !percent.isDouble() || percent.toDouble()!=percent.toInt(-1) || percent.toInt(-1)<0 || percent.toInt()>100
            || !object["snapshot"].isObject())return fail(error,"Invalid job in export queue.");
        if(!ProjectFile::fromJson(object["snapshot"].toObject(),QFileInfo(m_path).absolutePath(),job.snapshot,error) || job.snapshot.clips.isEmpty())return fail(error,"Invalid job snapshot.");
        job.hardware=object["hardware"].toBool();job.status=ExportJob::Status(status.toInt());job.percent=percent.toInt();
        job.message=object["message"].toString().left(4096);job.log=object["log"].toString().right(16384);
        if(job.status==ExportJob::Running){job.status=ExportJob::Interrupted;job.percent=0;job.message="Previous export was interrupted. Retry explicitly to restart from the beginning.";}
        ids.insert(job.id);outputs.insert(pathKey(job.outputPath));candidate.append(job);
    }
    m_jobs=candidate;return true;
}
bool ExportQueue::commit(const QVector<ExportJob> &jobs,QString *error) {
    if(!m_ready || m_shutdown)return fail(error,m_error.isEmpty()?"Export queue is unavailable.":m_error);
    QString issue;if(!save(jobs,&issue)){m_error="Cannot save queue: "+issue;emit changed();return fail(error,m_error);}
    m_jobs=jobs;m_error.clear();emit changed();return true;
}
bool ExportQueue::persist(){QString issue;if(save(m_jobs,&issue)){m_error.clear();return true;}m_error="Queue stopped: cannot save state: "+issue;m_running=false;return false;}
bool ExportQueue::outputValid(const QString &path,const ProjectState &state,int except,QString *error) const {
    if(path.trimmed().isEmpty() || !path.endsWith(".mp4",Qt::CaseInsensitive))return fail(error,"Choose an MP4 output filename.");
    QFileInfo info(path);if(!info.absoluteDir().exists())return fail(error,"Output folder does not exist.");
    if(info.exists() || info.isSymLink())return fail(error,"Output already exists. Choose another filename; queued exports never overwrite existing files.");
    const auto key=pathKey(path);
    auto uses=[&](const ProjectState &snapshot){for(const auto &clip:snapshot.clips)if(pathKey(clip.filePath)==key)return true;for(const auto &media:snapshot.media)if(pathKey(media.path)==key)return true;return false;};
    if(uses(state))return fail(error,"Output cannot replace source media.");
    for(int i=0;i<m_jobs.size();++i){if(i!=except && pathKey(m_jobs[i].outputPath)==key)return fail(error,"Another job already uses this output filename.");if(uses(m_jobs[i].snapshot))return fail(error,"Output is referenced as media by another queued job.");}
    return true;
}
bool ExportQueue::add(const ProjectState &snapshot,const QString &name,const QString &output,bool hardware,QString *error) {
    if(m_jobs.size()>=100)return fail(error,"Queue is limited to 100 jobs. Remove old jobs first.");
    if(snapshot.clips.isEmpty() || !snapshot.sequence.valid())return fail(error,"Add clips and valid sequence settings first.");
    for(const auto &clip:snapshot.clips)if(!VideoEffects::valid(clip.effects))return fail(error,"Invalid effect stack.");
    // Validate and normalize a private copy using the same project schema.
    ProjectState copy;if(!ProjectFile::fromJson(ProjectFile::toJson(snapshot,QDir::currentPath()),QDir::currentPath(),copy,error))return false;
    const auto full=QFileInfo(output).absoluteFilePath();if(!outputValid(full,copy,-1,error))return false;
    for(const auto &clip:copy.clips)if(!QFileInfo(clip.filePath).isFile())return fail(error,"Missing source: "+clip.filePath);
    ExportJob job;job.id=QUuid::createUuid().toString(QUuid::WithoutBraces);job.name=name.left(256);job.outputPath=full;job.snapshot=copy;job.hardware=hardware;
    auto candidate=m_jobs;candidate.append(job);return commit(candidate,error);
}
QVector<TimelineClip> ExportQueue::v2Videos(const ProjectState &snapshot) {
    QVector<TimelineClip> clips;
    for(const auto &clip:snapshot.clips)if(clip.track==1 && !clip.image && !clip.audioOnly)clips.append(clip);
    std::stable_sort(clips.begin(),clips.end(),[](const auto &a,const auto &b){return a.startMs<b.startMs;});
    return clips;
}
bool ExportQueue::addV2Clips(const ProjectState &snapshot,int count,const QString &directory,bool hardware,QString *error) {
    if(!m_ready || m_shutdown)return fail(error,"Export queue is unavailable.");
    const int total=v2Videos(snapshot).size();
    if(count==-1)count=total;
    if(count<1 || count>total)return fail(error,"Choose between 1 and the number of video clips on V2.");
    if(count>100-m_jobs.size())return fail(error,QString("Queue has room for %1 more job(s). Remove old jobs or choose fewer videos.").arg(100-m_jobs.size()));
    if(directory.trimmed().isEmpty() || !QFileInfo(directory).isDir())return fail(error,"Choose an existing output folder.");
    ProjectState copy=snapshot;copy.exportRange={}; // Each V2 clip supplies its own In/Out.
    if(!ProjectFile::fromJson(ProjectFile::toJson(copy,QDir::currentPath()),QDir::currentPath(),copy,error))return false;
    QSet<QString> reserved;
    for(const auto &clip:copy.clips) {
        if(!QFileInfo(clip.filePath).isFile())return fail(error,"Missing source: "+clip.filePath);
        reserved.insert(pathKey(clip.filePath).toCaseFolded());
    }
    for(const auto &media:copy.media)reserved.insert(pathKey(media.path).toCaseFolded());
    for(const auto &job:m_jobs) {
        reserved.insert(pathKey(job.outputPath).toCaseFolded());
        for(const auto &clip:job.snapshot.clips)reserved.insert(pathKey(clip.filePath).toCaseFolded());
        for(const auto &media:job.snapshot.media)reserved.insert(pathKey(media.path).toCaseFolded());
    }
    auto candidate=m_jobs;const auto videos=v2Videos(copy);const QDir folder(QFileInfo(directory).absoluteFilePath());
    for(int i=0;i<count;++i) {
        const auto &clip=videos[i];
        if(copy.sequence.nearestFrame(clip.startMs+clip.durationMs())<=copy.sequence.nearestFrame(clip.startMs))
            return fail(error,"A V2 clip is shorter than one output frame.");
        QString name=clip.name.trimmed();if(name.isEmpty())name=QFileInfo(clip.filePath).fileName();
        QString stem=name;
        if(QStringList{"mp4","mov","m4v","mkv","avi","webm"}.contains(QFileInfo(stem).suffix().toLower()))stem.chop(QFileInfo(stem).suffix().size()+1);
        stem.replace(QRegularExpression("[<>:\"/\\\\|?*\\x00-\\x1f]"),"_");stem=stem.left(120).trimmed();
        while(stem.endsWith('.') || stem.endsWith(' '))stem.chop(1);
        if(stem.isEmpty())stem=QString("Video_%1").arg(i+1);
        if(QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)",QRegularExpression::CaseInsensitiveOption).match(stem).hasMatch())stem.prepend('_');
        QString output;int suffix=1;
        do {
            output=folder.absoluteFilePath(stem+(suffix==1?QString():QString(" (%1)").arg(suffix))+".mp4");++suffix;
        } while(QFileInfo::exists(output) || QFileInfo(output).isSymLink() || reserved.contains(pathKey(output).toCaseFolded()));
        if(!outputValid(output,copy,-1,error))return false;
        ExportJob job;job.id=QUuid::createUuid().toString(QUuid::WithoutBraces);job.name=name.left(256);job.outputPath=output;job.hardware=hardware;
        job.snapshot=copy;job.snapshot.exportRange={true,clip.startMs,clip.startMs+clip.durationMs()};
        candidate.append(job);reserved.insert(pathKey(output).toCaseFolded());
    }
    return commit(candidate,error);
}
bool ExportQueue::start(QString *error) {
    if(!m_ready || m_shutdown)return fail(error,m_error.isEmpty()?"Queue unavailable.":m_error);
    if(busy())return fail(error,"Queue is already running or stopping.");
    bool pending=false;for(const auto &job:m_jobs)pending|=job.status==ExportJob::Pending;
    if(!pending)return fail(error,"No pending jobs. Retry a failed/cancelled/interrupted job or add a new one.");
    if(!persist()){emit changed();return fail(error,m_error);}
    m_running=true;emit changed();QTimer::singleShot(0,this,&ExportQueue::next);return true;
}
void ExportQueue::next() {
    if(!m_running || m_active>=0 || m_shutdown)return;
    int index=-1;for(int i=0;i<m_jobs.size();++i)if(m_jobs[i].status==ExportJob::Pending){index=i;break;}
    if(index<0){m_running=false;emit changed();emit batchFinished();return;}
    m_active=index;m_cancelRequested=false;auto &job=m_jobs[index];job.status=ExportJob::Running;job.percent=0;job.log.clear();job.message="Preparing export...";
    if(!persist()){job.status=ExportJob::Pending;m_active=-1;emit changed();return;}
    QString issue;if(!outputValid(job.outputPath,job.snapshot,index,&issue)){finished(false,issue);return;}
    // Copy parameters before notifying widgets; their event handlers may append jobs.
    const auto snapshot=job.snapshot;const auto output=job.outputPath;const bool hardware=job.hardware;
    emit changed();
    if(m_cancelRequested || !m_running){finished(false,"Export cancelled.");return;}
    m_exporter.exportSequence(segmentsFor(snapshot),output,hardware,false,snapshot.sequence,false,snapshot.exportRange);
    if(m_cancelRequested || m_shutdown)m_exporter.cancel();
}
void ExportQueue::finished(bool success,const QString &message) {
    if(m_active<0 || m_shutdown)return;
    auto &job=m_jobs[m_active];job.status=success?ExportJob::Completed:m_cancelRequested?ExportJob::Cancelled:ExportJob::Failed;
    job.message=message;job.remainingMs=-1;if(success)job.percent=100;const auto id=job.id;m_active=-1;
    persist();emit changed();emit jobFinished(id,success);
    if(m_running)QTimer::singleShot(0,this,&ExportQueue::next);
}
void ExportQueue::cancelCurrent(){if(m_active>=0){m_cancelRequested=true;m_exporter.cancel();}}
void ExportQueue::stop(){m_running=false;cancelCurrent();emit changed();}
void ExportQueue::shutdown() {
    if(m_shutdown)return;m_shutdown=true;m_running=false;
    if(m_active>=0){auto &job=m_jobs[m_active];job.status=ExportJob::Interrupted;job.percent=0;job.message="Editor closed during export. Retry to restart.";m_active=-1;m_exporter.cancel();persist();}
}
bool ExportQueue::retry(int index,bool cpuOnly,QString *error) {
    if(index<0 || index>=m_jobs.size() || (m_jobs[index].status!=ExportJob::Failed && m_jobs[index].status!=ExportJob::Cancelled && m_jobs[index].status!=ExportJob::Interrupted))return fail(error,"Select a failed, cancelled or interrupted job.");
    if(!outputValid(m_jobs[index].outputPath,m_jobs[index].snapshot,index,error))return false;
    auto candidate=m_jobs;auto &job=candidate[index];job.status=ExportJob::Pending;job.percent=0;job.message.clear();job.log.clear();job.elapsedMs=0;job.remainingMs=-1;if(cpuOnly)job.hardware=false;
    return commit(candidate,error);
}
bool ExportQueue::changeOutput(int index,const QString &path,QString *error) {
    if(index<0 || index>=m_jobs.size() || index==m_active || m_jobs[index].status==ExportJob::Completed)return fail(error,"Select a waiting or unsuccessful job.");
    if(!outputValid(path,m_jobs[index].snapshot,index,error))return false;
    auto candidate=m_jobs;candidate[index].outputPath=QFileInfo(path).absoluteFilePath();return commit(candidate,error);
}
bool ExportQueue::remove(int index,QString *error) {
    if(index<0 || index>=m_jobs.size() || index==m_active)return fail(error,"Cannot remove the active job.");
    auto candidate=m_jobs;candidate.removeAt(index);const int previous=m_active;
    if(m_active>index)--m_active;
    if(commit(candidate,error))return true;m_active=previous;emit changed();return false;
}
bool ExportQueue::move(int index,int delta,QString *error) {
    if((delta!=-1 && delta!=1) || index<0 || index>=m_jobs.size() || index+delta<0 || index+delta>=m_jobs.size()
        || m_jobs[index].status!=ExportJob::Pending || m_jobs[index+delta].status!=ExportJob::Pending)return fail(error,"Only adjacent pending jobs can be reordered.");
    auto candidate=m_jobs;candidate.swapItemsAt(index,index+delta);return commit(candidate,error);
}
