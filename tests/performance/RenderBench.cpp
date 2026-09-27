#include "FFmpegExporter.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>
#include <QFile>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();if(args.size()!=4)return 2;
    const auto scenario=args[3];FFmpegExporter exporter;
    FFmpegExporter::ClipSegment foreground;foreground.inputPath=args[1]+"/source.mp4";foreground.outMs=6000;foreground.muted=true;foreground.track=1;
    QVector<FFmpegExporter::ClipSegment> clips;
    if(scenario!="single") {
        FFmpegExporter::ClipSegment background;background.inputPath=args[1]+(scenario=="video" ? "/source.mp4" : "/background.png");background.outMs=6000;background.muted=true;background.image=scenario!="video";
        if(scenario=="blur") {auto blur=VideoEffect::make(VideoEffect::Blur);blur.value[0]=1;background.effects.append(blur);}
        auto zoom=VideoEffect::make(VideoEffect::Zoom);zoom.value[0]=80;foreground.effects.append(zoom);clips.append(background);
    }
    clips.append(foreground);
    QEventLoop loop;QElapsedTimer clock;clock.start();bool success=false;QString log;
    QObject::connect(&exporter,&FFmpegExporter::logMessage,[&](const QString &s){log+=s+"\n";});
    QObject::connect(&exporter,&FFmpegExporter::finished,[&](bool ok,const QString &message){success=ok;if(!ok)qWarning()<<message<<log;loop.quit();});
    QTimer timeout;timeout.setSingleShot(true);QObject::connect(&timeout,&QTimer::timeout,&exporter,&FFmpegExporter::cancel);timeout.start(120000);
    const QString output=args[1]+"/"+args[2]+"-"+scenario+".mp4";
    exporter.exportSequence(clips,output,false,false,{1920,1080,30,1},true,{true,0,6000});
    if(exporter.isRunning())loop.exec();
    qInfo().noquote()<<QString("%1 scenario=%2 total_ms=%3 ok=%4").arg(args[2],scenario).arg(clock.elapsed()).arg(success);
    QFile f(output+".log");if(f.open(QIODevice::WriteOnly))f.write(log.toUtf8());return success?0:1;
}
