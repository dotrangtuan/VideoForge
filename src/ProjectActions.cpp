#include "MainWindow.h"
#include "ExportQueue.h"
#include "EffectsPanel.h"
#include "TimelineWidget.h"
#include "MediaListWidget.h"
#include "RealtimePreview.h"
#include <QMenuBar>
#include <QToolBar>
#include <QAction>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QSlider>
#include <QSignalBlocker>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QUuid>
#include <QStatusBar>
#include <QApplication>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

void MainWindow::buildProjectUi() {
    auto file=menuBar()->addMenu("&File");
    auto add=[&](const QString &text,QKeySequence key,auto callback) {
        auto action=file->addAction(text);action->setShortcut(key);connect(action,&QAction::triggered,this,callback);return action;
    };
    add("&New Project",QKeySequence::New,[this]{newProject();});
    add("&Open Project...",QKeySequence::Open,[this]{openProject();});
    add("&Save Project",QKeySequence::Save,[this]{saveProject();});
    add("Save Project &As...",QKeySequence::SaveAs,[this]{saveProjectAs();});
    file->addSeparator();
    add("Relink Missing Media...",{},[this]{relinkMissingMedia();});
    add("Recover Autosave...",{},[this]{recoverAutosave();});
    auto bar=addToolBar("Clip audio");bar->setMovable(false);
    m_snapCheck=new QCheckBox("Snapping (Alt bypass)",bar);m_snapCheck->setChecked(true);bar->addWidget(m_snapCheck);
    connect(m_snapCheck,&QCheckBox::toggled,this,[this](bool enabled){m_timeline->setSnapping(enabled);markDirty();});
    bar->addSeparator();
    auto spin=[&](const QString &name,double maximum,const QString &suffix) {
        bar->addWidget(new QLabel(name,bar));auto box=new QDoubleSpinBox(bar);box->setRange(0,maximum);box->setDecimals(2);box->setSuffix(suffix);box->setKeyboardTracking(false);bar->addWidget(box);return box;
    };
    m_gainSpin=spin("Clip volume ",200," %");m_gainSpin->setObjectName("clipVolume");
    m_fadeInSpin=spin(" Fade in ",86400," s");m_fadeInSpin->setObjectName("clipFadeIn");
    m_fadeOutSpin=spin(" Fade out ",86400," s");m_fadeOutSpin->setObjectName("clipFadeOut");
    for(auto box:{m_gainSpin,m_fadeInSpin,m_fadeOutSpin}) connect(box,&QDoubleSpinBox::editingFinished,this,&MainWindow::commitClipAudio);
    connect(m_timeline,&TimelineWidget::selectionChanged,this,[this](int){updateClipAudioUi();});
    connect(m_zoomSlider,&QSlider::valueChanged,this,[this](int){markDirty();});
    updateClipAudioUi();
    QString recovery=qEnvironmentVariable("VIDEOFORGE_RECOVERY_DIR");
    if(recovery.isEmpty())recovery=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/recovery";
    if(QDir().mkpath(recovery)) {
        m_autosavePath=QDir(recovery).absoluteFilePath(QUuid::createUuid().toString(QUuid::Id128)+".vfproj");
        m_sessionLock=std::make_unique<QLockFile>(m_autosavePath+".lock");
        m_sessionLock->setStaleLockTime(0);
        if(!m_sessionLock->tryLock())m_autosavePath.clear();
    }
    m_autosaveTimer.setParent(this);m_autosaveTimer.setObjectName("autosaveTimer");
    m_autosaveTimer.setInterval(30000);
    connect(&m_autosaveTimer,&QTimer::timeout,this,[this]{
        if(!m_dirty)return;QString error;
        if(!writeAutosave(&error))statusBar()->showMessage("Autosave failed: "+error,10000);
    });
    m_autosaveTimer.start();
    QTimer::singleShot(0,this,&MainWindow::checkRecovery);
}
void MainWindow::commitClipAudio() {
    if(m_loading || !m_gainSpin || !m_gainSpin->isEnabled())return;
    for(auto box:{m_gainSpin,m_fadeInSpin,m_fadeOutSpin})box->interpretText();
    const double gain=m_gainSpin->value()/100.0;
    const qint64 fadeIn=qRound64(m_fadeInSpin->value()*1000),fadeOut=qRound64(m_fadeOutSpin->value()*1000);
    const int i=m_timeline->selectedClipIndex();const auto clips=m_timeline->clips();
    if(i<0 || i>=clips.size())return;
    if(clips[i].gain==gain && clips[i].fadeInMs==fadeIn && clips[i].fadeOutMs==fadeOut)return;
    pauseForEdit();m_timeline->setClipAudio(i,gain,fadeIn,fadeOut);
}
void MainWindow::updateClipAudioUi() {
    if(!m_gainSpin)return;
    const auto clips=m_timeline->clips();const int i=m_timeline->selectedClipIndex();
    const bool enabled=i>=0 && i<clips.size() && !clips[i].image;
    QSignalBlocker a(m_gainSpin),b(m_fadeInSpin),c(m_fadeOutSpin);
    for(auto box:{m_gainSpin,m_fadeInSpin,m_fadeOutSpin})box->setEnabled(enabled);
    if(enabled) {
        m_gainSpin->setValue(clips[i].gain*100);
        m_fadeInSpin->setMaximum(clips[i].durationMs()/1000.0);m_fadeOutSpin->setMaximum(clips[i].durationMs()/1000.0);
        m_fadeInSpin->setValue(clips[i].fadeInMs/1000.0);m_fadeOutSpin->setValue(clips[i].fadeOutMs/1000.0);
    }
}
void MainWindow::updateTitle() {
    setWindowTitle(QString("%1%2 — VideoForge Editor 0.10.0").arg(m_projectPath.isEmpty()?"Untitled":QFileInfo(m_projectPath).fileName(),m_dirty?" *":""));
}
void MainWindow::markDirty() { if(!m_loading){m_dirty=true;updateTitle();} }
ProjectState MainWindow::projectState() const {
    ProjectState state;state.sequence=m_sequenceSettings;state.clips=m_timeline->clips();
    state.backgroundMode=m_backgroundMode;state.exportRange=m_exportRange;
    for(int i=0;i<m_mediaList->count();++i) {
        auto item=m_mediaList->item(i);state.media.push_back({item->data(Qt::UserRole).toString(),item->data(Qt::UserRole+1).toLongLong()});
    }
    for(int t=0;t<5;++t){state.visible[t]=m_timeline->trackVisible(t);state.muted[t]=m_timeline->trackMuted(t);}
    state.positionMs=m_timelinePositionMs;state.zoom=m_timeline->zoom();state.snapping=m_timeline->snapping();return state;
}
void MainWindow::applyProject(const ProjectState &state) {
    m_loading=true;pauseForEdit();
    m_sequenceSettings=state.sequence;m_videoWidget->setSettings(state.sequence);
    refreshSequenceInfo();m_mediaList->clear();
    for(const auto &m:state.media) {
        auto item=new QListWidgetItem(QFileInfo(m.path).fileName());
        item->setToolTip(m.path);item->setData(Qt::UserRole,m.path);item->setData(Qt::UserRole+1,m.durationMs);
        if(!QFileInfo(m.path).isFile())item->setText("[Offline] "+item->text());
        m_mediaList->addItem(item);
    }
    m_timeline->setContents(state.clips,state.visible,state.muted);
    m_backgroundMode=state.backgroundMode;m_exportRange=state.exportRange;
    m_originalFiles.clear();m_backgroundFiles.clear();m_backgroundAudio->setChecked(false);
    m_backgroundHint->setText("Choose original videos and background files to append a new group. Existing clips remain editable in either mode.");m_backgroundHint->setToolTip({});
    updateEditingUi();updateRangeUi();
    m_zoomSlider->setValue(state.zoom);m_snapCheck->setChecked(state.snapping);m_timeline->setSnapping(state.snapping);
    m_videoWidget->seek(state.positionMs);m_loading=false;
}
bool MainWindow::saveProjectFile(const QString &path,QString *error) {
    commitClipAudio();m_effectsPanel->commitPendingEdits();
    const QString full=QFileInfo(path).absoluteFilePath();
    auto state=projectState();
    for(const auto &c:state.clips)if(QFileInfo(c.filePath)==QFileInfo(full)){if(error)*error="Cannot overwrite source media.";return false;}
    for(const auto &m:state.media)if(QFileInfo(m.path)==QFileInfo(full)){if(error)*error="Cannot overwrite source media.";return false;}
    if(!ProjectFile::save(full,state,error))return false;
    m_projectPath=full;m_dirty=false;QFile::remove(m_autosavePath);updateTitle();return true;
}
bool MainWindow::loadProjectFile(const QString &path,QString *error) {
    ProjectState state;if(!ProjectFile::load(path,state,error))return false;
    applyProject(state);m_projectPath=QFileInfo(path).absoluteFilePath();m_dirty=false;updateTitle();return true;
}
bool MainWindow::writeAutosave(QString *error) {
    if(m_autosavePath.isEmpty()){if(error)*error="Recovery folder is unavailable.";return false;}
    auto state=projectState();state.recoveryOrigin=m_projectPath;
    return ProjectFile::save(m_autosavePath,state,error);
}
bool MainWindow::restoreAutosave(const QString &path,QString *error) {
    ProjectState state;if(!ProjectFile::load(path,state,error))return false;
    applyProject(state);m_projectPath=state.recoveryOrigin;m_dirty=true;updateTitle();
    return writeAutosave(error);
}
bool MainWindow::saveProject() {
    if(m_projectPath.isEmpty())return saveProjectAs();
    QString error;if(saveProjectFile(m_projectPath,&error))return true;
    QMessageBox::warning(this,"Save project",error);return false;
}
bool MainWindow::saveProjectAs() {
    auto path=QFileDialog::getSaveFileName(this,"Save project",m_projectPath.isEmpty()?"Untitled.vfproj":m_projectPath,"VideoForge Project (*.vfproj)");
    if(path.isEmpty())return false;
    if(!path.endsWith(".vfproj",Qt::CaseInsensitive))path+=".vfproj";
    QString error;if(saveProjectFile(path,&error))return true;
    QMessageBox::warning(this,"Save project",error);return false;
}
bool MainWindow::confirmSave() {
    commitClipAudio();m_effectsPanel->commitPendingEdits();
    if(!m_dirty)return true;
    pauseForEdit();
    auto choice=QMessageBox::question(this,"Unsaved changes","Save your project before continuing?",QMessageBox::Save|QMessageBox::Discard|QMessageBox::Cancel,QMessageBox::Save);
    if(choice==QMessageBox::Cancel)return false;
    if(choice==QMessageBox::Save)return saveProject();
    return choice==QMessageBox::Discard;
}
void MainWindow::newProject() {
    if(!confirmSave())return;
    QFile::remove(m_autosavePath);applyProject(ProjectState{});m_projectPath.clear();m_dirty=false;updateTitle();
}
void MainWindow::openProject() {
    auto path=QFileDialog::getOpenFileName(this,"Open project",{},"VideoForge Project (*.vfproj)");if(path.isEmpty())return;
    // Validate before asking to discard the current session.
    ProjectState state;QString error;
    if(!ProjectFile::load(path,state,&error)){QMessageBox::warning(this,"Open project",error);return;}
    if(!confirmSave())return;
    applyProject(state);m_projectPath=QFileInfo(path).absoluteFilePath();m_dirty=false;QFile::remove(m_autosavePath);updateTitle();
    const auto missing=ProjectFile::missingMedia(state);
    if(!missing.isEmpty()) {
        if(QMessageBox::question(this,"Offline media",QString("%1 media files are missing. Locate them now?").arg(missing.size()))==QMessageBox::Yes)relinkMissingMedia();
    }
}
bool MainWindow::relinkMediaFile(const QString &oldPath,const QString &newPath,QString *error) {
    auto state=projectState();
    if(!QFileInfo(newPath).isFile()){if(error)*error="Replacement file does not exist.";return false;}
    const bool image=QImageReader(newPath).canRead();qint64 duration=image?10000:probeDurationMs(newPath);
    qint64 required=120;bool referenced=false,needsVideo=false,needsAudio=false;
    for(const auto &m:state.media)if(m.path==oldPath)referenced=true;
    for(const auto &c:state.clips)if(c.filePath==oldPath){referenced=true;required=qMax(required,c.sourceOutMs);if(c.image!=image){if(error)*error="Replacement media type differs.";return false;}needsVideo|=c.track<3;needsAudio|=c.audioOnly || c.track>=3;}
    if(!referenced){if(error)*error="Source is not referenced in this project.";return false;}
    if(!image) {
        QProcess probe;probe.start(m_exporter->ffprobeExecutable(),{"-v","error","-show_entries","stream=codec_type","-of","json",newPath});
        bool video=false,audio=false;
        if(probe.waitForFinished(6000))for(const auto &v:QJsonDocument::fromJson(probe.readAllStandardOutput()).object()["streams"].toArray()) {
            video|=v.toObject()["codec_type"]=="video";audio|=v.toObject()["codec_type"]=="audio";
        }
        if((needsVideo&&!video)||(needsAudio&&!audio)){if(error)*error="Replacement lacks the required video/audio stream.";return false;}
    }
    if(duration<required){if(error)*error="Replacement is shorter than a clip's source out point.";return false;}
    ProjectFile::relink(state,oldPath,newPath,duration);applyProject(state);markDirty();return true;
}
void MainWindow::relinkMissingMedia() {
    pauseForEdit();const auto missing=ProjectFile::missingMedia(projectState());
    if(missing.isEmpty()){statusBar()->showMessage("All media files are available.",4000);return;}
    for(const auto &oldPath:missing) {
        auto path=QFileDialog::getOpenFileName(this,"Locate: "+QFileInfo(oldPath).fileName(),QFileInfo(oldPath).absolutePath(),"Media files (*)");
        if(path.isEmpty())continue;
        QString error;if(!relinkMediaFile(oldPath,path,&error))QMessageBox::warning(this,"Relink media",error);
    }
}
void MainWindow::checkRecovery() {
    if(m_autosavePath.isEmpty() || qApp->property("disableRecoveryPrompt").toBool())return;
    QDir dir=QFileInfo(m_autosavePath).absoluteDir();
    for(const auto &entry:dir.entryInfoList({"*.vfproj"},QDir::Files,QDir::Time)) {
        const auto path=entry.absoluteFilePath();if(path==m_autosavePath)continue;
        QLockFile lock(path+".lock");lock.setStaleLockTime(0);if(!lock.tryLock())continue; // Another running editor owns it.
        if(QMessageBox::question(this,"Recover project","An autosaved session is available. Recover it now?\n"+entry.lastModified().toString(),QMessageBox::Yes|QMessageBox::No)==QMessageBox::Yes) {
            QString error;if(restoreAutosave(path,&error))QFile::remove(path);else QMessageBox::warning(this,"Recovery",error);
        }
        break; // No keeps the file for File > Recover Autosave.
    }
}
void MainWindow::recoverAutosave() {
    auto path=QFileDialog::getOpenFileName(this,"Recover autosaved project",QFileInfo(m_autosavePath).absolutePath(),"VideoForge Project (*.vfproj)");
    if(path.isEmpty() || path==m_autosavePath)return;
    QLockFile lock(path+".lock");lock.setStaleLockTime(0);if(!lock.tryLock()){QMessageBox::information(this,"Recovery","This session is open in another editor.");return;}
    ProjectState state;QString error;if(!ProjectFile::load(path,state,&error)){QMessageBox::warning(this,"Recovery",error);return;}
    if(!confirmSave())return;
    if(restoreAutosave(path,&error))QFile::remove(path);else QMessageBox::warning(this,"Recovery",error);
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if(m_queue->busy() && QMessageBox::question(this,"Export running","Interrupt export and close? The queue is saved; retry the interrupted job after reopening.",QMessageBox::Yes|QMessageBox::No,QMessageBox::No)!=QMessageBox::Yes){event->ignore();return;}
    if(!confirmSave()){event->ignore();return;}
    m_queue->shutdown();m_autosaveTimer.stop();QFile::remove(m_autosavePath);event->accept();
}
