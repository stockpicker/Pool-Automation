
/*
 * Copyright (c) 2017 Shaun Feakes - All rights reserved
 *
 * You may use redistribute and/or modify this code under the terms of
 * the GNU General Public License version 2 as published by the 
 * Free Software Foundation. For the terms of this license, 
 * see <http://www.gnu.org/licenses/>.
 *
 * You are free to use this software under the terms of the GNU General
 * Public License, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 *  https://github.com/sfeakes/aqualinkd
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "iaqtouch_aq_programmer.h"
#include "aq_serial.h"
#include "utils.h"
#include "aq_programmer.h"
#include "aqualink.h"
//#include "packetLogger.h"
#include "iaqtouch.h"
#include "rs_msg_utils.h"
#include "aq_panel.h"
#include "config.h"
#include "devices_jandy.h"

// System Page is obfiously fixed and not dynamic loaded, so set buttons to stop confustion.

#define KEY_IAQTCH_SCHEDULE       KEY_IAQTCH_KEY01
#define KEY_IAQTCH_CUSTOMIZE_HOME KEY_IAQTCH_KEY02
#define KEY_IAQTCH_PROGRAM_GROUP  KEY_IAQTCH_KEY03
#define KEY_IAQTCH_SET_TEMP       KEY_IAQTCH_KEY04
#define KEY_IAQTCH_SYSTEM_SETUP   KEY_IAQTCH_KEY05
#define KEY_IAQTCH_TOUCH_SETUP    KEY_IAQTCH_KEY06
#define KEY_IAQTCH_SET_DATETIME   KEY_IAQTCH_KEY07
#define KEY_IAQTCH_LOCKOUT_PASSWD KEY_IAQTCH_KEY08
#define KEY_IAQTCH_SET_ACQUAPURE  KEY_IAQTCH_KEY09



bool _cansend = false;

unsigned char _iaqt_pgm_command = NUL;

/**************
 * 
 *  Command queue stuff
 * 
 */

// External command
bool iaqt_queue_cmd(unsigned char cmd) {

  if (_iaqt_pgm_command == NUL) {
    _iaqt_pgm_command = cmd;
    return true;
  }

  return false;
}

void set_iaq_cansend(bool cansend){
  _cansend = cansend;
}

unsigned char pop_iaqt_cmd(unsigned char receive_type)
{
  unsigned char cmd = NUL;

  if (!_cansend)
    return cmd;

  if (receive_type == CMD_IAQ_POLL) {
    cmd = _iaqt_pgm_command;
    _iaqt_pgm_command = NUL;
  } 

  if (cmd != NUL)
    LOG(IAQT_LOG,LOG_DEBUG, "IAQ Touch Sending '0x%02hhx' to controller\n", cmd);
  return cmd;
}

void waitfor_iaqt_queue2empty()
{
  int i=0;

  while ( (_iaqt_pgm_command != NUL) && ( i++ < 20) ) {
    delay(50);
  }

  if (_iaqt_pgm_command != NUL) {
      // Wait for longer interval
      while ( (_iaqt_pgm_command != NUL) && ( i++ < 100) ) {
        delay(100);
      }
  }

  if (_iaqt_pgm_command != NUL) {
    LOG(IAQT_LOG,LOG_WARNING, "iAQ Touch Send command Queue did not empty, timeout\n");
  }
}

void send_aqt_cmd(unsigned char cmd)
{
  waitfor_iaqt_queue2empty();
  
  iaqt_queue_cmd(cmd);

  LOG(IAQT_LOG,LOG_DEBUG, "iAQ Touch Queue send '0x%02hhx' to controller (programming)\n", _iaqt_pgm_command);
}

/**************
 * 
 *  Control Command queue stuff
 * 
 */

unsigned char _iaqt_control_cmd[AQ_MAXPKTLEN];
int _iaqt_control_cmd_len;


int ref_iaqt_control_cmd(unsigned char **cmd)
{
  //printf("*********** GET READY SENDING CONTROL ****************\n");
  *cmd = _iaqt_control_cmd;
  return _iaqt_control_cmd_len;
}

void rem_iaqt_control_cmd(unsigned char *cmd)
{
  memset(_iaqt_control_cmd, 0, AQ_MAXPKTLEN * sizeof(unsigned char));
  _iaqt_control_cmd_len = 0;
}

bool waitfor_iaqt_ctrl_queue2empty()
{
  int i=0;

  while ( (_iaqt_control_cmd_len >0 ) && ( i++ < 20) ) {
    delay(50);
  }

  if (_iaqt_control_cmd_len > 0 ) {
    LOG(IAQT_LOG,LOG_WARNING, "iAQ Touch Send control command Queue did not empty, timeout\n");
    return false;
  }
  return true;
}


unsigned const char waitfor_iaqt_nextPage(struct aqualinkdata *aq_data) {
  
  waitfor_iaqt_queue2empty();

  int i=0;
  const int numMessageReceived = 30;

  pthread_mutex_lock(&aq_data->active_thread.thread_mutex);

  while( ++i <= numMessageReceived)
  {
    LOG(IAQT_LOG,LOG_DEBUG, "waitfor_iaqt_nextPage (%d of %d)\n",i,numMessageReceived);
    pthread_cond_wait(&aq_data->active_thread.thread_cond, &aq_data->active_thread.thread_mutex);
    if(wasiaqtThreadKickTypePage()) break;
  }

  pthread_mutex_unlock(&aq_data->active_thread.thread_mutex);

  if(wasiaqtThreadKickTypePage())
    return iaqtCurrentPage();
  else
    return NUL;

}

unsigned const char waitfor_iaqt_nextMessage(struct aqualinkdata *aq_data, const unsigned char msg_type) {
  
  waitfor_iaqt_queue2empty();

  int i=0;
  const int numMessageReceived = 30;

  pthread_mutex_lock(&aq_data->active_thread.thread_mutex);

  while( ++i <= numMessageReceived)
  {
    LOG(IAQT_LOG,LOG_DEBUG, "waitfor_iaqt_nextMessage 0x%02hhx (%d of %d)\n",msg_type,i,numMessageReceived);
    pthread_cond_wait(&aq_data->active_thread.thread_cond, &aq_data->active_thread.thread_mutex);
    if( msg_type == NUL || iaqtLastMsg() == msg_type) break;
  }

  pthread_mutex_unlock(&aq_data->active_thread.thread_mutex);

  return iaqtLastMsg();
}

//typedef enum iaqt_control_command_type()
typedef enum {icct_setrpm, icct_settime, icct_setdate} iaqtControlCmdYype;

// Type is always 0 at the moment, haven't found any 
void queue_iaqt_control_command(iaqtControlCmdYype type, int num) {
  //unsigned char packets[AQ_MAXPKTLEN];
  //int cnt;

  if (waitfor_iaqt_ctrl_queue2empty() == false)
    return;

  _iaqt_control_cmd[0] = DEV_MASTER;
  _iaqt_control_cmd[1] = 0x24;
  _iaqt_control_cmd[2] = 0x31;

  _iaqt_control_cmd_len = num2iaqtRSset(&_iaqt_control_cmd[3], num, true);

  // Pad with 0xcd for some reason.
  for(_iaqt_control_cmd_len = _iaqt_control_cmd_len+3; _iaqt_control_cmd_len <= 18; _iaqt_control_cmd_len++)
    _iaqt_control_cmd[_iaqt_control_cmd_len] = 0xcd;

  // Tell the control panel we are ready to send this shit.
  send_aqt_cmd(ACK_CMD_READY_CTRL);

  //printHex(packets, 19);
  //printf("\n");

  //send_jandy_command(NULL, packets, cnt);
}

void queue_iaqt_control_command_str(iaqtControlCmdYype type, char *str) {
  if (waitfor_iaqt_ctrl_queue2empty() == false)
    return;

  _iaqt_control_cmd[0] = DEV_MASTER;
  _iaqt_control_cmd[1] = 0x24;
  _iaqt_control_cmd[2] = 0x31;

  _iaqt_control_cmd_len = char2iaqtRSset(&_iaqt_control_cmd[3], str, strlen(str));
  
  // Need to bad time for some reason not yet known
  if (type == icct_settime) {
    //Debug:   RS Serial: To 0x00 of type iAq pBut | HEX: 0x10|0x02|0x00|0x24|0x31|0x30|0x31|0x3a|0x30|0x31|0x00|0x30|0x32|0x00|0xcd|0xcd|0xcd|0xcd|0xcd|0xcd|0xcd|0x60|0x10|0x03|
    // From position 11 (8 without pre) add 0x30|0x32|0x00
    _iaqt_control_cmd[++_iaqt_control_cmd_len] = 0x30;
    _iaqt_control_cmd[++_iaqt_control_cmd_len] = 0x32;
    _iaqt_control_cmd[++_iaqt_control_cmd_len] = 0x00;
  }
  // Pad with 0xcd for some reason.
  for(_iaqt_control_cmd_len = _iaqt_control_cmd_len+3; _iaqt_control_cmd_len <= 18; _iaqt_control_cmd_len++)
    _iaqt_control_cmd[_iaqt_control_cmd_len] = 0xcd;

  // Tell the control panel we are ready to send this shit.
  send_aqt_cmd(ACK_CMD_READY_CTRL);
}



bool goto_iaqt_page(const unsigned char pageID, struct aqualinkdata *aq_data) {
  if (iaqtCurrentPage() == pageID)
    return true;

  // If we go to Other Status Page, do that so we can quit
  if (pageID == IAQ_PAGE_STATUS) {
    send_aqt_cmd(KEY_IAQTCH_STATUS);
    unsigned char rtn = waitfor_iaqt_nextPage(aq_data); 
    if (rtn != IAQ_PAGE_STATUS && rtn != IAQ_PAGE_STATUS2) {
      LOG(IAQT_LOG, LOG_ERR, "IAQ Touch did not find Status page\n");
      return false;
    }
    LOG(IAQT_LOG, LOG_DEBUG, "IAQ Touch got to Status page\n");
    return true;
  }

  if (pageID == IAQ_PAGE_HOME || pageID == IAQ_PAGE_DEVICES) {
    if (iaqtCurrentPage() != IAQ_PAGE_HOME) {
      send_aqt_cmd(KEY_IAQTCH_HOME);
      if (waitfor_iaqt_nextPage(aq_data) != IAQ_PAGE_HOME) {
        LOG(IAQT_LOG, LOG_ERR, "IAQ Touch did not find Home page\n");
        return false;
      }
      LOG(IAQT_LOG, LOG_DEBUG, "IAQ Touch got to Home page\n");
    }

    if (pageID == IAQ_PAGE_DEVICES) {
      send_aqt_cmd(KEY_IAQTCH_HOMEP_KEY08);
      if (waitfor_iaqt_nextPage(aq_data) != IAQ_PAGE_DEVICES) {
        LOG(IAQT_LOG, LOG_ERR, "IAQ Touch did not find Device page\n");
        return false;
      }
    }
    LOG(IAQT_LOG, LOG_DEBUG, "IAQ Touch got to Device page\n");
    return true;
  } else if (pageID == IAQ_PAGE_MENU || pageID == IAQ_PAGE_SET_TEMP || pageID == IAQ_PAGE_SET_TIME || pageID == IAQ_PAGE_SET_SWG ||
             pageID == IAQ_PAGE_SYSTEM_SETUP || pageID == IAQ_PAGE_FREEZE_PROTECT || pageID == IAQ_PAGE_LABEL_AUX || pageID == IAQ_PAGE_VSP_SETUP) {
    // All other pages require us to go to Menu page
    send_aqt_cmd(KEY_IAQTCH_MENU);
    if (waitfor_iaqt_nextPage(aq_data) != IAQ_PAGE_MENU) {
      LOG(IAQT_LOG, LOG_ERR, "IAQ Touch did not find Menu page\n");
      return false;
    } else
      LOG(IAQT_LOG, LOG_INFO, "IAQ Touch got to Menu page\n");

    // There are several pages from the MENU page that have no other pages,
    // So hit those first
    if (pageID == IAQ_PAGE_SET_TEMP) {
      send_aqt_cmd(KEY_IAQTCH_SET_TEMP);
      if (waitfor_iaqt_nextPage(aq_data) != IAQ_PAGE_SET_TEMP) {
        LOG(IAQT_LOG, LOG_ERR, "IAQ Touch did not find Set Temp page\n");
        return false;
      }
      LOG(IAQT_LOG, LOG_INFO, "IAQ Touch got to Set Temp page\n");
      return true;
    }

    if (pageID == IAQ_PAGE_SET_TIME) {
      send_aqt_cmd(KEY_IAQTCH_SET_DATETIME);
      if (waitfor_iaqt_nextPage(aq_data) != IAQ_PAGE_SET_TIME) {
        LOG(IAQT_LOG, LOG_ERR, "IAQ Touch did not find Set Time page\n");
        return false;
      }
      LOG(IAQT_LOG, LOG_INFO, "IAQ Touch got to Set Time page\n");
      return true;
    }

    if (pageID == IAQ_PAGE_SET_SWG) {
      send_aqt_cmd(KEY_IAQTCH_SET_ACQUAPURE);
      if (waitfor_iaqt_nextPage(aq_data) != IAQ_PAGE_SET_SWG) {
        LOG(IAQT_LOG, LOG_ERR, "IAQ Touch did not find Aquapure page\n");
        return false;
      }
      LOG(IAQT_LOG, LOG_INFO, "IAQ Touch got to Aquapure page\n");
      return true;
    }

    // All pages now require us to goto System Setup
    send_aqt_cmd(KEY_IAQTCH_SYSTEM_SETUP);
    if (waitfor_iaqt_nextPage(aq_data) != IAQ_PAGE_SYSTEM_SETUP) {
      LOG(IAQT_LOG, LOG_ERR, "IAQ Touch did not find System Setup page\n");
      return false;
    }
    LOG(IAQT_LOG, LOG_INFO, "IAQ Touch got to System Setup page\n");

    // Look for menu items for the next pages as they can change
    char *menuText;
    struct iaqt_page_button *button;

    switch (pageID) {
    case IAQ_PAGE_FREEZE_PROTECT:
      menuText = "Freeze Protection";
      break;
    case IAQ_PAGE_LABEL_AUX:
      menuText = "Label Aux";
      break;
    case IAQ_PAGE_VSP_SETUP:
      menuText = "VSP Setup";
      break;
    default:
      LOG(IAQT_LOG, LOG_ERR, "IAQ Touch unknown menu '0x%02hhx'\n", pageID);
      return false;
      break;
    }

    button = iaqtFindButtonByLabel(menuText);
    if (button == NULL) {
      LOG(IAQT_LOG, LOG_ERR, "IAQ Touch did not find '%s' button on page setup\n", menuText);
      return false;
    }
    // send_aqt_cmd(KEY_IAQTCH_KEY01);
    send_aqt_cmd(button->keycode);
    if (waitfor_iaqt_nextPage(aq_data) != pageID) {
      LOG(IAQT_LOG, LOG_ERR, "IAQ Touch did not find %s page\n", menuText);
      return false;
    } else
      LOG(IAQT_LOG, LOG_INFO, "IAQ Touch got to %s page\n", menuText);

    return true;
  }

  LOG(IAQT_LOG, LOG_ERR, "IAQ Touch unknown menu '0x%02hhx'\n", pageID);
  return false;
}

void *set_aqualink_iaqtouch_pump_rpm( void *ptr )
{
  struct programmingThreadCtrl *threadCtrl;
  threadCtrl = (struct programmingThreadCtrl *) ptr;
  struct aqualinkdata *aq_data = threadCtrl->aq_data;
  char *buf = (char*)threadCtrl->thread_args;
  char VSPstr[20];
  int structIndex;
  struct iaqt_page_button *button;

  //printf("**** program string '%s'\n",buf);
  
  int pumpIndex = atoi(&buf[0]);
  int pumpRPM = -1;
  //int pumpRPM = atoi(&buf[2]);
  for (structIndex=0; structIndex < aq_data->num_pumps; structIndex++) {
    if (aq_data->pumps[structIndex].pumpIndex == pumpIndex) {
      if (aq_data->pumps[structIndex].pumpType == PT_UNKNOWN) {
        LOG(IAQT_LOG,LOG_ERR, "Can't set Pump RPM/GPM until type is known\n");
        cleanAndTerminateThread(threadCtrl);
        return ptr;
      }
      pumpRPM = RPM_check(aq_data->pumps[structIndex].pumpType, atoi(&buf[2]), aq_data);
      break;
    }
  }
  
  //int pumpRPM = atoi(&buf[2]);
  //int pumpIndex = 1;

  // Just force to pump 1 for testing
  sprintf(VSPstr, "VSP%1d Spd ADJ",pumpIndex);
  // NSF Should probably check pumpRPM is not -1 here

  waitForSingleThreadOrTerminate(threadCtrl, AQ_SET_IAQTOUCH_PUMP_RPM);

  LOG(IAQT_LOG,LOG_NOTICE, "IAQ Touch Set Pump %d to RPM %d\n",pumpIndex,pumpRPM);

  if ( goto_iaqt_page(IAQ_PAGE_DEVICES, aq_data) == false )
    goto f_end;

  button = iaqtFindButtonByLabel(VSPstr);
  if (button == NULL) {
    LOG(IAQT_LOG, LOG_ERR, "IAQ Touch did not find '%s' button on page setup\n", VSPstr);
    goto f_end;
  }

  send_aqt_cmd(button->keycode);
  if (waitfor_iaqt_nextPage(aq_data) != IAQ_PAGE_SET_VSP) {
    LOG(IAQT_LOG, LOG_ERR, "IAQ Touch did not find %s page\n", VSPstr);
    goto f_end;
  }
  LOG(IAQT_LOG, LOG_INFO, "IAQ Touch got to %s page\n", VSPstr);

  //send_aqt_cmd(ACK_CMD_READY_CTRL);
  queue_iaqt_control_command(0, pumpRPM);

  waitfor_iaqt_ctrl_queue2empty();

  LOG(IAQT_LOG, LOG_INFO, "IAQ Touch got to %s page\n", VSPstr);

  // We should get the device page back.
  waitfor_iaqt_nextPage(aq_data);

  //goto_iaqt_page(IAQ_PAGE_STATUS, aq_data);
/*
  // Send Devices button.
  send_aqt_cmd(KEY_IAQTCH_HOME);
  waitfor_iaqt_queue2empty();
  sleep(1);

  send_aqt_cmd(KEY_IAQTCH_HOMEP_KEY08);
  waitfor_iaqt_queue2empty();
  sleep(1);

  send_aqt_cmd(KEY_IAQTCH_KEY03);
*/
  //send_aqt_cmd(0x80);

  f_end:
  //goto_iaqt_page(IAQ_PAGE_HOME, aq_data);
  goto_iaqt_page(IAQ_PAGE_STATUS, aq_data);
  cleanAndTerminateThread(threadCtrl);

  // just stop compiler error, ptr is not valid as it's just been freed
  return ptr;
}

void *set_aqualink_iaqtouch_vsp_assignments( void *ptr )
{
  struct programmingThreadCtrl *threadCtrl;
  threadCtrl = (struct programmingThreadCtrl *) ptr;
  struct aqualinkdata *aq_data = threadCtrl->aq_data;
  waitForSingleThreadOrTerminate(threadCtrl, AQ_GET_IAQTOUCH_VSP_ASSIGNMENT);

  if ( goto_iaqt_page(IAQ_PAGE_VSP_SETUP, aq_data) == false )
    goto f_end;

  /* Info:   Button 00|         ePump   | type=0xff | state=0x00 | unknown=0xff  
   * Info:   Button 01| Intelliflo VF   | type=0xff | state=0x00 | unknown=0xff 
   * Info:   Button 02| Intelliflo VS   | type=0xff | state=0x00 | unknown=0xff 
   * Info:   Button 03|         ePump   | type=0xff | state=0x00 | unknown=0xff 
   * ---- This is next column
   * Info:   Button 04|    Filtration   | type=0xff | state=0x00 | unknown=0xff 
   * Info:   Button 05|    Filtration   | type=0xff | state=0x00 | unknown=0xff 
   * Info:   Button 06|    Filtration   | type=0xff | state=0x00 | unknown=0xff 
   * Info:   Button 07| Not Installed   | type=0xff | state=0x00 | unknown=0xff 
   * ----- Next column
   * Info:   Button 08|        600 RPM  | type=0xff | state=0x00 | unknown=0xff 
   * Info:   Button 09|         15 GPM  | type=0xff | state=0x00 | unknown=0xff 
   * Info:   Button 10|        600 RPM  | type=0xff | state=0x00 | unknown=0xff 
   * Info:   Button 11|        600 RPM  | type=0xff | state=0x00 | unknown=0xff 
   * ----- Next colums
   * Info:   Button 12|       3450 RPM  | type=0xff | state=0x00 | unknown=0xff 
   * Info:   Button 13|        130 GPM  | type=0xff | state=0x00 | unknown=0xff 
   * Info:   Button 14|       3450 RPM  | type=0xff | state=0x00 | unknown=0xff 
   * Info:   Button 15|       3450 RPM  | type=0xff | state=0x00 | unknown=0xff 
   * ----- Next column
   * Info:   Button 16|          2500   | type=0xff | state=0x00 | unknown=0xff 
   * Info:   Button 17|           N/A   | type=0xff | state=0x00 | unknown=0xff 
   * Info:   Button 18|          2500   | type=0xff | state=0x00 | unknown=0xff 
   * Info:   Button 19|          2500   | type=0xff | state=0x00 | unknown=0xff 
   * ----- Next column
   * Info:   Button 20|           :03   | type=0xff | state=0x00 | unknown=0xff 
   * Info:   Button 21|           N/A   | type=0xff | state=0x00 | unknown=0xff 
   * Info:   Button 22|           :03   | type=0xff | state=0x00 | unknown=0xff 
   * Info:   Button 23|           :03   | type=0xff | state=0x00 | unknown=0xff 
   *  
*/
  f_end:
  goto_iaqt_page(IAQ_PAGE_HOME, aq_data);
  cleanAndTerminateThread(threadCtrl);

  // just stop compiler error, ptr is not valid as it's just been freed
  return ptr;
}


void *get_aqualink_iaqtouch_setpoints( void *ptr )
{
  struct programmingThreadCtrl *threadCtrl;
  threadCtrl = (struct programmingThreadCtrl *) ptr;
  struct aqualinkdata *aq_data = threadCtrl->aq_data;
  struct iaqt_page_button *button;

  waitForSingleThreadOrTerminate(threadCtrl, AQ_GET_IAQTOUCH_SETPOINTS);

  if ( goto_iaqt_page(IAQ_PAGE_SET_TEMP, aq_data) == false )
    goto f_end;

  // Button 0 is "Pool Heat 50"
  // Button 2 is "Spa Heat 100"
  button = iaqtFindButtonByLabel("Pool Heat");
  if (button != NULL) {
    aq_data->pool_htr_set_point = rsm_atoi((char *)&button->name + 9);
    LOG(IAQT_LOG,LOG_DEBUG, "IAQ Touch got to Pool heater setpoint %d\n",aq_data->pool_htr_set_point);
  } else {
    button = iaqtFindButtonByLabel("Temp1");
    if (button != NULL) {
      aq_data->pool_htr_set_point = rsm_atoi((char *)&button->name + 5);
      LOG(IAQT_LOG,LOG_DEBUG, "IAQ Touch got to Temp1 setpoint %d\n",aq_data->pool_htr_set_point);
      if (isSINGLE_DEV_PANEL != true)
      {
        changePanelToMode_Only();
        LOG(AQRS_LOG,LOG_ERR, "AqualinkD set to 'Combo Pool & Spa' but detected 'Only Pool OR Spa' panel, please change config\n");
      }
    }
  }

  button = iaqtFindButtonByLabel("Spa Heat");
  if (button != NULL) {
    aq_data->spa_htr_set_point = rsm_atoi((char *)&button->name + 8);
    LOG(IAQT_LOG,LOG_DEBUG, "IAQ Touch got to Spa heater setpoint %d\n",aq_data->spa_htr_set_point);
  } else {
    button = iaqtFindButtonByLabel("Temp2");
    if (button != NULL) {
      aq_data->spa_htr_set_point = rsm_atoi((char *)&button->name + 5);
      LOG(IAQT_LOG,LOG_DEBUG, "IAQ Touch got to Temp2 setpoint %d\n",aq_data->spa_htr_set_point);
      if (isSINGLE_DEV_PANEL != true)
      {
        changePanelToMode_Only();
        LOG(AQRS_LOG,LOG_ERR, "AqualinkD set to 'Combo Pool & Spa' but detected 'Only Pool OR Spa' panel, please change config\n");
      }
    }
  }

  if ( goto_iaqt_page(IAQ_PAGE_FREEZE_PROTECT, aq_data) == false )
    goto f_end;

  // The Message at index 0 is the deg that freeze protect is set to.
  int frz = rsm_atoi(iaqtGetMessageLine(0));
  if (frz >= 0) {
    aq_data->frz_protect_set_point = frz;
    LOG(IAQT_LOG,LOG_NOTICE, "IAQ Touch Freeze Protection setpoint %d\n",frz);
  }

  // Need to run over table messages and check ens with X for on off.

  // Go to status page on startup to read devices
  goto_iaqt_page(IAQ_PAGE_STATUS, aq_data);

  f_end:
  goto_iaqt_page(IAQ_PAGE_HOME, aq_data);
  cleanAndTerminateThread(threadCtrl);

  // just stop compiler error, ptr is not valid as it's just been freed
  return ptr;
}

void *get_aqualink_iaqtouch_aux_labels( void *ptr )
{
  struct programmingThreadCtrl *threadCtrl;
  threadCtrl = (struct programmingThreadCtrl *) ptr;
  struct aqualinkdata *aq_data = threadCtrl->aq_data;
  int i;

  waitForSingleThreadOrTerminate(threadCtrl, AQ_GET_IAQTOUCH_AUX_LABELS);

  if ( goto_iaqt_page(IAQ_PAGE_LABEL_AUX, aq_data) == false )
    goto f_end;

  // Need to loop over messages.  Tab 0x09 is next in each message
  /*
   * Info:   Table Messages 00| Circuit      Circuit Name    Freeze
   * Info:   Table Messages 01| Aux1 Aux1    No 
   * Info:   Table Messages 02| Aux2 Aux2    No 
   * Info:   Table Messages 03| Aux3 Pool Light      No 
   * Info:   Table Messages 04| Aux4 Aux4    No 
   * Info:   Table Messages 05| Aux5 Aux5    No 
   * Info:   Table Messages 06| Aux6 Aux6    No 
   * Info:   Table Messages 07| Aux7 Aux7    No 
   */
  const char *buf;
  int aux;
  for(i=1; i < 18; i++) // NSF Need to take out hard code of 18
  {
    buf = iaqtGetTableInfoLine(i);
    //printf("**** BUF '%s'\n",aux,buf);
    aux = rsm_atoi(buf + 3);
    printf("**** AUX %d = '%s'\n",aux,buf + 5);
  }

  f_end:
  goto_iaqt_page(IAQ_PAGE_HOME, aq_data);
  cleanAndTerminateThread(threadCtrl);

  return ptr;
}

bool set_aqualink_iaqtouch_aquapure( struct aqualinkdata *aq_data, bool boost, int val )
{
  struct iaqt_page_button *button;
  static unsigned char b_pool = NUL;
  static unsigned char b_spa = NUL;
  static unsigned char b_boost = NUL;

  if ( goto_iaqt_page(IAQ_PAGE_SET_SWG, aq_data) == false )
    return false;

  // IF pool button not set, first time here, need to cache the buttons
  // as we only get them once.
  if (b_pool == NUL) {
    button = iaqtFindButtonByLabel("Pool");
    if (button != NULL)
      b_pool = button->keycode;
    button = iaqtFindButtonByLabel("Spa");
    if (button != NULL)
      b_spa = button->keycode;
    button = iaqtFindButtonByLabel("Quick Boost");
    if (button != NULL)
      b_boost = button->keycode;
  }
  // If spa is on, set SWG for spa, if not set SWG for pool

  if (boost) {
    if (b_boost != NUL)
      send_aqt_cmd(b_boost);
    else {
      LOG(IAQT_LOG, LOG_ERR, "IAQ Touch did not find Boost button on SWG page\n");
      return false;
    }
    waitfor_iaqt_queue2empty();
    if (waitfor_iaqt_nextPage(aq_data) != IAQ_PAGE_SET_QBOOST) {
      LOG(IAQT_LOG, LOG_ERR, "IAQ Touch did not find Boost Start button on SWG page\n");
    }
    if(val==true)
      button = iaqtFindButtonByLabel("Start");
    else
      button = iaqtFindButtonByLabel("Stop");
    send_aqt_cmd(button->keycode);
    waitfor_iaqt_queue2empty();
    waitfor_iaqt_nextPage(aq_data);
    aq_data->boost = val;
  } else {
    if (aq_data->aqbuttons[SPA_INDEX].led->state != OFF) {
      if (b_spa != NUL)
        send_aqt_cmd(b_spa);
      else {
        LOG(IAQT_LOG, LOG_ERR, "IAQ Touch did not find Spa button on SWG page\n");
        return false;
      }
    } else {
      if (b_pool != NUL)
        send_aqt_cmd(b_pool);
      else {
        LOG(IAQT_LOG, LOG_ERR, "IAQ Touch did not find Pool button on SWG page\n");
        return false;;
      }
    }

    waitfor_iaqt_queue2empty();
    queue_iaqt_control_command(0, val);
    waitfor_iaqt_ctrl_queue2empty();
    waitfor_iaqt_nextMessage(aq_data, CMD_IAQ_PAGE_BUTTON);
  }
  //LOG(IAQT_LOG, LOG_NOTICE, "IAQ Touch got to %s page\n", VSPstr);

  // We should get the device page back.
  //waitfor_iaqt_nextPage(aq_data);

  goto_iaqt_page(IAQ_PAGE_STATUS, aq_data);

  return true;
}


void *set_aqualink_iaqtouch_swg_percent( void *ptr )
{
  struct programmingThreadCtrl *threadCtrl;
  threadCtrl = (struct programmingThreadCtrl *) ptr;
  struct aqualinkdata *aq_data = threadCtrl->aq_data;

  waitForSingleThreadOrTerminate(threadCtrl, AQ_SET_IAQTOUCH_SWG_PERCENT);
  int val = atoi((char*)threadCtrl->thread_args);
  val = setpoint_check(SWG_SETPOINT, val, aq_data);

  if (set_aqualink_iaqtouch_aquapure(aq_data, false, val))
    setSWGpercent(aq_data, val);

  goto_iaqt_page(IAQ_PAGE_HOME, aq_data);
  cleanAndTerminateThread(threadCtrl);

  return ptr;
}

void *set_aqualink_iaqtouch_swg_boost( void *ptr )
{
  struct programmingThreadCtrl *threadCtrl;
  threadCtrl = (struct programmingThreadCtrl *) ptr;
  struct aqualinkdata *aq_data = threadCtrl->aq_data;

  waitForSingleThreadOrTerminate(threadCtrl, AQ_SET_IAQTOUCH_SWG_BOOST);
  int val = atoi((char*)threadCtrl->thread_args);

  //logMessage(LOG_DEBUG, "programming BOOST to %s\n", val==true?"On":"Off");

  set_aqualink_iaqtouch_aquapure(aq_data, true, val);

  goto_iaqt_page(IAQ_PAGE_HOME, aq_data);
  cleanAndTerminateThread(threadCtrl);

  return ptr;
}



bool set_aqualink_iaqtouch_heater_setpoint( struct aqualinkdata *aq_data, bool pool, int val)
{
  struct iaqt_page_button *button;
  char *name;

  if ( goto_iaqt_page(IAQ_PAGE_SET_TEMP, aq_data) == false )
    return false;
  
  if (isCOMBO_PANEL) {
    if (pool)
      name = "Pool Heat";
    else
      name = "Spa Heat";
  } else {
    if (pool)
      name = "Temp1";
    else
      name = "Temp2";
  }

  button = iaqtFindButtonByLabel(name);

  if (button == NULL) {
    //aq_data->pool_htr_set_point = rsm_atoi((char *)&button->name + 9);
    LOG(IAQT_LOG,LOG_ERR, "IAQ Touch did not find heater setpoint '%s' on page\n",name);
    return false;
  }

  send_aqt_cmd(button->keycode);
  waitfor_iaqt_queue2empty();

  queue_iaqt_control_command(0, val);

  waitfor_iaqt_ctrl_queue2empty();
  waitfor_iaqt_nextMessage(aq_data, CMD_IAQ_PAGE_BUTTON);

  button = iaqtFindButtonByLabel(name);

  if (button != NULL) {
    if (pool)
      aq_data->pool_htr_set_point = rsm_atoi((char *)&button->name + strlen(name));
    else
      aq_data->spa_htr_set_point = rsm_atoi((char *)&button->name + strlen(name));

    LOG(IAQT_LOG,LOG_DEBUG, "IAQ Touch set %s heater setpoint to %d\n",name,pool?aq_data->pool_htr_set_point:aq_data->spa_htr_set_point);
  }

  return true;
}

void *set_aqualink_iaqtouch_spa_heater_temp( void *ptr )
{
  struct programmingThreadCtrl *threadCtrl;
  threadCtrl = (struct programmingThreadCtrl *) ptr;
  struct aqualinkdata *aq_data = threadCtrl->aq_data;
  
  waitForSingleThreadOrTerminate(threadCtrl, AQ_SET_IAQTOUCH_SPA_HEATER_TEMP);

  int val = atoi((char*)threadCtrl->thread_args);
  val = setpoint_check(SPA_HTR_SETOINT, val, aq_data);

  set_aqualink_iaqtouch_heater_setpoint(aq_data, false, val);

  goto_iaqt_page(IAQ_PAGE_HOME, aq_data);
  cleanAndTerminateThread(threadCtrl);

  return ptr;
}

void *set_aqualink_iaqtouch_pool_heater_temp( void *ptr )
{
  struct programmingThreadCtrl *threadCtrl;
  threadCtrl = (struct programmingThreadCtrl *) ptr;
  struct aqualinkdata *aq_data = threadCtrl->aq_data;

  waitForSingleThreadOrTerminate(threadCtrl, AQ_SET_IAQTOUCH_POOL_HEATER_TEMP);
  
  int val = atoi((char*)threadCtrl->thread_args);
  val = setpoint_check(POOL_HTR_SETOINT, val, aq_data);

  set_aqualink_iaqtouch_heater_setpoint(aq_data, true, val);

  goto_iaqt_page(IAQ_PAGE_HOME, aq_data);
  cleanAndTerminateThread(threadCtrl);

  return ptr;
}

void *set_aqualink_iaqtouch_pump_vs_program( void *ptr )
{
  struct programmingThreadCtrl *threadCtrl;
  threadCtrl = (struct programmingThreadCtrl *) ptr;
  struct aqualinkdata *aq_data = threadCtrl->aq_data;
  char *buf = (char*)threadCtrl->thread_args;
  char VSPstr[20];
  int structIndex;
  struct iaqt_page_button *button;

  waitForSingleThreadOrTerminate(threadCtrl, AQ_SET_IAQTOUCH_PUMP_VS_PROGRAM);

  int pumpIndex = atoi(&buf[0]);
  //int pumpRPM = -1;
  int vspindex = atoi(&buf[2]);
  for (structIndex=0; structIndex < aq_data->num_pumps; structIndex++) {
    if (aq_data->pumps[structIndex].pumpIndex == pumpIndex) {
      if (aq_data->pumps[structIndex].pumpType == PT_UNKNOWN) {
        LOG(IAQT_LOG,LOG_ERR, "Can't set Pump RPM/GPM until type is known\n");
        cleanAndTerminateThread(threadCtrl);
        return ptr;
      }
      //pumpRPM = RPM_check(aq_data->pumps[structIndex].pumpType, atoi(&buf[2]), aq_data);
      break;
    }
  }
  //int pumpRPM = atoi(&buf[2]);
  //int pumpIndex = 1;

  sprintf(VSPstr, "VSP%1d Spd ADJ",pumpIndex);

  LOG(IAQT_LOG,LOG_NOTICE, "Set Pump %d to VSP Index %d\n",pumpIndex,vspindex);

  if ( goto_iaqt_page(IAQ_PAGE_DEVICES, aq_data) == false )
    goto f_end;

  button = iaqtFindButtonByLabel(VSPstr);
  if (button == NULL) {
    LOG(IAQT_LOG, LOG_ERR, "Did not find '%s' button on page setup\n", VSPstr);
    goto f_end;
  }

  send_aqt_cmd(button->keycode);
  if (waitfor_iaqt_nextPage(aq_data) != IAQ_PAGE_SET_VSP) {
    LOG(IAQT_LOG, LOG_ERR, "Did not find %s page\n", VSPstr);
    goto f_end;
  }
  LOG(IAQT_LOG, LOG_INFO, "Got to %s page\n", VSPstr);

  // Select the button index.
  button = iaqtFindButtonByIndex(vspindex);
  if (button == NULL) {
    LOG(IAQT_LOG, LOG_ERR, "Did not find '%d' button on page\n", vspindex);
    goto f_end;
  }

  send_aqt_cmd(button->keycode);
  waitfor_iaqt_queue2empty();
  // Probably wait.

  f_end:
  goto_iaqt_page(IAQ_PAGE_HOME, aq_data);
  cleanAndTerminateThread(threadCtrl);

  return ptr;
}

void *set_aqualink_iaqtouch_time( void *ptr )
{
  struct programmingThreadCtrl *threadCtrl;
  threadCtrl = (struct programmingThreadCtrl *) ptr;
  struct aqualinkdata *aq_data = threadCtrl->aq_data;
  struct iaqt_page_button *button;
  char buf[20];
  int len;
  int i;
  bool AM;
  
  waitForSingleThreadOrTerminate(threadCtrl, AQ_SET_IAQTOUCH_SET_TIME);

  time_t now = time(0);   // get time now
  struct tm *result = localtime(&now);
 

  if ( goto_iaqt_page(IAQ_PAGE_SET_TIME, aq_data) == false ) {
    LOG(IAQT_LOG,LOG_ERR, "IAQ Touch didn't find set time page\n");
    goto f_end;
  }

  // Button 0 is date.
  button = iaqtFindButtonByIndex(0);  
  if (button == NULL) {
    LOG(IAQT_LOG,LOG_ERR, "IAQ Touch date button on set time page\n");
    goto f_end;
  }

  // Print DD/MM/YY into string
  strftime(buf, 20, "%D", result);
  // Do we need to set the date
  if (rsm_strcmp(buf, (char *)button->name) != 0) {
    // Press date button.
    send_aqt_cmd(button->keycode);
    waitfor_iaqt_queue2empty();
    // Queue the date string
    queue_iaqt_control_command_str(icct_setdate, buf);
    LOG(IAQT_LOG,LOG_DEBUG, "Set date to %s\n",buf);
    waitfor_iaqt_ctrl_queue2empty();
  } else {
    LOG(IAQT_LOG,LOG_DEBUG, "Date %s is accurate, not changing\n",button->name);
  }
  
  // Always assume time is incorrect if wer have been called.
  // Button 1 is time.
  button = iaqtFindButtonByIndex(1);
  if (button == NULL) {
    LOG(IAQT_LOG,LOG_ERR, "IAQ Touch time button on set time page\n");
    goto f_end;
  }
  // Press time button.
  send_aqt_cmd(button->keycode);
  waitfor_iaqt_queue2empty();

  // Set AM/PM button, not sure how to get default state since it's blank to start, so keep pressing till we get AM or PM we want
  i = 0;
  strftime(buf, 20, "%p", result);
  LOG(IAQT_LOG,LOG_DEBUG, "IAQ Touch AM/PM check '%s' to '%s'\n",buf,iaqtGetMessageLine(2));
  while ( rsm_strcmp(buf, iaqtGetMessageLine(2)) != 0 && i < 3) {
    send_aqt_cmd(0x13);
    waitfor_iaqt_queue2empty();
    waitfor_iaqt_nextMessage(aq_data, CMD_IAQ_PAGE_MSG);
    i++;
  }
  LOG(IAQT_LOG,LOG_DEBUG, "IAQ Touch AM/PM is now '%s'\n",iaqtGetMessageLine(2));

  // Print HH:MM into string
  strftime(buf, 20, "%I:%M", result);
  queue_iaqt_control_command_str(icct_settime, buf);
  LOG(IAQT_LOG,LOG_DEBUG, "Set time to %s\n",buf);
  waitfor_iaqt_ctrl_queue2empty();


  f_end:
  goto_iaqt_page(IAQ_PAGE_HOME, aq_data);
  cleanAndTerminateThread(threadCtrl);

  return ptr;
}

/* Infor on setting time

  //Info:    iAQ Touch: Button 00|            07/05/20   | type=0x01 | state=0x00 | unknown=0x00 | keycode=0x11
  //Info:    iAQ Touch: Button 01|             1:02 PM   | type=0x01 | state=0x00 | unknown=0x00 | keycode=0x12

  // Send button to select time / date.
  // Date
  //Debug:   RS Serial: To 0x00 of type      Ack | HEX: 0x10|0x02|0x00|0x01|0x00|0x11|0x24|0x10|0x03|
  // Set Time
  //Debug:   RS Serial: To 0x00 of type      Ack | HEX: 0x10|0x02|0x00|0x01|0x00|0x12|0x25|0x10|0x03|

  // Wait for Time page comes back
  //Info:    iAQ Touch: Page: Set Time | 0x4b

  // Set time to 01/01/01
  //Debug:   RS Serial: To 0x00 of type      Ack | HEX: 0x10|0x02|0x00|0x01|0x00|0x80|0x93|0x10|0x03|
  //Debug:   iAQ Touch: To 0x33 of type Unknown  | HEX: 0x10|0x02|0x33|0x31|0x01|0x77|0x10|0x03|
  //Debug:   RS Serial: To 0x00 of type iAq pBut | HEX: 0x10|0x02|0x00|0x24|0x31|0x30|0x31|0x2f|0x30|0x31|0x2f|0x30|0x31|0x00|0xcd|0xcd|0xcd|0xcd|0xcd|0xcd|0xcd|0x83|0x10|0x03|
                                                                                 //0x30|0x31|0x2f|0x30|0x31|0x2f|0x30|0x31|
  // Set Time to 02/02/02
  //Debug:   RS Serial: To 0x00 of type      Ack | HEX: 0x10|0x02|0x00|0x01|0x00|0x80|0x93|0x10|0x03|
  //Debug:   iAQ Touch: To 0x33 of type Unknown  | HEX: 0x10|0x02|0x33|0x31|0x01|0x77|0x10|0x03|
  //Debug:   RS Serial: To 0x00 of type iAq pBut | HEX: 0x10|0x02|0x00|0x24|0x31|0x30|0x32|0x2f|0x30|0x32|0x2f|0x30|0x32|0x00|0xcd|0xcd|0xcd|0xcd|0xcd|0xcd|0xcd|0x86|0x10|0x03|

  // So 0x30 is base numbers again (ie 0)
  //0x24|0x31 Set time & start?
  //0x2f is seperator or /
  //0x00 is end
  //0xcd padding

  // Set time 01:01 AM
  //Debug:   RS Serial: To 0x00 of type      Ack | HEX: 0x10|0x02|0x00|0x01|0x00|0x80|0x93|0x10|0x03|
  //Debug:   iAQ Touch: To 0x33 of type Unknown  | HEX: 0x10|0x02|0x33|0x31|0x01|0x77|0x10|0x03|
  //Debug:   RS Serial: To 0x00 of type iAq pBut | HEX: 0x10|0x02|0x00|0x24|0x31|0x30|0x31|0x3a|0x30|0x31|0x00|0x30|0x32|0x00|0xcd|0xcd|0xcd|0xcd|0xcd|0xcd|0xcd|0x60|0x10|0x03|
  // Set time 01:01 PM
  //Debug:   RS Serial: To 0x00 of type      Ack | HEX: 0x10|0x02|0x00|0x01|0x00|0x80|0x93|0x10|0x03|
  //Debug:   iAQ Touch: To 0x33 of type Unknown  | HEX: 0x10|0x02|0x33|0x31|0x01|0x77|0x10|0x03|
  //Debug:   RS Serial: To 0x00 of type iAq pBut | HEX: 0x10|0x02|0x00|0x24|0x31|0x30|0x31|0x3a|0x30|0x31|0x00|0x30|0x32|0x00|0xcd|0xcd|0xcd|0xcd|0xcd|0xcd|0xcd|0x60|0x10|0x03|
  // Set time 09:55
  //Debug:   RS Serial: To 0x00 of type      Ack | HEX: 0x10|0x02|0x00|0x01|0x00|0x80|0x93|0x10|0x03|
  //Debug:   iAQ Touch: To 0x33 of type Unknown  | HEX: 0x10|0x02|0x33|0x31|0x01|0x77|0x10|0x03|
  //Debug:   RS Serial: To 0x00 of type iAq pBut | HEX: 0x10|0x02|0x00|0x24|0x31|0x30|0x39|0x3a|0x35|0x35|0x00|0x30|0x32|0x00|0xcd|0xcd|0xcd|0xcd|0xcd|0xcd|0xcd|0x71|0x10|0x03|


  // 0x3a seperator :
  // Select PM
  // Debug:   RS Serial: To 0x00 of type      Ack | HEX: 0x10|0x02|0x00|0x01|0x00|0x13|0x26|0x10|0x03|
  // Debug:   iAQ Touch: To 0x33 of type iAq pMes | HEX: 0x10|0x02|0x33|0x25|0x02|0x50|0x4d|0x00|0x09|0x10|0x03|
  // Select AM
  // Debug:   RS Serial: To 0x00 of type      Ack | HEX: 0x10|0x02|0x00|0x01|0x00|0x13|0x26|0x10|0x03|
  // Debug:   iAQ Touch: To 0x33 of type iAq pMes | HEX: 0x10|0x02|0x33|0x25|0x02|0x41|0x4d|0x00|0xfa|0x10|0x03|

  //LOG(IAQT_LOG,LOG_DEBUG "Setting time to %d/%d/%d %d:%d\n", result->tm_mon + 1, result->tm_mday, result->tm_year + 1900, result->tm_hour + 1, result->tm_min);
*/