// Generated 2026-10-03 from BOX 2 STEP.txt (the Box 2 Designer export, 24 steps; frozen as BOX2_STEPS_2026-10-03_v2.json) for the S3C.
// Lives in its own header so the Arduino IDE does not scan it for C functions. Differences from the Designer's preview strings,
// all deliberate (same conventions as S3A_Screens.h): the rating grids start with NOTHING picked (value 0); TIME ALLOT carries
// printf fields (%u = seconds per coin, %u = suggested seconds); HEATING / HEATING FREE / COOLDOWN carry a %lu field = the
// total in seconds for the progress bar; FREE RETRY's "2 MINUTES" is a field so the setting drives it; an empty text field
// (',,') becomes a single space, because C1's parser drops empty fields and would shift everything after.
// Every string is <= 199 characters once its fields are filled in (checked when this file was generated).
#pragma once

const char* const SCR_TAPS =
    R"SCR(#000000|ST,165,105,#FFFFFF,TAP anywhere to ,MC,2|TT,160,136,#FFFF00,START,500,MC,4)SCR";
const char* const SCR_WELCOME =
    R"SCR(#000000|ST,160,60,#FFFFFF,Welcome to,BC,2|ST,160,60,#FFFFFF,JOLLY HELMET,TC,4|ST,162,120,#FFFFFF,Make your Helmet,BC,2|ST,160,120,#FFFFFF,Happy Again!,TC,4|TT,160,180,#FFFF00,Tap to START,500,TC,2)SCR";
const char* const SCR_RATE_A =
    R"SCR(#000000|BTN,19,192,282,36,#1C1C20,CONFIRM,ACTION|ST,160,60,#FFFFFF,DRY O' METER,BC,4|ST,28,20,#FFFFFF,Before,MC,1|NR2,24,70,272,100,0,#FFFFFF,#22c55e,DRY;WET)SCR";
const char* const SCR_RATE_B =
    R"SCR(#000000|BTN,19,192,282,36,#1C1C20,CONFIRM,ACTION|ST,160,60,#FFFFFF,DRY O' METER,BC,4|ST,28,20,#FFFFFF,After,MC,1|NR2,24,70,272,100,0,#FFFFFF,#22c55e,DRY;WET)SCR";
const char* const SCR_TIME_ALLOT =
    R"SCR(#000000|NP,0,0,320,240,#FFFFFF,%u,ALLOCATE HEATING TIME,%u)SCR";
const char* const SCR_INSERT_COIN =
    R"SCR(#000000|ST,160,60,#FFFFFF,INSERT THE REQUIRED COIN,TC,2|TT,144,120,#FFFFFF,/ {REQUIREDCOIN} Coin,0,ML,2|TT,126,120,#FFFF00,{COINS},500,MR,2|BTN,20,190,90,32,#1C1C20,CANCEL,INSERT_CANCEL)SCR";
const char* const SCR_OPEN_A =
    R"SCR(#000000|ST,160,88,#FFFFFF,Please,MC,2|ST,160,156,#FFFFFF,Door to Proceed,MC,2|TT,152,120,#FFFF00, OPEN,500,MC,4)SCR";
const char* const SCR_SENSORS_A =
    R"SCR(#000000|ST,160,92,#FFFFFF,Please,MC,2|ST,160,148,#FFFFFF, Headgear Inside,MC,2|TT,160,120,#FFFF00,PLACE,500,MC,4)SCR";
const char* const SCR_CLOSE_DOOR_A =
    R"SCR(#000000|ST,160,92,#FFFFFF,Please,MC,2|ST,160,148,#FFFFFF,Door to Proceed,MC,2|TT,160,120,#FFFF00,CLOSE,500,MC,4)SCR";
const char* const SCR_HELMET_NOTICE_A =
    R"SCR(#000000|TT,160,120,#FFFF00,HELMET,500,MC,4|ST,160,92,#FFFFFF,NO,MC,2|ST,160,152,#FFFFFF,DETECTED,MC,2)SCR";
const char* const SCR_HEATING =
    R"SCR(#000000|ST,160,92,#FFFFFF,Heating...,MC,2|TT,160,120,#00FF00,{MINUTE}:{SECOND},0,MC,2|BTN,215,8,95,32,#000000,CANCEL,ABORT|PBAR,50,137,220,14,#505050,#FFFFFF,%lu|TT,160,180,#949494,{STATUS},0,TC,2)SCR";
const char* const SCR_HEATING_FREE =
    R"SCR(#000000|ST,160,92,#FFFFFF,Heating...,MC,2|TT,160,120,#00FF00,{MINUTE}:{SECOND},0,MC,2|BTN,215,8,95,32,#000000,CANCEL,ABORT|PBAR,50,137,220,14,#505050,#FFFFFF,%lu|TT,160,180,#949494,{STATUS},0,TC,2)SCR";
const char* const SCR_COOLDOWN =
    R"SCR(#000000|ST,160,92,#FFFFFF,Cooling Down...,MC,2|TT,160,120,#00FF00,{MINUTE}:{SECOND},0,MC,2|PBAR,50,137,220,14,#505050,#FFFFFF,%lu|TT,160,180,#949494,{STATUS},0,TC,2)SCR";
const char* const SCR_OPEN_B =
    R"SCR(#000000|ST,160,88,#FFFFFF,Please,MC,2|ST,160,156,#FFFFFF,Door to Proceed,MC,2|TT,152,120,#FFFF00, OPEN,500,MC,4)SCR";
const char* const SCR_SENSORS_B =
    R"SCR(#000000|ST,160,92,#FFFFFF,Please,MC,2|ST,160,148,#FFFFFF, Headgear Outside,MC,2|TT,160,120,#FFFF00,TAKE,500,MC,4)SCR";
const char* const SCR_CLOSE_DOOR_B =
    R"SCR(#000000|ST,160,92,#FFFFFF,Please,MC,2|ST,160,148,#FFFFFF,Door to Proceed,MC,2|TT,160,120,#FFFF00,CLOSE,500,MC,4)SCR";
const char* const SCR_HELMET_NOTICE_B =
    R"SCR(#000000|TT,160,120,#FFFF00,HELMET,500,MC,4|ST,160,152,#FFFFFF,DETECTED,MC,2)SCR";
const char* const SCR_FREE_RETRY =
    R"SCR(#000000|ST,160,120,#FFFFFF,%u %s,MC,2|ST,160,88,#FFFFFF,FREE TIME,MC,4|BTN,12,154,140,52,#069322,YES,FREE_YES|BTN,168,154,140,52,#A91414,NO,FREE_NO)SCR";
const char* const SCR_FINISH =
    R"SCR(#000000|ST,160,76,#FFFFFF,Thank you,MC,2|ST,160,102,#FFFFFF,for your patronage!,MC,2|TT,160,144,#FFFF00,We hope to see you again! :D,500,MC,2)SCR";
const char* const SCR_ABORT =
    R"SCR(#000000|ST,160,120,#FFFFFF,COIN(s) won't be Refunded.,MC,2|ST,160,88,#FFFFFF,Cancel operation?,MC,2|BTN,12,154,140,52,#A91414,CONFIRM,ABORT_YES|BTN,168,154,140,52,#069322,RESUME,ABORT_NO)SCR";
const char* const SCR_CANCEL =
    R"SCR(#000000|ST,160,88,#FFFFFF,Cancel operation?,MC,2|ST,160,120,#FFFFFF,COIN(s) won't be Refunded.,MC,2|BTN,12,154,140,52,#3B891A,YES,CANCEL_YES|BTN,168,154,140,52,#991414,NO,CANCEL_NO)SCR";
const char* const SCR_MAINTENANCE =
    R"SCR(#000000|ST,160,80,#FFFFFF,IT'S ON,TC,2|TT,160,120,#FFD84E,MAINTENANCE,1000,MC,4)SCR";
const char* const SCR_OUT_OF_ORDER =
    R"SCR(#000000|TT,160,120,#FF0000,OUT OF ORDER,1000,MC,4)SCR";
const char* const SCR_SAFETY_PAUSE =
    R"SCR(#800000|ST,160,90,#FFFFFF,SAFETY PAUSE,MC,4|TT,160,140,#FFFF00,CLOSE THE DOOR,500,MC,2)SCR";
const char* const SCR_TIME_WARN =
    R"SCR(#000000|ST,160,70,#FF6666,%s,MC,2|ST,160,100,#FFFFFF,%s,MC,2|BTN,100,150,120,40,#22c55e,OK,WARN_OK)SCR";
