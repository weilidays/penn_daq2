#include "XL3PacketTypes.h"
#include "XL3Registers.h"
#include "UnpackBundles.h"
#include "Globals.h"
#include "Json.h"

#include "DB.h"
#include "ControllerLink.h"
#include "XL3Model.h"
#include "MTCModel.h"
#include "SeeReflectionEsum.h"

int SeeReflectionEsum(int crateNum, uint32_t slotMask, int dacValue, float frequency, int updateDB, int finalTest)
{
  lprintf("*** Starting See Reflection ESUM ************\n");

  char channel_results[8][100];

  try {

    // set up pulser
    int errors = mtc->SetupPedestals(frequency, DEFAULT_PED_WIDTH, DEFAULT_GT_DELAY,0,
        (0x1<<crateNum),(0x1<<crateNum) | MSK_TUB | MSK_TUB_B);
    if (errors){
      lprintf("Error setting up MTC for pedestals. Exiting\n");
      mtc->UnsetPedCrateMask(MASKALL);
      mtc->UnsetGTCrateMask(MASKALL);
      return -1;
    }

    // loop over slots
    for (int i=0;i<16;i++){
      if ((0x1<<i) & slotMask){
        // loop over channels
        for (int j=0;j<8;j++){
         int four_channels = j*4;
         uint32_t temp_pattern = 0xf<<four_channels;
          memset(channel_results[j],'\0',100);

          // set up charge injection for four channels at a time
          xl3s[crateNum]->SetupChargeInjection((0x1<<i),temp_pattern,dacValue);
          // wait until something is typed
          lprintf("Slot %d, channel %d. If good, hit enter. Otherwise type in a description of the problem (or just \"fail\") and hit enter.\n",i,j);

          contConnection->GetInput(channel_results[j],100);

          for (int k=0;k<strlen(channel_results[j]);k++)
            if (channel_results[j][k] == '\n')
              channel_results[j][k] = '\0';

          if (strncmp(channel_results[j],"quit",4) == 0){
            lprintf("Quitting.\n");
            mtc->DisablePulser();
            xl3s[crateNum]->DeselectFECs();
            return 0;
          }
        } // end loop over channels

        // clear chinj for this slot
        xl3s[crateNum]->SetupChargeInjection((0x1<<i),0x0,dacValue);

        // update the database
        if (updateDB){
          lprintf("updating the database\n");
          lprintf("updating slot %d\n",i);
          JsonNode *newdoc = json_mkobject();
          json_append_member(newdoc,"type",json_mkstring("see_refl_esum"));

          int passflag = 1;
          JsonNode *all_channels = json_mkarray();
          for (int j=0;j<8;j++){
            JsonNode *one_chan = json_mkobject();
            json_append_member(one_chan,"id",json_mknumber(j));
            if (strlen(channel_results[j]) != 0){
              passflag = 0;
              json_append_member(one_chan,"error",json_mkstring(channel_results[j]));
            }else{
              json_append_member(one_chan,"error",json_mkstring(""));
            }
            json_append_element(all_channels,one_chan);
          }
          json_append_member(newdoc,"channels",all_channels);
          json_append_member(newdoc,"pass",json_mkbool(passflag));
          if (finalTest)
            json_append_member(newdoc,"final_test_id",json_mkstring(finalTestIDs[crateNum][i]));	
          PostDebugDoc(crateNum,i,newdoc);
          json_delete(newdoc); // Only have to delete the head node
        }
      } // end if slot mask
    } // end loop over slots

    mtc->DisablePulser();
    xl3s[crateNum]->DeselectFECs();

    if (errors)
      lprintf("There were %d errors.\n",errors);
    else
      lprintf("No errors.\n");
  }
  catch(const char* s){
    lprintf("SeeReflectionEsum: %s\n",s);
  }

  lprintf("****************************************\n");
  return 0;
}
