#include "comparedialog.h"
#include "ui_comparedialog.h"
#include "helpers.h"
#include "helpers_html.h"
#include "eeprominterface.h"
#include <QtGui>
#include <QImage>
#include <QColor>
#include <QPainter>

#if !defined WIN32 && defined __GNUC__
#include <unistd.h>
#endif

#define ISIZE 200 // curve image size
class DragDropHeader {
public:
  DragDropHeader():
    general_settings(false),
    models_count(0)
  {
  }
  bool general_settings;
  uint8_t models_count;
  uint8_t models[C9X_MAX_MODELS];
};

CompareDialog::CompareDialog(QWidget * parent, Firmware * firmware):
  QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint),
  firmware(firmware),
  model1(0),
  model2(0),
  ui(new Ui::CompareDialog)
{
  ui->setupUi(this);

  this->setWindowIcon(CompanionIcon("compare.png"));
  te = ui->textEdit;
  this->setAcceptDrops(true);

  // TODO this is really horrible  
  g_model1=(ModelData *)malloc(sizeof(ModelData));
  g_model2=(ModelData *)malloc(sizeof(ModelData));
  modeltemp=(ModelData *)malloc(sizeof(ModelData));

  //setDragDropOverwriteMode(true);
  //setDropIndicatorShown(true);
  te->scrollToAnchor("1");
}

void CompareDialog::dragMoveEvent(QDragMoveEvent *event)
{
  if (event->mimeData()->hasFormat("application/x-companion")) {   
    event->acceptProposedAction();
  }
  else {
    event->ignore();
  }
}

void CompareDialog::dragEnterEvent(QDragEnterEvent *event)
{
  // accept just text/uri-list mime format
  if (event->mimeData()->hasFormat("application/x-companion")) {   
    event->acceptProposedAction();
  }
  else {
    event->ignore();
  }
}

void CompareDialog::dragLeaveEvent(QDragLeaveEvent *event)
{
  event->accept();
}

void CompareDialog::printDiff()
{
  te->clear();
  printSetup();
  if (GetCurrentFirmware()->getCapability(FlightModes)) {
    printPhases();
  }
  curvefile1=generateProcessUniqueTempFileName("curve1.png");
  curvefile2=generateProcessUniqueTempFileName("curve2.png");  
  printExpos();
  printMixers();
  printLimits();
  printCurves();
  printGvars();
  printSwitches();
  printFSwitches();
  printFrSky();
  te->scrollToAnchor("1");
}

void CompareDialog::dropEvent(QDropEvent *event)
{
  QLabel *child = qobject_cast<QLabel*>(childAt(event->pos()));
  const QMimeData  *mimeData = event->mimeData();
  if (child) {
    if (child->objectName().contains("label_1")) {        
      if(mimeData->hasFormat("application/x-companion")) {
        QByteArray gmData = mimeData->data("application/x-companion");
        DragDropHeader *header = (DragDropHeader *)gmData.data();
        if (!header->general_settings) {
          char *gData = gmData.data()+sizeof(DragDropHeader);//new char[gmData.size() + 1];
          char c = *gData;
          gData++;
          if(c=='M') {
            memcpy(modeltemp,(ModelData *)gData,sizeof(ModelData));
            if (modeltemp->used) {
              memcpy(g_model1,(ModelData *)gData,sizeof(ModelData));
              QString name;
              name.append(g_model1->name);
              if (!name.isEmpty()) {
                ui->label_1->setText(name);
              } else {
                ui->label_1->setText(tr("No name"));
              }
              model1=1;
            }
          }
        }
      }          
    }
    else if (child->objectName().contains("label_2")) {
      if (mimeData->hasFormat("application/x-companion")) {
        QByteArray gmData = mimeData->data("application/x-companion");
        DragDropHeader *header = (DragDropHeader *)gmData.data();
        if (!header->general_settings) {
          char *gData = gmData.data()+sizeof(DragDropHeader);//new char[gmData.size() + 1];
          char c = *gData;
          gData++;
          if(c=='M') {
            memcpy(modeltemp,(ModelData *)gData,sizeof(ModelData));
            if (modeltemp->used) {
              memcpy(g_model2,(ModelData *)gData,sizeof(ModelData));
              QString name;
              name.append(g_model2->name);
              if (!name.isEmpty()) {
                ui->label_2->setText(name);
              } else {
                ui->label_2->setText(tr("No name"));
              }
              model2=1;
            }
          }
        }
      }                  
    }
  }  else {
    return;
  }
  event->accept();
  if ((model1==1) & (model2==1)) {
    printDiff();
  }
}

void CompareDialog::closeEvent(QCloseEvent *event) 
{
}

CompareDialog::~CompareDialog()
{
  qunlink(curvefile1);
  qunlink(curvefile2);
  delete ui;
}

int CompareDialog::ModelHasExpo(ExpoData * ExpoArray, ExpoData expo, bool * expoused)
{
  for (int i=0; i< C9X_MAX_EXPOS; i++) {
    if ((memcmp(&expo,&ExpoArray[i],sizeof(ExpoData))==0) && (expoused[i]==false)) {
      return i;
    }
  }
  return -1;
}

bool CompareDialog::ChannelHasExpo(ExpoData * expoArray, uint8_t destCh)
{
  for (int i=0; i< C9X_MAX_EXPOS; i++) {
    if ((expoArray[i].chn==destCh)&&(expoArray[i].mode!=0)) {
      return true;
    }
  }
  return false;
}

int CompareDialog::ModelHasMix(MixData * mixArray, MixData mix, bool * mixused)
{
  for (int i=0; i< C9X_MAX_MIXERS; i++) {
    if ((memcmp(&mix,&mixArray[i],sizeof(MixData))==0) && (mixused[i]==false)) {
      return i;
    }
  }
  return -1;
}

bool CompareDialog::ChannelHasMix(MixData * mixArray, uint8_t destCh)
{
  for (int i=0; i< C9X_MAX_MIXERS; i++) {
    if (mixArray[i].destCh==destCh) {
      return true;
    }
  }
  return false;
}

void CompareDialog::printSetup()
{
  QString color;
  QString str = "<a name=1></a><table border=1 cellspacing=0 cellpadding=3 width=\"100%\">";
  str.append("<tr><td colspan=2><h2>"+tr("General Model Settings")+"</h2></td></tr>");
  str.append("<tr><td><table border=0 cellspacing=0 cellpadding=3 width=\"50%\">");
  color=getColor1(g_model1->name,g_model2->name);
  str.append(fv(tr("Name"), g_model1->name, color));
  color=getColor1(GetEepromInterface()->getSize(*g_model1), GetEepromInterface()->getSize(*g_model2));
  str.append("<b>"+tr("EEprom Size")+QString(": </b><font color=%2>%1</font><br>").arg(GetEepromInterface()->getSize(*g_model1)).arg(color));
  color=getColor1(getTimerStr(g_model1->timers[0]), getTimerStr(g_model2->timers[0]));
  str.append(fv(tr("Timer1"), getTimerStr(g_model1->timers[0]), color));  //value, mode, count up/down
  color=getColor1(getTimerStr(g_model1->timers[1]), getTimerStr(g_model2->timers[1]));
  str.append(fv(tr("Timer2"), getTimerStr(g_model1->timers[1]), color));  //value, mode, count up/down
  color=getColor1(getProtocol(g_model1->moduleData[0]),getProtocol(g_model2->moduleData[0]));
  str.append(fv(tr("Protocol"), getProtocol(g_model1->moduleData[0]), color)); //proto, numch, delay,
  color=getColor1(g_model1->thrTrim,g_model2->thrTrim);
  str.append(fv(tr("Throttle Trim"), g_model1->thrTrim ? tr("Enabled") : tr("Disabled"), color));
  color=getColor1(getTrimInc(g_model1),getTrimInc(g_model2));
  str.append(fv(tr("Trim Increment"), getTrimInc(g_model1),color));
  color = getColor1(getCenterBeepStr(g_model1), getCenterBeepStr(g_model2));
  str.append(fv(tr("Center Beep"), getCenterBeepStr(g_model1), color)); // specify which channels beep
  str.append("</table></td>");
  str.append("<td><table border=0 cellspacing=0 cellpadding=3 width=\"50%\">");
  color=getColor2(g_model1->name,g_model2->name);
  str.append(fv(tr("Name"), g_model2->name, color));
  color=getColor2(GetEepromInterface()->getSize(*g_model1), GetEepromInterface()->getSize(*g_model2));
  str.append("<b>"+tr("EEprom Size")+QString(": </b><font color=%2>%1</font><br>").arg(GetEepromInterface()->getSize(*g_model2)).arg(color));
  color=getColor2(getTimerStr(g_model1->timers[0]), getTimerStr(g_model2->timers[0]));
  str.append(fv(tr("Timer1"), getTimerStr(g_model2->timers[0]),color));  //value, mode, count up/down
  color=getColor2(getTimerStr(g_model1->timers[1]), getTimerStr(g_model2->timers[1]));
  str.append(fv(tr("Timer2"), getTimerStr(g_model2->timers[1]),color));  //value, mode, count up/down
  color=getColor2(getProtocol(g_model1->moduleData[0]),getProtocol(g_model2->moduleData[0]));
  str.append(fv(tr("Protocol"), getProtocol(g_model2->moduleData[0]), color)); //proto, numch, delay,
  color=getColor2(g_model1->thrTrim,g_model2->thrTrim);
  str.append(fv(tr("Throttle Trim"), g_model2->thrTrim ? tr("Enabled") : tr("Disabled"), color));
  color=getColor2(getTrimInc(g_model1),getTrimInc(g_model2));
  str.append(fv(tr("Trim Increment"), getTrimInc(g_model2),color));
  color = getColor2(getCenterBeepStr(g_model1),getCenterBeepStr(g_model2));
  str.append(fv(tr("Center Beep"), getCenterBeepStr(g_model2), color)); // specify which channels beep
  str.append("</td></tr></table></td></tr></table>");
  te->append(str);
}

void CompareDialog::printPhases()
{
  QString color;
  int i,k;
  QString str = "<table border=1 cellspacing=0 cellpadding=3 width=\"100%\">";
  str.append("<tr><td colspan=2><h2>"+tr("Flight modes Settings")+"</h2></td></tr>");
  str.append("<tr><td  width=\"50%\"><table border=1 cellspacing=0 cellpadding=1 width=\"100%\">");
  str.append("<tr><td style=\"border-style:none;\">&nbsp;</td><td colspan=2 align=center><b>");
  str.append(tr("Fades")+"</b></td><td colspan=4 align=center><b>"+tr("Trims"));
  str.append("</b></td><td rowspan=2 align=\"center\" valign=\"bottom\"><b>"+tr("Switch")+"</b></td></tr><tr><td align=center width=\"80\"><b>"+tr("Flight mode name"));
  str.append("</b></td><td align=center width=\"30\"><b>"+tr("IN")+"</b></td><td align=center width=\"30\"><b>"+tr("OUT")+"</b></td>");
  for (i=0; i<4; i++) {
    str.append(QString("<td width=\"40\" align=\"center\"><b>%1</b></td>").arg(getInputStr(g_model1, i)));
  }
  str.append("</tr>");
  for (i=0; i<GetCurrentFirmware()->getCapability(FlightModes); i++) {
    FlightModeData *pd1=&g_model1->flightModeData[i];
    FlightModeData *pd2=&g_model2->flightModeData[i];
    str.append("<tr><td><b>"+tr("FM")+QString("%1</b> ").arg(i));
    color=getColor1(pd1->name,pd2->name);
    str.append(QString("<font size=+1 face='Courier New' color=%2>%1</font></td>").arg(pd1->name).arg(color));
    color=getColor1(pd1->fadeIn,pd2->fadeIn);
    str.append(QString("<td width=\"30\" align=\"right\"><font size=+1 face='Courier New' color=%2>%1</font></td>").arg(pd1->fadeIn).arg(color));
    color=getColor1(pd1->fadeOut,pd2->fadeOut);
    str.append(QString("<td width=\"30\" align=\"right\"><font size=+1 face='Courier New' color=%2>%1</font></td>").arg(pd1->fadeOut).arg(color));
    for (k=0; k<4; k++) {
      if (pd1->trimRef[k]==-1) {
        color=getColor1(pd1->trim[k],pd2->trim[k]);
        str.append(QString("<td align=\"right\" width=\"30\"><font size=+1 face='Courier New' color=%2>%1</font></td>").arg(pd1->trim[k]).arg(color));
      } else {
        color=getColor1(pd1->trimRef[k],pd2->trimRef[k]);
        str.append(QString("<td align=\"right\" width=\"30\"><font size=+1 face='Courier New' color=%1>").arg(color)+tr("FM")+QString("%1</font></td>").arg(pd1->trimRef[k]));
      }
    }
    color=getColor1(pd1->swtch,pd2->swtch);
    str.append(QString("<td align=center><font size=+1 face='Courier New' color=%2>%1</font></td>").arg(pd1->swtch.toString()).arg(color));
    str.append("</tr>");
  }
  str.append("</table>");
  int gvars = GetCurrentFirmware()->getCapability(Gvars);
  if ((gvars && GetCurrentFirmware()->getCapability(GvarsFlightModes)) || GetCurrentFirmware()->getCapability(RotaryEncoders)) {
    str.append("<br><table border=1 cellspacing=0 cellpadding=1 width=\"100%\">");
    str.append("<tr><td style=\"border-style:none;\">&nbsp;</td>");
    if (GetCurrentFirmware()->getCapability(GvarsFlightModes)) {
      str.append(QString("<td colspan=%1 align=center><b>").arg(gvars)+tr("Gvars")+"</td>");
    }
    if (GetCurrentFirmware()->getCapability(RotaryEncoders)) {
      str.append(QString("<td colspan=%1 align=center><b>").arg(GetCurrentFirmware()->getCapability(RotaryEncoders))+tr("Rot. Enc.")+"</td>");
    }
    str.append("</tr><tr><td align=center><b>"+tr("Flight mode name")+"</b></td>");
    if (GetCurrentFirmware()->getCapability(GvarsFlightModes)) {
      for (i=0; i<gvars; i++) {
        str.append(QString("<td width=\"40\" align=\"center\"><b>GV%1</b><br>%2</td>").arg(i+1).arg(g_model1->gvars_names[i]));
      }
    }
    for (i=0; i<GetCurrentFirmware()->getCapability(RotaryEncoders); i++) {
      str.append(QString("<td align=\"center\"><b>RE%1</b></td>").arg((i==0 ? 'A': 'B')));
    }
    str.append("</tr>");
    for (i=0; i<GetCurrentFirmware()->getCapability(FlightModes); i++) {
      FlightModeData *pd1=&g_model1->flightModeData[i];
      FlightModeData *pd2=&g_model2->flightModeData[i];
      str.append("<tr><td><b>"+tr("FM")+QString("%1</b> ").arg(i));
      color=getColor1(pd1->name,pd2->name);
      str.append(QString("<font size=+1 face='Courier New' color=%2>%1</font></td>").arg(pd1->name).arg(color));
      if (GetCurrentFirmware()->getCapability(GvarsFlightModes)) {
        for (k=0; k<gvars; k++) {
          color=getColor1(pd1->gvars[k],pd2->gvars[k]);
          if (pd1->gvars[k]<=1024) {
            str.append(QString("<td align=\"right\" width=\"40\"><font size=+1 face='Courier New' color=%2>%1").arg(pd1->gvars[k]).arg(color)+"</font></td>");
          }
          else {
            int num = pd1->gvars[k] - 1025;
            if (num>=i) num++;
            str.append(QString("<td align=\"right\" width=\"40\"><font size=+1 face='Courier New' color=%1>").arg(color)+tr("FM")+QString("%1</font></td>").arg(num));
          }
        }
      }
      for (k=0; k<GetCurrentFirmware()->getCapability(RotaryEncoders); k++) {
        color=getColor1(pd1->rotaryEncoders[k],pd2->rotaryEncoders[k]);
        if (pd1->rotaryEncoders[k]<=1024) {
          str.append(QString("<td align=\"right\"><font size=+1 face='Courier New' color=%2>%1").arg(pd1->rotaryEncoders[k]).arg(color)+"</font></td>");
        }
        else {
          int num = pd1->rotaryEncoders[k] - 1025;
          if (num>=i) num++;
          str.append(QString("<td align=\"right\"><font size=+1 face='Courier New' color=%1>").arg(color)+tr("FM")+QString("%1</font></td>").arg(num));
        }
      }
      str.append("</tr>");
    }
    str.append("</table>");
  }
  str.append("</td>");

  str.append("<td  width=\"50%\"><table border=1 cellspacing=0 cellpadding=1 width=\"100%\">");
  str.append("<tr><td style=\"border-style:none;\">&nbsp;</td><td colspan=2 align=center><b>");
  str.append(tr("Fades")+"</b></td><td colspan=4 align=center><b>"+tr("Trims"));
  str.append("</b></td><td rowspan=2 align=\"center\" valign=\"bottom\"><b>"+tr("Switch")+"</b></td></tr><tr><td align=center width=\"80\"><b>"+tr("Flight mode name"));
  str.append("</b></td><td align=center width=\"30\"><b>"+tr("IN")+"</b></td><td align=center width=\"30\"><b>"+tr("OUT")+"</b></td>");
  for (i=0; i<4; i++) {
    str.append(QString("<td width=\"40\" align=\"center\"><b>%1</b></td>").arg(getInputStr(g_model1, i)));
  }
  str.append("</tr>");
  for (i=0; i<GetCurrentFirmware()->getCapability(FlightModes); i++) {
    FlightModeData *pd1=&g_model1->flightModeData[i];
    FlightModeData *pd2=&g_model2->flightModeData[i];
    str.append("<tr><td><b>"+tr("FM")+QString("%1</b> ").arg(i));
    color=getColor2(pd1->name,pd2->name);
    str.append(QString("<font size=+1 face='Courier New' color=%2>%1</font></td>").arg(pd2->name).arg(color));
    color=getColor2(pd1->fadeIn,pd2->fadeIn);
    str.append(QString("<td width=\"30\" align=\"right\"><font size=+1 face='Courier New' color=%2>%1</font></td>").arg(pd2->fadeIn).arg(color));
    color=getColor2(pd1->fadeOut,pd2->fadeOut);
    str.append(QString("<td width=\"30\" align=\"right\"><font size=+1 face='Courier New' color=%2>%1</font></td>").arg(pd2->fadeOut).arg(color));
    for (k=0; k<4; k++) {
      if (pd2->trimRef[k]==-1) {
        color=getColor2(pd1->trim[k],pd2->trim[k]);
        str.append(QString("<td align=\"right\" width=\"30\"><font size=+1 face='Courier New' color=%2>%1</font></td>").arg(pd2->trim[k]).arg(color));
      } else {
        color=getColor2(pd1->trimRef[k],pd2->trimRef[k]);
        str.append(QString("<td align=\"right\" width=\"30\"><font size=+1 face='Courier New' color=%1>").arg(color)+tr("FM")+QString("%1</font></td>").arg(pd2->trimRef[k]));
      }
    }
    color=getColor2(pd1->swtch,pd2->swtch);
    str.append(QString("<td align=center><font size=+1 face='Courier New' color=%2>%1</font></td>").arg(pd2->swtch.toString()).arg(color));
    str.append("</tr>");
  }
  str.append("</table>");
  
  if ((gvars && GetCurrentFirmware()->getCapability(GvarsFlightModes)) || GetCurrentFirmware()->getCapability(RotaryEncoders)) {
    str.append("<br><table border=1 cellspacing=0 cellpadding=1 width=\"100%\">");
    str.append("<tr><td style=\"border-style:none;\">&nbsp;</td>");
    if (GetCurrentFirmware()->getCapability(GvarsFlightModes)) {
      str.append(QString("<td colspan=%1 align=center><b>").arg(gvars)+tr("Gvars")+"</td>");
    }
    if (GetCurrentFirmware()->getCapability(RotaryEncoders)) {
      str.append(QString("<td colspan=%1 align=center><b>").arg(GetCurrentFirmware()->getCapability(RotaryEncoders))+tr("Rot. Enc.")+"</td>");
    }
    str.append("</tr><tr><td align=center ><b>"+tr("Flight mode name")+"</b></td>");
    if (GetCurrentFirmware()->getCapability(GvarsFlightModes)) {
      for (i=0; i<gvars; i++) {
        str.append(QString("<td width=\"40\" align=\"center\"><b>GV%1</b><br>%2</td>").arg(i+1).arg(g_model2->gvars_names[i]));
      }
    }
    for (i=0; i<GetCurrentFirmware()->getCapability(RotaryEncoders); i++) {
      str.append(QString("<td align=\"center\"><b>RE%1</b></td>").arg((i==0 ? 'A': 'B')));
    }
    str.append("</tr>");
    for (i=0; i<GetCurrentFirmware()->getCapability(FlightModes); i++) {
      FlightModeData *pd1=&g_model1->flightModeData[i];
      FlightModeData *pd2=&g_model2->flightModeData[i];
      str.append("<tr><td><b>"+tr("FM")+QString("%1</b> ").arg(i));
      color=getColor1(pd1->name,pd2->name);
      str.append(QString("<font size=+1 face='Courier New' color=%2>%1</font></td>").arg(pd2->name).arg(color));
      if (GetCurrentFirmware()->getCapability(GvarsFlightModes)) {
        for (k=0; k<gvars; k++) {
          color=getColor1(pd1->gvars[k],pd2->gvars[k]);
          if (pd2->gvars[k]<=1024) {
            str.append(QString("<td align=\"right\" width=\"40\"><font size=+1 face='Courier New' color=%2>%1").arg(pd2->gvars[k]).arg(color)+"</font></td>");
          }
          else {
            int num = pd2->gvars[k] - 1025;
            if (num>=i) num++;
            str.append(QString("<td align=\"right\" width=\"40\"><font size=+1 face='Courier New' color=%1>").arg(color)+tr("FM")+QString("%1</font></td>").arg(num));
          }
        }
      }
      for (k=0; k<GetCurrentFirmware()->getCapability(RotaryEncoders); k++) {
        color=getColor1(pd1->rotaryEncoders[k],pd2->rotaryEncoders[k]);
        if (pd2->rotaryEncoders[k]<=1024) {
          str.append(QString("<td align=\"right\"><font size=+1 face='Courier New' color=%2>%1").arg(pd2->rotaryEncoders[k]).arg(color)+"</font></td>");
        }
        else {
          int num = pd2->rotaryEncoders[k] - 1025;
          if (num>=i) num++;
          str.append(QString("<td align=\"right\"><font size=+1 face='Courier New' color=%1>").arg(color)+tr("FM")+QString("%1</font></td>").arg(num));
        }
      }
      str.append("</tr>");
    }
    str.append("</table>");
  }  
  str.append("</td></tr></table>");
  te->append(str);
}

void CompareDialog::printLimits()
{
  QString color;
  QString str = "<table border=1 cellspacing=0 cellpadding=3 style=\"page-break-after:always;\" width=\"100%\">";
  str.append("<tr><td colspan=2><h2>"+tr("Limits")+"</h2></td></tr>");
  str.append("<tr><td><table border=1 cellspacing=0 cellpadding=1 width=\"50%\">");
  if (GetCurrentFirmware()->getCapability(HasChNames)) {
    str.append("<tr><td>"+tr("Name")+"</td><td align=center><b>"+tr("Offset")+"</b></td><td align=center><b>"+tr("Min")+"</b></td><td align=center><b>"+tr("Max")+"</b></td><td align=center><b>"+tr("Invert")+"</b></td></tr>");
  } else {
    str.append("<tr><td></td><td align=center><b>"+tr("Offset")+"</b></td><td align=center><b>"+tr("Min")+"</b></td><td align=center><b>"+tr("Max")+"</b></td><td align=center><b>"+tr("Invert")+"</b></td></tr>");    
  }
  for(int i=0; i<GetCurrentFirmware()->getCapability(Outputs); i++) {
    str.append("<tr>");
    if (GetCurrentFirmware()->getCapability(HasChNames)) {
      QString name1=g_model1->limitData[i].name;
      QString name2=g_model2->limitData[i].name;
      color=getColor1(name1,name2);
      if (name1.trimmed().isEmpty()) {
        str.append(doTC(tr("CH")+QString(" %1").arg(i+1,2,10,QChar('0')),color,true));
      } else {
        str.append(doTC(name1,color,true));
      }
    } else {
      str.append(doTC(tr("CH")+QString(" %1").arg(i+1,2,10,QChar('0')),"",true));
    }
    color=getColor1(g_model1->limitData[i].offset,g_model2->limitData[i].offset);
    str.append(doTR(g_model1->limitData[i].offsetToString(), color));
    color=getColor1(g_model1->limitData[i].min,g_model2->limitData[i].min);
    str.append(doTR(g_model1->limitData[i].minToString(), color));
    color=getColor1(g_model1->limitData[i].max,g_model2->limitData[i].max);
    str.append(doTR(g_model1->limitData[i].maxToString(), color));
    color=getColor1(g_model1->limitData[i].revert,g_model2->limitData[i].revert);
    str.append(doTR(QString(g_model1->limitData[i].revert ? tr("INV") : tr("NOR")),color));
    str.append("</tr>");
  }
  str.append("</table></td>");
  str.append("<td><table border=1 cellspacing=0 cellpadding=1 width=\"50%\">");
  str.append("<tr><td></td><td align=center><b>"+tr("Offset")+"</b></td><td align=center><b>"+tr("Min")+"</b></td><td align=center><b>"+tr("Max")+"</b></td><td align=center><b>"+tr("Invert")+"</b></td></tr>");
  for(int i=0; i<GetCurrentFirmware()->getCapability(Outputs); i++) {
    str.append("<tr>");
    if (GetCurrentFirmware()->getCapability(HasChNames)) {
      QString name1=g_model1->limitData[i].name;
      QString name2=g_model2->limitData[i].name;
      color=getColor2(name1,name2);
      if (name2.trimmed().isEmpty()) {
        str.append(doTC(tr("CH")+QString(" %1").arg(i+1,2,10,QChar('0')),color,true));
      } else {
        str.append(doTC(name2,color,true));
      }
    } else {
      str.append(doTC(tr("CH")+QString(" %1").arg(i+1,2,10,QChar('0')),"",true));
    }
    color=getColor2(g_model1->limitData[i].offset,g_model2->limitData[i].offset);
    str.append(doTR(g_model2->limitData[i].offsetToString(), color));
    color=getColor2(g_model1->limitData[i].min,g_model2->limitData[i].min);
    str.append(doTR(g_model2->limitData[i].minToString(), color));
    color=getColor2(g_model1->limitData[i].max,g_model2->limitData[i].max);
    str.append(doTR(g_model2->limitData[i].maxToString(), color));
    color=getColor2(g_model1->limitData[i].revert,g_model2->limitData[i].revert);
    str.append(doTR(QString(g_model2->limitData[i].revert ? tr("INV") : tr("NOR")),color));
    str.append("</tr>");
  }
  str.append("</table></td></tr></table>");
  te->append(str);
}

void CompareDialog::printGvars()
{
  QString color;
  int gvars = GetCurrentFirmware()->getCapability(Gvars);

  if (!GetCurrentFirmware()->getCapability(GvarsFlightModes) && gvars) {
    QString str = "<table border=1 cellspacing=0 cellpadding=3 width=\"100%\">";
    str.append("<tr><td colspan=2><h2>"+tr("Global Variables")+"</h2></td></tr>");
    str.append("<tr><td width=50%>");
    str.append("<table border=1 cellspacing=0 cellpadding=3 width=100>");
    FlightModeData *pd1=&g_model1->flightModeData[0];
    FlightModeData *pd2=&g_model2->flightModeData[0];
    int width = 100 / gvars;
    str.append("<tr>");
    for (int i=0; i<gvars; i++) {
      str.append(QString("<td width=\"%1%\" align=\"center\"><b>").arg(width)+tr("GV")+QString("%1</b></td>").arg(i+1));
    }
    str.append("</tr>");
    str.append("<tr>");
    for (int i=0; i<gvars; i++) {
      color=getColor1(pd1->gvars[i],pd2->gvars[i]);
      str.append(QString("<td width=\"%1%\" align=\"center\"><font color=%2>").arg(width).arg(color)+QString("%1</font></td>").arg(pd1->gvars[i]));
    }
    str.append("</tr>");
    str.append("</table></td><td width=50%>");
    str.append("<table border=1 cellspacing=0 cellpadding=3 width=100>");
    str.append("<tr>");
    for (int i=0; i<gvars; i++) {
      str.append(QString("<td width=\"%1%\" align=\"center\"><b>").arg(width)+tr("GV")+QString("%1</b></td>").arg(i+1));
    }
    str.append("</tr>");
    str.append("<tr>");
    for (int i=0; i<gvars; i++) {
      color=getColor2(pd1->gvars[i],pd2->gvars[i]);
      str.append(QString("<td width=\"%1%\" align=\"center\"><font color=%2>").arg(width).arg(color)+QString("%1</font></td>").arg(pd2->gvars[i]));
    }
    str.append("</tr>");
    str.append("</table></td>");
    str.append("</tr></table>");
    te->append(str);
  }
}

void CompareDialog::printExpos()
{
  QString color;
  bool expoused[C9X_MAX_EXPOS]={false};
  bool expoused2[C9X_MAX_EXPOS]={false};

  QString str = "<table border=1 cellspacing=0 cellpadding=3 style=\"page-break-after:always;\" width=\"100%\"><tr><td><h2>";
  str.append(tr("Expo/Dr Settings"));
  str.append("</h2></td></tr><tr><td><table border=1 cellspacing=0 cellpadding=3>");
  for(uint8_t i=0; i<GetCurrentFirmware()->getCapability(Outputs); i++) {
    if (ChannelHasExpo(g_model1->expoData, i) || ChannelHasExpo(g_model2->expoData, i)) {
      str.append("<tr>");
      str.append("<td width=\"45%\">");
      str.append("<table border=0 cellspacing=0 cellpadding=0>");
      for (int j=0; j<C9X_MAX_EXPOS; j++) {    
        if (g_model1->expoData[j].chn==i){
          int expo=ModelHasExpo(g_model2->expoData, g_model1->expoData[j],expoused);
          if (expo>-1) {
            if (expoused[expo]==false) {
              color="grey";
              expoused[expo]=true;
            } else {
              color="green";
            }    
          } else {
            color="green";
          }
          ExpoData *ed=&g_model1->expoData[j];
          if(ed->mode==0)
            continue;
          str.append("<tr><td><font face='Courier New' color=\""+color+"\">");
          switch(ed->mode) {
            case (1): 
              str += "&lt;-&nbsp;";
              break;
            case (2): 
              str += "-&gt;&nbsp;";
              break;
            default:
              str += "&nbsp;&nbsp;&nbsp;";
              break;
          };

          str += tr("Weight") + QString("%1").arg(getGVarString(ed->weight)).rightJustified(6, ' ');
          str += ed->curve.toString().replace("<", "&lt;").replace(">", "&gt;");

          if (GetCurrentFirmware()->getCapability(FlightModes)) {
            if(ed->phases) {
              if (ed->phases!=(unsigned int)(1<<GetCurrentFirmware()->getCapability(FlightModes))-1) {
                int mask=1;
                int first=0;
                for (int i=0; i<GetCurrentFirmware()->getCapability(FlightModes);i++) {
                  if (!(ed->phases & mask)) {
                    first++;
                  }
                  mask <<=1;
                }
                if (first>1) {
                  str += " " + tr("Flight modes") + QString("(");
                } else {
                  str += " " + tr("Flight mode") + QString("(");
                }
                mask=1;
                first=1;
                for (int j=0; j<GetCurrentFirmware()->getCapability(FlightModes);j++) {
                  if (!(ed->phases & mask)) {
                    FlightModeData *pd = &g_model1->flightModeData[j];
                    if (!first) {
                      str += QString(", ")+ QString("%1").arg(getPhaseName(j+1, pd->name));
                    } else {
                      str += QString("%1").arg(getPhaseName(j+1,pd->name));
                      first=0;
                    }
                  }
                  mask <<=1;
                }
                str += QString(")");
              }
              else {
                str += tr("DISABLED")+QString(" !!!");
              }
            }
          } 
          if (ed->swtch.type)
            str += " " + tr("Switch") + QString("(%1)").arg(ed->swtch.toString());
          str += "</font></td></tr>";
        }
      }
      str.append("</table></td>");
      str.append("<td width=\"10%\" align=\"center\" valign=\"middle\"><b>"+getInputStr(g_model2, i)+"</b></td>");
      str.append("<td width=\"45%\">");
      str.append("<table border=0 cellspacing=0 cellpadding=0>");
      for (int j=0; j<C9X_MAX_EXPOS; j++) {
        if (g_model2->expoData[j].chn==i){
          int expo=ModelHasExpo(g_model1->expoData, g_model2->expoData[j], expoused2);
          if (expo>-1) {
            if (expoused2[expo]==false) {
              color="grey";
              expoused2[expo]=true;
            } else {
              color="red";
            }    
          } else {
            color="red";
          }
          ExpoData *ed=&g_model2->expoData[j];
          if(ed->mode==0)
            continue;
          str.append("<tr><td><font face='Courier New' color=\""+color+"\">");
          switch(ed->mode) {
            case (1): 
              str += "&lt;-&nbsp;";
              break;
            case (2): 
              str += "-&gt;&nbsp;";
              break;
            default:
              str += "&nbsp;&nbsp;&nbsp;";
              break;
          }

          str += tr("Weight") + QString("%1").arg(getGVarString(ed->weight)).rightJustified(6, ' ');
          str += ed->curve.toString().replace("<", "&lt;").replace(">", "&gt;");

          if (GetCurrentFirmware()->getCapability(FlightModes)) {
            if(ed->phases) {
              if (ed->phases!=(unsigned int)(1<<GetCurrentFirmware()->getCapability(FlightModes))-1) {
                int mask=1;
                int first=0;
                for (int i=0; i<GetCurrentFirmware()->getCapability(FlightModes);i++) {
                  if (!(ed->phases & mask)) {
                    first++;
                  }
                  mask <<=1;
                }
                if (first>1) {
                  str += " " + tr("Flight modes") + QString("(");
                } else {
                  str += " " + tr("Flight mode") + QString("(");
                }
                mask=1;
                first=1;
                for (int j=0; j<GetCurrentFirmware()->getCapability(FlightModes);j++) {
                  if (!(ed->phases & mask)) {
                    FlightModeData *pd = &g_model2->flightModeData[j];
                    if (!first) {
                      str += QString(", ")+ QString("%1").arg(getPhaseName(j+1, pd->name));
                    } else {
                      str += QString("%1").arg(getPhaseName(j+1,pd->name));
                      first=0;
                    }
                  }
                  mask <<=1;
                }
                str += QString(")");
              } else {
                str += tr("DISABLED")+QString(" !!!");
              }
            }
          } 
          if (ed->swtch.type)
            str += " " + tr("Switch") + QString("(%1)").arg(ed->swtch.toString());

          str += "</font></td></tr>";
        }
      }
      str.append("</table></td></tr>");
    }
  }
  str.append("</table></td></tr></table>");
  te->append(str);
}

void CompareDialog::printMixers()
{
  QString color;
  QString str = "<table border=1 cellspacing=0 cellpadding=3 style=\"page-break-after:always;\" width=\"100%\"><tr><td><h2>";
  str.append(tr("Mixers"));
  str.append("</h2></td></tr><tr><td><table border=1 cellspacing=0 cellpadding=3>");
  float scale=GetCurrentFirmware()->getCapability(SlowScale);
  bool mixused[64]={false};
  bool mixused2[64]={false};
  for(uint8_t i=1; i<=GetCurrentFirmware()->getCapability(Outputs); i++) {
    if (ChannelHasMix(g_model1->mixData, i) || ChannelHasMix(g_model2->mixData, i)) {
      str.append("<tr>");
      str.append("<td width=\"45%\">");
      str.append("<table border=0 cellspacing=0 cellpadding=0>");
      for (int j=0; j<GetCurrentFirmware()->getCapability(Mixes); j++) {
        if (g_model1->mixData[j].destCh==i) {
          int mix=ModelHasMix(g_model2->mixData, g_model1->mixData[j], mixused);
          if (mix>-1) {
            if (mixused[mix]==false) {
              color="grey";
              mixused[mix]=true;
            } else {
              color="green";
            }    
          } else {
            color="green";
          }
          MixData *md = &g_model1->mixData[j];
          str.append("<tr><td><font  face='Courier New' color=\""+color+"\">");
          switch(md->mltpx) {
            case (1):
              str += "&nbsp;*";
              break;
            case (2):
              str += "&nbsp;R";
              break;
            default:
              str += "&nbsp;&nbsp;";
              break;
          };
          str += QString(" %1").arg(getGVarString(md->weight)).rightJustified(6, ' ');
          str += md->srcRaw.toString(g_model1);
          if (md->swtch.type) str += " " + tr("Switch") + QString("(%1)").arg(md->swtch.toString());
          if (md->carryTrim) str += " " + tr("noTrim");
          if (md->sOffset)  str += " "+ tr("Offset") + QString(" (%1%)").arg(getGVarString(md->sOffset));
          if (md->curve.value) str += " " + Qt::escape(md->curve.toString());
          if (md->delayDown || md->delayUp) str += tr(" Delay(u%1:d%2)").arg(md->delayUp/scale).arg(md->delayDown/scale);
          if (md->speedDown || md->speedUp) str += tr(" Slow(u%1:d%2)").arg(md->speedUp/scale).arg(md->speedDown/scale);
          if (md->mixWarn)  str += " "+tr("Warn")+QString("(%1)").arg(md->mixWarn);
          if (GetCurrentFirmware()->getCapability(FlightModes)) {
            if(md->phases) {
              if (md->phases!=(unsigned int)(1<<GetCurrentFirmware()->getCapability(FlightModes))-1) {
                int mask=1;
                int first=0;
                for (int i=0; i<GetCurrentFirmware()->getCapability(FlightModes);i++) {
                  if (!(md->phases & mask)) {
                    first++;
                  }
                  mask <<=1;
                }
                if (first>1) {
                  str += " " + tr("Flight modes") + QString("(");
                } else {
                  str += " " + tr("Flight mode") + QString("(");
                }
                mask=1;
                first=1;
                for (int j=0; j<GetCurrentFirmware()->getCapability(FlightModes);j++) {
                  if (!(md->phases & mask)) {
                    FlightModeData *pd = &g_model1->flightModeData[j];
                    if (!first) {
                      str += QString(", ")+ QString("%1").arg(getPhaseName(j+1, pd->name));
                    } else {
                      str += QString("%1").arg(getPhaseName(j+1,pd->name));
                      first=0;
                    }
                  }
                  mask <<=1;
                }
                str += QString(")");
              } else {
                str += tr("DISABLED")+QString(" !!!");
              }
            }
          }
          str.append("</font></td></tr>");
        }
      }
      str.append("</table></td>");
      str.append("<td width=\"10%\" align=\"center\" valign=\"middle\"><b>"+tr("CH")+QString("%1</b></td>").arg(i,2,10,QChar('0')));
      str.append("<td width=\"45%\">");
      str.append("<table border=0 cellspacing=0 cellpadding=0>");
      for (int j=0; j<GetCurrentFirmware()->getCapability(Mixes); j++) {
        if (g_model2->mixData[j].destCh==i) {
          int mix=ModelHasMix(g_model1->mixData, g_model2->mixData[j],mixused2);
          if (mix>-1) {
            if (mixused2[mix]==false) {
              color="grey";
              mixused2[mix]=true;
            } else {
              color="red";
            }    
          } else {
            color="red";
          }
          MixData *md = &g_model2->mixData[j];
          str.append("<tr><td><font  face='Courier New' color=\""+color+"\">");
          switch(md->mltpx) {
            case (1):
              str += "&nbsp;*";
              break;
            case (2):
              str += "&nbsp;R";
              break;
            default:
              str += "&nbsp;&nbsp;";
              break;
          };
          str += QString(" %1").arg(getGVarString(md->weight)).rightJustified(6, ' ');
          str += md->srcRaw.toString(g_model2);
          if (md->swtch.type) str += " " + tr("Switch") + QString("(%1)").arg(md->swtch.toString());
          if (md->carryTrim) str += " " + tr("noTrim");
          if (md->sOffset)  str += " "+ tr("Offset") + QString(" (%1%)").arg(getGVarString(md->sOffset));
          if (md->curve.value) str += " " + Qt::escape(md->curve.toString());
          if (md->delayDown || md->delayUp) str += tr(" Delay(u%1:d%2)").arg(md->delayUp/scale).arg(md->delayDown/scale);
          if (md->speedDown || md->speedUp) str += tr(" Slow(u%1:d%2)").arg(md->speedUp/scale).arg(md->speedDown/scale);
          if (md->mixWarn)  str += " "+tr("Warn")+QString("(%1)").arg(md->mixWarn);
          if (GetCurrentFirmware()->getCapability(FlightModes)) {
            if(md->phases) {
              if (md->phases!=(unsigned int)(1<<GetCurrentFirmware()->getCapability(FlightModes))-1) {
                int mask=1;
                int first=0;
                for (int i=0; i<GetCurrentFirmware()->getCapability(FlightModes);i++) {
                  if (!(md->phases & mask)) {
                    first++;
                  }
                  mask <<=1;
                }
                if (first>1) {
                  str += " " + tr("Flight modes") + QString("(");
                } else {
                  str += " " + tr("Flight mode") + QString("(");
                }
                mask=1;
                first=1;
                for (int j=0; j<GetCurrentFirmware()->getCapability(FlightModes);j++) {
                  if (!(md->phases & mask)) {
                    FlightModeData *pd = &g_model2->flightModeData[j];
                    if (!first) {
                      str += QString(", ")+ QString("%1").arg(getPhaseName(j+1, pd->name));
                    } else {
                      str += QString("%1").arg(getPhaseName(j+1,pd->name));
                      first=0;
                    }
                  }
                  mask <<=1;
                }
                str += QString(")");
              } else {
                str += tr("DISABLED")+QString(" !!!");
              }
            }
          }
          str.append("</font></td></tr>");
        }
      }
      str.append("</table></td>");
      str.append("</tr>");
    }
  }
  str.append("</table></td></tr></table>");
  te->append(str);
}

void CompareDialog::printCurves()
{
  int i,r,g,b,c,count1,count2,usedcurves=0;
  QString cm1y,cm1x, cm2y,cm2x;
  char buffer [16];
  QPen pen(Qt::black, 2, Qt::SolidLine);
  int numcurves=firmware->getCapability(NumCurves);
  if (numcurves==0) {
    numcurves=16;
  }

  QString str = "<table border=1 cellspacing=0 cellpadding=3 style=\"page-break-after:always;\" width=\"100%\"><tr><td><h2>";
  str.append(tr("Curves"));
  str.append("</h2></td></tr><tr><td>");
  str.append("<table border=1 cellspacing=0 cellpadding=3 width=\"100%\">");
  QImage qi1(ISIZE+1,ISIZE+1,QImage::Format_RGB32);
  QPainter painter1(&qi1);
  QImage qi2(ISIZE+1,ISIZE+1,QImage::Format_RGB32);
  QPainter painter2(&qi2);
  painter1.setBrush(QBrush("#FFFFFF"));
  painter2.setBrush(QBrush("#FFFFFF"));
  painter1.setPen(QColor(0,0,0));
  painter2.setPen(QColor(0,0,0));
  painter1.drawRect(0,0,ISIZE,ISIZE);
  painter2.drawRect(0,0,ISIZE,ISIZE);
  
  for(i=0; i<numcurves; i++) {
    count1=0;
    for(int j=0; j<g_model1->curves[i].count; j++) {
      if (g_model1->curves[i].points[j].y!=0)
        count1++;
    }
    count2=0;
    for(int j=0; j<g_model2->curves[i].count; j++) {
      if (g_model2->curves[i].points[j].y!=0)
        count2++;
    }
    if ((count1>0) || (g_model1->curves[i].type == CurveData::CURVE_TYPE_CUSTOM)|| (g_model1->curves[i].count !=5) ||
        (count2>0) || (g_model2->curves[i].type == CurveData::CURVE_TYPE_CUSTOM)|| (g_model2->curves[i].count !=5)) {
      pen.setColor(colors[usedcurves]);
      painter1.setPen(pen);
      painter2.setPen(pen);

      colors[usedcurves].getRgb(&r,&g,&b);
      c=r;
      c*=256;
      c+=g;
      c*=256;
      c+=b;
      sprintf(buffer,"%06x",c);
      // curves are different in number of points or curve type so makes little sense to compare they are just different
      if ((g_model1->curves[i].count!=g_model2->curves[i].count) || (g_model1->curves[i].type!=g_model2->curves[i].type)) {        
        cm1y="[";
        cm1x="[";
        for(int j=0; j<g_model1->curves[i].count; j++) {
          cm1y.append(QString("%1").arg(g_model1->curves[i].points[j].y));
          cm1x.append(QString("%1").arg(g_model1->curves[i].points[j].x));
          if (j<(g_model1->curves[i].count-1)) {
            cm1y.append(",");
            cm1x.append(",");
          }
          if (g_model1->curves[i].type == CurveData::CURVE_TYPE_CUSTOM)
            painter1.drawLine(ISIZE/2+(ISIZE*g_model1->curves[i].points[j-1].x)/200,ISIZE/2-(ISIZE*g_model1->curves[i].points[j-1].y)/200,ISIZE/2+(ISIZE*g_model1->curves[i].points[j].x)/200,ISIZE/2-(ISIZE*g_model1->curves[i].points[j].y)/200);
          else
            painter1.drawLine(ISIZE*(j-1)/(g_model1->curves[i].count-1),ISIZE/2-(ISIZE*g_model1->curves[i].points[j-1].y)/200,ISIZE*(j)/(g_model1->curves[i].count-1),ISIZE/2-(ISIZE*g_model1->curves[i].points[j].y)/200);
          
        }
        cm1y.append("]");
        cm1x.append("]");
        cm2y="[";
        cm2x="[";
        for(int j=0; j<g_model2->curves[i].count; j++) {
          cm2y.append(QString("%1").arg(g_model2->curves[i].points[j].y));
          cm2x.append(QString("%1").arg(g_model2->curves[i].points[j].x));
          if (j<(g_model2->curves[i].count-1)) {
            cm2y.append(",");
            cm2x.append(",");
          }
          if (g_model2->curves[i].type == CurveData::CURVE_TYPE_CUSTOM)
            painter2.drawLine(ISIZE/2+(ISIZE*g_model2->curves[i].points[j-1].x)/200,ISIZE/2-(ISIZE*g_model2->curves[i].points[j-1].y)/200,ISIZE/2+(ISIZE*g_model2->curves[i].points[j].x)/200,ISIZE/2-(ISIZE*g_model2->curves[i].points[j].y)/200);
          else
            painter2.drawLine(ISIZE*(j-1)/(g_model2->curves[i].count-1),ISIZE/2-(ISIZE*g_model2->curves[i].points[j-1].y)/200,ISIZE*(j)/(g_model2->curves[i].count-1),ISIZE/2-(ISIZE*g_model2->curves[i].points[j].y)/200);
          
        }
        cm2y.append("]");
        cm2x.append("]");
        str.append("<tr><td nowrap width=\"45%\"><font color=green>");
        str.append(cm1y);
        if ((g_model1->curves[i].type == CurveData::CURVE_TYPE_CUSTOM)) {
          str.append(QString("<br>")+cm1x+QString("</font></td>"));
        }      
        str.append(QString("<td width=\"10%\" align=\"center\"><font color=#%1><b>").arg(buffer)+tr("Curve")+QString(" %1</b></font></td>").arg(i+1));
        str.append("<td nowrap width=\"45%\"><font color=red>");
        str.append(cm2y);
        if ((g_model2->curves[i].type == CurveData::CURVE_TYPE_CUSTOM)) {
          str.append(QString("<br>")+cm2x+QString("</font></td></tr>"));
        }      
      } else {
      // curves have the same number of points and the same type, we can compare them point by point
        cm1y="[";
        cm1x="[";
        cm2y="[";
        cm2x="[";
        for(int j=0; j<g_model1->curves[i].count; j++) {
          if (g_model1->curves[i].points[j].y!=g_model2->curves[i].points[j].y) {
            cm1y.append(QString("<font color=green>%1</font>").arg(g_model1->curves[i].points[j].y));
            cm2y.append(QString("<font color=red>%1</font>").arg(g_model2->curves[i].points[j].y));
          } else {
            cm1y.append(QString("<font color=grey>%1</font>").arg(g_model1->curves[i].points[j].y));
            cm2y.append(QString("<font color=grey>%1</font>").arg(g_model2->curves[i].points[j].y));            
          }
          if (g_model1->curves[i].points[j].x!=g_model2->curves[i].points[j].x) {
            cm1x.append(QString("<font color=green>%1</font>").arg(g_model1->curves[i].points[j].x));
            cm2x.append(QString("<font color=red>%1</font>").arg(g_model2->curves[i].points[j].x));
          } else {
            cm1x.append(QString("<font color=grey>%1</font>").arg(g_model1->curves[i].points[j].x));
            cm2x.append(QString("<font color=grey>%1</font>").arg(g_model2->curves[i].points[j].x));            
          }
          if (j<(g_model1->curves[i].count-1)) {
            cm1y.append(",");
            cm1x.append(",");
            cm2y.append(",");
            cm2x.append(",");
          }
          if (g_model1->curves[i].type == CurveData::CURVE_TYPE_CUSTOM)
            painter1.drawLine(ISIZE/2+(ISIZE*g_model1->curves[i].points[j-1].x)/200,ISIZE/2-(ISIZE*g_model1->curves[i].points[j-1].y)/200,ISIZE/2+(ISIZE*g_model1->curves[i].points[j].x)/200,ISIZE/2-(ISIZE*g_model1->curves[i].points[j].y)/200);
          else
            painter1.drawLine(ISIZE*(j-1)/(g_model1->curves[i].count-1),ISIZE/2-(ISIZE*g_model1->curves[i].points[j-1].y)/200,ISIZE*(j)/(g_model1->curves[i].count-1),ISIZE/2-(ISIZE*g_model1->curves[i].points[j].y)/200);
          if (g_model2->curves[i].type == CurveData::CURVE_TYPE_CUSTOM)
            painter2.drawLine(ISIZE/2+(ISIZE*g_model2->curves[i].points[j-1].x)/200,ISIZE/2-(ISIZE*g_model2->curves[i].points[j-1].y)/200,ISIZE/2+(ISIZE*g_model2->curves[i].points[j].x)/200,ISIZE/2-(ISIZE*g_model2->curves[i].points[j].y)/200);
          else
            painter2.drawLine(ISIZE*(j-1)/(g_model2->curves[i].count-1),ISIZE/2-(ISIZE*g_model2->curves[i].points[j-1].y)/200,ISIZE*(j)/(g_model2->curves[i].count-1),ISIZE/2-(ISIZE*g_model2->curves[i].points[j].y)/200);          
        }
        painter1.setPen(QColor(0,0,0));
        painter2.setPen(QColor(0,0,0));
        painter1.drawLine(0,ISIZE/2,ISIZE,ISIZE/2);
        painter2.drawLine(0,ISIZE/2,ISIZE,ISIZE/2);
        painter1.drawLine(ISIZE/2,0,ISIZE/2,ISIZE);
        painter2.drawLine(ISIZE/2,0,ISIZE/2,ISIZE);
        for(i=0; i<21; i++) {
          painter1.drawLine(ISIZE/2-5,(ISIZE*i)/(20),ISIZE/2+5,(ISIZE*i)/(20));
          painter2.drawLine(ISIZE/2-5,(ISIZE*i)/(20),ISIZE/2+5,(ISIZE*i)/(20));
          painter1.drawLine((ISIZE*i)/(20),ISIZE/2-5,(ISIZE*i)/(20),ISIZE/2+5);
          painter2.drawLine((ISIZE*i)/(20),ISIZE/2-5,(ISIZE*i)/(20),ISIZE/2+5);
        }

        cm1y.append("]");
        cm1x.append("]");
        cm2y.append("]");
        cm2x.append("]");
        str.append("<tr><td nowrap width=\"45%\">");
        str.append(cm1y);
        if ((g_model1->curves[i].type == CurveData::CURVE_TYPE_CUSTOM)) {
          str.append(QString("<br>")+cm1x+QString("</td>"));
        }      
        str.append(QString("<td width=\"10%\" align=\"center\"><font color=#%1><b>").arg(buffer)+tr("Curve")+QString(" %1</b></font></td>").arg(i+1));
        str.append("<td nowrap width=\"45%\">");
        str.append(cm2y);
        if ((g_model2->curves[i].type == CurveData::CURVE_TYPE_CUSTOM)) {
          str.append(QString("<br>")+cm2x+QString("</td></tr>"));
        }      
      }
      usedcurves++;
    }
  }
  if (usedcurves>0) {
    str.append(QString("<tr><td width=45 align=center><img src=\"%1\" border=0></td><td>&nbsp;</td><td width=45 align=center><img src=\"%2\" border=0></td>").arg(curvefile1).arg(curvefile2));
    str.append("</table></td></tr></table>");
    qi1.save(curvefile1, "png",100); 
    qi2.save(curvefile2, "png",100); 
    te->append(str);
  }
}

void CompareDialog::printSwitches()
{
    int sc=0;
    QString color;
    QString str = "<table border=1 cellspacing=0 cellpadding=3 width=\"100%\">";
    str.append("<tr><td><h2>"+tr("Logical Switches")+"</h2></td></tr>");
    str.append("<tr><td><table border=1 cellspacing=0 cellpadding=1 width=\"100%\">");
    for (int i=0; i<GetCurrentFirmware()->getCapability(LogicalSwitches); i++) {
      GeneralSettings settings;
      QString sw1 = g_model1->logicalSw[i].toString(*g_model1, settings);
      QString sw2 = g_model2->logicalSw[i].toString(*g_model2, settings);
      if (!(sw1.isEmpty() && sw2.isEmpty())) {
        str.append("<tr>");
        color=getColor1(sw1,sw2);
        str.append(QString("<td  width=\"45%\"><font color=%1>").arg(color)+sw1+"</font></td>");
        str.append("<td align=\"center\" width=\"10%\"><b>"+tr("L%1").arg(i+1)+QString("</b></td>"));
        color=getColor2(sw1,sw2);
        str.append(QString("<td  width=\"45%\"><font color=%1>").arg(color)+sw2+"</font></td>");
        str.append("</tr>");
        sc++;
      }
    }
    str.append("</table></td></tr></table>");
    if (sc>0)
        te->append(str);
}

void CompareDialog::printFSwitches()
{
  QString color1;
  QString color2;
  int sc=0;
  QString str = "<table border=1 cellspacing=0 cellpadding=3 style=\"page-break-before:always;\" width=\"100%\">";
  str.append("<tr><td><h2>"+tr("Special Functions")+"</h2></td></tr>");
  str.append("<tr><td><table border=1 cellspacing=0 cellpadding=1 width=\"100%\"><tr>");
  str.append("<td width=\"7%\" align=\"center\"><b>"+tr("Switch")+"</b></td>");
  str.append("<td width=\"12%\" align=\"center\"><b>"+tr("Function")+"</b></td>");
  str.append("<td width=\"12%\" align=\"center\"><b>"+tr("Param")+"</b></td>");
  str.append("<td width=\"7%\" align=\"center\"><b>"+tr("Repeat")+"</b></td>");
  str.append("<td width=\"7%\" align=\"center\"><b>"+tr("Enable")+"</b></td>");
  str.append("<td width=\"10%\">&nbsp;</td>");
  str.append("<td width=\"7%\" align=\"center\"><b>"+tr("Switch")+"</b></td>");
  str.append("<td width=\"12%\" align=\"center\"><b>"+tr("Function")+"</b></td>");
  str.append("<td width=\"12%\" align=\"center\"><b>"+tr("Param")+"</b></td>");
  str.append("<td width=\"7%\" align=\"center\"><b>"+tr("Repeat")+"</b></td>");
  str.append("<td width=\"7%\" align=\"center\"><b>"+tr("Enable")+"</b></td>");
  str.append("</tr>");
  for(int i=0; i<GetCurrentFirmware()->getCapability(CustomFunctions); i++)
  {
    if (g_model1->customFn[i].swtch.type || g_model2->customFn[i].swtch.type) {
      if ((g_model1->customFn[i].swtch != g_model2->customFn[i].swtch) || (g_model1->customFn[i].func!=g_model2->customFn[i].func) || (g_model1->customFn[i].adjustMode!=g_model2->customFn[i].adjustMode) || (g_model1->customFn[i].param!=g_model2->customFn[i].param) || (g_model1->customFn[i].enabled != g_model2->customFn[i].enabled) || (g_model1->customFn[i].repeatParam != g_model2->customFn[i].repeatParam)) {
        color1="green";
        color2="red";
      } else {
        color1="grey";
        color2="grey";
      }
      str.append("<tr>");
      if (g_model1->customFn[i].swtch.type) {
        str.append(doTC(g_model1->customFn[i].swtch.toString(),color1));
        str.append(doTC(g_model1->customFn[i].funcToString(),color1));
        str.append(doTC(g_model1->customFn[i].paramToString(),color1));
        int index=g_model1->customFn[i].func;
        if (index==FuncPlaySound || index==FuncPlayHaptic || index==FuncPlayValue || index==FuncPlayPrompt || index==FuncPlayBoth || index==FuncBackgroundMusic) {
          str.append(doTC(QString("%1").arg(g_model1->customFn[i].repeatParam),color1));
        } else {
          str.append(doTC( "---",color1));
        }
        if ((index<=FuncInstantTrim) || (index>FuncBackgroundMusicPause)) {
          str.append(doTC((g_model1->customFn[i].enabled ? "ON" : "OFF"),color1));
        } else {
          str.append(doTC( "---",color1));
        }
      } else {
        str.append("<td>&nbsp;</td><td>&nbsp;</td><td>&nbsp;</td><td>&nbsp;</td><td>&nbsp;</td>");
      }
      str.append(doTC(tr("SF")+QString("%1").arg(i+1),"",true));
      if (g_model2->customFn[i].swtch.type) {
        str.append(doTC(g_model2->customFn[i].swtch.toString(),color2));
        str.append(doTC(g_model2->customFn[i].funcToString(),color2));
        str.append(doTC(g_model2->customFn[i].paramToString(),color2));
        int index=g_model2->customFn[i].func;
        if (index==FuncPlaySound || index==FuncPlayHaptic || index==FuncPlayValue || index==FuncPlayPrompt || index==FuncPlayBoth || index==FuncBackgroundMusic) {
          str.append(doTC(QString("%1").arg(g_model2->customFn[i].repeatParam),color2));
        } else {
          str.append(doTC( "---",color2));
        }
        if ((index<=FuncInstantTrim) || (index>FuncBackgroundMusicPause)) {
          str.append(doTC((g_model2->customFn[i].enabled ? "ON" : "OFF"),color2));
        } else {
          str.append(doTC( "---",color2));
        }
      }
      else {
        str.append("<td>&nbsp;</td><td>&nbsp;</td>");
      }
      str.append("</tr>");
      sc++;
    }
}
  str.append("</table></td></tr></table>");
  str.append("<br>");
  if (sc!=0)
      te->append(str);
}

void CompareDialog::printFrSky()
{
  QString color;
  float value1,value2;
  QString str = "<table border=1 cellspacing=0 cellpadding=3 width=\"100%\">";
  str.append("<tr><td colspan=2><h2>"+tr("Telemetry Settings")+"</h2></td></tr>");
  str.append("<tr><td width=\"50%\">");
  str.append("<table border=1 cellspacing=0 cellpadding=1 width=\"100%\">");
  FrSkyData *fd1=&g_model1->frsky;
  FrSkyData *fd2=&g_model2->frsky;
  str.append("<tr><td align=\"center\" width=\"22%\"><b>"+tr("Analog")+"</b></td><td align=\"center\" width=\"26%\"><b>"+tr("Unit")+"</b></td><td align=\"center\" width=\"26%\"><b>"+tr("Scale")+"</b></td><td align=\"center\" width=\"26%\"><b>"+tr("Offset")+"</b></td></tr>");
  for (int i=0; i<2; i++) {
    str.append("<tr>");
    float ratio=(fd1->channels[i].ratio/(fd1->channels[i].type==0 ?10.0:1));
    str.append("<td align=\"center\"><b>"+tr("A%1").arg(i+1)+"</b></td>");
    color=getColor1(fd1->channels[i].type,fd2->channels[i].type);
    str.append("<td align=\"center\"><font color="+color+">"+getFrSkyUnits(fd1->channels[i].type)+"</font></td>");
    color=getColor1(fd1->channels[i].ratio,fd2->channels[i].ratio);
    str.append("<td align=\"center\"><font color="+color+">"+QString::number(ratio,10,(fd1->channels[i].type==0 ? 1:0))+"</font></td>");
    color=getColor1(fd1->channels[i].offset*fd1->channels[i].ratio,fd2->channels[i].offset*fd2->channels[i].ratio);
    str.append("<td align=\"center\"><font color="+color+">"+QString::number((fd1->channels[i].offset*ratio)/255,10,(fd1->channels[i].type==0 ? 1:0))+"</font></td>");
    str.append("</tr>");
  }
  str.append("</table><br>");
  str.append("<table border=1 cellspacing=0 cellpadding=1 width=\"100%\">");
  str.append("<tr><td></td><td colspan=\"3\" align=\"center\"><b>"+tr("Alarm 1")+"</b></td><td colspan=\"3\" align=\"center\"><b>"+tr("Alarm 2")+"</b></td>");
  str.append("<tr><td width=\"22%\"></td>");
  str.append("<td width=\"13%\" align=\"center\"><b>"+tr("Type")+"</b></td>");
  str.append("<td width=\"13%\" align=\"center\"><b>"+tr("Condition")+"</b></td>");
  str.append("<td width=\"13%\" align=\"center\"><b>"+tr("Value")+"</b></td>");
  str.append("<td width=\"13%\" align=\"center\"><b>"+tr("Type")+"</b></td>");
  str.append("<td width=\"13%\" align=\"center\"><b>"+tr("Condition")+"</b></td>");
  str.append("<td width=\"13%\" align=\"center\"><b>"+tr("Value")+"</b></td></tr>");
  for (int i=0; i<2; i++) {
    float ratio1=(fd1->channels[i].ratio/(fd1->channels[i].type==0 ?10.0:1));
    float ratio2=(fd1->channels[i].ratio/(fd1->channels[i].type==0 ?10.0:1));
    str.append("<tr>");
    str.append("<td align=\"center\"><b>"+tr("A%1").arg(i+1)+"</b></td>");
    color=getColor1(fd1->channels[i].alarms[0].level,fd2->channels[i].alarms[0].level);
    str.append("<td align=\"center\"><font color="+color+">"+getFrSkyAlarmType(fd1->channels[i].alarms[0].level)+"</font></td>");
    color=getColor1(fd1->channels[i].alarms[0].greater,fd2->channels[i].alarms[0].greater);
    str.append("<td align=\"center\"><font color="+color+">");
    str.append((fd1->channels[i].alarms[0].greater==1) ? "&gt;" : "&lt;");
    value1=ratio1*(fd1->channels[i].alarms[0].value/255.0+fd1->channels[i].offset/255.0);
    value2=ratio2*(fd2->channels[i].alarms[0].value/255.0+fd2->channels[i].offset/255.0);
    color=getColor1(value1,value2);
    str.append("</font></td><td align=\"center\"><font color="+color+">"+QString::number(value1,10,(fd1->channels[i].type==0 ? 1:0))+"</font></td>");
    color=getColor1(fd1->channels[i].alarms[1].level,fd2->channels[i].alarms[1].level);
    str.append("<td align=\"center\"><font color="+color+">"+getFrSkyAlarmType(fd1->channels[i].alarms[1].level)+"</font></td>");
    color=getColor1(fd1->channels[i].alarms[1].greater,fd2->channels[i].alarms[1].greater);
    str.append("<td align=\"center\"><font color="+color+">");
    str.append((fd1->channels[i].alarms[1].greater==1) ? "&gt;" : "&lt;");
    value1=ratio1*(fd1->channels[i].alarms[1].value/255.0+fd1->channels[i].offset/255.0);
    value2=ratio2*(fd2->channels[i].alarms[1].value/255.0+fd2->channels[i].offset/255.0);
    color=getColor1(value1,value2);
    str.append("</font></td><td align=\"center\"><font color="+color+">"+QString::number(value1,10,(fd1->channels[i].type==0 ? 1:0))+"</font></td></tr>");
  }
  str.append("<tr><td align=\"center\"><b>"+tr("RSSI Alarm")+"</b></td>");
  color=getColor1(fd1->rssiAlarms[0].level,fd2->rssiAlarms[0].level);
  str.append("<td align=\"center\"><font color="+color+">"+getFrSkyAlarmType(fd1->rssiAlarms[0].level)+"</td>");
  str.append("<td align=\"center\">&lt;</td>");
  color=getColor1(fd1->rssiAlarms[0].value,fd2->rssiAlarms[0].value);
  str.append("<td align=\"center\"><font color="+color+">"+QString::number(fd1->rssiAlarms[0].value,10)+"</td>");
  color=getColor1(fd1->rssiAlarms[1].level,fd2->rssiAlarms[1].level);
  str.append("<td align=\"center\"><font color="+color+">"+getFrSkyAlarmType(fd1->rssiAlarms[1].level)+"</td>");
  str.append("<td align=\"center\">&lt;</td>");
  color=getColor1(fd1->rssiAlarms[1].value,fd2->rssiAlarms[1].value);
  str.append("<td align=\"center\"><font color="+color+">"+QString::number(fd1->rssiAlarms[1].value,10)+"</td>");
  str.append("</table>");
#if 0
  if (GetCurrentFirmware()->getCapability(TelemetryBars) || GetCurrentFirmware()->getCapability(TelemetryCSFields)) {
    int cols=GetCurrentFirmware()->getCapability(TelemetryColsCSFields);
    if (cols==0) cols=2;
    for (int j=0; j<GetCurrentFirmware()->getCapability(TelemetryCSFields)/(4*cols); j++) {
      QString tcols;
      QString cwidth;
      QString swidth;
      if (cols==2) {
        tcols="3";
        cwidth="45";
        swidth="10";
      } else {
        tcols="5";
        cwidth="30";
        swidth="5";
      }
      color=getColor1(fd1->screens[j].type,fd2->screens[j].type);
      if (fd1->screens[j].type==0) {
        str.append("<br><table border=1 cellspacing=0 cellpadding=3 width=\"100%\"><tr><td colspan="+tcols+" align=\"Left\"><b><font color="+color+">"+tr("Custom Telemetry View")+"</font></b></td></tr><tr><td colspan=3>&nbsp;</td></tr>");
        for (int r=0; r<4; r++) {
          str.append("<tr>");
          for (int c=0; c<cols; c++) {
            if (fd1->screens[j].type==fd2->screens[j].type) 
              color=getColor1(fd1->screens[j].body.lines[r].source[c],fd2->screens[j].body.lines[r].source[c]);
            str.append("<td  align=\"Center\" width=\""+cwidth+"%\"><font color="+color+">"+getFrSkySrc(fd1->screens[j].body.lines[r].source[c])+"</font></td>");
            if (c<(cols-1)) {
              str.append("<td  align=\"Center\" width=\""+swidth+"%\"><b>&nbsp;</b></td>");
            }
          }
          str.append("</tr>");  
        }
        str.append("</table>");        
      } else {
        str.append("<br><table border=1 cellspacing=0 cellpadding=1 width=\"100%\"><tr><td colspan=4 align=\"Left\"><b><font color="+color+">"+tr("Telemetry Bars")+"</font></b></td></tr>");
        str.append("<tr><td  align=\"Center\"><b>"+tr("Bar Number")+"</b></td><td  align=\"Center\"><b>"+tr("Source")+"</b></td><td  align=\"Center\"><b>"+tr("Min")+"</b></td><td  align=\"Center\"><b>"+tr("Max")+"</b></td></tr>");
        for (int i=0; i<4; i++) {
          str.append("<tr><td  align=\"Center\"><b>"+QString::number(i+1,10)+"</b></td>");
          if (fd1->screens[0].type==fd2->screens[0].type)
            color=getColor1(fd1->screens[0].body.bars[i].source,fd2->screens[0].body.bars[i].source);
          str.append("<td  align=\"Center\"><font color="+color+">"+getFrSkySrc(fd1->screens[0].body.bars[i].source)+"</font></td>");
          // TODO value1 = getBarValue(fd1->screens[0].body.bars[i].source,fd1->screens[0].body.bars[i].barMin,fd1);
          // TODO value2 = getBarValue(fd2->screens[0].body.bars[i].source,fd2->screens[0].body.bars[i].barMin,fd2);
          if (fd1->screens[0].type==fd2->screens[0].type)
            color=getColor1(value1,value2);
          str.append("<td  align=\"Right\"><font color="+color+">"+QString::number(value1)+"</td>");
          // TODO value1=getBarValue(fd1->screens[0].body.bars[i].source,fd1->screens[0].body.bars[i].barMax,fd1);
          // TODO value2=getBarValue(fd2->screens[0].body.bars[i].source,fd2->screens[0].body.bars[i].barMax,fd2);
          if (fd1->screens[0].type==fd2->screens[0].type)
           color=getColor1(value1,value2);
          str.append("<td  align=\"Right\"><font color="+color+">"+QString::number(value1)+"</td></tr>");
        }
        str.append("</table>");
      }
    }
  }
#endif
  
  str.append("</td><td width=\"50%\">");
  str.append("<table border=1 cellspacing=0 cellpadding=1 width=\"100%\">");
  str.append("<tr><td align=\"center\" width=\"22%\"><b>"+tr("Analog")+"</b></td><td align=\"center\" width=\"26%\"><b>"+tr("Unit")+"</b></td><td align=\"center\" width=\"26%\"><b>"+tr("Scale")+"</b></td><td align=\"center\" width=\"26%\"><b>"+tr("Offset")+"</b></td></tr>");
  for (int i=0; i<2; i++) {
    str.append("<tr>");
    float ratio=(fd2->channels[i].ratio/(fd1->channels[i].type==0 ?10.0:1));
    str.append("<td align=\"center\"><b>"+tr("A%1").arg(i+1)+"</b></td>");
    color=getColor2(fd1->channels[i].type,fd2->channels[i].type);
    str.append("<td align=\"center\"><font color="+color+">"+getFrSkyUnits(fd2->channels[i].type)+"</font></td>");
    color=getColor2(fd1->channels[i].ratio,fd2->channels[i].ratio);
    str.append("<td align=\"center\"><font color="+color+">"+QString::number(ratio,10,(fd2->channels[i].type==0 ? 1:0))+"</font></td>");
    color=getColor2(fd1->channels[i].offset*fd1->channels[i].ratio,fd2->channels[i].offset*fd2->channels[i].ratio);
    str.append("<td align=\"center\"><font color="+color+">"+QString::number((fd2->channels[i].offset*ratio)/255,10,(fd2->channels[i].type==0 ? 1:0))+"</font></td>");
    str.append("</tr>");
  }
  str.append("</table><br>");
  str.append("<table border=1 cellspacing=0 cellpadding=1 width=\"100%\">");
  str.append("<tr><td></td><td colspan=\"3\" align=\"center\"><b>"+tr("Alarm 1")+"</b></td><td colspan=\"3\" align=\"center\"><b>"+tr("Alarm 2")+"</b></td>");
  str.append("<tr><td width=\"22%\"></td>");
  str.append("<td width=\"13%\" align=\"center\"><b>"+tr("Type")+"</b></td>");
  str.append("<td width=\"13%\" align=\"center\"><b>"+tr("Condition")+"</b></td>");
  str.append("<td width=\"13%\" align=\"center\"><b>"+tr("Value")+"</b></td>");
  str.append("<td width=\"13%\" align=\"center\"><b>"+tr("Type")+"</b></td>");
  str.append("<td width=\"13%\" align=\"center\"><b>"+tr("Condition")+"</b></td>");
  str.append("<td width=\"13%\" align=\"center\"><b>"+tr("Value")+"</b></td></tr>");
  for (int i=0; i<2; i++) {
    float ratio1=(fd1->channels[i].ratio/(fd1->channels[i].type==0 ?10.0:1));
    float ratio2=(fd1->channels[i].ratio/(fd1->channels[i].type==0 ?10.0:1));
    str.append("<tr>");
    str.append("<td align=\"center\"><b>"+tr("A%1").arg(i+1)+"</b></td>");
    color=getColor2(fd1->channels[i].alarms[0].level,fd2->channels[i].alarms[0].level);
    str.append("<td align=\"center\"><font color="+color+">"+getFrSkyAlarmType(fd2->channels[i].alarms[0].level)+"</font></td>");
    color=getColor2(fd1->channels[i].alarms[0].greater,fd2->channels[i].alarms[0].greater);
    str.append("<td align=\"center\"><font color="+color+">");
    str.append((fd2->channels[i].alarms[0].greater==1) ? "&gt;" : "&lt;");
    value1=ratio1*(fd1->channels[i].alarms[0].value/255.0+fd1->channels[i].offset/255.0);
    value2=ratio2*(fd2->channels[i].alarms[0].value/255.0+fd2->channels[i].offset/255.0);
    color=getColor2(value1,value2);
    str.append("</font></td><td align=\"center\"><font color="+color+">"+QString::number(value2,10,(fd2->channels[i].type==0 ? 1:0))+"</font></td>");
    color=getColor2(fd1->channels[i].alarms[1].level,fd2->channels[i].alarms[1].level);
    str.append("<td align=\"center\"><font color="+color+">"+getFrSkyAlarmType(fd2->channels[i].alarms[1].level)+"</font></td>");
    color=getColor2(fd1->channels[i].alarms[1].greater,fd2->channels[i].alarms[1].greater);
    str.append("<td align=\"center\"><font color="+color+">");
    str.append((fd2->channels[i].alarms[1].greater==1) ? "&gt;" : "&lt;");
    value1=ratio1*(fd1->channels[i].alarms[1].value/255.0+fd1->channels[i].offset/255.0);
    value2=ratio2*(fd2->channels[i].alarms[1].value/255.0+fd2->channels[i].offset/255.0);
    color=getColor2(value1,value2);
    str.append("</font></td><td align=\"center\"><font color="+color+">"+QString::number(value2,10,(fd2->channels[i].type==0 ? 1:0))+"</font></td></tr>");
  }
  str.append("<tr><td align=\"Center\"><b>"+tr("RSSI Alarm")+"</b></td>");
  color=getColor2(fd1->rssiAlarms[0].level,fd2->rssiAlarms[0].level);
  str.append("<td align=\"center\"><font color="+color+">"+getFrSkyAlarmType(fd2->rssiAlarms[0].level)+"</td>");
  str.append("<td align=\"center\">&lt;</td>");
  color=getColor2(fd1->rssiAlarms[0].value,fd2->rssiAlarms[0].value);
  str.append("<td align=\"center\"><font color="+color+">"+QString::number(fd2->rssiAlarms[0].value,10)+"</td>");
  color=getColor2(fd1->rssiAlarms[1].level,fd2->rssiAlarms[1].level);
  str.append("<td align=\"center\"><font color="+color+">"+getFrSkyAlarmType(fd2->rssiAlarms[1].level)+"</td>");
  str.append("<td align=\"center\">&lt;</td>");
  color=getColor2(fd1->rssiAlarms[1].value,fd2->rssiAlarms[1].value);
  str.append("<td align=\"center\"><font color="+color+">"+QString::number(fd2->rssiAlarms[1].value,10)+"</td>");
  str.append("</table></br>");
#if 0
  if (GetCurrentFirmware()->getCapability(TelemetryBars) || GetCurrentFirmware()->getCapability(TelemetryCSFields)) {
    int cols=GetCurrentFirmware()->getCapability(TelemetryColsCSFields);
    if (cols==0) cols=2;
    for (int j=0; j<GetCurrentFirmware()->getCapability(TelemetryCSFields)/(4*cols); j++) {
      QString tcols;
      QString cwidth;
      QString swidth;
      if (cols==2) {
        tcols="3";
        cwidth="45";
        swidth="10";
      } else {
        tcols="5";
        cwidth="30";
        swidth="5";
      }
      color=getColor2(fd1->screens[j].type,fd2->screens[j].type);
      if (fd2->screens[j].type==0) {
        str.append("<br><table border=1 cellspacing=0 cellpadding=3 width=\"100%\"><tr><td colspan="+tcols+" align=\"Left\"><b><font color="+color+">"+tr("Custom Telemetry View")+"</font></b></td></tr><tr><td colspan=3>&nbsp;</td></tr>");
        for (int r=0; r<4; r++) {
          str.append("<tr>");
          for (int c=0; c<cols; c++) {
            if (fd1->screens[j].type==fd2->screens[j].type) 
              color=getColor2(fd1->screens[j].body.lines[r].source[c],fd2->screens[j].body.lines[r].source[c]);
            str.append("<td  align=\"Center\" width=\""+cwidth+"%\"><font color="+color+">"+getFrSkySrc(fd2->screens[j].body.lines[r].source[c])+"</font></td>");
            if (c<(cols-1)) {
              str.append("<td  align=\"Center\" width=\""+swidth+"%\"><b>&nbsp;</b></td>");
            }
          }
          str.append("</tr>");  
        }
        str.append("</table>");        
      } else {
        str.append("<br><table border=1 cellspacing=0 cellpadding=1 width=\"100%\"><tr><td colspan=4 align=\"Left\"><b><font color="+color+">"+tr("Telemetry Bars")+"</b></td></tr>");
        str.append("<tr><td  align=\"Center\"><b>"+tr("Bar Number")+"</b></td><td  align=\"Center\"><b>"+tr("Source")+"</b></td><td  align=\"Center\"><b>"+tr("Min")+"</b></td><td  align=\"Center\"><b>"+tr("Max")+"</b></td></tr>");
        for (int i=0; i<4; i++) {
          str.append("<tr><td  align=\"Center\"><b>"+QString::number(i+1,10)+"</b></td>");
          if (fd1->screens[0].type==fd2->screens[0].type)
            color=getColor2(fd1->screens[0].body.bars[i].source,fd2->screens[0].body.bars[i].source);
          str.append("<td  align=\"Center\"><font color="+color+">"+getFrSkySrc(fd2->screens[0].body.bars[i].source)+"</font></td>");
          // TODO value1=getBarValue(fd1->screens[0].body.bars[i].source,fd1->screens[0].body.bars[i].barMin,fd1);
          // TODO value2=getBarValue(fd2->screens[0].body.bars[i].source,fd2->screens[0].body.bars[i].barMin,fd2);
          if (fd1->screens[0].type==fd2->screens[0].type)
            color=getColor2(value1,value2);
          str.append("<td  align=\"Right\"><font color="+color+">"+QString::number(value2)+"</font></td>");
          // TODO value1=getBarValue(fd1->screens[0].body.bars[i].source,fd1->screens[0].body.bars[i].barMax,fd1);
          // TODO value2=getBarValue(fd2->screens[0].body.bars[i].source,fd2->screens[0].body.bars[i].barMax,fd2);
          if (fd1->screens[0].type==fd2->screens[0].type)
            color=getColor2(value1,value2);
          str.append("<td  align=\"Right\"><font color="+color+">"+QString::number(value2)+"</font></td></tr>");
        }
        str.append("</table>");
      }
    }
  }
#endif
  str.append("</td></tr></table>");
  te->append(str);
}

void CompareDialog::on_printButton_clicked()
{
    QPrinter printer;
    printer.setPageMargins(10.0,10.0,10.0,10.0,printer.Millimeter);
    QPrintDialog *dialog = new QPrintDialog(&printer, this);
    dialog->setWindowTitle(tr("Print Document"));
    if (dialog->exec() != QDialog::Accepted)
        return;
    te->print(&printer);
}

void CompareDialog::on_printFileButton_clicked()
{
    QPrinter printer;
    QString filename = QFileDialog::getSaveFileName(this,tr("Select PDF output file"),QString(),"Pdf File(*.pdf)"); 
    printer.setPageMargins(10.0,10.0,10.0,10.0,printer.Millimeter);
    printer.setOutputFormat(QPrinter::PdfFormat);
    printer.setOrientation(QPrinter::Landscape);
    printer.setColorMode(QPrinter::Color);
    if(!filename.isEmpty()) { 
        if(QFileInfo(filename).suffix().isEmpty()) 
            filename.append(".pdf"); 
        printer.setOutputFileName(filename);
        te->print(&printer);
    }
}
