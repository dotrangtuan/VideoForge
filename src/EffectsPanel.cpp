#include "EffectsPanel.h"
#include <QLabel>
#include <QListWidget>
#include <QComboBox>
#include <QPushButton>
#include <QDoubleSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QSignalBlocker>

EffectsPanel::EffectsPanel(QWidget *parent):QWidget(parent) {
    setObjectName("videoEffectsPanel");auto layout=new QVBoxLayout(this);
    m_title=new QLabel("Select a video or image clip on V1 / V2 / V3",this);m_title->setWordWrap(true);layout->addWidget(m_title);
    auto explanation=new QLabel("Effects run from top to bottom. Add in the order you want; use Up / Down to reorder.",this);explanation->setWordWrap(true);layout->addWidget(explanation);
    auto addRow=new QHBoxLayout;m_type=new QComboBox(this);m_type->setObjectName("effectType");m_type->addItems({"Crop","Blur","Opacity","Zoom"});
    m_add=new QPushButton("Add FX",this);m_add->setObjectName("addEffect");addRow->addWidget(m_type,1);addRow->addWidget(m_add);layout->addLayout(addRow);
    m_list=new QListWidget(this);m_list->setObjectName("effectStack");m_list->setMinimumHeight(100);layout->addWidget(m_list,1);
    auto actions=new QHBoxLayout;
    m_up=new QPushButton("Up",this);m_up->setObjectName("effectUp");m_down=new QPushButton("Down",this);m_down->setObjectName("effectDown");m_remove=new QPushButton("Remove",this);m_remove->setObjectName("removeEffect");
    for(auto button:{m_up,m_down,m_remove})actions->addWidget(button);layout->addLayout(actions);
    auto form=new QFormLayout;
    for(int i=0;i<4;++i) {
        m_labels[i]=new QLabel(this);m_values[i]=new QDoubleSpinBox(this);m_values[i]->setObjectName(QString("effectValue%1").arg(i));
        m_values[i]->setDecimals(2);m_values[i]->setSuffix(" %");m_values[i]->setKeyboardTracking(false);form->addRow(m_labels[i],m_values[i]);
        connect(m_values[i],&QDoubleSpinBox::editingFinished,this,&EffectsPanel::commitPendingEdits);
        connect(m_values[i],qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{commitPendingEdits();});
    }
    layout->addLayout(form);m_hint=new QLabel(this);m_hint->setWordWrap(true);layout->addWidget(m_hint);
    connect(m_add,&QPushButton::clicked,this,[this]{
        commitPendingEdits();if(m_clipId<0 || m_effects.size()>=VideoEffects::maximumCount)return;
        m_effects.append(VideoEffect::make(VideoEffect::Type(m_type->currentIndex())));publish(m_effects.size()-1);
    });
    connect(m_list,&QListWidget::currentRowChanged,this,[this]{if(!m_updating)showParameters();});
    connect(m_list,&QListWidget::itemChanged,this,[this](QListWidgetItem *item){
        if(m_updating)return;int row=m_list->row(item);if(row<0 || row>=m_effects.size())return;
        m_effects[row].enabled=item->checkState()==Qt::Checked;publish(row);
    });
    auto move=[this](int delta){commitPendingEdits();int row=m_list->currentRow();if(row<0 || row+delta<0 || row+delta>=m_effects.size())return;m_effects.swapItemsAt(row,row+delta);publish(row+delta);};
    connect(m_up,&QPushButton::clicked,this,[move]{move(-1);});connect(m_down,&QPushButton::clicked,this,[move]{move(1);});
    connect(m_remove,&QPushButton::clicked,this,[this]{int row=m_list->currentRow();if(row<0)return;m_effects.removeAt(row);publish(qMin(row,int(m_effects.size())-1));});
    rebuild(-1);
}
void EffectsPanel::setClip(int id,const QString &name,const QVector<VideoEffect> &effects) {
    const bool changed=id!=m_clipId;const int row=changed?0:m_list->currentRow();
    m_title->setText(id<0?"Select a video or image clip on V1 / V2 / V3":"Clip: "+name);
    if(!changed && effects==m_effects)return;
    m_clipId=id;m_effects=effects;rebuild(row);
}
void EffectsPanel::rebuild(int row) {
    m_updating=true;m_list->clear();
    for(int i=0;i<m_effects.size();++i) {
        auto item=new QListWidgetItem(QString("%1. %2").arg(i+1).arg(m_effects[i].name()),m_list);
        item->setFlags(item->flags()|Qt::ItemIsUserCheckable);item->setCheckState(m_effects[i].enabled?Qt::Checked:Qt::Unchecked);
    }
    m_list->setCurrentRow(qMin(row,int(m_effects.size())-1));m_updating=false;
    m_type->setEnabled(m_clipId>=0);m_add->setEnabled(m_clipId>=0 && m_effects.size()<VideoEffects::maximumCount);
    showParameters();
}
void EffectsPanel::showParameters() {
    m_updating=true;const int row=m_list->currentRow();const bool selected=row>=0 && row<m_effects.size();
    m_up->setEnabled(selected && row>0);m_down->setEnabled(selected && row+1<m_effects.size());m_remove->setEnabled(selected);
    for(int i=0;i<4;++i){m_labels[i]->hide();m_values[i]->hide();m_values[i]->setEnabled(selected);}
    m_hint->setText("Check an effect to enable it. Up to 16 effects per clip.");
    if(selected) {
        const auto &effect=m_effects[row];const int count=effect.type==VideoEffect::Crop?4:1;
        const QStringList cropLabels{"Left","Top","Right","Bottom"};
        for(int i=0;i<count;++i) {
            QSignalBlocker blocker(m_values[i]);m_values[i]->setRange(effect.type==VideoEffect::Zoom?10:0,effect.type==VideoEffect::Crop?99:effect.type==VideoEffect::Blur?5:effect.type==VideoEffect::Zoom?400:100);
            m_values[i]->setSingleStep(effect.type==VideoEffect::Blur?0.1:1);m_values[i]->setValue(effect.value[i]);
            m_labels[i]->setText(effect.type==VideoEffect::Crop?cropLabels[i]:effect.type==VideoEffect::Blur?"Blur radius":effect.type==VideoEffect::Opacity?"Opacity":"Scale");m_labels[i]->show();m_values[i]->show();
        }
        switch(effect.type) {
        case VideoEffect::Crop:m_hint->setText("Trim each edge of the sequence canvas to transparency. Opposite edges must total 99% or less. No automatic enlargement.");break;
        case VideoEffect::Blur:m_hint->setText("Box blur radius as a percentage of the shorter frame edge. Includes transparent edges.");break;
        case VideoEffect::Opacity:m_hint->setText("100% = original opacity; 0% = invisible. Audio is unchanged.");break;
        case VideoEffect::Zoom:m_hint->setText("100% = unchanged. Scale around the center; content outside the canvas is clipped.");break;
        }
    }
    m_updating=false;
}
void EffectsPanel::commitPendingEdits() {
    const int row=m_list->currentRow();if(m_updating || m_clipId<0 || row<0 || row>=m_effects.size())return;
    auto effect=m_effects[row];m_updating=true;
    for(int i=0;i<(effect.type==VideoEffect::Crop?4:1);++i){m_values[i]->interpretText();effect.value[i]=m_values[i]->value();}
    m_updating=false;
    if(!effect.valid()){showParameters();m_hint->setText("Crop rejected: opposite edges must total 99% or less. Previous values restored.");return;}
    if(effect==m_effects[row])return;
    m_effects[row]=effect;publish(row);
}
void EffectsPanel::publish(int row) {
    rebuild(row);emit editRequested(m_clipId,m_effects);
}
