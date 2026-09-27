#include "TimelineWidget.h"
#include "TimelineRuler.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QUrl>
#include <QScrollArea>
#include <QScrollBar>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kTrackLeft = 110;
constexpr int kRightPadding = 28;
constexpr int kRulerHeight = 31;
constexpr int kClipTop = 39;
constexpr int kClipHeight = 46;
constexpr int kRowHeight = 58;
constexpr int kHandleHitWidth = 8;
constexpr qint64 kMinClipDurationMs = 120;
int rowForTrack(int t) { return t < 3 ? 2-t : t; }
}
TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent) {
    setFixedHeight(335); setAcceptDrops(true); setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus); updateGeometryForDuration();
}
void TimelineWidget::setContents(const QVector<TimelineClip> &clips,const std::array<bool,5> &visible,const std::array<bool,5> &muted) {
    m_clips=clips; m_undoStack.clear(); m_redoStack.clear();
    m_nextClipId=1;
    for(const auto &c:clips) m_nextClipId=qMax(m_nextClipId,c.id+1);
    for(int t=0;t<5;++t) {m_visible[t]=visible[t];m_muted[t]=muted[t];}
    m_selectedIndex=-1; m_dragClipIndex=-1; m_scrubbing=false;
    emitTimelineChanged(); emit selectionChanged(-1);
}
bool TimelineWidget::setClipEffects(int index,const QVector<VideoEffect> &effects) {
    if(index<0 || index>=m_clips.size() || m_clips[index].audioOnly || m_clips[index].track>=3 || !VideoEffects::valid(effects))return false;
    if(m_clips[index].effects==effects)return true;
    recordUndo();m_clips[index].effects=effects;emitTimelineChanged();return true;
}
bool TimelineWidget::setClipAudio(int index,double gain,qint64 fadeIn,qint64 fadeOut) {
    if(index<0 || index>=m_clips.size() || !std::isfinite(gain) || gain<0 || gain>2) return false;
    auto c=m_clips[index];
    fadeIn=qBound<qint64>(0,fadeIn,c.durationMs()); fadeOut=qBound<qint64>(0,fadeOut,c.durationMs());
    if(c.gain==gain && c.fadeInMs==fadeIn && c.fadeOutMs==fadeOut) return true;
    recordUndo(); c.gain=gain; c.fadeInMs=fadeIn; c.fadeOutMs=fadeOut; m_clips[index]=c;
    emitTimelineChanged(); return true;
}
qint64 TimelineWidget::snapTime(qint64 time,int except,qint64 duration) const {
    if(!m_snapping) return time;
    qint64 best=time, distance=8000/m_pixelsPerSecond;
    QVector<qint64> targets{0,m_positionMs};
    for(int i=0;i<m_clips.size();++i) if(i!=except) targets << m_clips[i].startMs << m_clips[i].startMs+m_clips[i].durationMs();
    for(qint64 target:targets) for(qint64 edge:{qint64(0),duration}) {
        qint64 candidate=target-edge, delta=qAbs(candidate-time);
        if(candidate>=0 && delta<distance) {distance=delta;best=candidate;}
    }
    return best;
}
bool TimelineWidget::canPlace(const TimelineClip &clip, int except) const {
    if (clip.startMs < 0 || clip.track < 0 || clip.track > 4 || clip.durationMs() < (clip.role==TimelineClip::Background?1:120) || clip.sourceDurationMs>86400000 || clip.startMs+clip.durationMs()>86400000) return false;
    if ((clip.audioOnly && clip.track < 3) || (clip.image && clip.track > 2)) return false;
    for (int i=0;i<m_clips.size();++i) {
        const auto &other=m_clips[i];
        if (i!=except && other.track==clip.track && clip.startMs < other.startMs+other.durationMs()
            && other.startMs < clip.startMs+clip.durationMs()) return false;
    }
    return true;
}
bool TimelineWidget::appendComposition(const QVector<TimelineClip> &clips) {
    if(clips.isEmpty() || m_clips.size()+clips.size()>10000)return false;
    const auto before=m_clips;
    int next=m_nextClipId;
    for(auto clip:clips) {
        if(clip.sourceInMs<0 || clip.sourceOutMs>clip.sourceDurationMs || !canPlace(clip)) {m_clips=before;return false;}
        clip.id=next++;m_clips.append(clip);
    }
    const auto combined=m_clips;m_clips=before;recordUndo();m_clips=combined;m_nextClipId=next;
    m_selectedIndex=before.size();emitTimelineChanged();emit selectionChanged(m_selectedIndex);return true;
}
void TimelineWidget::setBackgroundAudio(bool enabled) {
    bool change=false;for(const auto &clip:m_clips)if(clip.role==TimelineClip::Background && clip.muted==enabled)change=true;
    if(!change)return;
    recordUndo();for(auto &clip:m_clips)if(clip.role==TimelineClip::Background)clip.muted=!enabled;
    emitTimelineChanged();
}
void TimelineWidget::setExportRange(const ExportRange &range) {m_exportRange=range;update();}
void TimelineWidget::setSequenceSettings(const SequenceSettings &settings) {
    if(!settings.valid())return;
    m_sequenceSettings=settings;
    setToolTip(QString("Timecode HH:MM:SS:FF | %1 fps | Non-drop-frame").arg(settings.rateLabel()));
    update();
}
void TimelineWidget::addClip(const QString &name, const QString &path, qint64 duration, int track, qint64 start) {
    if (path.isEmpty() || duration<kMinClipDurationMs) return;
    TimelineClip c; c.id=m_nextClipId++; c.name=name; c.filePath=path;
    const auto ext=QFileInfo(path).suffix().toLower();
    c.image=QStringList{"png","jpg","jpeg","bmp","webp"}.contains(ext);
    c.audioOnly=QStringList{"wav","mp3","aac","m4a","flac","ogg","opus"}.contains(ext);
    c.track=track<0 ? (c.audioOnly ? 3 : 0) : track;
    c.sourceDurationMs=c.sourceOutMs=duration;
    c.startMs=start;
    if (start<0) { c.startMs=0; for (const auto &o:m_clips) if(o.track==c.track) c.startMs=qMax(c.startMs,o.startMs+o.durationMs()); }
    if (!canPlace(c)) { setToolTip("Drop rejected: incompatible track or overlapping clip. Choose an empty range."); return; }
    recordUndo(); m_clips.push_back(c); m_selectedIndex=m_clips.size()-1;
    emitTimelineChanged(); emit selectionChanged(m_selectedIndex);
}
bool TimelineWidget::moveClip(int index,int track,qint64 start) {
    if(index<0 || index>=m_clips.size()) return false;
    auto c=m_clips[index]; c.track=track; c.startMs=start;
    if(!canPlace(c,index)) return false;
    recordUndo(); m_clips[index]=c; emitTimelineChanged(); return true;
}
bool TimelineWidget::trimClip(int index,qint64 in,qint64 out) {
    if(index<0 || index>=m_clips.size()) return false;
    auto c=m_clips[index]; c.startMs+=in-c.sourceInMs; c.sourceInMs=in; c.sourceOutMs=out;
    if(in<0 || out>c.sourceDurationMs || out-in<kMinClipDurationMs || !canPlace(c,index)) return false;
    c.fadeInMs=qMin(c.fadeInMs,c.durationMs()); c.fadeOutMs=qMin(c.fadeOutMs,c.durationMs());
    recordUndo(); m_clips[index]=c; emitTimelineChanged(); return true;
}
void TimelineWidget::toggleTrack(int track,bool visibility) {
    if(track<0 || track>4 || (visibility && track>2)) return;
    if(visibility) m_visible[track]=!m_visible[track]; else m_muted[track]=!m_muted[track];
    emitTimelineChanged();
}
int TimelineWidget::trackAtY(int y) const {
    if(y<kClipTop || y>=kClipTop+5*kRowHeight) return -1;
    int row=(y-kClipTop)/kRowHeight; return row<3 ? 2-row : row;
}
void TimelineWidget::clearTimeline()
{
    if (m_clips.isEmpty())
        return;

    recordUndo();
    m_clips.clear();
    m_selectedIndex = -1;
    m_positionMs = 0;
    emitTimelineChanged();
    emit selectionChanged(-1);
}

QVector<TimelineClip> TimelineWidget::clips() const
{
    return m_clips;
}

qint64 TimelineWidget::sequenceDurationMs() const
{
    qint64 total = 0;
    for (const TimelineClip &clip : m_clips)
        total = qMax(total, clip.startMs + clip.durationMs());
    return total;
}

qint64 TimelineWidget::clipStartMs(int index) const
{
    return index>=0 && index<m_clips.size() ? m_clips[index].startMs : 0;
}
int TimelineWidget::clipIndexAtTime(qint64 t) const {
    int best=-1;
    for(int i=0;i<m_clips.size();++i) {
        const auto &c=m_clips[i];
        if(c.track<3 && m_visible[c.track] && t>=c.startMs && t<c.startMs+c.durationMs()
           && (best<0 || c.track>m_clips[best].track)) best=i;
    }
    return best;
}

int TimelineWidget::selectedClipIndex() const
{
    return m_selectedIndex;
}

void TimelineWidget::setPosition(qint64 positionMs)
{
    m_positionMs = std::clamp<qint64>(positionMs, 0, sequenceDurationMs());
    update();
}

qint64 TimelineWidget::position() const
{
    return m_positionMs;
}

void TimelineWidget::setScrollArea(QScrollArea *area)
{
    if (m_scrollArea) m_scrollArea->viewport()->removeEventFilter(this);
    m_scrollArea = area;
    if (area) area->viewport()->installEventFilter(this);
    updateGeometryForDuration();
    update();
}

bool TimelineWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (m_scrollArea && watched == m_scrollArea->viewport() && event->type() == QEvent::Resize) {
        updateGeometryForDuration();
        update();
    }
    return QWidget::eventFilter(watched, event);
}

void TimelineWidget::setZoomLevel(int level)
{
    // Keep a visible playhead anchored; otherwise keep the centre of the view.
    int offset = 0;
    qint64 anchor = 0;
    if (m_scrollArea) {
        const int left = m_scrollArea->horizontalScrollBar()->value();
        const int viewWidth = m_scrollArea->viewport()->width();
        const int playhead = timeToX(m_positionMs) - left;
        offset = playhead >= kTrackLeft && playhead < viewWidth - kRightPadding
            ? playhead : viewWidth / 2;
        anchor = xToTime(left + offset);
    }
    m_zoomLevel = std::clamp(level, 25, 220);
    updateGeometryForDuration();
    if (m_scrollArea)
        m_scrollArea->horizontalScrollBar()->setValue(m_zoomLevel == 25 ? 0 : timeToX(anchor) - offset);
    update();
}

bool TimelineWidget::splitSelectedAt(qint64 timelineMs)
{
    if (m_selectedIndex < 0 || m_selectedIndex >= m_clips.size())
        return false;

    const qint64 start = clipStartMs(m_selectedIndex);
    TimelineClip clip = m_clips[m_selectedIndex];
    const qint64 localMs = timelineMs - start;
    if (localMs <= kMinClipDurationMs ||
        localMs >= clip.durationMs() - kMinClipDurationMs)
        return false;

    recordUndo();

    const qint64 sourceSplitMs = clip.sourceInMs + localMs;
    TimelineClip left = clip;
    TimelineClip right = clip;
    right.id = m_nextClipId++;
    left.sourceOutMs = sourceSplitMs;
    right.sourceInMs = sourceSplitMs;
    right.startMs = timelineMs;
    left.fadeOutMs=0; right.fadeInMs=0;
    left.fadeInMs=qMin(left.fadeInMs,left.durationMs());
    right.fadeOutMs=qMin(right.fadeOutMs,right.durationMs());

    m_clips[m_selectedIndex] = left;
    m_clips.insert(m_selectedIndex + 1, right);
    m_selectedIndex += 1;

    emitTimelineChanged();
    emit selectionChanged(m_selectedIndex);
    return true;
}

bool TimelineWidget::deleteSelected()
{
    if (m_selectedIndex < 0 || m_selectedIndex >= m_clips.size())
        return false;

    recordUndo();
    m_clips.removeAt(m_selectedIndex);

    if (m_clips.isEmpty())
        m_selectedIndex = -1;
    else
        m_selectedIndex = std::min(m_selectedIndex, static_cast<int>(m_clips.size()) - 1);

    m_positionMs = std::min(m_positionMs, sequenceDurationMs());
    emitTimelineChanged();
    emit selectionChanged(m_selectedIndex);
    return true;
}

bool TimelineWidget::removeClips(const QSet<int> &ids)
{
    if(std::none_of(m_clips.cbegin(),m_clips.cend(),[&](const auto &clip){return ids.contains(clip.id);}))return false;
    const int selectedId=m_selectedIndex>=0 && m_selectedIndex<m_clips.size()?m_clips[m_selectedIndex].id:-1;
    recordUndo();
    m_clips.erase(std::remove_if(m_clips.begin(),m_clips.end(),[&](const auto &clip){return ids.contains(clip.id);}),m_clips.end());
    m_selectedIndex=-1;
    for(int i=0;i<m_clips.size();++i)if(m_clips[i].id==selectedId)m_selectedIndex=i;
    m_positionMs=std::min(m_positionMs,sequenceDurationMs());
    emitTimelineChanged();emit selectionChanged(m_selectedIndex);
    return true;
}

bool TimelineWidget::undo()
{
    if (m_undoStack.isEmpty())
        return false;

    m_redoStack.push_back(m_clips);
    m_clips = m_undoStack.takeLast();
    m_selectedIndex = m_clips.isEmpty() ? -1 : std::min(m_selectedIndex, static_cast<int>(m_clips.size()) - 1);
    m_positionMs = std::min(m_positionMs, sequenceDurationMs());
    emitTimelineChanged();
    emit selectionChanged(m_selectedIndex);
    return true;
}

bool TimelineWidget::redo()
{
    if (m_redoStack.isEmpty())
        return false;

    m_undoStack.push_back(m_clips);
    m_clips = m_redoStack.takeLast();
    m_selectedIndex = m_clips.isEmpty() ? -1 : std::min(m_selectedIndex, static_cast<int>(m_clips.size()) - 1);
    m_positionMs = std::min(m_positionMs, sequenceDurationMs());
    emitTimelineChanged();
    emit selectionChanged(m_selectedIndex);
    return true;
}

int TimelineWidget::timeToX(qint64 ms) const
{
    const double seconds = static_cast<double>(qMax<qint64>(0, ms)) / 1000.0;
    return kTrackLeft + static_cast<int>(seconds * m_pixelsPerSecond);
}

qint64 TimelineWidget::xToTime(int x) const
{
    if (x <= kTrackLeft)
        return 0;

    const double seconds = static_cast<double>(x - kTrackLeft) /
                           static_cast<double>(m_pixelsPerSecond);
    return std::clamp<qint64>(static_cast<qint64>(seconds * 1000.0),
                              0,
                              24LL * 60 * 60 * 1000);
}

QRect TimelineWidget::clipRect(int index) const
{
    if (index < 0 || index >= m_clips.size())
        return {};

    const qint64 start = clipStartMs(index);
    const qint64 end = start + m_clips[index].durationMs();
    const int x1 = timeToX(start);
    const int x2 = timeToX(end);
    return QRect(x1, kClipTop + rowForTrack(m_clips[index].track)*kRowHeight, std::max(2, x2 - x1), kClipHeight);
}

void TimelineWidget::updateGeometryForDuration()
{
    if (!m_scrollArea) {
        // Standalone widgets have no viewport to fit to.
        m_pixelsPerSecond = m_zoomLevel;
        setFixedWidth(std::clamp(timeToX(sequenceDurationMs() + 15000) + kRightPadding, 1100, 16777215));
        return;
    }
    const int viewportWidth = qMax(1, m_scrollArea->viewport()->width());
    const qint64 duration = sequenceDurationMs();
    const double seconds = (duration > 0 ? duration : 15000) / 1000.0;
    const double available = qMax(1, viewportWidth - kTrackLeft - kRightPadding);
    const double fitScale = available / seconds;
    // Logarithmic zoom stays smooth even for sequences several hours long.
    const double maximumScale = qMin(qMax(220.0, fitScale * 8.0),
                                     (16777215.0 - kTrackLeft - kRightPadding) / seconds);
    const double fraction = (m_zoomLevel - 25) / 195.0;
    m_pixelsPerSecond = fitScale * std::pow(maximumScale / fitScale, fraction);
    const int timelineWidth = m_zoomLevel == 25 ? viewportWidth
        : qMax(viewportWidth, timeToX(sequenceDurationMs()) + kRightPadding);
    setFixedWidth(timelineWidth);
    if (m_zoomLevel == 25) m_scrollArea->horizontalScrollBar()->setValue(0);
}

void TimelineWidget::recordUndo()
{
    m_undoStack.push_back(m_clips);
    if (m_undoStack.size() > 50)
        m_undoStack.removeFirst();
    m_redoStack.clear();
}

void TimelineWidget::emitTimelineChanged()
{
    m_positionMs = std::min(m_positionMs, sequenceDurationMs());
    updateGeometryForDuration();
    update();
    emit clipsChanged();
}

bool TimelineWidget::isVideoPath(const QString &path) const {
    return QStringList{"mp4","mov","m4v","mkv","avi","webm","png","jpg","jpeg","bmp","webp","wav","mp3","aac","m4a","flac","ogg","opus"}.contains(QFileInfo(path).suffix().toLower());
}
void TimelineWidget::paintEvent(QPaintEvent *) {
    QPainter p(this); p.fillRect(rect(),QColor(28,31,38));
    for(int t=0;t<5;++t) {
        int y=kClipTop+rowForTrack(t)*kRowHeight;
        p.fillRect(0,y,width(),kClipHeight,QColor(40,45,54)); p.setPen(Qt::white);
        p.drawText(5,y+20,t<3 ? QString("V%1").arg(t+1) : QString("A%1").arg(t-2));
        if(t<3) p.drawText(34,y+20,m_visible[t] ? "Eye" : "Off");
        p.drawText(72,y+20,m_muted[t] ? "Mute" : "On");
    }
    const QRect visible=visibleRegion().boundingRect();
    if(m_exportRange.enabled && m_exportRange.outMs>m_exportRange.inMs) {
        const int left=timeToX(m_exportRange.inMs),right=timeToX(m_exportRange.outMs);
        p.fillRect(QRect(left,0,qMax(1,right-left),24),QColor(35,95,76));
    }
    const int labelWidth=p.fontMetrics().horizontalAdvance("00:00:00:00");
    const auto ticks=TimelineRuler::ticks(m_sequenceSettings,m_pixelsPerSecond,
        xToTime(visible.left()),xToTime(visible.right()),labelWidth);
    for(const auto &tick:ticks) {
        const double seconds=double(tick.frame)*m_sequenceSettings.fpsDen/m_sequenceSettings.fpsNum;
        const int x=kTrackLeft+int(seconds*m_pixelsPerSecond);
        if(tick.major) {
            p.setPen(QColor(55,60,69));p.drawLine(x,kRulerHeight+1,x,height());
            if(x>=qMax(kTrackLeft,visible.left()) && x+4+labelWidth<=visible.right()) {
                p.setPen(QColor(190,196,207));p.drawText(x+4,16,m_sequenceSettings.timecodeForFrame(tick.frame));
            }
        }
        p.setPen(tick.major?QColor(185,190,200):QColor(115,121,134));
        const int length=tick.major?12:tick.middle?8:4;
        p.drawLine(x,kRulerHeight-length,x,kRulerHeight);
    }
    p.setPen(QPen(QColor(211,224,80),1));
    p.drawLine(qMax(kTrackLeft,visible.left()),kRulerHeight,visible.right(),kRulerHeight);
    for(int i=0;i<m_clips.size();++i) {
        auto r=clipRect(i); if(!r.intersects(visible)) continue;
        p.setBrush(m_clips[i].track<3 ? QColor(47,105,158) : QColor(44,124,92));
        p.setPen(QPen(i==m_selectedIndex ? Qt::yellow : Qt::gray,i==m_selectedIndex ? 3 : 1));
        p.drawRoundedRect(r.adjusted(1,1,-1,-1),4,4); p.setPen(Qt::white);
        const QString prefix=m_clips[i].role==TimelineClip::Background ? (m_clips[i].muted ? "[BG muted] " : "[BG] ") : "";
        p.save(); p.setClipRect(r.adjusted(8,0,-8,0)); p.drawText(r.adjusted(10,0,-10,0),Qt::AlignVCenter,prefix+m_clips[i].name); p.restore();
        p.drawLine(r.left()+4,r.top()+6,r.left()+4,r.bottom()-6);
        p.drawLine(r.right()-4,r.top()+6,r.right()-4,r.bottom()-6);
    }
    if(m_exportRange.enabled && m_exportRange.outMs>m_exportRange.inMs) {
        p.setPen(QPen(QColor(75,230,165),2,Qt::DashLine));
        for(qint64 mark:{m_exportRange.inMs,m_exportRange.outMs}) {const int x=timeToX(mark);p.drawLine(x,24,x,height());}
    }
    p.setPen(QPen(Qt::red,2)); int x=timeToX(m_positionMs); p.drawLine(x,25,x,height());
}
void TimelineWidget::mousePressEvent(QMouseEvent *e) {
    if(e->button()!=Qt::LeftButton) return;
    m_scrubbing=false;
    setFocus(); int x=int(e->position().x()), y=int(e->position().y());
    int t=trackAtY(y);
    if(x<kTrackLeft && t>=0) { if(x>=34 && x<70) toggleTrack(t,true); else if(x>=70) toggleTrack(t,false); return; }
    m_dragMode=DragMode::None; m_dragClipIndex=-1; m_interactionBefore=m_clips;
    for(int i=0;i<m_clips.size();++i) if(clipRect(i).contains(x,y)) {
        auto r=clipRect(i); m_selectedIndex=i; m_dragClipIndex=i; m_dragStartX=x;
        m_dragMode=abs(x-r.left())<=kHandleHitWidth ? DragMode::TrimLeft : abs(x-r.right())<=kHandleHitWidth ? DragMode::TrimRight : DragMode::MoveClip;
        if(m_clips[i].durationMs()<kMinClipDurationMs)m_dragMode=DragMode::MoveClip;
        emit selectionChanged(i); emit seekRequested(xToTime(x)); update(); return;
    }
    m_scrubbing=true;
    // Ruler scrubbing preserves clip selection for split-at-playhead.
    if(y>=kClipTop) { m_selectedIndex=-1; emit selectionChanged(-1); }
    emit seekRequested(xToTime(x)); update();
}
void TimelineWidget::mouseMoveEvent(QMouseEvent *e) {
    if(!(e->buttons()&Qt::LeftButton)) return;
    if(m_scrubbing) { emit seekRequested(xToTime(int(e->position().x()))); return; }
    if(m_dragClipIndex<0) return;
    auto c=m_interactionBefore[m_dragClipIndex];
    qint64 delta=qint64((e->position().x()-m_dragStartX)*1000/m_pixelsPerSecond);
    if(m_dragMode==DragMode::MoveClip) {
        c.startMs=qMax<qint64>(0,c.startMs+delta);
        if(!(e->modifiers()&Qt::AltModifier)) c.startMs=snapTime(c.startMs,m_dragClipIndex,c.durationMs());
        int t=trackAtY(int(e->position().y())); if(t>=0) c.track=t;
    } else if(m_dragMode==DragMode::TrimLeft) {
        if(!(e->modifiers()&Qt::AltModifier)) delta=snapTime(c.startMs+delta,m_dragClipIndex)-c.startMs;
        qint64 in=std::clamp(c.sourceInMs+delta,qMax<qint64>(0,c.sourceInMs-c.startMs),c.sourceOutMs-kMinClipDurationMs);
        c.startMs+=in-c.sourceInMs; c.sourceInMs=in;
    } else if(m_dragMode==DragMode::TrimRight) {
        if(!(e->modifiers()&Qt::AltModifier)) delta=snapTime(c.startMs+c.durationMs()+delta,m_dragClipIndex)-c.startMs-c.durationMs();
        c.sourceOutMs=std::clamp(c.sourceOutMs+delta,c.sourceInMs+kMinClipDurationMs,c.sourceDurationMs);
    }
    c.fadeInMs=qMin(c.fadeInMs,c.durationMs()); c.fadeOutMs=qMin(c.fadeOutMs,c.durationMs());
    if(canPlace(c,m_dragClipIndex)) { m_clips[m_dragClipIndex]=c; update(); }
}
void TimelineWidget::mouseReleaseEvent(QMouseEvent *e) {
    if(e->button()!=Qt::LeftButton) return;
    m_scrubbing=false;
    if(m_dragClipIndex>=0) {
        const auto &a=m_clips[m_dragClipIndex], &b=m_interactionBefore[m_dragClipIndex];
        if(a.startMs!=b.startMs || a.track!=b.track || a.sourceInMs!=b.sourceInMs || a.sourceOutMs!=b.sourceOutMs) {
            m_undoStack.push_back(m_interactionBefore); if(m_undoStack.size()>50) m_undoStack.removeFirst();
            m_redoStack.clear(); emitTimelineChanged();
        }
    }
    m_dragMode=DragMode::None; m_dragClipIndex=-1; m_interactionBefore.clear();
}
void TimelineWidget::dragEnterEvent(QDragEnterEvent *e) { if(e->mimeData()->hasUrls()) e->acceptProposedAction(); }
void TimelineWidget::dragMoveEvent(QDragMoveEvent *e) { if(e->mimeData()->hasUrls()) e->acceptProposedAction(); }
void TimelineWidget::dropEvent(QDropEvent *e) {
    int track=trackAtY(int(e->position().y())); if(track<0 || !e->mimeData()->hasUrls()) return;
    QStringList paths;
    for(const auto &url:e->mimeData()->urls()) if(url.isLocalFile() && isVideoPath(url.toLocalFile())) paths<<url.toLocalFile();
    if(!paths.isEmpty()) { emit filesDropped(paths,track,snapTime(xToTime(int(e->position().x())))); e->acceptProposedAction(); }
}
