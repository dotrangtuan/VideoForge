#include <QtTest>
#include "ExportQueue.h"
#include "ExportQueueDialog.h"
#include <QTableWidget>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QCheckBox>
#include "EffectsPanel.h"
#include "VideoEffects.h"
#include <QPainter>
#include <QListWidget>
#include <QLineEdit>
#include <limits>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QImage>
#include "TimelineWidget.h"
#include "TimelineRuler.h"
#include "MainWindow.h"
#include "RealtimePreview.h"
#include "ProjectFile.h"
#include "AudioEnvelope.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QAbstractButton>
#include <QDoubleSpinBox>
#include <QDir>
#include <QDialog>
#include <QDialogButtonBox>
#include <QSpinBox>
#include <QComboBox>
#include <QAudioOutput>
#include <QPushButton>
#include <QLabel>
#include <QVideoSink>
#include <QVideoFrame>
#include <QFontDatabase>
#include <QMimeData>
#include <QDropEvent>
#include <QDragEnterEvent>
#include "FFmpegExporter.h"
#include "BackgroundComposition.h"
#include <QTabWidget>
#include <QRadioButton>

class EditorTests : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    QString red, blue, tone, still;
    QByteArray run(const QString &tool, const QStringList &args) {
        QProcess p; p.start(tool,args);
        if(!p.waitForFinished(30000) || p.exitCode()!=0) { qWarning()<<p.readAllStandardError(); return {}; }
        return p.readAllStandardOutput();
    }
    bool generate(const QStringList &args) {
        FFmpegExporter e; QProcess p;
        p.start(e.ffmpegExecutable(),QStringList{"-y","-loglevel","error"}+args);
        if(!p.waitForFinished(30000) || p.exitCode()!=0) { qWarning()<<p.readAllStandardError(); return false; }
        return true;
    }
    FFmpegExporter::ClipSegment segment(QString path,int track,qint64 start,qint64 duration) {
        FFmpegExporter::ClipSegment c; c.inputPath=path;c.track=track;c.startMs=start;c.outMs=duration;return c;
    }
    QColor pixel(QString path,double time,int x=300,int y=170) {
        FFmpegExporter e;
        auto bytes=run(e.ffmpegExecutable(),{"-v","error","-ss",QString::number(time),"-i",path,"-frames:v","1","-vf",QString("crop=2:2:%1:%2,scale=1:1").arg(x).arg(y),"-f","rawvideo","-pix_fmt","rgb24","pipe:1"});
        if(bytes.size()<3) return {};
        return QColor(uchar(bytes[0]),uchar(bytes[1]),uchar(bytes[2]));
    }
    ProjectState queueState(const QString &path,qint64 duration=400) {
        ProjectState state;state.sequence={320,180,30,1};TimelineClip clip;clip.id=1;clip.name="Queued clip";clip.filePath=path;
        clip.sourceDurationMs=clip.sourceOutMs=duration;clip.image=path.endsWith(".png");state.clips.append(clip);state.media.append({path,duration});return state;
    }
private slots:
    void initTestCase() {
        qApp->setProperty("disableRecoveryPrompt",true);
        qputenv("VIDEOFORGE_RECOVERY_DIR",dir.filePath("recovery").toUtf8());
        qputenv("VIDEOFORGE_QUEUE_DIR",dir.filePath("default-queue").toUtf8());
        #ifdef Q_OS_WIN
        QFontDatabase::addApplicationFont(qEnvironmentVariable("SystemRoot")+"/Fonts/arial.ttf");
        #endif
        FFmpegExporter e;
        QVERIFY2(!e.ffmpegExecutable().isEmpty(),"FFmpeg is required for integration tests.");
        QVERIFY(!e.ffprobeExecutable().isEmpty());
        red=dir.filePath("red source.mp4"); blue=dir.filePath("blue.mp4"); tone=dir.filePath("tone.wav"); still=dir.filePath("logo.png");
        QVERIFY(generate({"-f","lavfi","-i","color=red:s=320x180:r=30:d=3","-c:v","libx264","-pix_fmt","yuv420p",red}));
        QVERIFY(generate({"-f","lavfi","-i","color=blue:s=320x180:r=30:d=1","-c:v","libx264","-pix_fmt","yuv420p",blue}));
        QVERIFY(generate({"-f","lavfi","-i","sine=frequency=440:duration=1:sample_rate=48000",tone}));
        QImage logo(320,180,QImage::Format_ARGB32);logo.fill(Qt::transparent);
        for(int y=60;y<120;++y) for(int x=120;x<200;++x) logo.setPixelColor(x,y,Qt::green);
        QVERIFY(logo.save(still));
    }
    void resourceRemovalAndConfirmation() {
        ProjectState state=queueState(red);state.media.append({blue,1000});state.media.append({tone,1000});
        auto second=state.clips.first();second.id=2;second.track=2;second.startMs=1000;state.clips.append(second);
        auto other=second;other.id=3;other.track=1;other.startMs=2000;other.filePath=blue;state.clips.append(other);
        QString error,path=dir.filePath("remove-resources.vfproj");QVERIFY(ProjectFile::save(path,state,&error));
        QFile source(red);QVERIFY(source.open(QIODevice::ReadOnly));const auto originalBytes=source.readAll();source.close();
        MainWindow window;window.show();window.activateWindow();QVERIFY(window.loadProjectFile(path,&error));
        auto *list=window.findChild<QListWidget*>("importedResources");QVERIFY(list);
        auto *clear=window.findChild<QPushButton*>("removeAllResources");QVERIFY(clear);QVERIFY(clear->isEnabled());
        auto *timeline=window.findChild<TimelineWidget*>();
        list->setFocus();QTRY_VERIFY(list->hasFocus());
        // An unused resource is removed directly. Timeline/source files remain intact.
        list->setCurrentRow(2);QTest::keyClick(list,Qt::Key_Delete);
        QCOMPARE(list->count(),2);QCOMPARE(timeline->clips().size(),3);QVERIFY(QFileInfo::exists(tone));
        QVERIFY(window.hasUnsavedChanges());QVERIFY(window.saveProjectFile(path,&error));
        bool asked=false,defaultNo=false;QString prompt;
        auto answer=[&](QMessageBox::StandardButton choice) {
            asked=false;defaultNo=false;prompt.clear();
            QTimer::singleShot(20,&window,[&,choice] {
                auto *box=window.findChild<QMessageBox*>("confirmRemoveResources");
                if(!box)return;
                asked=true;defaultNo=box->defaultButton()==box->button(QMessageBox::No);prompt=box->text();
                const QString shot=qEnvironmentVariable("VIDEOFORGE_REMOVE_SCREENSHOT");
                if(!shot.isEmpty())box->grab().save(shot);
                box->button(choice)->click();
            });
        };
        list->setCurrentRow(0);answer(QMessageBox::No);QTest::keyClick(list,Qt::Key_Delete);
        QVERIFY(asked);QVERIFY(defaultNo);QVERIFY(prompt.contains("2 clip(s)"));
        QCOMPARE(list->count(),2);QCOMPARE(timeline->clips().size(),3);QVERIFY(!window.hasUnsavedChanges());
        window.activateWindow();list->setFocus();QTRY_VERIFY(list->hasFocus());answer(QMessageBox::Yes);QTest::keyClick(list,Qt::Key_Delete);
        QVERIFY(asked);QCOMPARE(list->count(),1);QCOMPARE(timeline->clips().size(),1);
        QCOMPARE(timeline->clips().first().filePath,blue);QCOMPARE(timeline->clips().first().startMs,qint64(2000));
        QVERIFY(source.open(QIODevice::ReadOnly));QCOMPARE(source.readAll(),originalBytes);source.close();
        // All matching clips were removed as a single timeline Undo operation.
        QVERIFY(timeline->undo());QCOMPARE(timeline->clips().size(),3);QCOMPARE(list->count(),1);
        QVERIFY(timeline->redo());QCOMPARE(timeline->clips().size(),1);
        QVERIFY(window.writeAutosave(&error));ProjectState saved;
        QVERIFY(ProjectFile::load(window.autosavePath(),saved,&error));QCOMPARE(saved.media.size(),1);QCOMPARE(saved.clips.size(),1);
        QVERIFY(window.saveProjectFile(path,&error));QVERIFY(window.loadProjectFile(path,&error));
        QCOMPARE(list->count(),1);QCOMPARE(timeline->clips().size(),1);
        answer(QMessageBox::No);clear->click();QVERIFY(asked);QCOMPARE(list->count(),1);QVERIFY(!window.hasUnsavedChanges());
        answer(QMessageBox::Yes);clear->click();QVERIFY(asked);QCOMPARE(list->count(),0);QCOMPARE(timeline->clips().size(),0);
        QVERIFY(!clear->isEnabled());QVERIFY(window.projectState().exportRange.valid(0));QVERIFY(QFileInfo::exists(blue));
        // Multiple selection prompts once and removes exactly that set, leaving unused resources.
        QVERIFY(ProjectFile::save(path,state,&error));QVERIFY(window.loadProjectFile(path,&error));
        list->clearSelection();list->item(0)->setSelected(true);list->item(1)->setSelected(true);
        window.activateWindow();list->setFocus();QTRY_VERIFY(list->hasFocus());answer(QMessageBox::Yes);QTest::keyClick(list,Qt::Key_Delete);
        QVERIFY(asked);QVERIFY(prompt.contains("2 resource(s) and 3 clip(s)"));
        QCOMPARE(list->count(),1);QCOMPARE(timeline->clips().size(),0);QCOMPARE(list->item(0)->data(Qt::UserRole).toString(),tone);
        clear->click();QCOMPARE(list->count(),0);QVERIFY(!clear->isEnabled());QVERIFY(QFileInfo::exists(tone));
    }
    void timelineEditing() {
        TimelineWidget w;
        w.addClip("main",red,3000,0,1000); w.addClip("overlay",blue,1000,1,2000); w.addClip("audio",tone,1000,3,5000);
        QCOMPARE(w.clips().size(),3); QCOMPARE(w.sequenceDurationMs(),6000); QCOMPARE(w.clipIndexAtTime(2500),1);
        QCOMPARE(w.clipIndexAtTime(0),-1);
        QVERIFY(w.moveClip(0,0,0)); QVERIFY(w.trimClip(0,500,2500));
        QCOMPARE(w.clips()[0].startMs,500); QCOMPARE(w.clips()[0].durationMs(),2000);
        QVERIFY(w.undo()); QCOMPARE(w.clips()[0].startMs,0); QVERIFY(w.redo());
        QVERIFY(!w.moveClip(2,0,0));
        w.addClip("collision",red,1000,0,700); QCOMPARE(w.clips().size(),3);
        QVERIFY(w.splitSelectedAt(5500)); QCOMPARE(w.clips().size(),4);
        QCOMPARE(w.clips()[3].startMs,5500); QCOMPARE(w.clips()[3].sourceInMs,500);
        QVERIFY(w.deleteSelected()); QCOMPARE(w.sequenceDurationMs(),5500); QVERIFY(w.undo()); QCOMPARE(w.sequenceDurationMs(),6000);
        w.toggleTrack(1,true); QCOMPARE(w.clipIndexAtTime(2200),0);
        w.toggleTrack(3,false); QVERIFY(w.trackMuted(3));
    }
    void pointerEditing() {
        TimelineWidget w; w.show(); w.addClip("main",red,3000,0,0);
        // V1 is the third visual row. Drag body 70 pixels = one second.
        QTest::mousePress(&w,Qt::LeftButton,Qt::NoModifier,QPoint(180,170));
        QMouseEvent move(QEvent::MouseMove,QPointF(250,170),QPointF(250,170),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&w,&move);
        QTest::mouseRelease(&w,Qt::LeftButton,Qt::NoModifier,QPoint(250,170));
        QCOMPARE(w.clips()[0].startMs,1000); QVERIFY(w.undo()); QCOMPARE(w.clips()[0].startMs,0);
        QTest::mousePress(&w,Qt::LeftButton,Qt::NoModifier,QPoint(111,170));
        QMouseEvent trim(QEvent::MouseMove,QPointF(146,170),QPointF(146,170),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&w,&trim);
        QTest::mouseRelease(&w,Qt::LeftButton,Qt::NoModifier,QPoint(146,170));
        QCOMPARE(w.clips()[0].startMs,500); QCOMPARE(w.clips()[0].sourceInMs,500);
    }
    void timelineFitAndResize() {
        QScrollArea area; area.resize(1100,365);
        area.setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto *timeline = new TimelineWidget;
        area.setWidget(timeline); timeline->setScrollArea(&area);
        timeline->setZoomLevel(25); area.show();
        timeline->addClip("Main video - 20 minutes",red,1200000,0,0);
        timeline->addClip("Overlay",blue,300000,2,600000);
        timeline->addClip("Audio tail",tone,120000,4,1800000);
        timeline->toggleTrack(4,false); // Muted clips still belong to the timeline.
        auto fits = [&] {
            return timeline->width() == area.viewport()->width()
                && area.horizontalScrollBar()->maximum() == 0
                && 110 + timeline->sequenceDurationMs() / 1000.0 * timeline->pixelsPerSecond() <= area.viewport()->width() - 27;
        };
        QTRY_VERIFY(fits());
        QTRY_VERIFY(!area.horizontalScrollBar()->isVisible());
        QCOMPARE(timeline->sequenceDurationMs(),qint64(1920000));
        QVERIFY(timeline->pixelsPerSecond() < 1.0);
        const QString screenshot = qEnvironmentVariable("VIDEOFORGE_TIMELINE_SCREENSHOT");
        if (!screenshot.isEmpty()) QVERIFY(area.grab().save(screenshot));
        // Dragging uses the fitted scale, held steady until mouse release.
        timeline->setSnapping(false);
        const double dragScale = timeline->pixelsPerSecond();
        const int dragX = 110 + int(1860 * dragScale);
        QTest::mousePress(timeline,Qt::LeftButton,Qt::NoModifier,QPoint(dragX,290));
        QMouseEvent drag(QEvent::MouseMove,QPointF(dragX+10,290),QPointF(dragX+10,290),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(timeline,&drag);
        QCOMPARE(timeline->pixelsPerSecond(),dragScale);
        QTest::mouseRelease(timeline,Qt::LeftButton,Qt::NoModifier,QPoint(dragX+10,290));
        QCOMPARE(timeline->clips()[2].startMs,1800000+qint64(10000/dragScale));
        QTRY_VERIFY(fits());
        QVERIFY(timeline->undo());
        const double before = timeline->pixelsPerSecond();
        area.resize(1500,365); QTRY_VERIFY(fits());
        QVERIFY(timeline->pixelsPerSecond() > before);
        area.resize(600,365); QTRY_VERIFY(fits());
        QVERIFY(timeline->moveClip(2,4,3600000)); QTRY_VERIFY(fits());
        QVERIFY(timeline->trimClip(2,0,60000)); QTRY_VERIFY(fits());
        QVERIFY(timeline->deleteSelected()); QTRY_VERIFY(fits());
        QVERIFY(timeline->undo()); QTRY_VERIFY(fits());
        QVERIFY(timeline->redo()); QTRY_VERIFY(fits());
        timeline->setZoomLevel(160);
        QTRY_VERIFY(area.horizontalScrollBar()->maximum() > 0);
        area.horizontalScrollBar()->setValue(area.horizontalScrollBar()->maximum());
        timeline->setZoomLevel(25); QTRY_VERIFY(fits());
        QCOMPARE(area.horizontalScrollBar()->value(),0);
        // Ruler seeking still maps the fitted endpoint to the sequence duration.
        QSignalSpy seek(timeline,&TimelineWidget::seekRequested);
        QTest::mouseClick(timeline,Qt::LeftButton,Qt::NoModifier,QPoint(area.viewport()->width()-28,15));
        QVERIFY(!seek.isEmpty());
        QVERIFY(qAbs(seek.last()[0].toLongLong()-timeline->sequenceDurationMs()) <= qCeil(1000 / timeline->pixelsPerSecond()));
        timeline->clearTimeline(); QTRY_VERIFY(fits());
        timeline->addClip("24 hours",red,86400000,0,0); QTRY_VERIFY(fits());
        QVERIFY(!timeline->grab().isNull()); // Includes the hour-scale ruler.
        timeline->clearTimeline(); timeline->addClip("Short clip",red,120,0,0); QTRY_VERIFY(fits());
        const double end = 110 + 0.12 * timeline->pixelsPerSecond();
        QVERIFY(qAbs(end-(area.viewport()->width()-28))<1);
        QVERIFY(!timeline->grab().isNull());
    }
    void timelineFitToolbarAndPersistence() {
        MainWindow window; window.show();
        auto *timeline=window.findChild<TimelineWidget*>();
        auto *slider=window.findChild<QSlider*>("timelineZoom");
        auto *fit=window.findChild<QPushButton*>("fitTimeline");
        auto *area=window.findChild<QScrollArea*>("timelineScroll");
        QVERIFY(slider); QVERIFY(fit); QVERIFY(area);
        timeline->addClip("Long sequence",still,7200000,0,0);
        slider->setValue(180); QTRY_VERIFY(area->horizontalScrollBar()->maximum()>0);
        area->horizontalScrollBar()->setValue(5000);
        QTest::mouseClick(fit,Qt::LeftButton);
        QCOMPARE(slider->value(),slider->minimum());
        QTRY_COMPARE(area->horizontalScrollBar()->maximum(),0);
        QTRY_VERIFY(!area->horizontalScrollBar()->isVisible());
        QCOMPARE(area->horizontalScrollBar()->value(),0);
        QString error,path=dir.filePath("fit-view.vfproj");
        QVERIFY2(window.saveProjectFile(path,&error),qPrintable(error));
        slider->setValue(150);
        QVERIFY2(window.loadProjectFile(path,&error),qPrintable(error));
        QCOMPARE(timeline->zoom(),25); QCOMPARE(slider->value(),25);
        QTRY_COMPARE(area->horizontalScrollBar()->maximum(),0);
        window.resize(1600,900);
        QTRY_COMPARE(timeline->width(),area->viewport()->width());
    }
    void compositeAndMix() {
        QVector<FFmpegExporter::ClipSegment> clips{segment(red,0,0,3000),segment(blue,1,1000,1000),segment(tone,3,1000,1000),segment(tone,4,1000,1000)};
        auto logo=segment(still,2,1500,500);logo.image=true;clips<<logo;
        clips<<segment(tone,4,3500,500); // black tail and duration=max end
        FFmpegExporter e; QSignalSpy finished(&e,&FFmpegExporter::finished),progress(&e,&FFmpegExporter::progressChanged);
        connect(&e,&FFmpegExporter::logMessage,[](QString s){qInfo().noquote()<<s;});
        QString output=dir.filePath("mixed.mp4"); e.exportSequence(clips,output,false,true);
        QVERIFY(finished.wait(30000)); QVERIFY(finished.last()[0].toBool()); QVERIFY(progress.size()>0);
        QCOMPARE(progress.last()[0].toInt(),100);
        auto r=pixel(output,0.5); QVERIFY(r.red()>200 && r.blue()<40);
        auto b=pixel(output,1.2); QVERIFY(b.blue()>200 && b.red()<40);
        auto g=pixel(output,1.7); QVERIFY(g.green()>180 && g.red()<50);
        auto under=pixel(output,1.7,50,50); QVERIFY(under.blue()>200); // transparent logo preserves V2
        auto resumed=pixel(output,2.5); QVERIFY(resumed.red()>200);
        auto black=pixel(output,3.2); QVERIFY(black.red()<20 && black.green()<20 && black.blue()<20);
        QByteArray duration=run(e.ffprobeExecutable(),{"-v","error","-show_entries","format=duration","-of","csv=p=0",output});
        QVERIFY(qAbs(duration.trimmed().toDouble()-4.0)<0.1);
        auto pcm=run(e.ffmpegExecutable(),{"-v","error","-i",output,"-vn","-ac","1","-ar","48000","-f","s16le","pipe:1"});
        auto energy=[&](double start,double end) { double sum=0;int count=0;
            for(int i=int(start*48000);i<int(end*48000) && i*2+1<pcm.size();++i) { qint16 v=qint16(uchar(pcm[i*2])|(uchar(pcm[i*2+1])<<8));sum+=double(v)*v;++count; }
            return count ? qSqrt(sum/count):0.0; };
        QVERIFY(energy(0.2,0.8)<20); QVERIFY(energy(1.1,1.4)>3000); QVERIFY(energy(2.2,2.8)<20);
        // Hidden V2/V3 leave V1; mute both tone tracks yields silence.
        for(auto &c:clips) {if(c.track==1 || c.track==2)c.visible=false; if(c.track>=3)c.muted=true;}
        finished.clear(); QString muted=dir.filePath("muted.mp4");e.exportSequence(clips,muted,false,true);
        QVERIFY(finished.wait(30000)); QVERIFY(finished.last()[0].toBool()); QVERIFY(pixel(muted,1.7).red()>200);
    }
    void exportFullResolutionAndTrim() {
        FFmpegExporter e; QSignalSpy done(&e,&FFmpegExporter::finished);
        auto c=segment(red,0,1000,2500); c.inMs=1000;
        QString output=dir.filePath("full.mp4");e.exportSequence({c},output,false);
        QVERIFY(done.wait(30000)); QVERIFY(done.last()[0].toBool());
        auto size=run(e.ffprobeExecutable(),{"-v","error","-select_streams","v:0","-show_entries","stream=width,height","-of","csv=p=0",output});
        QCOMPARE(size.trimmed(),QByteArray("1280,720"));
        QVERIFY(pixel(output,0.5).red()<20); QVERIFY(pixel(output,1.2).red()>200);
    }
    void dropCoordinates() {
        TimelineWidget w;w.show();QMimeData mime;mime.setUrls({QUrl::fromLocalFile(blue)});
        QSignalSpy drop(&w,&TimelineWidget::filesDropped);
        QDragEnterEvent enter(QPoint(250,110),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&w,&enter);
        QDropEvent event(QPointF(250,110),Qt::CopyAction,&mime,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&w,&event);QCOMPARE(drop.size(),1);
        QCOMPARE(drop[0][1].toInt(),1);QCOMPARE(drop[0][2].toLongLong(),2000);
    }
    void previewLifecycle() {
        MainWindow window; window.show();
        auto timeline=window.findChild<TimelineWidget *>();QVERIFY(timeline);
        timeline->addClip("Main video",red,3000,0,0);
        timeline->addClip("B-roll",blue,1000,1,1000);
        timeline->addClip("Logo",still,10000,2,1500);
        QVERIFY(timeline->trimClip(2,0,500));
        timeline->addClip("Voice",tone,1000,3,0);
        timeline->addClip("Music",tone,1000,4,1000);
        for(auto b:window.findChildren<QPushButton *>()) QVERIFY(b->text()!="Build Preview");
        auto preview=window.findChild<RealtimePreview *>();QVERIFY(preview);
        auto color=[&](int x=300,int y=170) {return preview->compositeFrame().pixelColor(x,y);};
        QVERIFY(QMetaObject::invokeMethod(&window,"onTimelineSeek",Q_ARG(qint64,1200)));
        QTRY_VERIFY_WITH_TIMEOUT(color().blue()>200 && color().red()<40,10000);
        QVERIFY(!preview->isPlaying());
        QVERIFY(QMetaObject::invokeMethod(&window,"onTimelineSeek",Q_ARG(qint64,1700)));
        QTRY_VERIFY_WITH_TIMEOUT(color().green()>200,10000);
        QTRY_VERIFY_WITH_TIMEOUT(color(40,40).blue()>200,10000);
        QVERIFY(window.grab().save("editor-live.png"));
        // Scrub backwards on the same sources, then beyond the overlay.
        preview->seek(500);
        QTRY_VERIFY_WITH_TIMEOUT(color().red()>200 && color().green()<40,10000);
        preview->seek(2500);
        QTRY_VERIFY_WITH_TIMEOUT(color().red()>200,10000);
        preview->seek(3000); QCOMPARE(color(),QColor(Qt::black));
        // No render cache or external preview process is created.
        QCOMPARE(window.findChildren<FFmpegExporter *>().size(),1);
        QVERIFY(timeline->moveClip(1,1,2000));
        preview->seek(2200);
        QTRY_VERIFY_WITH_TIMEOUT(color().blue()>200,10000);
        timeline->toggleTrack(1,true);
        QTRY_VERIFY_WITH_TIMEOUT(color().red()>200,10000);
        preview->seek(2900); preview->play();
        QTRY_VERIFY_WITH_TIMEOUT(!preview->isPlaying(),5000);
        QCOMPARE(preview->position(),qint64(3000));
    }
    void liveSeekTimingAndScrub() {
        QString changing=dir.filePath("changing.mp4");
        QVERIFY(generate({"-f","lavfi","-i","color=red:s=320x180:r=30:d=3,drawbox=color=blue:t=fill:enable='between(t,1,2)',drawbox=color=lime:t=fill:enable='gte(t,2)'","-c:v","libx264","-pix_fmt","yuv420p",changing}));
        RealtimePreview preview;
        auto c=segment(changing,0,200,3000); c.inMs=1000;
        preview.setSequence({c});preview.seek(400);
        auto color=[&]() {return preview.compositeFrame().pixelColor(300,170);};
        QTRY_VERIFY_WITH_TIMEOUT(color().blue()>200,10000);
        preview.seek(1400);
        QTRY_VERIFY_WITH_TIMEOUT(color().green()>200,10000);
        preview.seek(300);preview.seek(1600);preview.seek(500);
        QTRY_VERIFY_WITH_TIMEOUT(color().blue()>200,10000);
        QCOMPARE(preview.position(),qint64(500));
        preview.seek(900);preview.play();
        QTRY_VERIFY_WITH_TIMEOUT(color().green()>200,5000);
        preview.pause();
        TimelineWidget timeline;timeline.show();timeline.addClip("clip",changing,3000,0,0);
        QSignalSpy seek(&timeline,&TimelineWidget::seekRequested);
        QTest::mousePress(&timeline,Qt::LeftButton,Qt::NoModifier,QPoint(145,15));
        QMouseEvent move(QEvent::MouseMove,QPointF(250,15),QPointF(250,15),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QApplication::sendEvent(&timeline,&move);
        QTest::mouseRelease(&timeline,Qt::LeftButton,Qt::NoModifier,QPoint(250,15));
        QVERIFY(seek.size()>=2);QCOMPARE(seek.last()[0].toLongLong(),qint64(2000));
        QCOMPARE(timeline.selectedClipIndex(),0);
    }
    void liveAudioTransport() {
        RealtimePreview preview;
        auto a=segment(tone,3,100,1000), b=segment(tone,4,100,1000);
        preview.setSequence({a,b}); preview.seek(200);
        QTRY_COMPARE_WITH_TIMEOUT(preview.findChildren<QMediaPlayer *>().size(),2,5000);
        QTRY_VERIFY_WITH_TIMEOUT(preview.findChildren<QMediaPlayer *>().at(0)->mediaStatus()==QMediaPlayer::LoadedMedia || preview.findChildren<QMediaPlayer *>().at(0)->mediaStatus()==QMediaPlayer::BufferedMedia,5000);
        preview.play();
        QTRY_VERIFY_WITH_TIMEOUT(preview.findChildren<QMediaPlayer *>().at(0)->playbackState()==QMediaPlayer::PlayingState,5000);
        auto outputs=preview.findChildren<QAudioOutput *>(); QCOMPARE(outputs.size(),2);
        QVERIFY(!outputs[0]->isMuted()); QVERIFY(!outputs[1]->isMuted());
        preview.pause(); for(auto output:outputs) QVERIFY(output->isMuted());
        b.muted=true;preview.setSequence({a,b});preview.seek(200);preview.play();
        QTRY_VERIFY_WITH_TIMEOUT(preview.findChildren<QMediaPlayer *>().at(1)->mediaStatus()==QMediaPlayer::BufferedMedia || preview.findChildren<QMediaPlayer *>().at(1)->mediaStatus()==QMediaPlayer::LoadedMedia,5000);
        outputs=preview.findChildren<QAudioOutput *>(); QVERIFY(!outputs[0]->isMuted()); QVERIFY(outputs[1]->isMuted());
        preview.seek(0); QTest::qWait(80);
        for(auto output:preview.findChildren<QAudioOutput *>()) QVERIFY(output->isMuted());
    }
    void projectRoundTripAndRecovery() {
        MainWindow first;auto timeline=first.findChild<TimelineWidget *>();
        timeline->addClip("main",red,3000,0,200);
        timeline->addClip("voice",tone,1000,3,800);
        timeline->setClipAudio(1,0.4,200,300);timeline->toggleTrack(0,true);timeline->toggleTrack(3,false);
        timeline->setZoomLevel(110);timeline->setSnapping(false);
        first.findChild<RealtimePreview *>()->seek(900);
        QString path=dir.filePath("saved project.vfproj"),error;
        QVERIFY(first.hasUnsavedChanges());QVERIFY2(first.saveProjectFile(path,&error),qPrintable(error));QVERIFY(!first.hasUnsavedChanges());
        MainWindow second;QVERIFY2(second.loadProjectFile(path,&error),qPrintable(error));
        auto state=second.projectState();QCOMPARE(state.clips.size(),2);QCOMPARE(state.clips[1].gain,0.4);
        QCOMPARE(state.clips[1].fadeOutMs,300);QCOMPARE(state.clips[0].startMs,200);QCOMPARE(state.positionMs,900);
        QCOMPARE(state.zoom,110);QVERIFY(!state.snapping);QVERIFY(!state.visible[0]);QVERIFY(state.muted[3]);
        auto secondTimeline=second.findChild<TimelineWidget *>();secondTimeline->moveClip(1,3,1400);
        second.findChild<QTimer *>("autosaveTimer")->setInterval(10);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(second.autosavePath()),2000);
        second.findChild<QTimer *>("autosaveTimer")->stop();
        MainWindow recovered;QVERIFY2(recovered.restoreAutosave(second.autosavePath(),&error),qPrintable(error));
        QCOMPARE(recovered.projectState().clips[1].startMs,1400);QVERIFY(recovered.hasUnsavedChanges());
        QVERIFY(!recovered.saveProjectFile(dir.filePath("absent/fail.vfproj"),&error));QVERIFY(recovered.hasUnsavedChanges());
        QVERIFY(QFileInfo::exists(recovered.autosavePath()));
        QVERIFY(recovered.saveProjectFile(path,&error));QVERIFY(!QFileInfo::exists(recovered.autosavePath()));
        ProjectState original;QVERIFY(ProjectFile::load(path,original,&error));QCOMPARE(original.clips[1].startMs,1400);
    }
    void projectValidationAndRelink() {
        MainWindow window;auto timeline=window.findChild<TimelineWidget *>();timeline->addClip("missing",dir.filePath("missing.mp4"),3000,0,100);
        QString error;QVERIFY(!ProjectFile::missingMedia(window.projectState()).isEmpty());
        QVERIFY(!window.relinkMediaFile(dir.filePath("missing.mp4"),blue,&error));
        QVERIFY(window.relinkMediaFile(dir.filePath("missing.mp4"),red,&error));
        QCOMPARE(window.projectState().clips[0].filePath,QFileInfo(red).absoluteFilePath());
        QCOMPARE(window.projectState().clips[0].startMs,100);
        QVERIFY(ProjectFile::missingMedia(window.projectState()).isEmpty());
        QString valid=dir.filePath("valid.vfproj");QVERIFY(window.saveProjectFile(valid,&error));
        QFile input(valid);QVERIFY(input.open(QIODevice::ReadOnly));auto root=QJsonDocument::fromJson(input.readAll()).object();input.close();
        auto clips=root["clips"].toArray();auto clip=clips[0].toObject();clip["track"]=99;clips[0]=clip;root["clips"]=clips;
        QString invalid=dir.filePath("invalid.vfproj");QFile bad(invalid);QVERIFY(bad.open(QIODevice::WriteOnly));bad.write(QJsonDocument(root).toJson());bad.close();
        QVERIFY(!window.loadProjectFile(invalid,&error));QCOMPARE(window.projectState().clips[0].track,0);
        root["version"]=99;QVERIFY(bad.open(QIODevice::WriteOnly));bad.write(QJsonDocument(root).toJson());bad.close();
        ProjectState sentinel=window.projectState();QVERIFY(!ProjectFile::load(invalid,sentinel,&error));QCOMPARE(sentinel.clips.size(),1);
        QVERIFY(!window.saveProjectFile(red,&error));
        // A media folder moved together with its project keeps relative links.
        const QString bundle=dir.filePath("bundle"),moved=dir.filePath("moved");QVERIFY(QDir().mkpath(bundle));
        QVERIFY(QFile::copy(red,bundle+"/source.mp4"));ProjectState portable=window.projectState();portable.clips[0].filePath=bundle+"/source.mp4";
        QVERIFY(ProjectFile::save(bundle+"/edit.vfproj",portable,&error));QVERIFY(QDir().rename(bundle,moved));
        ProjectState loaded;QVERIFY(ProjectFile::load(moved+"/edit.vfproj",loaded,&error));QVERIFY(ProjectFile::missingMedia(loaded).isEmpty());
    }
    void snappingAndAudioEdit() {
        TimelineWidget w;w.addClip("main",red,3000,0,0);w.addClip("voice",tone,1000,3,5000);w.setPosition(2000);
        QCOMPARE(w.snapTime(2030,1),qint64(2000));QCOMPARE(w.snapTime(3010,1),qint64(3000));
        QCOMPARE(w.snapTime(2040,1,1000),qint64(2000));w.setSnapping(false);QCOMPARE(w.snapTime(2030),qint64(2030));
        QVERIFY(w.setClipAudio(1,1.5,200,300));QCOMPARE(w.clips()[1].gain,1.5);QVERIFY(w.undo());QCOMPARE(w.clips()[1].gain,1.0);QVERIFY(w.redo());
        QVERIFY(w.splitSelectedAt(5500));QCOMPARE(w.clips()[1].fadeOutMs,0);QCOMPARE(w.clips()[2].fadeInMs,0);
        QCOMPARE(clipAudioGain(1,200,300,1000,100),0.5);QVERIFY(qAbs(clipAudioGain(1,200,300,1000,850)-0.5)<0.001);
    }
    void closePromptProtectsEdits() {
        MainWindow w;w.show();w.findChild<TimelineWidget *>()->addClip("main",red,3000);
        QString error;QVERIFY(w.writeAutosave(&error));
        QTimer::singleShot(20,[]{if(auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))box->button(QMessageBox::Cancel)->click();});
        QVERIFY(!w.close());QVERIFY(w.hasUnsavedChanges());QVERIFY(QFileInfo::exists(w.autosavePath()));
        QTimer::singleShot(20,[]{if(auto box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))box->button(QMessageBox::Discard)->click();});
        QVERIFY(w.close());QVERIFY(!QFileInfo::exists(w.autosavePath()));
    }
    void audioFieldsCommitOnSave() {
        MainWindow w;w.show();auto timeline=w.findChild<TimelineWidget *>();timeline->addClip("voice",tone,1000,3,0);
        auto gain=w.findChild<QDoubleSpinBox *>("clipVolume");QVERIFY(gain);gain->setValue(65);
        auto fade=w.findChild<QDoubleSpinBox *>("clipFadeIn");fade->setValue(0.25);
        QString error,path=dir.filePath("fields.vfproj");QVERIFY(w.saveProjectFile(path,&error));
        ProjectState state;QVERIFY(ProjectFile::load(path,state,&error));QCOMPARE(state.clips[0].gain,0.65);QCOMPARE(state.clips[0].fadeInMs,250);
        gain->setFocus();QTest::keyClick(gain,Qt::Key_Backspace);QCOMPARE(timeline->clips().size(),1);
        QLockFile contender(w.autosavePath()+".lock");contender.setStaleLockTime(0);QVERIFY(!contender.tryLock());
    }
    void preloadReusesDecoder() {
        RealtimePreview preview;preview.setSequence({segment(red,0,0,1000),segment(blue,0,1000,1000)});preview.seek(200);
        QTRY_COMPARE_WITH_TIMEOUT(preview.findChildren<QMediaPlayer *>().size(),2,5000);
        QMediaPlayer *next=nullptr;for(auto player:preview.findChildren<QMediaPlayer *>())if(player->source().toLocalFile()==blue)next=player;
        QVERIFY(next);QTRY_VERIFY_WITH_TIMEOUT(next->mediaStatus()==QMediaPlayer::BufferedMedia || next->mediaStatus()==QMediaPlayer::LoadedMedia,5000);
        QVERIFY(next->audioOutput()->isMuted());
        preview.seek(1100);QTRY_VERIFY_WITH_TIMEOUT(preview.compositeFrame().pixelColor(300,170).blue()>200,5000);
        QVERIFY(preview.findChildren<QMediaPlayer *>().contains(next));QCOMPARE(preview.findChildren<QMediaPlayer *>().size(),1);
    }
    void renderedGainAndFades() {
        FFmpegExporter e;auto c=segment(tone,3,0,1000);c.gain=0.5;c.fadeInMs=300;c.fadeOutMs=300;
        QSignalSpy finished(&e,&FFmpegExporter::finished);QString output=dir.filePath("faded.mp4");e.exportSequence({c},output,false,true);
        QVERIFY(finished.wait(30000));QVERIFY(finished.last()[0].toBool());
        auto pcm=run(e.ffmpegExecutable(),{"-v","error","-i",output,"-vn","-ac","1","-ar","48000","-f","s16le","pipe:1"});
        auto rms=[&](double begin,double end){double sum=0;int n=0;for(int i=int(begin*48000);i<int(end*48000)&&i*2+1<pcm.size();++i){qint16 v=qint16(uchar(pcm[i*2])|(uchar(pcm[i*2+1])<<8));sum+=double(v)*v;++n;}return n?qSqrt(sum/n):0;};
        double center=rms(0.4,0.6);QVERIFY(center>700 && center<1700);QVERIFY(rms(0.02,0.08)<center*0.4);QVERIFY(rms(0.92,0.98)<center*0.4);
    }
    void rationalFrameStepping() {
        for(const auto &rate:SequenceSettings::rates()) {
            SequenceSettings settings{1080,1920,rate.numerator,rate.denominator};QVERIFY(settings.valid());
            qint64 position=0;
            for(int i=1;i<=10000;++i) {position=settings.step(position,1,86400000);QCOMPARE(position,settings.frameToMs(i));}
            for(int i=9999;i>=0;--i) {position=settings.step(position,-1,86400000);QCOMPARE(position,settings.frameToMs(i));}
            QCOMPARE(settings.step(0,-1,1000),qint64(0));QCOMPARE(settings.step(1000,1,1000),qint64(1000));
            QVERIFY(settings.step(1000,-1,1000)<1000);
        }
        SequenceSettings ntsc{1920,1080,30000,1001};QCOMPARE(ntsc.frameToMs(30000),qint64(1001000));
        QCOMPARE(ntsc.step(50,1,1000),qint64(67));QCOMPARE(ntsc.step(50,-1,1000),qint64(33));
        QVERIFY(!SequenceSettings({1081,1920,30,1}).valid());QVERIFY(!SequenceSettings({1080,1920,30,0}).valid());
    }
    void timecodeNumbering() {
        for(const auto &rate:SequenceSettings::rates()) {
            SequenceSettings s{1280,720,rate.numerator,rate.denominator};const int n=s.nominalFps();
            QCOMPARE(s.timecode(-1),QString("00:00:00:00"));
            QCOMPARE(s.timecodeForFrame(n-1),QString("00:00:00:%1").arg(n-1,2,10,QLatin1Char('0')));
            QCOMPARE(s.timecodeForFrame(n),QString("00:00:01:00"));
            QCOMPARE(s.timecodeForFrame(n*60),QString("00:01:00:00"));
            QCOMPARE(s.timecodeForFrame(qint64(n)*3723+4),QString("01:02:03:04"));
            for(qint64 frame:{0LL,1LL,qint64(n-1),qint64(n),qint64(n)*60-1,qint64(n)*3600+7,qint64(n)*86399})
                QCOMPARE(s.timecode(s.frameToMs(frame)),s.timecodeForFrame(frame));
        }
        SequenceSettings ntsc{1280,720,30000,1001};
        QCOMPARE(ntsc.frameToMs(300),qint64(10010));
        QCOMPARE(ntsc.timecode(10010),QString("00:00:10:00"));
        QCOMPARE(ntsc.timecode(3600000),QString("00:59:56:12")); // NDF, not wall-clock seconds.
    }
    void timecodeRulerSpacing() {
        for(const auto &rate:SequenceSettings::rates()) for(double pps:{0.01,0.4,25.0,320.0}) {
            SequenceSettings s{1280,720,rate.numerator,rate.denominator};
            const qint64 start=3600000, end=start+qint64(1100000/pps);
            auto ticks=TimelineRuler::ticks(s,pps,start,end,90);
            QVERIFY(!ticks.isEmpty());QVERIFY(ticks.size()<200);
            qint64 previous=-1,major=-1;bool hasMinor=false;
            for(const auto &tick:ticks) {
                if(previous>=0)QVERIFY((tick.frame-previous)*double(s.fpsDen)/s.fpsNum*pps>=5.999);
                previous=tick.frame;
                if(tick.major) {
                    if(major>=0)QVERIFY((tick.frame-major)*double(s.fpsDen)/s.fpsNum*pps>=105.999);
                    major=tick.frame;
                } else hasMinor=true;
            }
            QVERIFY(hasMinor);
            auto shifted=TimelineRuler::ticks(s,pps,start+qint64(100000/pps),end,90);
            for(const auto &tick:shifted) {
                auto it=std::find_if(ticks.begin(),ticks.end(),[&](const auto &item){return item.frame==tick.frame;});
                QVERIFY(it!=ticks.end());QCOMPARE(it->major,tick.major);
            }
        }
        QVERIFY(TimelineRuler::ticks({},0,0,1000,90).isEmpty());
        QVERIFY(TimelineRuler::ticks({},30,1000,0,90).isEmpty());
    }
    void previewTimecodeUi() {
        MainWindow window;window.show();
        auto *label=window.findChild<QLabel*>("previewTimecode");QVERIFY(label);
        QCOMPARE(label->text(),QString("00:00:00:00 / 00:00:00:00"));
        auto *timeline=window.findChild<TimelineWidget*>();
        auto state=queueState(still,7200000);timeline->setContents(state.clips,state.visible,state.muted);
        auto *preview=window.findChild<RealtimePreview*>();
        for(const auto &rate:SequenceSettings::rates()) {
            SequenceSettings s{1280,720,rate.numerator,rate.denominator};QVERIFY(window.setSequenceSettings(s));
            const qint64 frame=qint64(s.nominalFps())*3723+4;preview->seek(s.frameToMs(frame));
            QCOMPARE(label->text(),QString("01:02:03:04 / ")+s.timecode(7200000));
            QVERIFY(timeline->toolTip().contains(s.rateLabel()+" fps"));
            QVERIFY(QMetaObject::invokeMethod(&window,"nextFrame"));
            QCOMPARE(label->text(),QString("01:02:03:05 / ")+s.timecode(7200000));
            QVERIFY(QMetaObject::invokeMethod(&window,"previousFrame"));
            QCOMPARE(label->text(),QString("01:02:03:04 / ")+s.timecode(7200000));
        }
        QString error,path=dir.filePath("timecode.vfproj");QVERIFY(window.saveProjectFile(path,&error));
        MainWindow reopened;QVERIFY(reopened.loadProjectFile(path,&error));
        QVERIFY(reopened.findChild<TimelineWidget*>()->toolTip().contains("60 fps"));
        QCOMPARE(reopened.findChild<QLabel*>("previewTimecode")->text(),label->text());
        const QString shot=qEnvironmentVariable("VIDEOFORGE_TIMECODE_SCREENSHOT");
        if(!shot.isEmpty()) {
            QTest::qWait(100);QVERIFY(window.grab().save(shot));
            timeline->setZoomLevel(220);preview->seek(2000);
            window.findChild<QScrollArea*>("timelineScroll")->horizontalScrollBar()->setValue(0);
            QTest::qWait(50);QVERIFY(window.grab().save(shot+".detail.png"));
        }
        timeline->clearTimeline();QCOMPARE(label->text(),QString("00:00:00:00 / 00:00:00:00"));
    }
    void sequenceProjectCompatibility() {
        MainWindow window;window.findChild<TimelineWidget *>()->addClip("main",red,3000,0,125);
        SequenceSettings settings{1080,1920,30000,1001};QVERIFY(window.setSequenceSettings(settings));
        QCOMPARE(window.projectState().clips[0].startMs,125);QCOMPARE(window.projectState().clips[0].durationMs(),3000);
        QString path=dir.filePath("sequence.vfproj"),error;QVERIFY(window.saveProjectFile(path,&error));
        ProjectState restored;QVERIFY(ProjectFile::load(path,restored,&error));QVERIFY(restored.sequence==settings);
        MainWindow reopened;QVERIFY(reopened.loadProjectFile(path,&error));QVERIFY(reopened.projectState().sequence==settings);
        QVERIFY(reopened.setSequenceSettings({1080,1080,24000,1001}));QVERIFY(reopened.hasUnsavedChanges());
        QVERIFY(reopened.writeAutosave(&error));ProjectState backup;QVERIFY(ProjectFile::load(reopened.autosavePath(),backup,&error));QCOMPARE(backup.sequence.fpsNum,24000);
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));auto root=QJsonDocument::fromJson(file.readAll()).object();file.close();
        root["version"]=1;root.remove("sequence");QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(root).toJson());file.close();
        QVERIFY(ProjectFile::load(path,restored,&error));QVERIFY(restored.sequence==SequenceSettings{});
        root["version"]=2;root["sequence"]=QJsonObject{{"width",1081},{"height",1920},{"fpsNum",30000},{"fpsDen",1001}};
        QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(root).toJson());file.close();
        QVERIFY(!ProjectFile::load(path,restored,&error));QVERIFY(restored.sequence==SequenceSettings{});
        QVERIFY(!window.setSequenceSettings({1080,1920,30,0}));QVERIFY(window.projectState().sequence==settings);
    }
    void sequenceDialogAndPortraitPreview() {
        MainWindow window;window.show();auto timeline=window.findChild<TimelineWidget *>();timeline->addClip("main",red,3000);
        QTimer::singleShot(30,[]{
            auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(!dialog)return;
            dialog->findChild<QSpinBox *>("sequenceWidth")->setValue(1080);
            dialog->findChild<QSpinBox *>("sequenceHeight")->setValue(1920);
            dialog->findChild<QComboBox *>("sequenceTimebase")->setCurrentIndex(3);
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->click();
        });
        QVERIFY(QMetaObject::invokeMethod(&window,"editSequenceSettings"));
        QVERIFY(window.projectState().sequence==SequenceSettings({1080,1920,30000,1001}));
        auto preview=window.findChild<RealtimePreview *>();preview->seek(500);
        QTRY_VERIFY_WITH_TIMEOUT(preview->compositeFrame().pixelColor(180,320).red()>200,10000);
        QCOMPARE(preview->compositeFrame().size(),QSize(360,640));QVERIFY(preview->compositeFrame().pixelColor(180,10).red()<20);
        QVERIFY(window.grab().save("sequence-portrait.png"));
        QTimer::singleShot(30,[]{
            auto dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(!dialog)return;
            dialog->findChild<QSpinBox *>("sequenceWidth")->setValue(1081);
            QVERIFY(!dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Ok)->isEnabled());
            dialog->reject();
        });
        QVERIFY(QMetaObject::invokeMethod(&window,"editSequenceSettings"));QCOMPARE(window.projectState().sequence.width,1080);
        preview->seek(0);QVERIFY(QMetaObject::invokeMethod(&window,"nextFrame"));QCOMPARE(preview->position(),qint64(33));
        QVERIFY(QMetaObject::invokeMethod(&window,"nextFrame"));QCOMPARE(preview->position(),qint64(67));
        QVERIFY(QMetaObject::invokeMethod(&window,"previousFrame"));QCOMPARE(preview->position(),qint64(33));
        QVERIFY(window.setSequenceSettings({1080,1080,25,1}));QCOMPARE(preview->position(),qint64(33));
        QCOMPARE(timeline->sequenceDurationMs(),qint64(3000));QCOMPARE(preview->compositeFrame().size(),QSize(640,640));
    }
    void pausedFramePictureChanges() {
        const QString alternating=dir.filePath("alternating.mp4");
        QVERIFY(generate({"-f","lavfi","-i","color=black:s=320x180:r=30:d=1,geq=lum='if(mod(N,2),200,40)':cb=128:cr=128","-c:v","libx264","-crf","0","-pix_fmt","yuv420p",alternating}));
        RealtimePreview preview;SequenceSettings settings;preview.setSettings(settings);preview.setSequence({segment(alternating,0,0,1000)});
        for(int frame=0;frame<10;++frame) {
            preview.seek(settings.frameToMs(frame));
            if(frame%2) {QTRY_VERIFY_WITH_TIMEOUT(preview.compositeFrame().pixelColor(300,170).red()>180,5000);}
            else {QTRY_VERIFY_WITH_TIMEOUT(preview.compositeFrame().pixelColor(300,170).red()>10 && preview.compositeFrame().pixelColor(300,170).red()<80,5000);}
        }
        for(int frame=8;frame>=0;--frame) {
            preview.seek(settings.frameToMs(frame));
            if(frame%2) {QTRY_VERIFY_WITH_TIMEOUT(preview.compositeFrame().pixelColor(300,170).red()>180,5000);}
            else {QTRY_VERIFY_WITH_TIMEOUT(preview.compositeFrame().pixelColor(300,170).red()>10 && preview.compositeFrame().pixelColor(300,170).red()<80,5000);}
        }
    }
    void exportSequenceRates() {
        for(const auto &rate:SequenceSettings::rates()) {
            SequenceSettings settings{180,320,rate.numerator,rate.denominator};
            FFmpegExporter exporter;QSignalSpy finished(&exporter,&FFmpegExporter::finished);
            const QString path=dir.filePath(QString("rate-%1-%2.mp4").arg(rate.numerator).arg(rate.denominator));
            exporter.exportSequence({segment(red,0,0,1000),segment(tone,3,0,1000)},path,false,false,settings);
            QVERIFY(finished.wait(30000));QVERIFY2(finished.last()[0].toBool(),qPrintable(finished.last()[1].toString()));
            auto data=QJsonDocument::fromJson(run(exporter.ffprobeExecutable(),{"-v","error","-select_streams","v:0","-show_entries","stream=width,height,r_frame_rate,avg_frame_rate,nb_frames,duration","-of","json",path})).object()["streams"].toArray();
            QVERIFY(!data.isEmpty());auto stream=data[0].toObject();QCOMPARE(stream["width"].toInt(),180);QCOMPARE(stream["height"].toInt(),320);
            QCOMPARE(stream["r_frame_rate"].toString(),settings.rateString());QCOMPARE(stream["avg_frame_rate"].toString(),settings.rateString());
            QCOMPARE(stream["nb_frames"].toString().toLongLong(),settings.frameCount(1000));
            QVERIFY(qAbs(stream["duration"].toString().toDouble()-1.0)<double(rate.denominator)/rate.numerator+0.002);
            QVERIFY(pixel(path,0.5,90,160).red()>200);QVERIFY(pixel(path,0.5,90,10).red()<20);
        }
    }
    void exportFrameSizePresets() {
        const std::array<QSize,4> sizes{{QSize(1920,1080),QSize(1080,1920),QSize(1080,1080),QSize(3840,2160)}};
        for(const auto size:sizes) {
            SequenceSettings settings{size.width(),size.height(),30,1};FFmpegExporter exporter;QSignalSpy done(&exporter,&FFmpegExporter::finished);
            QString path=dir.filePath(QString("size-%1-%2.mp4").arg(size.width()).arg(size.height()));
            exporter.exportSequence({segment(red,0,0,200)},path,false,false,settings);QVERIFY(done.wait(30000));QVERIFY(done.last()[0].toBool());
            auto result=run(exporter.ffprobeExecutable(),{"-v","error","-select_streams","v:0","-show_entries","stream=width,height","-of","csv=p=0",path});
            QCOMPARE(result.trimmed(),QString("%1,%2").arg(size.width()).arg(size.height()).toUtf8());
        }
    }

    void effectsEditingAndPersistence() {
        MainWindow window;window.show();auto timeline=window.findChild<TimelineWidget*>();
        timeline->addClip("Picture",red,2000,0,0);
        auto crop=VideoEffect::make(VideoEffect::Crop);crop.value={20,5,10,15};
        auto blur=VideoEffect::make(VideoEffect::Blur);blur.value[0]=2.5;blur.enabled=false;
        auto opacity=VideoEffect::make(VideoEffect::Opacity);opacity.value[0]=65;
        auto zoom=VideoEffect::make(VideoEffect::Zoom);zoom.value[0]=150;
        QVector<VideoEffect> stack{crop,blur,opacity,zoom};
        QVERIFY(timeline->setClipEffects(0,stack));QVERIFY(window.hasUnsavedChanges());
        QVERIFY(timeline->undo());QVERIFY(timeline->clips()[0].effects.isEmpty());
        QVERIFY(timeline->redo());QVERIFY(timeline->clips()[0].effects==stack);
        QVERIFY(timeline->splitSelectedAt(1000));QCOMPARE(timeline->clips().size(),2);
        QVERIFY(timeline->clips()[0].effects==stack);QVERIFY(timeline->clips()[1].effects==stack);
        QVERIFY(timeline->trimClip(1,1100,2000));QVERIFY(timeline->clips()[1].effects==stack);
        QString error,path=dir.filePath("fx-project.vfproj");
        QVERIFY2(window.saveProjectFile(path,&error),qPrintable(error));
        MainWindow loaded;QVERIFY2(loaded.loadProjectFile(path,&error),qPrintable(error));
        QVERIFY(loaded.projectState().clips[0].effects==stack);
        QVERIFY(loaded.projectState().clips[1].effects==stack);
        QVERIFY(window.writeAutosave(&error));QVERIFY(loaded.restoreAutosave(window.autosavePath(),&error));
        QVERIFY(loaded.projectState().clips[0].effects==stack);
        QVERIFY(timeline->moveClip(1,3,1000)); // Video moved to an audio track keeps its dormant effects.
        QVERIFY(timeline->clips()[1].effects==stack);QVERIFY(!timeline->setClipEffects(1,{}));
        QVERIFY(timeline->moveClip(1,1,1000));QVERIFY(timeline->clips()[1].effects==stack);
        auto invalid=crop;invalid.value[2]=90;QVERIFY(!timeline->setClipEffects(0,{invalid}));
        invalid=opacity;invalid.value[0]=std::numeric_limits<double>::quiet_NaN();QVERIFY(!timeline->setClipEffects(0,{invalid}));
        QVector<VideoEffect> excessive(17,opacity);QVERIFY(!timeline->setClipEffects(0,excessive));
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));auto root=QJsonDocument::fromJson(file.readAll()).object();file.close();
        QCOMPARE(root["version"].toInt(),4);
        const auto original=root;auto clips=root["clips"].toArray();auto clip=clips[0].toObject();auto effects=clip["effects"].toArray();
        auto badEffect=effects[0].toObject();badEffect["type"]="unsupported";effects[0]=badEffect;clip["effects"]=effects;clips[0]=clip;root["clips"]=clips;
        QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(root).toJson());file.close();
        const auto previous=loaded.projectState();QVERIFY(!loaded.loadProjectFile(path,&error));QVERIFY(loaded.projectState().clips[0].effects==previous.clips[0].effects);
        root=original;root["version"]=2;clips=root["clips"].toArray();
        for(int i=0;i<clips.size();++i){auto c=clips[i].toObject();c.remove("effects");clips[i]=c;}root["clips"]=clips;
        QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(root).toJson());file.close();
        QVERIFY(loaded.loadProjectFile(path,&error));QVERIFY(loaded.projectState().clips[0].effects.isEmpty());
    }
    void effectsPanelWorkflow() {
        MainWindow window;window.show();auto timeline=window.findChild<TimelineWidget*>();auto panel=window.findChild<EffectsPanel*>();
        timeline->addClip("FX test",red,2000,0,0);auto preview=window.findChild<RealtimePreview*>();
        preview->seek(500);QTRY_VERIFY_WITH_TIMEOUT(preview->compositeFrame().pixelColor(300,170).red()>200,5000);
        auto type=panel->findChild<QComboBox*>("effectType");auto add=panel->findChild<QPushButton*>("addEffect");auto list=panel->findChild<QListWidget*>("effectStack");
        auto value=panel->findChild<QDoubleSpinBox*>("effectValue0");
        QCOMPARE(type->currentIndex(),0);QTest::mouseClick(add,Qt::LeftButton);QCOMPARE(timeline->clips()[0].effects.size(),1);
        value->setValue(35);QMetaObject::invokeMethod(value,"editingFinished");QCOMPARE(timeline->clips()[0].effects[0].value[0],35.0);
        type->setCurrentIndex(3);QTest::mouseClick(add,Qt::LeftButton);value->setValue(200);QMetaObject::invokeMethod(value,"editingFinished");
        QCOMPARE(list->count(),2);QVERIFY(preview->compositeFrame().pixelColor(180,170).red()>200);
        QTest::mouseClick(panel->findChild<QPushButton*>("effectUp"),Qt::LeftButton);
        QCOMPARE(timeline->clips()[0].effects[0].type,VideoEffect::Zoom);
        QVERIFY(preview->compositeFrame().pixelColor(180,170).red()<20);
        QVERIFY(QMetaObject::invokeMethod(&window,"undoEdit"));QCOMPARE(timeline->clips()[0].effects[0].type,VideoEffect::Crop);
        list->item(0)->setCheckState(Qt::Unchecked);QVERIFY(!timeline->clips()[0].effects[0].enabled);
        list->setCurrentRow(0);QTest::mouseClick(panel->findChild<QPushButton*>("removeEffect"),Qt::LeftButton);QCOMPARE(list->count(),1);
        // Save while the field is still being typed must commit the pending value.
        type->setCurrentIndex(2);QTest::mouseClick(add,Qt::LeftButton);value->setFocus();value->selectAll();QTest::keyClicks(value,"50");
        QString error;QVERIFY(window.saveProjectFile(dir.filePath("fx-ui.vfproj"),&error));QCOMPARE(timeline->clips()[0].effects[1].value[0],50.0);
        QTRY_VERIFY_WITH_TIMEOUT(preview->compositeFrame().pixelColor(300,170).red()>100 && preview->compositeFrame().pixelColor(300,170).red()<150,5000);
        QVERIFY(window.grab().save("effects-panel.png"));
        timeline->addClip("Audio",tone,1000,3,0);QVERIFY(!add->isEnabled());QCOMPARE(list->count(),0);
    }
    void effectsPreviewSemantics() {
        QImage image(320,180,QImage::Format_ARGB32);image.fill(Qt::red);
        auto crop=VideoEffect::make(VideoEffect::Crop);crop.value[0]=35;
        auto zoom=VideoEffect::make(VideoEffect::Zoom);zoom.value[0]=200;
        auto first=VideoEffects::render(image,image.size(),{crop,zoom});auto second=VideoEffects::render(image,image.size(),{zoom,crop});
        QCOMPARE(first.pixelColor(90,90).alpha(),255);QCOMPARE(second.pixelColor(90,90).alpha(),0);
        auto small=zoom;small.value[0]=50;auto reduced=VideoEffects::render(image,image.size(),{small});
        QCOMPARE(reduced.pixelColor(40,90).alpha(),0);QCOMPARE(reduced.pixelColor(160,90).red(),255);
        auto opacity=VideoEffect::make(VideoEffect::Opacity);opacity.value[0]=50;
        QCOMPARE(VideoEffects::render(image,image.size(),{opacity}).pixelColor(160,90).alpha(),128);
        opacity.enabled=false;QCOMPARE(VideoEffects::render(image,image.size(),{opacity}).pixelColor(160,90).alpha(),255);
        QImage edge(320,180,QImage::Format_ARGB32);edge.fill(Qt::transparent);{QPainter p(&edge);p.fillRect(80,45,160,90,Qt::red);}
        auto blur=VideoEffect::make(VideoEffect::Blur);blur.value[0]=5;auto soft=VideoEffects::render(edge,edge.size(),{blur});
        auto boundary=soft.pixelColor(80,90);QVERIFY(boundary.alpha()>100 && boundary.alpha()<170);QCOMPARE(boundary.red(),255);
        auto cropBlur=VideoEffects::render(image,image.size(),{crop,blur});auto blurCrop=VideoEffects::render(image,image.size(),{blur,crop});
        QVERIFY(cropBlur.pixelColor(108,90).alpha()>0);QCOMPARE(blurCrop.pixelColor(108,90).alpha(),0);
        auto extreme=crop;extreme.value={99,99,0,0};QVERIFY(!VideoEffects::render(image,QSize(10,640),{extreme}).isNull());
    }
    void effectsExportMatchesPreview() {
        QImage source(320,180,QImage::Format_ARGB32);source.fill(Qt::red);
        {QPainter p(&source);p.fillRect(160,0,160,180,Qt::green);p.fillRect(80,60,40,60,Qt::white);}
        QString path=dir.filePath("fx-pattern.png");QVERIFY(source.save(path));
        auto crop=VideoEffect::make(VideoEffect::Crop);crop.value={35,10,5,10};
        auto blur=VideoEffect::make(VideoEffect::Blur);blur.value[0]=3;
        auto opacity=VideoEffect::make(VideoEffect::Opacity);opacity.value[0]=50;
        auto zero=opacity;zero.value[0]=0;auto disabled=crop;disabled.enabled=false;
        auto zoom=VideoEffect::make(VideoEffect::Zoom);zoom.value[0]=200;
        auto shrink=zoom;shrink.value[0]=50;
        QVector<QVector<VideoEffect>> stacks{{crop},{blur},{opacity},{zoom},{shrink},{crop,zoom},{zoom,crop},{crop,blur,opacity,shrink},{blur,crop},{zero},{disabled}};
        for(int n=0;n<stacks.size();++n) {
            FFmpegExporter exporter;QSignalSpy done(&exporter,&FFmpegExporter::finished);QString log;
            connect(&exporter,&FFmpegExporter::logMessage,this,[&](const QString &text){log+=text;});
            auto layer=segment(path,1,0,400);layer.image=true;layer.effects=stacks[n];
            auto output=dir.filePath(QString("fx-%1.mp4").arg(n));
            exporter.exportSequence({segment(blue,0,0,400),layer},output,false,false,{320,180,30,1});
            QVERIFY(done.wait(30000));QVERIFY2(done.last()[0].toBool(),qPrintable(log));
            QImage actual=QImage::fromData(run(exporter.ffmpegExecutable(),{"-v","error","-ss","0.2","-i",output,"-frames:v","1","-f","image2pipe","-vcodec","png","pipe:1"}));
            QCOMPARE(actual.size(),source.size());QImage expected(source.size(),QImage::Format_ARGB32);expected.fill(Qt::blue);
            {QPainter p(&expected);p.drawImage(0,0,VideoEffects::render(source,source.size(),stacks[n]));}
            double error=0;int count=0;
            for(int y=4;y<176;y+=3)for(int x=4;x<316;x+=3){auto a=actual.pixelColor(x,y),b=expected.pixelColor(x,y);error+=qAbs(a.red()-b.red())+qAbs(a.green()-b.green())+qAbs(a.blue()-b.blue());count+=3;}
            QVERIFY2(error/count<9,qPrintable(QString("Stack %1: mean RGB difference %2").arg(n).arg(error/count)));
            if(n==5)QVERIFY(actual.pixelColor(90,90).red()>200);
            if(n==6)QVERIFY(actual.pixelColor(90,90).blue()>200);
            if(n==2){auto pixel=actual.pixelColor(30,90);QVERIFY(pixel.red()>110 && pixel.red()<145 && pixel.blue()>110 && pixel.blue()<145);}
        }
        // Blurring alpha must soften the edge without a black halo over a white layer.
        QImage white(320,180,QImage::Format_ARGB32);white.fill(Qt::white);QString whitePath=dir.filePath("fx-white.png");QVERIFY(white.save(whitePath));
        QImage alpha(320,180,QImage::Format_ARGB32);alpha.fill(Qt::transparent);{QPainter p(&alpha);p.fillRect(80,45,160,90,Qt::red);}
        QString alphaPath=dir.filePath("fx-alpha.png");QVERIFY(alpha.save(alphaPath));
        auto bg=segment(whitePath,0,0,400);bg.image=true;auto fg=segment(alphaPath,1,0,400);fg.image=true;fg.effects={blur};
        FFmpegExporter exporter;QSignalSpy done(&exporter,&FFmpegExporter::finished);QString output=dir.filePath("fx-alpha.mp4");
        exporter.exportSequence({bg,fg},output,false,false,{320,180,30,1});QVERIFY(done.wait(30000));QVERIFY(done.last()[0].toBool());
        auto boundary=pixel(output,0.2,80,90);QVERIFY(boundary.red()>240);QVERIFY(boundary.green()>50 && boundary.green()<180);
        // Zoom after a softened transparent edge must preserve its red color too.
        done.clear();fg.effects={blur,shrink};output=dir.filePath("fx-alpha-shrink.mp4");
        exporter.exportSequence({bg,fg},output,false,false,{320,180,30,1});QVERIFY(done.wait(30000));QVERIFY(done.last()[0].toBool());
        boundary=pixel(output,0.2,120,90);QVERIFY2(boundary.red()>230,qPrintable(boundary.name()));QVERIFY(boundary.green()>40 && boundary.green()<180);
        // Inspect the lossless RGBA layer too: MP4 4:2:0 chroma subsampling can
        // shift individual colored-edge pixels even with correct alpha math.
        auto rawLayer=QImage::fromData(run(exporter.ffmpegExecutable(),{"-v","error","-filter_threads","1","-i",alphaPath,"-vf","format=rgba"+VideoEffects::filters({320,180},fg.effects),"-frames:v","1","-f","image2pipe","-vcodec","png","pipe:1"}));
        QCOMPARE(rawLayer.size(),QSize(320,180));auto edge=rawLayer.pixelColor(120,90);
        QVERIFY(edge.red()>245);QVERIFY(edge.alpha()>100 && edge.alpha()<180);
        done.clear();fg.effects={blur,zoom};output=dir.filePath("fx-alpha-zoom.mp4");
        exporter.exportSequence({bg,fg},output,false,false,{320,180,30,1});QVERIFY(done.wait(30000));QVERIFY(done.last()[0].toBool());
        boundary=pixel(output,0.2,2,90);QVERIFY(boundary.red()>240);
        // Invalid effect data is rejected before starting FFmpeg or touching the destination.
        done.clear();fg.effects[0].value[0]=100;exporter.exportSequence({fg},output,false);QCOMPARE(done.size(),1);QVERIFY(!done[0][0].toBool());
    }

    void v2BatchCompositingNamesAndPersistence() {
        QTemporaryDir outputDir;QVERIFY(outputDir.isValid());QString error;
        const auto bgBlue=dir.filePath("batch-bg-blue.png"),bgGreen=dir.filePath("batch-bg-green.png");
        QImage image(320,180,QImage::Format_RGB32);image.fill(Qt::blue);QVERIFY(image.save(bgBlue));image.fill(Qt::green);QVERIFY(image.save(bgGreen));
        auto state=queueState(bgBlue,2000);auto background=state.clips[0];background.id=2;background.filePath=bgGreen;background.startMs=2000;state.clips.append(background);
        auto first=queueState(red,3000).clips[0];first.id=3;first.track=1;first.name="same.mp4";first.startMs=1000;first.sourceInMs=200;first.sourceOutMs=800;
        auto zoom=VideoEffect::make(VideoEffect::Zoom);zoom.value[0]=50;first.effects={zoom};
        auto second=first;second.id=4;second.filePath=blue;second.sourceDurationMs=1000;second.sourceInMs=100;second.sourceOutMs=800;second.startMs=2200;
        state.clips.prepend(second);state.clips.append(first); // Storage order differs from timeline order.
        auto sound=queueState(tone,1000).clips[0];sound.id=5;sound.track=3;sound.audioOnly=true;sound.startMs=1500;state.clips.append(sound);
        state.exportRange={true,0,100}; // Batch range must ignore the editor's range.
        QFile existing(outputDir.filePath("same.mp4"));QVERIFY(existing.open(QIODevice::WriteOnly));existing.write("keep");existing.close();
        const auto store=dir.filePath("v2-batch.json");QStringList paths;
        {
            ExportQueue queue(store);QVERIFY2(queue.addV2Clips(state,-1,outputDir.path(),false,&error),qPrintable(error));QCOMPARE(queue.jobs().size(),2);
            QCOMPARE(queue.jobs()[0].snapshot.exportRange.inMs,1000);QCOMPARE(queue.jobs()[0].snapshot.exportRange.outMs,1600);
            QCOMPARE(queue.jobs()[1].snapshot.exportRange.inMs,2200);QCOMPARE(queue.jobs()[1].snapshot.exportRange.outMs,2900);
            QCOMPARE(QFileInfo(queue.jobs()[0].outputPath).fileName(),QString("same (2).mp4"));QCOMPARE(QFileInfo(queue.jobs()[1].outputPath).fileName(),QString("same (3).mp4"));
            QCOMPARE(queue.jobs()[0].name,QString("same.mp4"));QVERIFY(!queue.busy());
            state.clips.clear();for(const auto &job:queue.jobs()){QCOMPARE(job.snapshot.clips.size(),5);paths.append(job.outputPath);}
        }
        ExportQueue restored(store);QVERIFY(restored.available());QCOMPARE(restored.jobs().size(),2);
        QSignalSpy done(&restored,&ExportQueue::batchFinished);QVERIFY(restored.start(&error));QVERIFY(done.wait(30000));
        for(int i=0;i<2;++i) {
            QCOMPARE(restored.jobs()[i].status,ExportJob::Completed);
            FFmpegExporter e;auto frames=run(e.ffprobeExecutable(),{"-v","error","-select_streams","v:0","-show_entries","stream=nb_frames","-of","csv=p=0",paths[i]}).trimmed();
            QCOMPARE(frames.toInt(),i==0?18:21);
        }
        QVERIFY(pixel(paths[0],0.2,160,90).red()>180);QVERIFY(pixel(paths[0],0.2,10,10).blue()>180);
        QVERIFY(pixel(paths[1],0.2,160,90).blue()>180);QVERIFY(pixel(paths[1],0.2,10,10).green()>180);
        FFmpegExporter e;const auto audio=run(e.ffmpegExecutable(),{"-v","error","-i",paths[1],"-vn","-f","s16le","-ac","1","pipe:1"});
        bool audible=false;for(int i=0;i+1<audio.size();i+=2)if(qAbs(qint16(uchar(audio[i])|(uchar(audio[i+1])<<8)))>500){audible=true;break;}QVERIFY(audible);
        QVERIFY(existing.open(QIODevice::ReadOnly));QCOMPARE(existing.readAll(),QByteArray("keep"));
    }
    void v2BatchValidationAndAtomicity() {
        QTemporaryDir outputDir;QString error;const auto store=dir.filePath("v2-atomic.json");ExportQueue queue(store);
        auto state=queueState(red,600);state.clips[0].track=1;state.clips[0].name="CON.mp4";
        auto second=state.clips[0];second.id=2;second.startMs=1000;second.name="a/b:?.mp4";state.clips.append(second);
        auto image=queueState(still,400).clips[0];image.id=3;image.track=1;image.startMs=2000;state.clips.append(image);
        QCOMPARE(ExportQueue::v2Videos(state).size(),2);
        auto empty=queueState(still);empty.clips[0].track=1;QVERIFY(!queue.addV2Clips(empty,-1,outputDir.path(),false,&error));
        QVERIFY(!queue.addV2Clips(state,0,outputDir.path(),false,&error));QVERIFY(!queue.addV2Clips(state,3,outputDir.path(),false,&error));
        QVERIFY(!queue.addV2Clips(state,1,outputDir.filePath("missing"),false,&error));QVERIFY(queue.jobs().isEmpty());
        QVERIFY(queue.addV2Clips(state,1,outputDir.path(),false,&error));QCOMPARE(queue.jobs().size(),1);QCOMPARE(QFileInfo(queue.jobs()[0].outputPath).fileName(),QString("_CON.mp4"));
        QVERIFY(queue.addV2Clips(state,-1,outputDir.path(),false,&error));QCOMPARE(queue.jobs().size(),3);
        QCOMPARE(QFileInfo(queue.jobs()[1].outputPath).fileName(),QString("_CON (2).mp4"));QCOMPARE(QFileInfo(queue.jobs()[2].outputPath).fileName(),QString("a_b__.mp4"));
        QFile journal(store);QVERIFY(journal.open(QIODevice::ReadOnly));const auto bytes=journal.readAll();journal.close();
        auto invalid=state;invalid.clips[1].filePath=dir.filePath("missing-source.mp4");
        QVERIFY(!queue.addV2Clips(invalid,-1,outputDir.path(),false,&error));QCOMPARE(queue.jobs().size(),3);
        QVERIFY(journal.open(QIODevice::ReadOnly));QCOMPARE(journal.readAll(),bytes);journal.close();
        auto many=queueState(red,120);many.clips[0].track=1;
        for(int i=1;i<98;++i){auto clip=many.clips[0];clip.id=i+1;clip.startMs=i*120;many.clips.append(clip);}
        QVERIFY(!queue.addV2Clips(many,-1,outputDir.path(),false,&error));QCOMPARE(queue.jobs().size(),3);
        QVERIFY(QFile::remove(store));QVERIFY(QDir().mkdir(store));
        QVERIFY(!queue.addV2Clips(state,-1,outputDir.path(),false,&error));QCOMPARE(queue.jobs().size(),3);QVERIFY(QDir().rmdir(store));
    }
    void v2BatchDialogWorkflow() {
        QTemporaryDir outputDir;MainWindow window;window.show();auto *timeline=window.findChild<TimelineWidget*>();auto *queue=window.findChild<ExportQueue*>();const int previous=queue->jobs().size();
        timeline->addClip("Later",blue,500,1,2000);timeline->addClip("First",red,700,1,1000);timeline->addClip("Background",still,4000,0,0);
        QTimer::singleShot(30,&window,[&]{
            auto *dialog=window.findChild<QDialog*>("v2BatchDialog");QVERIFY(dialog);QTimer::singleShot(5000,dialog,&QDialog::reject);
            auto *count=dialog->findChild<QSpinBox*>("v2VideoCount");QCOMPARE(count->maximum(),2);count->setValue(2);
            auto *table=dialog->findChild<QTableWidget*>("v2BatchPreview");QCOMPARE(table->rowCount(),2);QCOMPARE(table->item(0,0)->text(),QString("First"));
            dialog->findChild<QLineEdit*>("v2OutputFolder")->setText(outputDir.path());dialog->findChild<QCheckBox*>("v2Hardware")->setChecked(false);
            const auto shot=qEnvironmentVariable("VIDEOFORGE_V2_BATCH_SCREENSHOT");if(!shot.isEmpty())QVERIFY(dialog->grab().save(shot));
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        window.findChild<QPushButton*>("addV2ToExportQueue")->click();QCOMPARE(queue->jobs().size(),previous+2);QVERIFY(!queue->busy());QCOMPARE(queue->jobs()[previous].name,QString("First"));
        QTimer::singleShot(30,&window,[&]{auto *dialog=window.findChild<QDialog*>("v2BatchDialog");QVERIFY(dialog);QTimer::singleShot(5000,dialog,&QDialog::reject);dialog->findChild<QRadioButton*>("v2AllVideos")->setChecked(true);QVERIFY(!dialog->findChild<QSpinBox*>("v2VideoCount")->isEnabled());dialog->findChild<QLineEdit*>("v2OutputFolder")->setText(outputDir.path());dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
        QVERIFY(QMetaObject::invokeMethod(&window,"addV2ExportsToQueue"));QCOMPARE(queue->jobs().size(),previous+4);
        QTimer::singleShot(30,&window,[&]{window.findChild<QDialog*>("v2BatchDialog")->reject();});
        QVERIFY(QMetaObject::invokeMethod(&window,"addV2ExportsToQueue"));QCOMPARE(queue->jobs().size(),previous+4);
        for(int i=queue->jobs().size()-1;i>=previous;--i){QString error;QVERIFY(queue->remove(i,&error));}
    }
    void queueSnapshotsAndSequentialExport() {
        QString store=dir.filePath("queue-sequential.json"),error,first=dir.filePath("queue-first.mp4"),second=dir.filePath("queue-second.mp4");
        {
            ExportQueue queue(store);QVERIFY(queue.available());auto source=queueState(red);
            auto opacity=VideoEffect::make(VideoEffect::Opacity);opacity.value[0]=50;source.clips[0].effects={opacity};
            QVERIFY2(queue.add(source,"First snapshot",first,false,&error),qPrintable(error));
            source.clips[0].filePath=blue;source.clips[0].effects.clear();source.sequence={180,320,30000,1001};
            QVERIFY(queue.add(source,"Second snapshot",second,false,&error));source.clips.clear();
            QCOMPARE(queue.jobs()[0].snapshot.clips[0].filePath,red);QCOMPARE(queue.jobs()[0].snapshot.clips[0].effects[0].value[0],50.0);
            ExportQueue competing(store);QVERIFY(!competing.available());QVERIFY(!competing.start(&error));
            QVector<int> starts;connect(queue.exporter(),&FFmpegExporter::started,this,[&]{starts.append(queue.activeIndex());});
            QSignalSpy done(&queue,&ExportQueue::batchFinished),progress(queue.exporter(),&FFmpegExporter::progressChanged);
            QVERIFY(queue.start(&error));QVERIFY(done.wait(30000));QCOMPARE(starts,QVector<int>({0,1}));QVERIFY(!progress.isEmpty());
            QCOMPARE(queue.jobs()[0].status,ExportJob::Completed);QCOMPARE(queue.jobs()[1].status,ExportJob::Completed);
            auto firstColor=pixel(first,0.2,100,90);QVERIFY(firstColor.red()>105 && firstColor.red()<145 && firstColor.blue()<15);
            FFmpegExporter probe;auto data=run(probe.ffprobeExecutable(),{"-v","error","-select_streams","v:0","-show_entries","stream=width,height,r_frame_rate","-of","csv=p=0",second});
            QVERIFY(data.contains("180,320,30000/1001"));QVERIFY(pixel(second,0.2,90,160).blue()>200);
        }
        ExportQueue restored(store);QVERIFY(restored.available());QCOMPARE(restored.jobs().size(),2);QVERIFY(!restored.busy());
        QCOMPARE(restored.jobs()[0].status,ExportJob::Completed);QCOMPARE(restored.jobs()[0].snapshot.clips[0].effects[0].value[0],50.0);
    }
    void queueFailureContinueAndRetry() {
        QString source=dir.filePath("queue-disappearing.mp4"),error;QVERIFY(QFile::copy(blue,source));
        ExportQueue queue(dir.filePath("queue-retry.json"));
        QVERIFY(queue.add(queueState(source),"Missing later",dir.filePath("queue-retry-first.mp4"),false,&error));
        QVERIFY(queue.add(queueState(red),"Continue after error",dir.filePath("queue-retry-second.mp4"),false,&error));
        QVERIFY(QFile::remove(source));QSignalSpy done(&queue,&ExportQueue::batchFinished);QVERIFY(queue.start(&error));QVERIFY(done.wait(30000));
        QCOMPARE(queue.jobs()[0].status,ExportJob::Failed);QVERIFY(queue.jobs()[0].message.contains("missing",Qt::CaseInsensitive));QCOMPARE(queue.jobs()[1].status,ExportJob::Completed);
        QVERIFY(QFile::copy(blue,source));QVERIFY(queue.retry(0,true,&error));QVERIFY(!queue.jobs()[0].hardware);done.clear();QVERIFY(queue.start(&error));QVERIFY(done.wait(30000));QCOMPARE(queue.jobs()[0].status,ExportJob::Completed);
        QVERIFY(!queue.retry(0,true,&error));QVERIFY(!queue.add(queueState(red),"Duplicate",queue.jobs()[0].outputPath,false,&error));
    }
    void queueStopCancelAndRestart() {
        ExportQueue queue(dir.filePath("queue-cancel.json"));QString error;auto longState=queueState(still,300000);
        QString output=dir.filePath("queue-stopped.mp4");QVERIFY(queue.add(longState,"Stop this",output,false,&error));
        QVERIFY(queue.add(queueState(red),"Keep pending",dir.filePath("queue-resumed.mp4"),false,&error));
        auto connection=connect(queue.exporter(),&FFmpegExporter::started,this,[&]{QTimer::singleShot(50,&queue,&ExportQueue::stop);});
        QVERIFY(queue.start(&error));QTRY_VERIFY_WITH_TIMEOUT(!queue.busy(),10000);disconnect(connection);
        QCOMPARE(queue.jobs()[0].status,ExportJob::Cancelled);QCOMPARE(queue.jobs()[1].status,ExportJob::Pending);QVERIFY(!QFileInfo::exists(output));
        QVERIFY(queue.retry(0,true,&error));QCOMPARE(queue.jobs()[0].status,ExportJob::Pending);QVERIFY(queue.move(0,1,&error));QVERIFY(queue.remove(1,&error));
        QSignalSpy done(&queue,&ExportQueue::batchFinished);QVERIFY(queue.start(&error));QVERIFY(done.wait(30000));QCOMPARE(queue.jobs()[0].status,ExportJob::Completed);
        QVERIFY(queue.add(longState,"Cancel and continue",dir.filePath("queue-cancel-current.mp4"),false,&error));
        QVERIFY(queue.add(queueState(blue),"Next after cancel",dir.filePath("queue-next-after-cancel.mp4"),false,&error));
        connection=connect(queue.exporter(),&FFmpegExporter::started,this,[&]{if(queue.activeIndex()==1)QTimer::singleShot(50,&queue,&ExportQueue::cancelCurrent);});
        done.clear();QVERIFY(queue.start(&error));QVERIFY(done.wait(30000));disconnect(connection);
        QCOMPARE(queue.jobs()[1].status,ExportJob::Cancelled);QCOMPARE(queue.jobs()[2].status,ExportJob::Completed);
    }
    void queuePersistenceAndInterruptedRecovery() {
        QString store=dir.filePath("queue-recovery.json"),error;
        {
            ExportQueue queue(store);QVERIFY(queue.add(queueState(still,300000),"Interrupted",dir.filePath("queue-interrupted.mp4"),false,&error));
            QVERIFY(queue.add(queueState(red),"Pending",dir.filePath("queue-after-reopen.mp4"),false,&error));
            QSignalSpy started(queue.exporter(),&FFmpegExporter::started);QVERIFY(queue.start(&error));QVERIFY(started.wait(10000));
            QVERIFY(!queue.remove(0,&error));queue.shutdown();
        }
        {
            ExportQueue queue(store);QVERIFY(queue.available());QVERIFY(!queue.busy());QCOMPARE(queue.jobs()[0].status,ExportJob::Interrupted);QCOMPARE(queue.jobs()[1].status,ExportJob::Pending);
            QVERIFY(queue.retry(0,true,&error));QVERIFY(queue.remove(0,&error));
            QVERIFY(queue.changeOutput(0,dir.filePath("queue-renamed.mp4"),&error));
        }
        // A saved Running state (e.g. after a crash) must never auto-start.
        QFile file(store);QVERIFY(file.open(QIODevice::ReadOnly));auto root=QJsonDocument::fromJson(file.readAll()).object();file.close();auto entries=root["jobs"].toArray();auto entry=entries[0].toObject();entry["status"]=int(ExportJob::Running);entries[0]=entry;root["jobs"]=entries;
        QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument(root).toJson());file.close();
        {ExportQueue queue(store);QVERIFY(queue.available());QCOMPARE(queue.jobs()[0].status,ExportJob::Interrupted);QVERIFY(!queue.busy());}
        const QByteArray malformed="{broken queue";QVERIFY(file.open(QIODevice::WriteOnly));file.write(malformed);file.close();
        {ExportQueue queue(store);QVERIFY(!queue.available());QVERIFY(!queue.add(queueState(red),"Blocked",dir.filePath("queue-no-corrupt-overwrite.mp4"),false,&error));}
        QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),malformed);
    }
    void queueOutputProtectionAndStorageFailure() {
        QString store=dir.filePath("queue-protect.json"),error,output=dir.filePath("queue-protect.mp4");ExportQueue queue(store);
        auto state=queueState(red);QVERIFY(!queue.add(state,"Source overwrite",red,false,&error));
        QVERIFY(queue.add(state,"Protected",output,false,&error));QVERIFY(!queue.add(state,"Duplicate",output,false,&error));
        // Another program creates the output after enqueue; it must survive.
        QFile file(output);QVERIFY(file.open(QIODevice::WriteOnly));file.write("KEEP");file.close();
        QSignalSpy done(&queue,&ExportQueue::batchFinished);QVERIFY(queue.start(&error));QVERIFY(done.wait(10000));QCOMPARE(queue.jobs()[0].status,ExportJob::Failed);
        QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),QByteArray("KEEP"));file.close();
        QVERIFY(queue.changeOutput(0,dir.filePath("queue-new-output.mp4"),&error));QVERIFY(queue.retry(0,true,&error));
        // Simulate an unwritable journal without changing real OS permissions.
        QVERIFY(QFile::remove(store));QVERIFY(QDir().mkdir(store));QVERIFY(!queue.remove(0,&error));QCOMPARE(queue.jobs().size(),1);QVERIFY(!queue.start(&error));QVERIFY(!queue.busy());QVERIFY(QDir().rmdir(store));
        // Check the later race, after exporter validation but before final rename.
        FFmpegExporter exporter;QSignalSpy finished(&exporter,&FFmpegExporter::finished);QString race=dir.filePath("queue-finalize-race.mp4");
        connect(&exporter,&FFmpegExporter::started,this,[&]{QFile target(race);if(target.open(QIODevice::WriteOnly))target.write("KEEP RACE");});
        exporter.exportSequence({segment(red,0,0,400)},race,false,false,{320,180,30,1},false);QVERIFY(finished.wait(30000));QVERIFY(!finished[0][0].toBool());
        QFile target(race);QVERIFY(target.open(QIODevice::ReadOnly));QCOMPARE(target.readAll(),QByteArray("KEEP RACE"));
        QCOMPARE(QDir(dir.path()).entryList({"queue-finalize-race.mp4.*.part.mp4"},QDir::Files).size(),0);
    }
    void queueEditorDialogWorkflow() {
        MainWindow window;window.show();auto timeline=window.findChild<TimelineWidget*>();auto queue=window.findChild<ExportQueue*>();auto panel=window.findChild<ExportQueueDialog*>();QVERIFY(queue);QVERIFY(queue->available());
        timeline->addClip("Queue UI",red,400,0,0);
        auto opacity=VideoEffect::make(VideoEffect::Opacity);opacity.value[0]=75;QVERIFY(timeline->setClipEffects(0,{opacity}));
        QTimer::singleShot(30,&window,[&]{
            auto dialog=window.findChild<QDialog*>("addExportJobDialog");QVERIFY(dialog);QTimer::singleShot(5000,dialog,&QDialog::reject);
            dialog->findChild<QLineEdit*>("queueOutputPath")->setText(dir.filePath("queue-ui.mp4"));
            dialog->findChild<QSpinBox*>("queueWidth")->setValue(180);dialog->findChild<QSpinBox*>("queueHeight")->setValue(320);
            dialog->findChild<QComboBox*>("queueTimebase")->setCurrentIndex(3);dialog->findChild<QCheckBox*>("queueHardware")->setChecked(false);
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
        });
        QVERIFY(QMetaObject::invokeMethod(&window,"addExportToQueue"));QCOMPARE(queue->jobs().size(),1);QCOMPARE(window.projectState().sequence.width,1280);QCOMPARE(queue->jobs()[0].snapshot.sequence.width,180);
        opacity.value[0]=0;QVERIFY(timeline->setClipEffects(0,{opacity}));QCOMPARE(queue->jobs()[0].snapshot.clips[0].effects[0].value[0],75.0);
        auto start=panel->findChild<QPushButton*>("queueStart");QSignalSpy done(queue,&ExportQueue::batchFinished);QTest::mouseClick(start,Qt::LeftButton);QVERIFY(done.wait(30000));QCOMPARE(queue->jobs()[0].status,ExportJob::Completed);
        QString error;QVERIFY(queue->add(queueState(still,300000),"Waiting sequence",dir.filePath("queue-ui-long.mp4"),false,&error));
        QVERIFY(panel->grab().save("export-queue.png"));QVERIFY(queue->start(&error));QTRY_VERIFY_WITH_TIMEOUT(queue->activeIndex()>=0,5000);
        panel->close();QVERIFY(!panel->isVisible());QVERIFY(queue->busy());
        QTimer::singleShot(30,&window,[]{for(auto widget:QApplication::topLevelWidgets())if(auto box=qobject_cast<QMessageBox*>(widget))box->button(QMessageBox::No)->click();});
        QVERIFY(!window.close());QVERIFY(queue->busy());queue->stop();QTRY_VERIFY_WITH_TIMEOUT(!queue->busy(),10000);
        QVERIFY(window.saveProjectFile(dir.filePath("queue-ui-project.vfproj"),&error));
    }
    void backgroundCompositionLayoutAndUndo() {
        QVector<TimelineClip> clips;QString error;
        QVERIFY(BackgroundComposition::build({{red,3050,false},{blue,1000,false}},{{blue,1000,false}},2000,1000,false,clips,&error));
        QCOMPARE(clips.size(),3);QCOMPARE(clips[0].track,1);QCOMPARE(clips[1].startMs,5050);
        QCOMPARE(clips.last().durationMs(),1000);QCOMPARE(clips.last().startMs,1000);
        for(int i=2;i<clips.size();++i){QCOMPARE(clips[i].track,0);QVERIFY(clips[i].muted);}
        TimelineWidget timeline;timeline.addClip("Existing",red,1000,0,0);
        QVERIFY(timeline.appendComposition(clips));QCOMPARE(timeline.clips().size(),4);
        QVERIFY(timeline.undo());QCOMPARE(timeline.clips().size(),1);
        QVERIFY(timeline.redo());QCOMPARE(timeline.clips().size(),4);
        timeline.setBackgroundAudio(true);QVERIFY(!timeline.clips().last().muted);
        QVERIFY(timeline.undo());QVERIFY(timeline.clips().last().muted);
        ProjectState state;state.clips=timeline.clips();ProjectState loaded;
        QVERIFY2(ProjectFile::fromJson(ProjectFile::toJson(state,dir.path()),dir.path(),loaded,&error),qPrintable(error));
        QCOMPARE(loaded.clips.last().durationMs(),1000);QVERIFY(loaded.clips.last().muted);
        QVERIFY(BackgroundComposition::build({{red,3000,false},{blue,1000,false}},{{still,10000,true}},0,0,false,clips,&error));
        QCOMPARE(clips.size(),3);QCOMPARE(clips.last().durationMs(),10000);QVERIFY(clips.last().image);
        QVERIFY(BackgroundComposition::build({{red,3000,false}},{{blue,1000,false},{still,10000,true}},0,0,true,clips,&error));
        QCOMPARE(clips.size(),3);QCOMPARE(clips.last().durationMs(),10000);QCOMPARE(clips.last().startMs,1000);QVERIFY(!clips.last().muted);
        QVERIFY(!BackgroundComposition::build({{still,10000,true}},{{blue,1000,false}},0,0,false,clips,&error));
        QVERIFY(!BackgroundComposition::build({{red,3000,false}},{{blue,1000,false}},86400000-1000,0,false,clips,&error));
        QVERIFY(!BackgroundComposition::build({{red,3000,false}},{{blue,1000,false}},0,86400000-500,false,clips,&error));
        QCOMPARE(clips.size(),3);QCOMPARE(clips.last().durationMs(),10000); // Failures preserve the result.
    }
    void backgroundAppendsEachTrackIndependently() {
        MainWindow window;QString error;auto *timeline=window.findChild<TimelineWidget*>();
        timeline->addClip("Existing V2",red,1000,1,4000);
        timeline->addClip("Existing V1",blue,1000,0,1000);
        timeline->addClip("Long unrelated A2",tone,1000,4,30000);
        QVERIFY2(window.addBackgroundComposition({red,blue},{blue,still},false,&error),qPrintable(error));
        auto clips=timeline->clips();QCOMPARE(clips.size(),7);
        QCOMPARE(clips[3].track,1);QCOMPARE(clips[3].startMs,5000);QCOMPARE(clips[3].durationMs(),3000);
        QCOMPARE(clips[4].startMs,8000);QCOMPARE(clips[4].durationMs(),1000);
        QCOMPARE(clips[5].track,0);QCOMPARE(clips[5].startMs,2000);QCOMPARE(clips[5].durationMs(),1000);
        QCOMPARE(clips[6].startMs,3000);QCOMPARE(clips[6].durationMs(),10000);QVERIFY(clips[6].muted);
        QCOMPARE(timeline->sequenceDurationMs(),qint64(31000));
        QVERIFY2(window.addBackgroundComposition({blue},{red},true,&error),qPrintable(error));
        clips=timeline->clips();QCOMPARE(clips.size(),9);
        QCOMPARE(clips[7].startMs,9000);QCOMPARE(clips[8].startMs,13000);QCOMPARE(clips[8].durationMs(),3000);QVERIFY(!clips[8].muted);
        QVERIFY(timeline->undo());QCOMPARE(timeline->clips().size(),7);
        QVERIFY(timeline->redo());QCOMPARE(timeline->clips().size(),9);
        const auto path=dir.filePath("independent-tracks.vfproj");QVERIFY(window.saveProjectFile(path,&error));
        QVERIFY(window.loadProjectFile(path,&error));QCOMPARE(timeline->clips()[7].startMs,9000);QCOMPARE(timeline->clips()[8].startMs,13000);
        // A late clip on another track never delays either import track; an empty track starts at zero.
        timeline->clearTimeline();timeline->addClip("Late V2",red,1000,1,7000);
        QVERIFY(window.addBackgroundComposition({blue},{blue},false,&error));
        QCOMPARE(timeline->clips()[1].startMs,8000);QCOMPARE(timeline->clips()[2].startMs,0);
        timeline->clearTimeline();timeline->addClip("Late V1",blue,1000,0,7000);
        QVERIFY(window.addBackgroundComposition({red},{blue},false,&error));
        QCOMPARE(timeline->clips()[1].startMs,0);QCOMPARE(timeline->clips()[2].startMs,8000);
    }
    void editingModeRangeAndPersistence() {
        MainWindow window;window.show();QString error;
        auto *timeline=window.findChild<TimelineWidget*>();
        auto *mode=window.findChild<QComboBox*>("editMode");auto *audio=window.findChild<QCheckBox*>("backgroundAudio");
        QVERIFY(mode);QVERIFY(audio);QCOMPARE(mode->currentIndex(),0);
        QVERIFY2(window.addBackgroundComposition({red,blue},{still},false,&error),qPrintable(error));
        QCOMPARE(mode->currentIndex(),1);QCOMPARE(timeline->clips().size(),3);
        QCOMPARE(timeline->clips()[0].track,1);QVERIFY(timeline->clips()[0].effects.isEmpty());
        QCOMPARE(timeline->clips()[2].track,0);QVERIFY(timeline->clips()[2].muted);
        const int count=timeline->clips().size();mode->setCurrentIndex(0);QCOMPARE(timeline->clips().size(),count);mode->setCurrentIndex(1);
        audio->setChecked(true);QVERIFY(!timeline->clips().last().muted);
        QVERIFY(window.setExportRange(true,500,2500));QVERIFY(!window.setExportRange(true,2500,500));
        auto *preview=window.findChild<RealtimePreview*>();preview->seek(1000);
        window.findChild<QPushButton*>("setExportIn")->click();QCOMPARE(window.projectState().exportRange.inMs,1000);
        preview->seek(2000);window.findChild<QPushButton*>("setExportOut")->click();QCOMPARE(window.projectState().exportRange.outMs,2000);
        QVERIFY(window.setExportRange(true,500,2500));
        auto *scope=window.findChild<QComboBox*>("exportScope");QCOMPARE(scope->currentIndex(),1);
        scope->setCurrentIndex(0);QVERIFY(!window.projectState().exportRange.enabled);scope->setCurrentIndex(1);
        const QString path=dir.filePath("background-range.vfproj");QVERIFY(window.saveProjectFile(path,&error));
        timeline->clearTimeline();QVERIFY(window.loadProjectFile(path,&error));
        QVERIFY(window.projectState().backgroundMode);QVERIFY(window.projectState().exportRange.enabled);
        QCOMPARE(window.projectState().exportRange.inMs,500);QCOMPARE(window.projectState().exportRange.outMs,2500);
        QVERIFY(!timeline->clips().last().muted);QVERIFY(audio->isChecked());
        QVERIFY(window.writeAutosave(&error));ProjectState autosave;QVERIFY(ProjectFile::load(window.autosavePath(),autosave,&error));
        QCOMPARE(autosave.exportRange.outMs,2500);QVERIFY(autosave.backgroundMode);
        auto json=ProjectFile::toJson(autosave,dir.path());auto range=json["exportRange"].toObject();range["outMs"]=999999;json["exportRange"]=range;
        ProjectState rejected;QVERIFY(!ProjectFile::fromJson(json,dir.path(),rejected,&error));
        json=ProjectFile::toJson(autosave,dir.path());json["version"]=3;json.remove("backgroundMode");json.remove("exportRange");
        QVERIFY(ProjectFile::fromJson(json,dir.path(),rejected,&error));QVERIFY(!rejected.backgroundMode);QVERIFY(!rejected.exportRange.enabled);
        const auto before=timeline->clips().size();QVERIFY(!window.addBackgroundComposition({tone},{still},false,&error));QCOMPARE(timeline->clips().size(),before);
        window.findChild<QTabWidget*>("inspectorTabs")->setCurrentIndex(1);
        preview->seek(500);QTRY_VERIFY_WITH_TIMEOUT(preview->compositeFrame().pixelColor(300,170).red()>180,10000);
        const QString shot=qEnvironmentVariable("VIDEOFORGE_MODE_SCREENSHOT");if(!shot.isEmpty())QVERIFY(window.grab().save(shot));
        QVERIFY(window.setExportRange(true,500,3500));timeline->clearTimeline();
        QVERIFY(window.projectState().exportRange.valid(0));QVERIFY(window.saveProjectFile(dir.filePath("empty-range.vfproj"),&error));
    }
    void backgroundPreviewExportAndAudio() {
        QString bg=dir.filePath("background with audio.mp4");
        QVERIFY(generate({"-f","lavfi","-i","color=blue:s=320x180:r=30:d=1","-f","lavfi","-i","sine=frequency=440:duration=1:sample_rate=48000","-c:v","libx264","-c:a","aac",bg}));
        MainWindow window;window.show();QString error;QVERIFY(window.setSequenceSettings({320,180,30,1}));
        QVERIFY2(window.addBackgroundComposition({red},{bg},false,&error),qPrintable(error));
        auto *timeline=window.findChild<TimelineWidget*>();auto zoom=VideoEffect::make(VideoEffect::Zoom);zoom.value[0]=50;
        QVERIFY(timeline->setClipEffects(0,{zoom}));auto *preview=window.findChild<RealtimePreview*>();
        connect(preview,&RealtimePreview::playbackError,[](const QString &error){qWarning()<<error;});preview->seek(500);
        QTRY_VERIFY_WITH_TIMEOUT(preview->compositeFrame().pixelColor(10,10).blue()>180,10000);
        QTRY_VERIFY_WITH_TIMEOUT(preview->compositeFrame().pixelColor(320,180).red()>180,10000);
        FFmpegExporter exporter;QSignalSpy done(&exporter,&FFmpegExporter::finished);
        auto energy=[&](const QString &file) {
            auto pcm=run(exporter.ffmpegExecutable(),{"-v","error","-i",file,"-vn","-ac","1","-ar","48000","-f","s16le","pipe:1"});
            double sum=0;for(int i=0;i+1<pcm.size();i+=2){qint16 v=qint16(uchar(pcm[i]) | (uchar(pcm[i+1])<<8));sum+=double(v)*v;}
            return pcm.isEmpty() ? -1.0 : std::sqrt(sum/(pcm.size()/2));
        };
        auto state=window.projectState();QString output=dir.filePath("background-muted.mp4");
        exporter.exportSequence(ExportQueue::segmentsFor(state),output,false,false,state.sequence,true,{true,500,1500});
        QVERIFY(done.wait(30000));QVERIFY2(done.last()[0].toBool(),qPrintable(done.last()[1].toString()));
        QVERIFY(pixel(output,0.2,10,10).blue()>180);QVERIFY(pixel(output,0.2,160,90).red()>180);
        const double silent=energy(output);QVERIFY(silent>=0 && silent<20);
        // The one-second background ends naturally; it must not repeat under the longer original.
        QVERIFY(pixel(output,0.8,10,10).blue()<20);QVERIFY(pixel(output,0.8,160,90).red()>180);
        timeline->setBackgroundAudio(true);state=window.projectState();done.clear();output=dir.filePath("background-audible.mp4");
        exporter.exportSequence(ExportQueue::segmentsFor(state),output,false,false,state.sequence,true,{true,500,1500});
        QVERIFY(done.wait(30000));QVERIFY(done.last()[0].toBool());QVERIFY(energy(output)>500);
    }
    void exportRangeFramesGapsAndFades() {
        QString longTone=dir.filePath("range-tone.wav");QVERIFY(generate({"-f","lavfi","-i","sine=frequency=440:duration=3:sample_rate=48000",longTone}));
        auto base=segment(red,0,0,3000),overlay=segment(blue,1,1200,1000),sound=segment(longTone,3,0,3000);sound.fadeInMs=2000;
        FFmpegExporter exporter;QSignalSpy done(&exporter,&FFmpegExporter::finished),progress(&exporter,&FFmpegExporter::progressChanged);
        const QString output=dir.filePath("range-composite.mp4");
        exporter.exportSequence({base,overlay,sound},output,false,false,{320,180,30,1},true,{true,1000,2500});
        QVERIFY(done.wait(30000));QVERIFY2(done.last()[0].toBool(),qPrintable(done.last()[1].toString()));
        QVERIFY(pixel(output,0.1).red()>180);QVERIFY(pixel(output,0.5).blue()>180);QVERIFY(pixel(output,1.4).red()>180);
        QCOMPARE(progress.last()[0].toInt(),100);QCOMPARE(progress.last()[2].toLongLong(),1500);
        if(!exporter.hardwareEncoderDisplayName().isEmpty()) {
            done.clear();const QString hw=dir.filePath("range-hardware.mp4");
            exporter.exportSequence({base,overlay,sound},hw,true,false,{320,180,30,1},true,{true,1000,2500});
            QVERIFY(done.wait(30000));QVERIFY2(done.last()[0].toBool(),qPrintable(done.last()[1].toString()));
            QVERIFY(pixel(hw,0.5).blue()>180);
        }
        auto pcm=run(exporter.ffmpegExecutable(),{"-v","error","-i",output,"-vn","-ac","1","-ar","48000","-f","s16le","pipe:1"});
        double energy=0;int count=0;for(int sample=2400;sample<7200 && 2*sample+1<pcm.size();++sample){qint16 v=qint16(uchar(pcm[2*sample]) | (uchar(pcm[2*sample+1])<<8));energy+=double(v)*v;++count;}
        QVERIFY(count>0);QVERIFY(std::sqrt(energy/count)>1000); // Fade continues at original 1s, not restarted at In.
        for(const auto rate:{SequenceRate{30,1,"30"},SequenceRate{30000,1001,"29.97"}}) {
            SequenceSettings settings{320,180,rate.numerator,rate.denominator};done.clear();
            const QString file=dir.filePath(QString("range-frames-%1.mp4").arg(rate.denominator));
            exporter.exportSequence({base},file,false,false,settings,true,{true,settings.frameToMs(17),settings.frameToMs(47)});
            QVERIFY(done.wait(30000));QVERIFY(done.last()[0].toBool());
            auto frames=run(exporter.ffprobeExecutable(),{"-v","error","-select_streams","v:0","-show_entries","stream=nb_frames","-of","csv=p=0",file});
            QCOMPARE(frames.trimmed().toInt(),30);
        }
        done.clear();const QString gap=dir.filePath("range-gap.mp4");base.startMs=2000;
        exporter.exportSequence({base},gap,false,false,{320,180,30,1},true,{true,0,1000});QVERIFY(done.wait(30000));QVERIFY(done.last()[0].toBool());QVERIFY(pixel(gap,0.5).red()<20);
        done.clear();exporter.exportSequence({base},gap,false,false,{320,180,30,1},true,{true,2000,1000});QCOMPARE(done.size(),1);QVERIFY(!done.last()[0].toBool());
    }
    void rangeSeeksSourceAndPreservesFades() {
        const QString source=dir.filePath("seek-source.mp4");
        QVERIFY(generate({"-f","lavfi","-i","testsrc2=s=320x180:r=30:d=12",
            "-f","lavfi","-i","sine=frequency=997:duration=12:sample_rate=48000",
            "-c:v","libx264","-g","120","-pix_fmt","yuv420p","-c:a","aac",source}));
        auto clip=segment(source,0,0,11000);clip.inMs=1000;
        clip.fadeInMs=6000;clip.fadeOutMs=6000;clip.gain=0.7;
        FFmpegExporter exporter;QSignalSpy done(&exporter,&FFmpegExporter::finished);
        QSignalSpy logs(&exporter,&FFmpegExporter::logMessage);
        const SequenceSettings settings{320,180,30,1};
        const QString full=dir.filePath("seek-full.mp4"),cut=dir.filePath("seek-cut.mp4");
        exporter.exportSequence({clip},full,false,false,settings);
        QVERIFY(done.wait(30000));QVERIFY(done.last()[0].toBool());done.clear();logs.clear();
        clip.startMs=3600000;
        exporter.exportSequence({clip},cut,false,false,settings,true,{true,3604000,3606000});
        QVERIFY(done.wait(30000));QVERIFY2(done.last()[0].toBool(),qPrintable(done.last()[1].toString()));
        bool seeks=false;
        for(const auto &row:logs)seeks|=row[0].toString().contains("-ss 5.000000000");
        QVERIFY(seeks);
        auto rgb=[&](const QString &file,const QString &seek) {
            return run(exporter.ffmpegExecutable(),{"-v","error","-ss",seek,"-i",file,
                "-frames:v","1","-f","rawvideo","-pix_fmt","rgb24","pipe:1"});
        };
        for(int offset:{0,5,15,29,50}) {
            const auto expected=rgb(full,QString::number(4+offset/30.0,'f',9));
            const auto actual=rgb(cut,QString::number(offset/30.0,'f',9));
            QCOMPARE(actual.size(),320*180*3);QCOMPARE(expected.size(),actual.size());
            double error=0;for(int n=0;n<actual.size();++n)error+=qAbs(int(uchar(actual[n]))-int(uchar(expected[n])));
            QVERIFY2(error/actual.size()<4.0,qPrintable(QString("Mean pixel error: %1").arg(error/actual.size())));
        }
        auto pcm=[&](const QString &file,const QString &seek) {
            return run(exporter.ffmpegExecutable(),{"-v","error","-ss",seek,"-i",file,"-t","2",
                "-vn","-ac","1","-ar","48000","-f","s16le","pipe:1"});
        };
        auto energy=[](const QByteArray &data,int start,int end) {
            double sum=0;int count=0;
            for(int n=start;n<end && 2*n+1<data.size();++n) {
                const qint16 value=qint16(uchar(data[2*n])|(uchar(data[2*n+1])<<8));
                sum+=double(value)*value;++count;
            }
            return count>0 ? std::sqrt(sum/count) : 0.0;
        };
        const auto expected=pcm(full,"4"),actual=pcm(cut,"0");
        QVERIFY(actual.size()>=190000);
        for(int start:{4800,38400,76800}) {
            const double a=energy(actual,start,start+4800),b=energy(expected,start,start+4800);
            QVERIFY(b>100);QVERIFY2(qAbs(a-b)/b<0.08,"Range export changed the original fade envelope.");
        }
    }
    void rangeSkipsUnrelatedInputsAtLatePositions() {
        QVector<FFmpegExporter::ClipSegment> clips;
        for(int n=0;n<60;++n) {
            auto clip=segment(red,1,qint64(n)*60000,1000);clip.muted=true;clips.append(clip);
        }
        // A transparent still crosses the selected clip, with a nonzero image trim.
        auto image=segment(still,2,3540000,2000);image.image=true;image.inMs=500;image.outMs=2500;
        clips.append(image);
        auto hidden=segment(blue,0,3540000,1000);hidden.visible=false;hidden.muted=true;clips.append(hidden);
        FFmpegExporter exporter;QSignalSpy done(&exporter,&FFmpegExporter::finished),logs(&exporter,&FFmpegExporter::logMessage);
        const QString file=dir.filePath("late-range.mp4");
        exporter.exportSequence(clips,file,false,false,{320,180,30,1},true,{true,3540000,3541000});
        QVERIFY(done.wait(10000));QVERIFY2(done.last()[0].toBool(),qPrintable(done.last()[1].toString()));
        bool pruned=false;for(const auto &row:logs)pruned|=row[0].toString().contains("2 active inputs / 62 timeline clips");
        QVERIFY(pruned);QVERIFY(pixel(file,0.5).red()>180);QVERIFY(pixel(file,0.5,160,90).green()>180);
        done.clear();const QString gap=dir.filePath("late-empty-range.mp4");
        exporter.exportSequence(clips,gap,false,false,{320,180,30000,1001},true,{true,3500000,3501001});
        QVERIFY(done.wait(10000));QVERIFY(done.last()[0].toBool());QVERIFY(pixel(gap,0.5).red()<20);
        auto frames=run(exporter.ffprobeExecutable(),{"-v","error","-select_streams","v:0","-show_entries","stream=nb_frames","-of","csv=p=0",gap});
        QCOMPARE(frames.trimmed().toInt(),30);
    }
    void cachedStillKeepsFractionalTiming() {
        auto background=segment(red,0,0,3000);
        auto foreground=segment(still,1,300,1100);foreground.inMs=200;foreground.image=true;
        auto blur=VideoEffect::make(VideoEffect::Blur);blur.value[0]=1;foreground.effects={blur};
        FFmpegExporter exporter;QSignalSpy done(&exporter,&FFmpegExporter::finished),logs(&exporter,&FFmpegExporter::logMessage);
        const QString output=dir.filePath("cached-still-fractional.mp4");
        exporter.exportSequence({background,foreground},output,false,false,{320,180,30000,1001},true,{true,500,1800});
        QVERIFY(done.wait(30000));QVERIFY2(done.last()[0].toBool(),qPrintable(done.last()[1].toString()));
        QVERIFY(pixel(output,0.1,160,90).green()>180);QVERIFY(pixel(output,1.0,160,90).red()>180);
        bool cached=false;for(const auto &row:logs)cached|=row[0].toString().contains("1 cached stills");QVERIFY(cached);
        const auto frames=run(exporter.ffprobeExecutable(),{"-v","error","-select_streams","v:0","-show_entries","stream=nb_frames","-of","csv=p=0",output});
        QCOMPARE(frames.trimmed().toInt(),39);
    }
    void compactOpaqueZoomMatchesGeneralGeometry() {
        const QString source=dir.filePath("compact-source.mp4");
        QVERIFY(generate({"-f","lavfi","-i","testsrc2=s=320x180:r=30:d=1","-c:v","libx264","-pix_fmt","yuv420p",source}));
        FFmpegExporter exporter;QSignalSpy done(&exporter,&FFmpegExporter::finished),logs(&exporter,&FFmpegExporter::logMessage);
        const QString fit="scale=320:180:force_original_aspect_ratio=decrease,format=rgba,pad=320:180:(ow-iw)/2:(oh-ih)/2:color=black@0,setsar=1";
        for(double amount:{10.0,55.0,100.0,175.0,400.0}) {
            auto zoom=VideoEffect::make(VideoEffect::Zoom);zoom.value[0]=amount;
            auto clip=segment(source,1,0,1000);clip.effects={zoom};
            const QString output=dir.filePath(QString("compact-%1.mp4").arg(amount));done.clear();logs.clear();
            exporter.exportSequence({segment(blue,0,0,1000),clip},output,false,false,{320,180,30,1});
            QVERIFY(done.wait(30000));QVERIFY(done.last()[0].toBool());
            bool compact=false;for(const auto &row:logs)compact|=row[0].toString().contains(QString("%1 compact opaque layers").arg(amount==100.0 ? 0 : 1));QVERIFY(compact);
            QImage layer=QImage::fromData(run(exporter.ffmpegExecutable(),{"-v","error","-filter_threads","1","-ss","0.2","-i",source,
                "-vf",fit+VideoEffects::filters({320,180},clip.effects),"-frames:v","1","-f","image2pipe","-vcodec","png","pipe:1"}));
            QCOMPARE(layer.size(),QSize(320,180));QImage expected(layer.size(),QImage::Format_RGB32);expected.fill(QColor(0,0,254));
            {QPainter painter(&expected);painter.drawImage(0,0,layer);}
            const QImage actual=QImage::fromData(run(exporter.ffmpegExecutable(),{"-v","error","-ss","0.2","-i",output,"-frames:v","1","-f","image2pipe","-vcodec","png","pipe:1"}));
            QCOMPARE(actual.size(),expected.size());double error=0;
            for(int y=0;y<180;++y)for(int x=0;x<320;++x){const auto a=actual.pixelColor(x,y),b=expected.pixelColor(x,y);error+=qAbs(a.red()-b.red())+qAbs(a.green()-b.green())+qAbs(a.blue()-b.blue());}
            QVERIFY2(error/(320*180*3)<6,qPrintable(QString("Zoom %1, RGB error %2").arg(amount).arg(error/(320*180*3))));
        }
        auto opacity=VideoEffect::make(VideoEffect::Opacity);opacity.value[0]=50;
        auto clip=segment(source,1,0,1000);clip.effects={opacity};done.clear();logs.clear();
        exporter.exportSequence({clip},dir.filePath("compact-alpha-fallback.mp4"),false,false,{320,180,30,1});
        QVERIFY(done.wait(30000));QVERIFY(done.last()[0].toBool());
        bool fallback=false;for(const auto &row:logs)fallback|=row[0].toString().contains("0 compact opaque layers");QVERIFY(fallback);
    }
    void sourceProbeCacheRefreshesAfterReplacement() {
        const QString source=dir.filePath("replaced-source.mp4");
        QVERIFY(generate({"-f","lavfi","-i","color=red:s=320x180:r=30:d=1","-c:v","libx264",source}));
        FFmpegExporter exporter;QSignalSpy done(&exporter,&FFmpegExporter::finished),logs(&exporter,&FFmpegExporter::logMessage);
        auto clip=segment(source,0,0,1000);
        exporter.exportSequence({clip},dir.filePath("before-replacement.mp4"),false,false,{320,180,30,1});
        QVERIFY(done.wait(30000));QVERIFY(done.last()[0].toBool());
        // The same source path gains audio and changes aspect ratio.
        QVERIFY(generate({"-f","lavfi","-i","color=red:s=160x180:r=30:d=1","-f","lavfi","-i","sine=frequency=880:duration=1:sample_rate=48000","-c:v","libx264","-c:a","aac",source}));
        done.clear();logs.clear();const QString output=dir.filePath("after-replacement.mp4");
        exporter.exportSequence({clip},output,false,false,{320,180,30,1});
        QVERIFY(done.wait(30000));QVERIFY(done.last()[0].toBool());
        bool fallback=false;for(const auto &row:logs)fallback|=row[0].toString().contains("0 compact opaque layers");QVERIFY(fallback);
        QVERIFY(pixel(output,0.5,160,90).red()>180);QVERIFY(pixel(output,0.5,10,90).red()<20);
        const auto pcm=run(exporter.ffmpegExecutable(),{"-v","error","-i",output,"-vn","-ac","1","-ar","48000","-f","s16le","pipe:1"});
        double energy=0;for(int n=0;n+1<pcm.size();n+=2){const qint16 value=qint16(uchar(pcm[n])|(uchar(pcm[n+1])<<8));energy+=double(value)*value;}
        QVERIFY(!pcm.isEmpty());QVERIFY(std::sqrt(energy/(pcm.size()/2))>500);
    }
    void exportRangeQueueSnapshotAndDialog() {
        QString error;const QString store=dir.filePath("range-queue.json");auto state=queueState(red,3000);state.exportRange={true,500,1500};
        const QString output=dir.filePath("range-queued.mp4");
        {ExportQueue queue(store);QVERIFY(queue.add(state,"One second",output,false,&error));state.exportRange={true,0,2000};QCOMPARE(queue.jobs()[0].snapshot.exportRange.inMs,500);}
        {ExportQueue queue(store);QCOMPARE(queue.jobs()[0].snapshot.exportRange.outMs,1500);QSignalSpy done(&queue,&ExportQueue::batchFinished);QVERIFY(queue.start(&error));QVERIFY(done.wait(30000));QCOMPARE(queue.jobs()[0].status,ExportJob::Completed);}
        FFmpegExporter exporter;const auto duration=run(exporter.ffprobeExecutable(),{"-v","error","-show_entries","format=duration","-of","csv=p=0",output});QVERIFY(qAbs(duration.trimmed().toDouble()-1)<0.05);
        MainWindow window;auto *timeline=window.findChild<TimelineWidget*>();timeline->addClip("Range UI",red,3000,0,0);QVERIFY(window.setExportRange(true,500,1500));
        auto *queue=window.findChild<ExportQueue*>();const int previous=queue->jobs().size();
        QTimer::singleShot(30,&window,[&]{auto *dialog=window.findChild<QDialog*>("addExportJobDialog");QVERIFY(dialog);QTimer::singleShot(5000,dialog,&QDialog::reject);
            QCOMPARE(dialog->findChild<QComboBox*>("queueExportScope")->currentIndex(),1);
            dialog->findChild<QLineEdit*>("queueOutputPath")->setText(dir.filePath("range-dialog.mp4"));
            dialog->findChild<QDoubleSpinBox*>("queueExportIn")->setValue(1.0);dialog->findChild<QDoubleSpinBox*>("queueExportOut")->setValue(2.0);
            dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();});
        QVERIFY(QMetaObject::invokeMethod(&window,"addExportToQueue"));QCOMPARE(queue->jobs().size(),previous+1);
        QCOMPARE(queue->jobs().last().snapshot.exportRange.inMs,1000);QCOMPARE(window.projectState().exportRange.inMs,500);
    }
    void cancellationPreservesDestination() {
        FFmpegExporter e;QString output=dir.filePath("existing.mp4");
        QFile file(output);QVERIFY(file.open(QIODevice::WriteOnly));file.write("KEEP");file.close();
        auto c=segment(still,0,0,300000);c.image=true;
        QSignalSpy done(&e,&FFmpegExporter::finished);
        e.exportSequence({c},output,false); QTimer::singleShot(80,&e,&FFmpegExporter::cancel);
        QVERIFY(done.wait(10000)); QVERIFY(!done.last()[0].toBool());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(),QByteArray("KEEP"));
    }
    void rejectsSourceOverwrite() {
        FFmpegExporter e; QSignalSpy done(&e,&FFmpegExporter::finished);
        e.exportSequence({segment(red,0,0,1000)},red,false);
        QCOMPARE(done.size(),1);QVERIFY(!done.last()[0].toBool());
    }
};
QTEST_MAIN(EditorTests)
#include "EditorTests.moc"
