#include "MainWindow.h"
#include "TimelineWidget.h"
#include "RealtimePreview.h"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QToolBar>
#include <QPushButton>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QSpinBox>
#include <QComboBox>
#include <QLabel>
#include <QShortcut>
#include <QApplication>
#include <QAbstractSpinBox>
#include <QLineEdit>

void MainWindow::buildSequenceUi() {
    auto menu=menuBar()->addMenu("&Sequence");
    auto action=menu->addAction("Sequence Settings...");
    connect(action,&QAction::triggered,this,&MainWindow::editSequenceSettings);
    auto bar=addToolBar("Sequence");bar->setMovable(false);
    m_sequenceSettingsButton=new QPushButton(bar);
    m_sequenceSettingsButton->setObjectName("sequenceSettingsButton");
    bar->addWidget(m_sequenceSettingsButton);
    connect(m_sequenceSettingsButton,&QPushButton::clicked,this,&MainWindow::editSequenceSettings);
    // Arrow keys are scoped to the timeline, so typing a size/gain and moving
    // the transport slider retain their normal editing behavior.
    auto previous=new QShortcut(QKeySequence(Qt::Key_Left),m_timeline);
    auto next=new QShortcut(QKeySequence(Qt::Key_Right),m_timeline);
    previous->setContext(Qt::WidgetWithChildrenShortcut);next->setContext(Qt::WidgetWithChildrenShortcut);
    connect(previous,&QShortcut::activated,this,&MainWindow::previousFrame);
    connect(next,&QShortcut::activated,this,&MainWindow::nextFrame);
    refreshSequenceInfo();
}
void MainWindow::refreshSequenceInfo() {
    m_timeline->setSequenceSettings(m_sequenceSettings);
    if(m_sequenceSettingsButton)m_sequenceSettingsButton->setText("Sequence Settings: "+m_sequenceSettings.description());
    if(m_sequenceLabel)m_sequenceLabel->setText(QString("Sequence: %1 clips | %2\n%3").arg(m_timeline->clips().size()).arg(formatTime(m_timeline->sequenceDurationMs()),m_sequenceSettings.description()));
    if(m_timeLabel)updateTransportUi(m_timelinePositionMs);
}
bool MainWindow::setSequenceSettings(const SequenceSettings &settings) {
    if(!settings.valid())return false;
    if(settings==m_sequenceSettings)return true;
    commitClipAudio();pauseForEdit();
    // Source trims, sequence starts, audio fades and duration are all in time.
    // Changing the sampling grid must never retime them.
    m_sequenceSettings=settings;m_videoWidget->setSettings(settings);
    refreshSequenceInfo();updateRangeUi();markDirty();return true;
}
void MainWindow::previousFrame() {
    pauseForEdit();m_videoWidget->seek(m_sequenceSettings.step(m_videoWidget->position(),-1,m_timeline->sequenceDurationMs()));
}
void MainWindow::nextFrame() {
    pauseForEdit();m_videoWidget->seek(m_sequenceSettings.step(m_videoWidget->position(),1,m_timeline->sequenceDurationMs()));
}
void MainWindow::editSequenceSettings() {
    pauseForEdit();
    QDialog dialog(this);dialog.setWindowTitle("Sequence Settings");dialog.setObjectName("sequenceSettingsDialog");
    auto layout=new QVBoxLayout(&dialog);auto form=new QFormLayout;
    auto preset=new QComboBox(&dialog);preset->setObjectName("frameSizePreset");
    const std::array<QSize,5> sizes{{QSize(1280,720),QSize(1920,1080),QSize(1080,1920),QSize(1080,1080),QSize(3840,2160)}};
    preset->addItem("Custom");
    preset->addItems({"HD 1280 x 720","Full HD 1920 x 1080","Portrait 1080 x 1920","Square 1080 x 1080","UHD 3840 x 2160"});
    auto width=new QSpinBox(&dialog);auto height=new QSpinBox(&dialog);
    width->setObjectName("sequenceWidth");height->setObjectName("sequenceHeight");
    for(auto box:{width,height}){box->setRange(64,4096);box->setSingleStep(2);box->setSuffix(" px");}
    width->setValue(m_sequenceSettings.width);height->setValue(m_sequenceSettings.height);
    auto rate=new QComboBox(&dialog);rate->setObjectName("sequenceTimebase");
    int selected=0;
    for(const auto &fps:SequenceSettings::rates()) {
        rate->addItem(QString("%1 fps (%2/%3)").arg(fps.label).arg(fps.numerator).arg(fps.denominator));
        if(fps.numerator==m_sequenceSettings.fpsNum && fps.denominator==m_sequenceSettings.fpsDen)selected=rate->count()-1;
    }
    rate->setCurrentIndex(selected);
    form->addRow("Frame size preset",preset);form->addRow("Width",width);form->addRow("Height",height);form->addRow("Timebase",rate);layout->addLayout(form);
    auto hint=new QLabel("Sequence-wide settings. Clip timing and playback speed stay unchanged.\nSources fit inside the frame without cropping. Square pixels, progressive video.",&dialog);
    hint->setWordWrap(true);layout->addWidget(hint);
    auto error=new QLabel(&dialog);error->setStyleSheet("color: #d44;");layout->addWidget(error);
    auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel,&dialog);layout->addWidget(buttons);
    auto validate=[&] {
        const bool valid=width->value()%2==0 && height->value()%2==0;
        error->setText(valid?"":"Width and height must be even numbers (64–4096).");
        buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
        int index=0;for(int i=0;i<int(sizes.size());++i)if(sizes[i]==QSize(width->value(),height->value()))index=i+1;
        preset->setCurrentIndex(index);
    };
    connect(preset,qOverload<int>(&QComboBox::activated),&dialog,[&](int index){if(index>0){width->setValue(sizes[index-1].width());height->setValue(sizes[index-1].height());}});
    connect(width,qOverload<int>(&QSpinBox::valueChanged),&dialog,[&](int){validate();});
    connect(height,qOverload<int>(&QSpinBox::valueChanged),&dialog,[&](int){validate();});
    connect(buttons,&QDialogButtonBox::accepted,&dialog,[&]{width->interpretText();height->interpretText();validate();if(buttons->button(QDialogButtonBox::Ok)->isEnabled())dialog.accept();});
    connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);validate();
    if(dialog.exec()!=QDialog::Accepted)return;
    const auto fps=SequenceSettings::rates()[rate->currentIndex()];
    setSequenceSettings({width->value(),height->value(),fps.numerator,fps.denominator});
}
