#include "RealtimePreview.h"
#include "AudioEnvelope.h"
#include <QFileInfo>
#include <QAudioOutput>
#include <QVideoSink>
#include <QVideoFrame>
#include <QImageReader>
#include <QPainter>
#include <QUrl>
#include <algorithm>

RealtimePreview::RealtimePreview(QWidget *parent) : QWidget(parent) {
    setMinimumSize(640,360);
    m_seekTimer.setSingleShot(true);
    m_seekTimer.setInterval(25);
    connect(&m_seekTimer,&QTimer::timeout,this,[this] { sync(true); });
    m_timer.setInterval(8);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer,&QTimer::timeout,this,&RealtimePreview::tick);
}
RealtimePreview::~RealtimePreview() {
    m_timer.stop();
    for(int t=0;t<10;++t) clearTrack(t);
}
void RealtimePreview::clearTrack(int t) {
    auto &track=m_tracks[t];
    if(track.player) {
        track.player->disconnect(this);
        if(track.sink) track.sink->disconnect(this);
        track.player->stop();
        delete track.player;
    }
    delete track.audio;
    delete track.sink;
    track=Track{};
}
void RealtimePreview::setSequence(const QVector<FFmpegExporter::ClipSegment> &clips) {
    pause();
    bool sameSources=clips.size()==m_clips.size();
    for(int i=0;sameSources && i<clips.size();++i) {
        const auto &a=clips[i];const auto &b=m_clips[i];
        sameSources=a.inputPath==b.inputPath && a.inMs==b.inMs && a.outMs==b.outMs && a.startMs==b.startMs && a.track==b.track && a.image==b.image;
    }
    if(sameSources){
        m_clips=clips;
        // A pending seek owns decoder activation. Starting decoders here and
        // immediately seeking again can discard the first paused frame.
        if(m_seekTimer.isActive())update();else sync(false);
        return;
    } // FX edits retain the decoded paused frame.
    for(int t=0;t<10;++t) clearTrack(t);
    m_clips=clips;
    m_duration=0;
    for(const auto &c:clips) m_duration=qMax(m_duration,c.startMs+c.outMs-c.inMs);
    seek(qMin(m_position,m_duration));
}
void RealtimePreview::setSettings(const SequenceSettings &settings) {
    if(!settings.valid())return;
    pause();m_settings=settings;update();
}
void RealtimePreview::seek(qint64 position) {
    pause();
    m_position=std::clamp<qint64>(position,0,m_duration);
    // Throttle seeks to the latest requested time while the pointer moves.
    if(!m_seekTimer.isActive()) m_seekTimer.start();
    update();
    emit positionChanged(m_position);
}
void RealtimePreview::play() {
    if(m_playing || m_duration<=0) return;
    if(m_position>=m_duration) m_position=0;
    m_seekTimer.stop();
    m_anchor=m_position;
    m_clock.restart();
    m_playing=true;
    sync(true);
    m_timer.start();
    emit playingChanged(true);
}
void RealtimePreview::pause() {
    if(m_playing) m_position=qMin(m_duration,m_anchor+m_clock.elapsed());
    m_playing=false;
    m_timer.stop();
    for(auto &track:m_tracks) if(track.player) {
        track.audio->setMuted(true);
        track.player->pause();
    }
    emit playingChanged(false);
}
void RealtimePreview::tick() {
    const qint64 elapsed=qMin(m_duration,m_anchor+m_clock.elapsed());
    const qint64 position=elapsed>=m_duration ? m_duration : m_settings.frameToMs(m_settings.frameAt(elapsed));
    if(position==m_position)return;
    m_position=position;
    sync(false);
    emit positionChanged(m_position);
    if(m_position>=m_duration) pause();
}
void RealtimePreview::sync(bool forceSeek) {
    for(int t=0;t<5;++t) {
        int index=-1;
        for(int i=0;i<m_clips.size();++i) {
            const auto &c=m_clips[i];
            if(c.track==t && m_position>=c.startMs && m_position<c.startMs+c.outMs-c.inMs) {
                index=i; break;
            }
        }
        bool changed=index!=m_tracks[t].index;
        if(changed) {
            if(index>=0 && m_tracks[t+5].index==index) {
                std::swap(m_tracks[t],m_tracks[t+5]);
                clearTrack(t+5);
            } else activate(t,index);
        }
        if(index>=0) updatePlayer(t,forceSeek || changed);
    }
    prepareNext();
    update();
}
void RealtimePreview::activate(int t,int index) {
    clearTrack(t);
    auto &track=m_tracks[t];
    track.index=index;
    if(index<0) return;
    const auto &c=m_clips[index];
    if(!QFileInfo(c.inputPath).isFile()) return; // Offline clips remain editable.
    if(c.image) {
        QImageReader reader(c.inputPath);
        reader.setAutoTransform(true);
        if(reader.size().isValid()) reader.setScaledSize(reader.size().scaled(1280,720,Qt::KeepAspectRatio));
        track.frame=reader.read();
        if(track.frame.isNull()) emit playbackError("Cannot read image: "+c.inputPath);
        emit frameChanged();
        return;
    }
    auto *player=new QMediaPlayer(this);
    track.player=player;
    track.audio=new QAudioOutput(this);
    track.audio->setVolume(0.8f);
    track.audio->setMuted(true);
    player->setAudioOutput(track.audio);
    if(t%5<3) {
        track.sink=new QVideoSink(this);
        player->setVideoSink(track.sink);
        connect(track.sink,&QVideoSink::videoFrameChanged,this,[this,player](const QVideoFrame &frame) {
            const int t=trackForPlayer(player); if(t<0) return;
            auto &state=m_tracks[t];
            if(state.player!=player || !frame.isValid() || state.index<0) return;
            // Ignore frames still queued from an earlier seek on this source.
            const qint64 timestamp=frame.startTime()/1000;
            if((state.awaitingFrame || !m_playing || t>=5) && frame.startTime()>=0) {
                // Millisecond transport coordinates round rational frame starts.
                // Allow only that rounding when paused, rather than accepting
                // several old frames inside the playback drift tolerance.
                const qint64 targetUs=state.requestedSourceMs*1000+500;
                const bool contains=frame.startTime()<=targetUs && frame.endTime()>targetUs;
                const qint64 tolerance=(!m_playing || t>=5) ? 1 : 150;
                if(!contains && qAbs(timestamp-state.requestedSourceMs)>tolerance) return;
            }
            state.frame=frame.toImage();
            state.awaitingFrame=false;
            if(!m_playing || t>=5) player->pause();
            if(!m_playing)update(); emit frameChanged();
        });
    }
    connect(player,&QMediaPlayer::mediaStatusChanged,this,[this,player](QMediaPlayer::MediaStatus status) {
        const int t=trackForPlayer(player); if(t<0) return;
        if(status==QMediaPlayer::LoadedMedia) {
            m_tracks[t].loaded=true;
            updatePlayer(t,true);
        }
    });
    connect(player,&QMediaPlayer::errorOccurred,this,[this,player](QMediaPlayer::Error,const QString &message) {
        const int t=trackForPlayer(player); if(t<0) return;
        m_tracks[t].frame={}; update();
        emit playbackError(message);
    });
    player->setSource(QUrl::fromLocalFile(c.inputPath));
}
void RealtimePreview::updatePlayer(int t,bool forceSeek) {
    auto &state=m_tracks[t];
    if(!state.player || !state.loaded || state.index<0) return;
    const auto &c=m_clips[state.index];
    const qint64 source=t>=5 ? c.inMs : c.inMs+m_position-c.startMs;
    const bool drift=t<5 && m_playing && m_position-state.lastCorrectionMs>500 && qAbs(state.player->position()-source)>120;
    if(forceSeek || drift) {
        state.requestedSourceMs=source;
        state.awaitingFrame=t%5<3;
        state.lastCorrectionMs=m_position;
        if(forceSeek && !m_playing) state.frame={};
        state.player->setPosition(source);
    }
    state.audio->setVolume(float(0.5*clipAudioGain(c.gain,c.fadeInMs,c.fadeOutMs,c.outMs-c.inMs,m_position-c.startMs)));
    state.audio->setMuted(t>=5 || !m_playing || c.muted);
    if(t<5 && m_playing) {
        if(state.player->playbackState()!=QMediaPlayer::PlayingState) state.player->play();
    } else if(t%5<3 && state.awaitingFrame) {
        // Decode one frame even on backends which do not present a paused seek.
        // Audio remains muted, and the frame callback pauses the source again.
        state.player->play();
    } else state.player->pause();
}
int RealtimePreview::trackForPlayer(QMediaPlayer *player) const {
    for(int t=0;t<10;++t) if(m_tracks[t].player==player) return t;
    return -1;
}
void RealtimePreview::prepareNext() {
    // One silent standby decoder per track, at most two seconds ahead.
    // Its first source frame is decoded before the cut and the player is reused.
    for(int t=0;t<5;++t) {
        int next=-1;
        for(int i=0;i<m_clips.size();++i) {
            const auto &c=m_clips[i];
            if(c.track==t && c.startMs>m_position && c.startMs-m_position<=2000
                && (next<0 || c.startMs<m_clips[next].startMs)) next=i;
        }
        if(m_tracks[t+5].index!=next) activate(t+5,next);
    }
}
QImage RealtimePreview::compositeFrame(QSize size) const {
    if(!size.isValid())size=m_settings.monitorSize();
    QImage result(size,QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::black);
    QPainter p(&result);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    for(int t=0;t<3;++t) {
        const auto &state=m_tracks[t];
        if(state.index<0 || state.frame.isNull() || !m_clips[state.index].visible) continue;
        const auto &clip=m_clips[state.index];
        if(m_position<clip.startMs || m_position>=clip.startMs+clip.outMs-clip.inMs) continue;
        if(state.renderedSourceKey!=state.frame.cacheKey() || state.rendered.size()!=size || state.renderedEffects!=clip.effects) {
            state.rendered=VideoEffects::render(state.frame,size,clip.effects);
            state.renderedSourceKey=state.frame.cacheKey();state.renderedEffects=clip.effects;
        }
        p.drawImage(QPoint(0,0),state.rendered);
    }
    return result;
}
void RealtimePreview::paintEvent(QPaintEvent *) {
    QPainter p(this); p.fillRect(rect(),QColor(35,38,45));
    QSize fitted=m_settings.frameSize().scaled(size(),Qt::KeepAspectRatio);
    const QRect target(QPoint((width()-fitted.width())/2,(height()-fitted.height())/2),fitted);
    p.drawImage(target,compositeFrame());
    p.setPen(QColor(90,94,102));p.setBrush(Qt::NoBrush);p.drawRect(target.adjusted(0,0,-1,-1));
}
