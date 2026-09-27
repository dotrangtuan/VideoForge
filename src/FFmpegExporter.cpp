#include "FFmpegExporter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtGlobal>
#include <QStringList>
#include <algorithm>
#include <QUuid>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QThread>

FFmpegExporter::FFmpegExporter(QObject *parent)
    : QObject(parent)
{
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { QFile::remove(m_workPath); emit finished(false, "Could not start FFmpeg: " + m_process.errorString()); }
    });
    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &FFmpegExporter::readProgress);
    connect(&m_process, &QProcess::readyReadStandardError,
            this, &FFmpegExporter::readStderr);
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &FFmpegExporter::processFinished);
}

FFmpegExporter::~FFmpegExporter() {
    if(isRunning()) { m_process.kill(); m_process.waitForFinished(3000); }
    if(!m_workPath.isEmpty()) QFile::remove(m_workPath);
}

bool FFmpegExporter::isRunning() const
{
    return m_process.state() != QProcess::NotRunning;
}

QString FFmpegExporter::findBundledOrPathTool(const QString &baseName) const
{
    const QString appDir = QCoreApplication::applicationDirPath();
    QStringList candidates;

#ifdef Q_OS_WIN
    const QString exeName = baseName + ".exe";
    candidates << QDir(appDir).filePath(exeName)
               << QDir(appDir).filePath("bin/" + exeName)
               << QDir(appDir).filePath("ffmpeg/" + exeName);
#elif defined(Q_OS_MACOS)
    candidates << QDir(appDir).filePath(baseName)
               << QDir(appDir).filePath("../Resources/bin/" + baseName)
               << QDir(appDir).filePath("../Resources/" + baseName);
#else
    candidates << QDir(appDir).filePath(baseName)
               << QDir(appDir).filePath("bin/" + baseName);
#endif

    for (const QString &candidate : candidates) {
        const QFileInfo info(QDir::cleanPath(candidate));
        if (info.exists() && info.isFile() && info.isExecutable())
            return info.absoluteFilePath();
    }

    return QStandardPaths::findExecutable(baseName);
}

QString FFmpegExporter::ffmpegExecutable() const
{
    return findBundledOrPathTool("ffmpeg");
}

QString FFmpegExporter::ffprobeExecutable() const
{
    return findBundledOrPathTool("ffprobe");
}

bool FFmpegExporter::encoderListed(const QString &codec) const
{
    if (!m_encoderListLoaded) {
        m_encoderListLoaded = true;
        const QString ffmpeg = ffmpegExecutable();
        if (!ffmpeg.isEmpty()) {
            QProcess process;
            process.start(ffmpeg, {"-hide_banner", "-encoders"});
            if (process.waitForFinished(5000)) {
                m_encoderList = QString::fromUtf8(process.readAllStandardOutput())
                              + QString::fromUtf8(process.readAllStandardError());
            } else {
                process.kill();
                process.waitForFinished(500);
            }
        }
    }

    if (m_encoderList.isEmpty())
        return false;

    const QRegularExpression re(
        QString(R"(^\s*[A-Z\.]{6}\s+%1(?:\s|$))")
            .arg(QRegularExpression::escape(codec)),
        QRegularExpression::MultilineOption);
    return re.match(m_encoderList).hasMatch();
}

bool FFmpegExporter::encoderSmokeTest(const QString &codec, QString *reason) const
{
    const QString ffmpeg = ffmpegExecutable();
    if (ffmpeg.isEmpty()) {
        if (reason)
            *reason = "FFmpeg was not found.";
        return false;
    }

    QProcess process;
    QStringList args {
        "-hide_banner", "-loglevel", "error",
        "-f", "lavfi",
        "-i", "color=c=black:s=1280x720:r=30:d=0.1",
        "-frames:v", "1",
        "-an",
        "-c:v", codec,
        "-f", "null", "-"
    };

    process.start(ffmpeg, args);
    if (!process.waitForFinished(3500)) {
        process.kill();
        process.waitForFinished(500);
        if (reason)
            *reason = "Timed out while opening the encoder.";
        return false;
    }

    const bool ok = process.exitStatus() == QProcess::NormalExit
        && process.exitCode() == 0;
    if (!ok && reason) {
        QString error = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
        if (error.isEmpty())
            error = process.errorString().trimmed();
        if (error.isEmpty())
            error = QString("FFmpeg exited with code %1.").arg(process.exitCode());
        error.replace(QRegularExpression("\\s+"), " ");
        if (error.size() > 260)
            error = error.left(257) + "...";
        *reason = error;
    }
    return ok;
}

FFmpegExporter::EncoderChoice FFmpegExporter::detectHardwareEncoder() const
{
    if (m_hardwareDetectionDone)
        return m_cachedHardwareEncoder;

    m_hardwareDetectionDone = true;

    struct Candidate {
        const char *codec;
        const char *display;
    };

#ifdef Q_OS_MACOS
    const Candidate candidates[] = {
        {"h264_videotoolbox", "Apple VideoToolbox"}
    };
#elif defined(Q_OS_WIN)
    // Prefer discrete NVIDIA first, then Intel iGPU, then AMD.
    // We do a one-frame smoke test so an encoder merely compiled into FFmpeg
    // is not selected when the matching GPU/driver is absent.
    const Candidate candidates[] = {
        {"h264_nvenc", "NVIDIA NVENC"},
        {"h264_qsv", "Intel Quick Sync"},
        {"h264_amf", "AMD AMF"}
    };
#else
    const Candidate candidates[] = {
        {"h264_vaapi", "VA-API"}
    };
#endif

    QStringList diagnostics;
    for (const Candidate &candidate : candidates) {
        const QString codec = QString::fromLatin1(candidate.codec);
        const QString display = QString::fromLatin1(candidate.display);
        if (!encoderListed(codec)) {
            diagnostics << QString("%1: not listed by FFmpeg.").arg(display);
            continue;
        }
        QString reason;
        if (!encoderSmokeTest(codec, &reason)) {
            diagnostics << QString("%1: %2").arg(display, reason);
            continue;
        }

        m_cachedHardwareEncoder.codec = codec;
        m_cachedHardwareEncoder.displayName = display;
        m_cachedHardwareEncoder.hardware = true;
        diagnostics << QString("%1: available.").arg(display);
        m_hardwareEncoderDiagnostics = diagnostics.join('\n');
        return m_cachedHardwareEncoder;
    }

    m_hardwareEncoderDiagnostics = diagnostics.join('\n');
    return m_cachedHardwareEncoder;
}

QString FFmpegExporter::hardwareEncoderDisplayName() const
{
    return detectHardwareEncoder().displayName;
}

QString FFmpegExporter::hardwareEncoderDiagnostics() const
{
    detectHardwareEncoder();
    return m_hardwareEncoderDiagnostics;
}

FFmpegExporter::EncoderChoice
FFmpegExporter::chooseEncoder(bool preferHardware) const
{
    if (preferHardware) {
        const EncoderChoice hardware = detectHardwareEncoder();
        if (!hardware.codec.isEmpty())
            return hardware;
    }

    EncoderChoice cpu;
    cpu.codec = "libx264";
    cpu.displayName = "CPU / libx264";
    cpu.hardware = false;
    return cpu;
}

FFmpegExporter::SourceInfo FFmpegExporter::probeSource(const QString &path) const
{
    const QFileInfo file(path);
    const QString key=file.absoluteFilePath();
    const qint64 modified=file.lastModified().toMSecsSinceEpoch();
    const auto cached=m_sourceInfo.constFind(key);
    if(cached!=m_sourceInfo.constEnd() && cached->fileSize==file.size() && cached->modifiedMs==modified)
        return cached.value();

    SourceInfo info;
    info.fileSize=file.size();info.modifiedMs=modified;
    const QString ffprobe=ffprobeExecutable();
    if(ffprobe.isEmpty()) return info;
    QProcess probe;
    probe.start(ffprobe,{"-v","error","-show_entries",
        "stream=codec_type,width,height,pix_fmt:stream_side_data=rotation:stream_tags=rotate",
        "-of","json",path});
    if(!probe.waitForFinished(5000)) {probe.kill();probe.waitForFinished(500);return info;}
    if(probe.exitStatus()!=QProcess::NormalExit || probe.exitCode()!=0) return info;
    QJsonParseError error;
    const auto document=QJsonDocument::fromJson(probe.readAllStandardOutput(),&error);
    if(error.error!=QJsonParseError::NoError || !document.object()["streams"].isArray()) return info;
    info.hasAudio=false;
    // Only formats known to have no alpha can use the compact opaque path.
    // Unknown formats and rotated sources retain the general alpha compositor.
    const QStringList opaqueFormats{"yuv420p","yuvj420p","yuv422p","yuvj422p","yuv444p","yuvj444p",
        "nv12","nv21","rgb24","bgr24","gbrp","gray","yuv420p10le","yuv422p10le","yuv444p10le","p010le"};
    bool firstVideo=true;
    for(const auto &value:document.object()["streams"].toArray()) {
        const auto stream=value.toObject();
        if(stream["codec_type"].toString()=="audio")info.hasAudio=true;
        if(stream["codec_type"].toString()!="video" || !firstVideo)continue;
        firstVideo=false;
        info.frameSize=QSize(stream["width"].toInt(),stream["height"].toInt());
        bool rotated=stream["tags"].toObject()["rotate"].toString().toDouble()!=0.0;
        for(const auto &side:stream["side_data_list"].toArray())
            rotated|=side.toObject()["rotation"].toDouble()!=0.0;
        info.opaque=!rotated && opaqueFormats.contains(stream["pix_fmt"].toString());
    }
    if(m_sourceInfo.size()>=128)m_sourceInfo.clear();
    m_sourceInfo.insert(key,info);
    return info;
}

void FFmpegExporter::exportSequence(const QVector<ClipSegment> &clips,
                                    const QString &outputPath,
                                    bool useHardwareEncoder, bool preview, const SequenceSettings &settings, bool overwrite, const ExportRange &range)
{
    if (isRunning())
        return;

    m_overwrite=overwrite;
    if(!overwrite && QFileInfo::exists(outputPath)){emit finished(false,"Destination already exists; choose a new name.");return;}
    const QString ffmpeg = ffmpegExecutable();
    if (ffmpeg.isEmpty()) {
#ifdef Q_OS_WIN
        emit finished(false,
            "FFmpeg was not found. Put ffmpeg.exe and ffprobe.exe beside "
            "VideoForgeEditor.exe, or add FFmpeg to PATH.");
#elif defined(Q_OS_MACOS)
        emit finished(false,
            "FFmpeg was not found. Install it with: brew install ffmpeg");
#else
        emit finished(false,
            "FFmpeg was not found. Install FFmpeg or add it to PATH.");
#endif
        return;
    }

    if (clips.isEmpty()) {
        emit finished(false, "Timeline is empty.");
        return;
    }

    m_totalMs = 0;
    for (const auto &c: clips) {
        if(!VideoEffects::valid(c.effects)) {emit finished(false,"Invalid video effects.");return;}
        if(!QFileInfo::exists(c.inputPath) || c.inMs<0 || c.outMs<=c.inMs || c.startMs<0 || c.track<0 || c.track>4) {
            emit finished(false,"Invalid or missing timeline source: "+c.inputPath); return;
        }
        if(QFileInfo(c.inputPath).canonicalFilePath()==QFileInfo(outputPath).canonicalFilePath()) {
            emit finished(false,"Choose an output different from every source file."); return;
        }
        m_totalMs=qMax(m_totalMs,c.startMs+c.outMs-c.inMs);
    }
    if(!settings.valid()) {emit finished(false,"Invalid sequence frame size or timebase.");return;}
    const qint64 sequenceMs=m_totalMs;
    if(!range.valid(sequenceMs)) {emit finished(false,"Invalid export In/Out range.");return;}
    const qint64 rangeStart=range.enabled ? range.inMs : 0;
    const qint64 rangeEnd=range.enabled ? range.outMs : sequenceMs;
    m_totalMs=rangeEnd-rangeStart;
    const qint64 firstFrame=settings.nearestFrame(rangeStart),lastFrame=settings.nearestFrame(rangeEnd);
    if(range.enabled && lastFrame<=firstFrame){emit finished(false,"Export range must contain at least one frame.");return;}
    if(range.enabled)m_totalMs=settings.frameToMs(lastFrame-firstFrame);
    // Range jobs use a local clock. Never render all frames before In merely
    // to discard them later: that made each successive V2 job slower to start.
    // Keep frame boundaries as rational seconds, not rounded milliseconds.
    const double renderStart=range.enabled ? double(firstFrame)*settings.fpsDen/settings.fpsNum : 0.0;
    const double renderEnd=range.enabled ? double(lastFrame)*settings.fpsDen/settings.fpsNum : rangeEnd/1000.0;
    const double renderDuration=renderEnd-renderStart;
    const QSize size=preview ? settings.monitorSize() : settings.frameSize();
    const int outW=size.width(),outH=size.height();
    const QString fps=settings.rateString();
    auto seconds=[](qint64 ms) { return QString::number(ms/1000.0,'f',6); };
    auto time=[](double value) { return QString::number(value,'f',9); };
    const int filterThreads=outW*outH>=1280*720 ? qBound(1,QThread::idealThreadCount()/2,4) : 1;
    QStringList args{"-y","-filter_complex_threads",QString::number(filterThreads)};
    QStringList filters;
    filters << QString("color=c=black:s=%1x%2:r=%4:d=%3[base]").arg(outW).arg(outH).arg(time(renderDuration)).arg(fps);
    filters << QString("anullsrc=r=48000:cl=stereo,atrim=duration=%1[silence]").arg(time(renderDuration));
    QString video="base", audio="[silence]";
    int audioCount=1, input=0, layer=0, cachedStills=0, compactLayers=0;
    auto ordered=clips;
    std::stable_sort(ordered.begin(),ordered.end(),[](const ClipSegment &a,const ClipSegment &b) { return a.track<b.track; });
    for(const auto &c:ordered) {
        const double clipStart=c.startMs/1000.0;
        const double clipDuration=(c.outMs-c.inMs)/1000.0;
        const double overlapStart=qMax(clipStart,renderStart);
        const double overlapEnd=qMin(clipStart+clipDuration,renderEnd);
        if(overlapEnd<=overlapStart) continue;
        const bool hasVideo=c.track<3 && c.visible;
        if(!hasVideo && (c.muted || c.image))continue;
        const SourceInfo source=probeSource(c.inputPath);
        const bool hasAudio=!c.muted && !c.image && source.hasAudio;
        if(!hasVideo && !hasAudio) continue;

        const double skipped=overlapStart-clipStart;
        const double duration=overlapEnd-overlapStart;
        const double localStart=overlapStart-renderStart;
        // Still images have no source clock to seek; loop only the needed time.
        // Input -ss seeks at the demuxer, with FFmpeg's default accurate seek
        // decoding only the keyframe lead-in instead of the entire source prefix.
        const double sourceStart=c.image ? 0.0 : c.inMs/1000.0+skipped;
        // Decode a still once. Repeat the transformed frame AFTER its effects.
        if(c.image) args << "-framerate" << fps;
        if(range.enabled && !c.image && sourceStart>0.0)
            args << "-ss" << time(sourceStart);
        // One second of decode headroom lets fps/aresample flush at the cut.
        if(range.enabled) args << "-t" << time(duration+1.0);
        args << "-i" << c.inputPath;
        const double trimStart=range.enabled ? 0.0 : c.inMs/1000.0;
        const double trimEnd=range.enabled ? duration : c.outMs/1000.0;
        if(hasVideo) {
            QString v=QString("v%1").arg(input), next=QString("layer%1").arg(layer++);
            // With an opaque, canvas-filling input and at most one effective
            // zoom, transparency math and full-canvas padding are unnecessary.
            // Keep planar RGB scaling, the same integer geometry, and RGB
            // overlay; alpha/blur/crop stacks retain the original general path.
            double zoom=100.0;
            bool simpleZoom=true,hasZoom=false;
            for(const auto &effect:c.effects) {
                if(!effect.enabled)continue;
                if(effect.type==VideoEffect::Zoom) {
                    if(effect.value[0]==100.0)continue;
                    if(hasZoom){simpleZoom=false;break;}
                    hasZoom=true;zoom=effect.value[0];
                } else if(effect.type==VideoEffect::Opacity && effect.value[0]==100.0)continue;
                else if(effect.type==VideoEffect::Blur && effect.value[0]==0.0)continue;
                else if(effect.type==VideoEffect::Crop && effect.value==std::array<double,4>{{0,0,0,0}})continue;
                else {simpleZoom=false;break;}
            }
            const bool compact=source.opaque && source.frameSize.isValid() && simpleZoom && hasZoom
                && qint64(source.frameSize.width())*outH==qint64(source.frameSize.height())*outW;
            int overlayX=0,overlayY=0;
            QString geometry;
            if(compact) {
                ++compactLayers;
                geometry=QString("scale=%1:%2,format=gbrp,setsar=1").arg(outW).arg(outH);
                if(zoom!=100.0) {
                    const double factor=zoom<100.0 ? zoom/100.0 : 100.0/zoom;
                    const int width=qMax(1,qRound(outW*factor)),height=qMax(1,qRound(outH*factor));
                    if(zoom<100.0) {
                        geometry+=QString(",scale=%1:%2:flags=bilinear").arg(width).arg(height);
                        overlayX=(outW-width)/2;overlayY=(outH-height)/2;
                    } else {
                        geometry+=QString(",crop=%1:%2:%3:%4:exact=1,scale=%5:%6:flags=bilinear")
                            .arg(width).arg(height).arg((outW-width)/2).arg((outH-height)/2).arg(outW).arg(outH);
                    }
                }
                geometry+=",format=rgb24";
            } else {
                geometry=QString("scale=%1:%2:force_original_aspect_ratio=decrease,format=rgba,pad=%1:%2:(ow-iw)/2:(oh-ih)/2:color=black@0,setsar=1")
                    .arg(outW).arg(outH)+VideoEffects::filters(size,c.effects);
            }
            const QString trim=c.image ? QString("trim=end_frame=1")
                : QString("trim=start=%1:end=%2").arg(time(trimStart),time(trimEnd));
            QString repetition;
            if(c.image) {
                ++cachedStills;
                repetition=QString(",loop=loop=-1:size=1:start=0,settb=expr=%1/%2,setpts=N,trim=duration=%3")
                    .arg(settings.fpsDen).arg(settings.fpsNum).arg(time(duration));
            }
            filters << QString("[%1:v:0]%2,setpts=PTS-STARTPTS,%3%4,fps=%5,setpts=PTS+%6/TB[%7]")
                .arg(input).arg(trim,geometry,repetition,fps,time(localStart),v);
            filters << QString("[%1][%2]overlay=x=%3:y=%4:format=rgb:eof_action=pass:repeatlast=0:enable='gte(t,%5)*lt(t,%6)'[%7]")
                .arg(video,v).arg(overlayX).arg(overlayY).arg(time(localStart),time(overlapEnd-renderStart),next);
            video=next;
        }
        if(hasAudio) {
            QString a=QString("a%1").arg(input);
            QString envelope="volume="+QString::number(c.gain,'f',6);
            const qint64 originalDuration=c.outMs-c.inMs;
            if(c.fadeInMs>0) envelope+=",afade=t=in:st=0:d="+seconds(qMin(originalDuration,c.fadeInMs));
            if(c.fadeOutMs>0) envelope+=",afade=t=out:st="+seconds(qMax<qint64>(0,originalDuration-c.fadeOutMs))+":d="+seconds(qMin(originalDuration,c.fadeOutMs));
            // Evaluate fades on the ORIGINAL clip clock, then reset for mixing.
            // Restarting the fade at the range boundary changes the soundtrack.
            const qint64 delaySamples=qRound64(localStart*48000.0);
            filters << QString("[%1:a:0]atrim=start=%2:end=%3,asetpts=PTS-STARTPTS+%4/TB,aresample=48000,aformat=sample_fmts=fltp:sample_rates=48000:channel_layouts=stereo,%5,asetpts=PTS-STARTPTS,adelay=%6S:all=1,apad,atrim=duration=%7[%8]")
                .arg(input).arg(time(trimStart)).arg(time(trimEnd)).arg(time(skipped)).arg(envelope).arg(delaySamples).arg(time(renderDuration)).arg(a);
            audio+="["+a+"]"; ++audioCount;
        }
        ++input;
    }
    const QString videoTrim=range.enabled ? QString("trim=end_frame=%1,").arg(lastFrame-firstFrame) : QString();
    filters << QString("[%1]%2setpts=PTS-STARTPTS,format=yuv420p[vout]").arg(video,videoTrim);
    filters << QString("%1amix=inputs=%2:duration=longest:dropout_transition=0,volume=%2,aeval='clip(val(0),-0.95,0.95)|clip(val(1),-0.95,0.95)':c=stereo,atrim=duration=%3,asetpts=PTS-STARTPTS[aout]")
        .arg(audio).arg(audioCount).arg(time(renderDuration));
    m_filterFile=std::make_unique<QTemporaryFile>(QDir::tempPath()+"/videoforge-XXXXXX.ffgraph");
    if(!m_filterFile->open()) { emit finished(false,"Cannot create FFmpeg filter file."); return; }
    m_filterFile->write(filters.join(';').toUtf8()); m_filterFile->flush(); m_filterFile->close();
    const QString outputSeconds=range.enabled ? QString::number(double(lastFrame-firstFrame)*settings.fpsDen/settings.fpsNum,'f',9) : seconds(m_totalMs);
    args << "-filter_complex_script" << m_filterFile->fileName() << "-map" << "[vout]" << "-map" << "[aout]" << "-t" << outputSeconds;

    EncoderChoice encoder = chooseEncoder(useHardwareEncoder);

    // If a Windows/macOS user explicitly requested hardware but no usable
    // hardware encoder is present, transparently fall back to libx264.
    if (useHardwareEncoder && !encoder.hardware) {
        emit logMessage(
            "No usable hardware H.264 encoder detected; falling back to CPU/libx264.");
    }

    args << "-r" << fps << "-vsync" << "cfr" << "-video_track_timescale" << QString::number(settings.fpsNum);
    args << "-c:v" << encoder.codec;
    if (encoder.hardware) {
        // Keep hardware options intentionally conservative so the same
        // timeline export works across NVENC, QSV, AMF and VideoToolbox.
        args << "-b:v" << "10M";
    } else {
        args << "-preset" << (preview ? "ultrafast" : "medium")
             << "-crf" << "18";
    }

    args << "-ac" << "2" << "-ar" << "48000" << "-c:a" << "aac"
         << "-b:a" << "192k"
         << "-movflags" << "+faststart"
         << "-progress" << "pipe:1"
         << "-nostats"
         << (m_workPath = outputPath + "." + QUuid::createUuid().toString(QUuid::Id128) + ".part.mp4");

    m_processedMs = 0;
    m_speed = 0.0;
    m_outputPath = outputPath;
    m_cancelled = false;
    m_progressBuffer.clear();
    m_elapsedTimer.restart();

    emit logMessage(QString("Platform encoder: %1 [%2]")
                        .arg(encoder.displayName, encoder.codec));
    emit logMessage(encoder.hardware ? "Encode settings: hardware H.264, target 10 Mbps; AAC 192 kbps"
        : QString("Encode settings: libx264, preset %1, CRF 18; AAC 192 kbps").arg(preview ? "ultrafast" : "medium"));
    emit logMessage(QString("Sequence: %1 clips, %2x%3 @ %4 fps")
                        .arg(clips.size()).arg(outW).arg(outH).arg(fps));
    emit logMessage(QString("Range renderer: %1 active inputs / %2 timeline clips; local duration %3 seconds").arg(input).arg(clips.size()).arg(time(renderDuration)));
    emit logMessage(QString("Render pipeline: %1 filter threads, %2 cached stills, %3 compact opaque layers; software decode/composite")
        .arg(filterThreads).arg(cachedStills).arg(compactLayers));
    emit logMessage("ffmpeg " + args.join(' '));
    emit logMessage(range.enabled ? QString("Export range: %1 - %2 seconds (Out exclusive)").arg(seconds(rangeStart),seconds(rangeEnd)) : "Export range: entire timeline");
    emit started();
    emit progressChanged(0, 0, m_totalMs, 0.0, 0, -1);
    m_process.start(ffmpeg, args);
}

void FFmpegExporter::cancel()
{
    if (!isRunning())
        return;

    m_cancelled = true;
    m_process.kill();
}

void FFmpegExporter::readProgress()
{
    m_progressBuffer += m_process.readAllStandardOutput();

    int newlineIndex = -1;
    while ((newlineIndex = m_progressBuffer.indexOf('\n')) >= 0) {
        const QByteArray lineBytes = m_progressBuffer.left(newlineIndex);
        m_progressBuffer.remove(0, newlineIndex + 1);
        handleProgressLine(QString::fromUtf8(lineBytes).trimmed());
    }
}

void FFmpegExporter::handleProgressLine(const QString &line)
{
    if (line.isEmpty())
        return;

    const int equals = line.indexOf('=');
    if (equals <= 0)
        return;

    const QString key = line.left(equals);
    const QString value = line.mid(equals + 1);
    bool shouldEmit = false;

    if (key == "out_time") {
        const qint64 parsed = parseFfmpegTime(value);
        if (parsed >= 0) {
            m_processedMs = qBound<qint64>(0, parsed, m_totalMs);
            shouldEmit = true;
        }
    } else if (key == "speed") {
        QString normalized = value;
        normalized.remove('x');
        bool ok = false;
        const double parsedSpeed = normalized.toDouble(&ok);
        if (ok)
            m_speed = parsedSpeed;
    } else if (key == "progress" && value == "end") {
        m_processedMs = m_totalMs;
        shouldEmit = true;
    }

    if (!shouldEmit || m_totalMs <= 0)
        return;

    const int percent = qBound(0,
                               static_cast<int>((m_processedMs * 100) / m_totalMs),
                               100);
    const qint64 elapsedMs = m_elapsedTimer.isValid() ? m_elapsedTimer.elapsed() : 0;

    qint64 remainingMs = -1;
    if (m_processedMs > 0 && elapsedMs > 0 && m_processedMs < m_totalMs) {
        const double realtimeRate = static_cast<double>(m_processedMs) /
                                    static_cast<double>(elapsedMs);
        if (realtimeRate > 0.0) {
            remainingMs = static_cast<qint64>(
                static_cast<double>(m_totalMs - m_processedMs) / realtimeRate);
        }
    } else if (m_processedMs >= m_totalMs) {
        remainingMs = 0;
    }

    emit progressChanged(percent,
                         m_processedMs,
                         m_totalMs,
                         m_speed,
                         elapsedMs,
                         remainingMs);
}

qint64 FFmpegExporter::parseFfmpegTime(const QString &value) const
{
    static const QRegularExpression re(
        R"(^(\d+):(\d+):(\d+(?:\.\d+)?)$)");
    const QRegularExpressionMatch match = re.match(value);
    if (!match.hasMatch())
        return -1;

    bool okH = false;
    bool okM = false;
    bool okS = false;
    const qint64 hours = match.captured(1).toLongLong(&okH);
    const qint64 minutes = match.captured(2).toLongLong(&okM);
    const double seconds = match.captured(3).toDouble(&okS);
    if (!okH || !okM || !okS)
        return -1;

    return static_cast<qint64>(
        (hours * 3600.0 + minutes * 60.0 + seconds) * 1000.0);
}

void FFmpegExporter::readStderr()
{
    const QString text = QString::fromUtf8(
                             m_process.readAllStandardError()).trimmed();
    if (!text.isEmpty())
        emit logMessage(text);
}

void FFmpegExporter::processFinished(int exitCode,
                                     QProcess::ExitStatus exitStatus)
{
    readProgress();
    readStderr();

    if (m_cancelled) {
        if (!m_outputPath.isEmpty())
            QFile::remove(m_workPath);
        emit finished(false, "Export cancelled.");
        return;
    }

    const bool success =
        (exitStatus == QProcess::NormalExit && exitCode == 0);

    if (success) {
        if(QFile::exists(m_outputPath) && (!m_overwrite || !QFile::remove(m_outputPath))) { QFile::remove(m_workPath);emit finished(false,"Destination exists or cannot be replaced; choose a new name."); return; }
        if(!QFile::rename(m_workPath,m_outputPath)) { QFile::remove(m_workPath);emit finished(false,"Cannot finalize output file."); return; }
        emit progressChanged(100,
                             m_totalMs,
                             m_totalMs,
                             m_speed,
                             m_elapsedTimer.isValid()
                                 ? m_elapsedTimer.elapsed() : 0,
                             0);
        emit finished(true, "Export completed: " + m_outputPath);
    } else {
        QFile::remove(m_workPath);
        emit finished(false,
                      "FFmpeg export failed. Check the log for details.");
    }
}


