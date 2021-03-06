#include "Globals.h"
#include "Pouch.h"
#include "Json.h"
#include "DB.h"
#include "DetectorDB.h"

#include <libpq-fe.h>

#include "BoardID.h"
#include "FECTest.h"
#include "CGTTest.h"
#include "PedRun.h"
#include "TTot.h"
#include "DiscCheck.h"
#include "GTValidTest.h"
#include "CrateCBal.h"
#include "ZDisc.h"
#include "FindNoise.h"

#include "XL3Model.h"
#include "MTCModel.h"
#include "ControllerLink.h"
#include "XL3Cmds.h"
#include "MTCCmds.h"
#include "ECAL.h"

int ECAL(uint32_t crateMask, uint32_t *slotMasks, uint32_t testMask, int quickFlag, const char* loadECAL, int detectorFlag)
{
  time_t curtime = time(NULL);
  struct timeval moretime;
  gettimeofday(&moretime,0);
  struct tm *loctime = localtime(&curtime);
  char logName[500] = {'\0'};  // random size, it's a pretty nice number though.

  strftime(logName, 256, "ECAL_%Y_%m_%d_%H_%M_%S_", loctime);
  sprintf(logName+strlen(logName), "%d.log", (int)moretime.tv_usec);
  ecalLogFile = fopen(logName,"a+");
  if (ecalLogFile == NULL)
    lprintf("Problem enabling logging for ecal, could not open log file!\n");

  lprintf("*** Starting ECAL *****************************\n");

  char comments[1000];
  memset(comments,'\0',1000);

  lprintf("\nYou have selected the following configuration:\n\n");

  memset(ecalID,'\0',sizeof(ecalID));
  // load an old ECAL
  if (strlen(loadECAL)){
    // get the ecal document with the configuration
    char get_db_address[500];
    sprintf(get_db_address,"%s/%s/%s",DB_SERVER,DB_BASE_NAME,loadECAL);
    pouch_request *ecaldoc_response = pr_init();
    pr_set_method(ecaldoc_response, GET);
    pr_set_url(ecaldoc_response, get_db_address);
    pr_do(ecaldoc_response);
    if (ecaldoc_response->httpresponse != 200){
      lprintf("Unable to connect to database. error code %d\n",(int)ecaldoc_response->httpresponse);
      fclose(ecalLogFile);
      return -1;
    }
    JsonNode *ecalconfig_doc = json_decode(ecaldoc_response->resp.data);
 
    // get the configuration of the loaded ECAL if crate/slot mask not specified
    if(crateMask == 0x0 && *slotMasks == 0x0){
      for (int i=0;i<MAX_XL3_CON;i++){
        slotMasks[i] = 0x0;
      }

      JsonNode *crates = json_find_member(ecalconfig_doc,"crates");
      int num_crates = json_get_num_mems(crates);
      for (int i=0;i<num_crates;i++){
        JsonNode *one_crate = json_find_element(crates,i);
        int crate_num = (int) json_get_number(json_find_member(one_crate,"crate_id"));
        crateMask |= (0x1<<crate_num);
        JsonNode *slots = json_find_member(one_crate,"slots");
        int num_slots = json_get_num_mems(slots);
        for (int j=0;j<num_slots;j++){
          JsonNode *one_slot = json_find_element(slots,j);
          int slot_num = (int) json_get_number(json_find_member(one_slot,"slot_id"));
          slotMasks[crate_num] |= (0x1<<slot_num);
        }
      }
    }
    else if((crateMask != 0x0 && *slotMasks == 0x0) ||
            (crateMask == 0x0 && *slotMasks != 0x0)){
      lprintf("Specify both a crate and slot mask if you wish to run tests on a specific crate/slot, rather than every crate/slot in the loaded ECAL.\n");
      return -1;
    }

    pr_free(ecaldoc_response);
    json_delete(ecalconfig_doc);

    lprintf("You will be updating ECAL %s\n",loadECAL);
    strcpy(ecalID,loadECAL);
  }
  // Create a new ECAL
  else{
    GetNewID(ecalID);
    lprintf("Creating new ECAL %s\n",ecalID);
  }

  
  // Print the current location and if UG allow the
  // slot mask to be set by detector DB
  if(CURRENT_LOCATION == ABOVE_GROUND_TESTSTAND){
    lprintf("Running ECAL from surface teststand.\n");
  }
  else if(CURRENT_LOCATION == UNDERGROUND){
    lprintf("Running ECAL from underground.\n");
    if(detectorFlag){
      PGconn* detectorDB = ConnectToDetectorDB();
      lprintf("Using detectorDB to set slot masks.\n");
      if(DetectorSlotMask(crateMask, slotMasks, detectorDB)){
        lprintf("Failure setting slot mask using detectorDB, exiting.");
        return -1;
      }
    }
  }
  else if(CURRENT_LOCATION == PENN_TESTSTAND){
    lprintf("Running ECAL from penn teststand.\n");
  }

  // Get the testmask from -t and -q flags
  lprintf("Running the following tests: \n");
  if ((testMask == 0xFFFFFFFF || (testMask & 0x3FF) == 0x3FF) && (!quickFlag)){
    lprintf("All \n");
    testMask = 0xFFFFFFFF;
  }
  else if ((testMask != 0x0)){
    if (quickFlag){
      testMask &= 0x728;
    }
    for (int i=0;i<11;i++){
      if ((0x1<<i) & testMask){
        lprintf("%s \n",testList[i]);
      }
    }
  }
  else if (!quickFlag){
    lprintf("None.\n");
  }
  // Do only ECAL tests that set hardware settings
  else if(quickFlag){
    testMask = 0x728; 
    for (int i=0;i<11;i++){
      if ((0x1<<i) & testMask){
        lprintf("%s ",testList[i]);
      }
    }
  }

  // Print the crate and slot masks
  for (int i=0;i<MAX_XL3_CON;i++){
    if ((0x1<<i) & crateMask){
      lprintf("crate %d: 0x%04x\n",i,slotMasks[i]);
    }
  }

  lprintf("------------------------------------------\n");
  lprintf("Hit enter to start, or type quit if anything is incorrect\n");
  contConnection->GetInput(comments);
  if (strncmp("quit",comments,4) == 0){
    lprintf("Exiting ECAL\n");
    fclose(ecalLogFile);
    return 0;
  }
  lprintf("------------------------------------------\n");

  if (testMask != 0x0){

    for (int i=0;i<MAX_XL3_CON;i++)
      if ((0x1<<i) & crateMask)
        CrateInit(i,slotMasks[i],1,0,0,0,0,0,0,0,0);
    MTCInit(1);

    if (strlen(loadECAL) == 0){
      lprintf("Creating ECAL document...\n");
      PostECALDoc(crateMask,slotMasks,logName,ecalID);
      lprintf("Created! Starting tests\n");
      lprintf("------------------------------------------\n");
    }

    int testCounter = 0;
    if ((0x1<<testCounter) & testMask)
      for (int i=0;i<MAX_XL3_CON;i++)
        if ((0x1<<i) & crateMask)
          FECTest(i,slotMasks[i],1,0,1);
    testCounter++;

    if ((0x1<<testCounter) & testMask)
      for (int i=0;i<MAX_XL3_CON;i++)
        if ((0x1<<i) & crateMask)
          BoardID(i,slotMasks[i]);
    testCounter++;

    if ((0x1<<testCounter) & testMask)
      for (int i=0;i<MAX_XL3_CON;i++)
        if ((0x1<<i) & crateMask)
          CGTTest(i,slotMasks[i],0xFFFFFFFF,1,0,1);
    testCounter++;
    MTCInit(1);

    for (int i=0;i<MAX_XL3_CON;i++)
      if ((0x1<<i) & crateMask)
        CrateInit(i,slotMasks[i],0,0,0,0,0,0,0,0,0);

    if ((0x1<<testCounter) & testMask)
      for (int i=0;i<MAX_XL3_CON;i++)
        if ((0x1<<i) & crateMask)
          CrateCBal(i,slotMasks[i],0xFFFFFFFF,1,0,1);
    testCounter++;

    // load cbal values
    for (int i=0;i<MAX_XL3_CON;i++)
      if ((0x1<<i) & crateMask)
        CrateInit(i,slotMasks[i],0,1,0,0,0,0,0,0,0);

    if ((0x1<<testCounter) & testMask)
      for (int i=0;i<MAX_XL3_CON;i++)
        if ((0x1<<i) & crateMask)
          PedRun(i,slotMasks[i],0xFFFFFFFF,0,DEFAULT_GT_DELAY,DEFAULT_PED_WIDTH,50,1000,300,1,1,0,1);
    testCounter++;

    MTCInit(1);
    for (int i=0;i<MAX_XL3_CON;i++)
      if ((0x1<<i) & crateMask)
        CrateInit(i,slotMasks[i],0,1,0,0,0,0,0,0,0);

    try {
      tubii->SetECALBit(1);
    } catch(const char* c) {
      lprintf("Failed to connect to tubii.\n");
    }

    if ((0x1<<testCounter) & testMask)
      for (int i=0;i<MAX_XL3_CON;i++)
        if ((0x1<<i) & crateMask)
          SetTTot(i,slotMasks[i],420,1,0,1);
    testCounter++;

    // load cbal and tdisc values
    for (int i=0;i<MAX_XL3_CON;i++)
      if ((0x1<<i) & crateMask)
        CrateInit(i,slotMasks[i],0,1,0,1,0,0,0,0,0);

    if ((0x1<<testCounter) & testMask)
      for (int i=0;i<MAX_XL3_CON;i++)
        if ((0x1<<i) & crateMask)
          GetTTot(i,slotMasks[i],400,1,0,1);
    testCounter++;

    try {
      tubii->SetECALBit(0);
    } catch(const char* c) {
      lprintf("Failed to connect to tubii.\n");
    }

    for (int i=0;i<MAX_XL3_CON;i++)
      if ((0x1<<i) & crateMask)
        CrateInit(i,slotMasks[i],0,1,0,1,0,0,0,0,0);

    if ((0x1<<testCounter) & testMask)
      for (int i=0;i<MAX_XL3_CON;i++)
        if ((0x1<<i) & crateMask)
          DiscCheck(i,slotMasks[i],500000,1,0,1);
    testCounter++;

    MTCInit(1);
    for (int i=0;i<MAX_XL3_CON;i++)
      if ((0x1<<i) & crateMask)
        CrateInit(i,slotMasks[i],0,1,0,1,0,0,0,0,0);

    // pass quick flag as setOnly if you want to speed this up
    // commented out for now so gtvalid measurement is forced
    if ((0x1<<testCounter) & testMask)
          GTValidTest(crateMask,slotMasks,0xFFFFFFFF,410,0,0,1,1,0,1);
          //GTValidTest(crateMask,slotMasks,0xFFFFFFFF,410,0,quickFlag,1,0,0,1);
    testCounter++;

    MTCInit(1);
    // load cbal, tdisc, tcmos values
    for (int i=0;i<MAX_XL3_CON;i++)
      if ((0x1<<i) & crateMask)
        CrateInit(i,slotMasks[i],0,1,0,1,1,0,0,0,0);

    if ((0x1<<testCounter) & testMask)
      for (int i=0;i<MAX_XL3_CON;i++)
        if ((0x1<<i) & crateMask)
          ZDisc(i,slotMasks[i],10000,0,1,0,0,1);
    testCounter++;

    if ((0x1<<testCounter) & testMask)
      for (int i=0;i<MAX_XL3_CON;i++)
        if ((0x1<<i) & crateMask)
          FindNoise((0x1<<i),slotMasks,200,1,1,1,1,1);

    lprintf("ECAL finished!\n");

  }

  lprintf("**********************************************\n");
  fclose(ecalLogFile);
  return 0;
}

