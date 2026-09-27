#pragma once
#include "FFmpegExporter.h"
#include "ProjectFile.h"
#include <QLockFile>
#include <memory>

struct ExportJob {
    enum Status { Pending, Running, Completed, Failed, Cancelled, Interrupted };
    QString id,name,outputPath,message,log;
    ProjectState snapshot;
    bool hardware=false;
    Status status=Pending;
    int percent=0;
    qint64 elapsedMs=0,remainingMs=-1;
    double speed=0;
};

// Persistent sequential scheduler. No widgets or dialogs: the editor and a
// future standalone encoder can use this same snapshot/execution contract.
class ExportQueue : public QObject {
    Q_OBJECT
public:
    explicit ExportQueue(const QString &storePath,QObject *parent=nullptr);
    ~ExportQueue() override;
    const QVector<ExportJob> &jobs() const {return m_jobs;}
    FFmpegExporter *exporter() {return &m_exporter;}
    bool running() const {return m_running;}
    bool busy() const {return m_running || m_active>=0 || m_exporter.isRunning();}
    bool available() const {return m_ready;}
    QString error() const {return m_error;}
    QString storePath() const {return m_path;}
    int activeIndex() const {return m_active;}
    bool add(const ProjectState &snapshot,const QString &name,const QString &output,bool hardware,QString *error=nullptr);
    // count == -1 selects every video clip on V2. Commit the entire batch once.
    bool addV2Clips(const ProjectState &snapshot,int count,const QString &directory,bool hardware,QString *error=nullptr);
    static QVector<TimelineClip> v2Videos(const ProjectState &snapshot);
    bool start(QString *error=nullptr);
    void stop(); // Cancel active; keep remaining pending jobs for Start.
    void cancelCurrent(); // Cancel active; continue the queue.
    void shutdown(); // Mark interrupted and stop; never resume automatically.
    bool retry(int index,bool cpuOnly,QString *error=nullptr);
    bool changeOutput(int index,const QString &path,QString *error=nullptr);
    bool remove(int index,QString *error=nullptr);
    bool move(int index,int delta,QString *error=nullptr);
    static QString statusName(ExportJob::Status status);
    static QString defaultStorePath();
    static QVector<FFmpegExporter::ClipSegment> segmentsFor(const ProjectState &state);
signals:
    void changed();
    void jobFinished(const QString &id,bool success);
    void batchFinished();
private:
    bool load(QString *error);
    bool save(const QVector<ExportJob> &jobs,QString *error) const;
    bool commit(const QVector<ExportJob> &jobs,QString *error);
    bool persist();
    bool outputValid(const QString &path,const ProjectState &state,int except,QString *error) const;
    void next();
    void finished(bool success,const QString &message);
    QString m_path,m_error;
    std::unique_ptr<QLockFile> m_lock;
    FFmpegExporter m_exporter;
    QVector<ExportJob> m_jobs;
    int m_active=-1;
    bool m_ready=false,m_running=false,m_cancelRequested=false,m_shutdown=false;
};
