// Generated from upstream ZQuestClassic (3.0 Nightly, commit ef321f8) by tools/zq_gen_combo_names.py:
// combo type and map flag names, the combo types' help, and the combo editor's per-type labels
// for attributes and flags (ComboEditorDialog::loadComboType). Upstream's numbers, which
// 2.55 and 3.0 quests use. Regenerate rather than edit by hand.

#include "precompiled.h" //always first

#include <stdarg.h>
#include <stdio.h>
#include <string>

#include "zq_combo_names.h"
#include "zq_q255_store.h"

namespace
{

namespace up30
{
enum
{
    cNONE, cSTAIR, cCAVE, cWATER, cARMOS, cGRAVE,
    cDOCK, cUNDEF, cPUSH_WAIT, cPUSH_HEAVY, cPUSH_HW, cL_STATUE,
    cR_STATUE, cWALKSLOW, cCVUP, cCVDOWN, cCVLEFT, cCVRIGHT,
    cSWIMWARP, cDIVEWARP, cLADDERHOOKSHOT, cTRIGNOFLAG, cTRIGFLAG, cZELDA,
    cSLASH, cSLASHITEM, cPUSH_HEAVY2, cPUSH_HW2, cPOUND, cHSGRAB,
    cHSBRIDGE, cDAMAGE1, cDAMAGE2, cDAMAGE3, cDAMAGE4, cC_STATUE,
    cTRAP_H, cTRAP_V, cTRAP_4, cTRAP_LR, cTRAP_UD, cPIT,
    cHOOKSHOTONLY, cOVERHEAD, cNOFLYZONE, cMIRROR, cMIRRORSLASH, cMIRRORBACKSLASH,
    cMAGICPRISM, cMAGICPRISM4, cMAGICSPONGE, cCAVE2, cEYEBALL_A, cEYEBALL_B,
    cNOJUMPZONE, cBUSH, cFLOWERS, cTALLGRASS, cSHALLOWWATER, cLOCKBLOCK,
    cLOCKBLOCK2, cBOSSLOCKBLOCK, cBOSSLOCKBLOCK2, cLADDERONLY, cBSGRAVE, cCHEST,
    cCHEST2, cLOCKEDCHEST, cLOCKEDCHEST2, cBOSSCHEST, cBOSSCHEST2, cRESET,
    cSAVE, cSAVE2, cCAVEB, cCAVEC, cCAVED, cSTAIRB,
    cSTAIRC, cSTAIRD, cPITB, cPITC, cPITD, cCAVE2B,
    cCAVE2C, cCAVE2D, cSWIMWARPB, cSWIMWARPC, cSWIMWARPD, cDIVEWARPB,
    cDIVEWARPC, cDIVEWARPD, cSTAIRR, cPITR, cAWARPA, cAWARPB,
    cAWARPC, cAWARPD, cAWARPR, cSWARPA, cSWARPB, cSWARPC,
    cSWARPD, cSWARPR, cSTRIGNOFLAG, cSTRIGFLAG, cSTEP, cSTEPSAME,
    cSTEPALL, cSTEPCOPY, cNOENEMY, cBLOCKARROW1, cBLOCKARROW2, cBLOCKARROW3,
    cBLOCKBRANG1, cBLOCKBRANG2, cBLOCKBRANG3, cBLOCKSBEAM, cBLOCKALL, cBLOCKFIREBALL,
    cDAMAGE5, cDAMAGE6, cDAMAGE7, cCHANGE, cSPINTILE1, cSPINTILE2,
    cSCREENFREEZE, cSCREENFREEZEFF, cNOGROUNDENEMY, cSLASHNEXT, cSLASHNEXTITEM, cBUSHNEXT,
    cSLASHTOUCHY, cSLASHITEMTOUCHY, cBUSHTOUCHY, cFLOWERSTOUCHY, cTALLGRASSTOUCHY, cSLASHNEXTTOUCHY,
    cSLASHNEXTITEMTOUCHY, cBUSHNEXTTOUCHY, cEYEBALL_4, cTALLGRASSNEXT, cSCRIPT1, cSCRIPT2,
    cSCRIPT3, cSCRIPT4, cSCRIPT5, cSCRIPT6, cSCRIPT7, cSCRIPT8,
    cSCRIPT9, cSCRIPT10, cSCRIPT11, cSCRIPT12, cSCRIPT13, cSCRIPT14,
    cSCRIPT15, cSCRIPT16, cSCRIPT17, cSCRIPT18, cSCRIPT19, cSCRIPT20,
    cTRIGGERGENERIC, cPITFALL, cSTEPSFX, cBRIDGE, cSIGNPOST, cCSWITCH,
    cCSWITCHBLOCK, cTORCH, cSPOTLIGHT, cGLASS, cLIGHTTARGET, cSWITCHHOOK,
    cBUTTONPROMPT, cCUSTOMBLOCK, cSHOOTER, cSLOPE, cCUTSCENETRIG, cPUSHBLOCK,
    cICY, cMIRRORNEW, cCRUMBLE, cCUTSCENEEFFECT, cMAX,
};

enum
{
    mfNONE, mfPUSHUD, mfPUSH4, mfWHISTLE, mfANYFIRE, mfARROW,
    mfBOMB, mfFAIRY, mfRAFT, mfARMOS_SECRET, mfARMOS_ITEM, mfSBOMB,
    mfRAFT_BRANCH, mfDIVE_ITEM, mfLENSMARKER, mfZELDA, mfSECRETS01, mfSECRETS02,
    mfSECRETS03, mfSECRETS04, mfSECRETS05, mfSECRETS06, mfSECRETS07, mfSECRETS08,
    mfSECRETS09, mfSECRETS10, mfSECRETS11, mfSECRETS12, mfSECRETS13, mfSECRETS14,
    mfSECRETS15, mfSECRETS16, mfTRAP_H, mfTRAP_V, mfTRAP_4, mfTRAP_LR,
    mfTRAP_UD, mfENEMY0, mfENEMY1, mfENEMY2, mfENEMY3, mfENEMY4,
    mfENEMY5, mfENEMY6, mfENEMY7, mfENEMY8, mfENEMY9, mfPUSHLR,
    mfPUSHU, mfPUSHD, mfPUSHL, mfPUSHR, mfPUSHUDNS, mfPUSHLRNS,
    mfPUSH4NS, mfPUSHUNS, mfPUSHDNS, mfPUSHLNS, mfPUSHRNS, mfPUSHUDINS,
    mfPUSHLRINS, mfPUSH4INS, mfPUSHUINS, mfPUSHDINS, mfPUSHLINS, mfPUSHRINS,
    mfBLOCKTRIGGER, mfNOBLOCKS, mfBRANG, mfMBRANG, mfFBRANG, mfSARROW,
    mfGARROW, mfSTRONGFIRE, mfMAGICFIRE, mfDIVINEFIRE, mfWANDMAGIC, mfREFMAGIC,
    mfREFFIREBALL, mfSWORD, mfWSWORD, mfMSWORD, mfXSWORD, mfSWORDBEAM,
    mfWSWORDBEAM, mfMSWORDBEAM, mfXSWORDBEAM, mfHOOKSHOT, mfWAND, mfHAMMER,
    mfSTRIKE, mfBLOCKHOLE, mfMAGICFAIRY, mfALLFAIRY, mfSINGLE, mfSINGLE16,
    mfNOENEMY, mfNOGROUNDENEMY, mfSCRIPT1, mfSCRIPT2, mfSCRIPT3, mfSCRIPT4,
    mfSCRIPT5, mfRAFT_BOUNCE, mfPUSHED, mfSCRIPT6, mfSCRIPT7, mfSCRIPT8,
    mfSCRIPT9, mfSCRIPT10, mfSCRIPT11, mfSCRIPT12, mfSCRIPT13, mfSCRIPT14,
    mfSCRIPT15, mfSCRIPT16, mfSCRIPT17, mfSCRIPT18, mfSCRIPT19, mfSCRIPT20,
    mfPITHOLE, mfPITFALLFLOOR, mfLAVA, mfICE, mfICEDAMAGE, mfDAMAGE1,
    mfDAMAGE2, mfDAMAGE4, mfDAMAGE8, mfDAMAGE16, mfDAMAGE32, mfFREEZEALL,
    mfFREZEALLANSFFCS, mfFREEZEFFCSOLY, mfSCRITPTW1TRIG, mfSCRITPTW2TRIG, mfSCRITPTW3TRIG, mfSCRITPTW4TRIG,
    mfSCRITPTW5TRIG, mfSCRITPTW6TRIG, mfSCRITPTW7TRIG, mfSCRITPTW8TRIG, mfSCRITPTW9TRIG, mfSCRITPTW10TRIG,
    mfTROWEL, mfTROWELNEXT, mfTROWELSPECIALITEM, mfSLASHPOT, mfLIFTPOT, mfLIFTORSLASH,
    mfLIFTROCK, mfLIFTROCKHEAVY, mfDROPITEM, mfSPECIALITEM, mfDROPKEY, mfDROPLKEY,
    mfDROPCOMPASS, mfDROPMAP, mfDROPBOSSKEY, mfSPAWNNPC, mfSWITCHHOOK, mfSIDEVIEWLADDER,
    mfSIDEVIEWPLATFORM, mfNOENEMYSPAWN, mfENEMYALL, mfSECRETSNEXT, mfNOMIRROR, mfUNSAFEGROUND_SENS,
    mfUNSAFEGROUND, mf169, mf170, mf171, mf172, mf173,
    mf174, mf175, mf176, mf177, mf178, mf179,
    mf180, mf181, mf182, mf183, mf184, mf185,
    mf186, mf187, mf188, mf189, mf190, mf191,
    mf192, mf193, mf194, mf195, mf196, mf197,
    mf198, mf199, mf200, mf201, mf202, mf203,
    mf204, mf205, mf206, mf207, mf208, mf209,
    mf210, mf211, mf212, mf213, mf214, mf215,
    mf216, mf217, mf218, mf219, mf220, mf221,
    mf222, mf223, mf224, mf225, mf226, mf227,
    mf228, mf229, mf230, mf231, mf232, mf233,
    mf234, mf235, mf236, mf237, mf238, mf239,
    mf240, mf241, mf242, mf243, mf244, mf245,
    mf246, mf247, mf248, mf249, mf250, mf251,
    mf252, mf253, mf254, mfEXTENDED, mfMAX,
};

enum { cflag1 = 0x0001, cflag2 = 0x0002, cflag3 = 0x0004, cflag4 = 0x0008, cflag5 = 0x0010, cflag6 = 0x0020,
       cflag7 = 0x0040, cflag8 = 0x0080, cflag9 = 0x0100, cflag10 = 0x0200, cflag11 = 0x0400, cflag12 = 0x0800,
       cflag13 = 0x1000, cflag14 = 0x2000, cflag15 = 0x4000, cflag16 = 0x8000 };
enum { CMBTY_CRUMBLE_RESET, CMBTY_CRUMBLE_CUMULATIVE, CMBTY_CRUMBLE_INEVITABLE };
enum direction { dir_invalid = -1, up, down, left, right, l_up, r_up, l_down, r_down };
const char *dirstr[] = { "up", "down", "left", "right", "up-left", "up-right", "down-left", "down-right" };

const char g_ctype_names[up30::cMAX][255] = 
{
	"(None)", "Stairs [A]", "Cave (Walk Down) [A]", "Liquid", "Armos",
	"Grave", "Dock", "-UNDEF", "Push (Wait)", "Push (Heavy)",
	"Push (Heavy, Wait)", "Left Statue", "Right Statue", "Slow Walk", "Conveyor Up",
	"Conveyor Down", "Conveyor Left", "Conveyor Right", "Swim Warp [A]", "Dive Warp [A]",
	"Ladder or Hookshot", "Step->Secrets (Temporary)", "Step->Secrets (Permanent)", "-WINGAME", "Slash",
	"Slash (Item)", "Push (Very Heavy)", "Push (Very Heavy, Wait)", "Pound", "Hookshot Grab",
	"-HSBRIDGE", "Damage (1/2 Heart)", "Damage (1 Heart)", "Damage (2 hearts)", "Damage (4 Hearts)",
	"Center Statue", "Trap (Horizontal, Line of Sight)", "Trap (Vertical, Line of Sight)", "Trap (4-Way)", "Trap (Horizontal, Constant)",
	"Trap (Vertical, Constant)", "Direct Warp [A]", "Hookshot Only", "Overhead", "No Flying Enemies",
	"Mirror (4-Way)", "Mirror (Up-Left, Down-Right)", "Mirror (Up-Right, Down-Left)", "Magic Prism (3-Way)", "Magic Prism (4-Way)",
	"Block Magic", "Cave (Walk Up) [A]", "Eyeball (8-Way A)", "Eyeball (8-Way B)", "No Jumping Enemies",
	"Bush", "Flowers", "Tall Grass", "Shallow Liquid", "Lock Block (Basic)",
	"Lock Block (Basic, Copycat)", "Lock Block (Boss)", "Lock Block (Boss, Copycat)", "Ladder Only", "BS Grave",
	"Chest (Basic)", "Chest (Basic, Copycat)", "Chest (Locked)", "Chest (Locked, Copycat)", "Chest (Boss)",
	"Chest (Boss, Copycat)", "Reset Room", "Save Point", "Save-Quit Point", "Cave (Walk Down) [B]",
	"Cave (Walk Down) [C]", "Cave (Walk Down) [D]", "Stairs [B]", "Stairs [C]", "Stairs [D]",
	"Direct Warp [B]", "Direct Warp [C]", "Direct Warp [D]", "Cave (Walk Up) [B]", "Cave (Walk Up) [C]",
	"Cave (Walk Up) [D]", "Swim Warp [B]", "Swim Warp [C]", "Swim Warp [D]", "Dive Warp [B]",
	"Dive Warp [C]", "Dive Warp [D]", "Stairs [Random]", "Direct Warp [Random]", "Auto Side Warp [A]",
	"Auto Side Warp [B]","Auto Side Warp [C]","Auto Side Warp [D]","Auto Side Warp [Random]","Sensitive Warp [A]",
	"Sensitive Warp [B]","Sensitive Warp [C]","Sensitive Warp [D]","Sensitive Warp [Random]","Step->Secrets (Sensitive, Temp)",
	"Step->Secrets (Sensitive, Perm.)","Step->Next","Step->Next (Same)","Step->Next (All)","Step->Next (Copycat)",
	"No Enemies","Block Arrow (L1)","Block Arrow (L1, L2)","Block Arrow (All)","Block Brang (L1)",
	"Block Brang (L1, L2)","Block Brang (All)","Block Sword Beam","Block All","Block Fireball",
	"Damage (8 hearts)","Damage (16 hearts)","Damage (32 hearts)","-Unused","Spinning Tile (Immediate)",
	"-Unused","Screen Freeze (Except FFCs)","Screen Freeze (FFCs Only)","No Ground Enemies","Slash->Next",
	"Slash->Next (Item)","Bush->Next","Slash (Continuous)","Slash (Item, Continuous)","Bush (Continuous)",
	"Flowers (Continuous)","Tall Grass (Continuous)","Slash->Next (Continuous)","Slash->Next (Item, Continuous)","Bush->Next (Continuous)",
	"Eyeball (4-Way)","Tall Grass->Next","Script 01","Script 02","Script 03",
	"Script 04","Script 05","Script 06","Script 07","Script 08",
	"Script 09","Script 10","Script 11","Script 12","Script 13",
	"Script 14","Script 15","Script 16","Script 17","Script 18",
	"Script 19", "Script 20", "Generic", "Pitfall", "Step->Effects",
	"Bridge", "Signpost", "Switch", "Switch Block", "Torch",
	"Spotlight", "Glass", "Light Trigger", "SwitchHook Block", "ButtonPrompt",
	"Block Weapon (Custom)", "Shooter", "Slope", "Cutscene Trigger", "Push (Generic)",
	"Icy Floor", "Mirror (Custom)", "Crumbling", "Cutscene Effects"
};

const char g_mflag_names[up30::mfMAX][255] =
{
	"(None)", "Push Block (Vertical, Trigger)", "Push Block (4-Way, Trigger)", "Whistle Trigger", "Burn Trigger (Any)", "Arrow Trigger (Any)", "Bomb Trigger (Any)", "Fairy Ring (Life)",
	"Raft Path", "Armos -> Secret", "Armos/Chest -> Item", "Bomb (Super)", "Raft Branch", "Dive -> Item", "Lens Marker", "Win Game",
	"Secret Tile 00", "Secret Tile 01", "Secret Tile 02", "Secret Tile 03", "Secret Tile 04", "Secret Tile 05", "Secret Tile 06", "Secret Tile 07",
	"Secret Tile 08", "Secret Tile 09", "Secret Tile 10", "Secret Tile 11", "Secret Tile 12", "Secret Tile 13", "Secret Tile 14", "Secret Tile 15",
	"Trap (Horizontal, Line of Sight)", "Trap (Vertical, Line of Sight)", "Trap (4-Way, Line of Sight)", "Trap (Horizontal, Constant)", "Trap (Vertical, Constant)", "Enemy 0", "Enemy 1", "Enemy 2",
	"Enemy 3", "Enemy 4", "Enemy 5", "Enemy 6", "Enemy 7", "Enemy 8", "Enemy 9", "Push Block (Horiz, Once, Trigger)",
	"Push Block (Up, Once, Trigger)", "Push Block (Down, Once, Trigger)", "Push Block (Left, Once, Trigger)", "Push Block (Right, Once, Trigger)", "Push Block (Vert, Once)", "Push Block (Horizontal, Once)", "Push Block (4-Way, Once)", "Push Block (Up, Once)",
	"Push Block (Down, Once)", "Push Block (Left, Once)", "Push Block (Right, Once)", "Push Block (Vertical, Many)", "Push Block (Horizontal, Many)", "Push Block (4-Way, Many)", "Push Block (Up, Many)", "Push Block (Down, Many)",
	"Push Block (Left, Many)", "Push Block (Right, Many)", "Block Trigger", "No Push Blocks", "Boomerang Trigger (Any)", "Boomerang Trigger (Magic +)", "Boomerang Trigger (Fire)", "Arrow Trigger (Silver +)",
	"Arrow Trigger (Golden)", "Burn Trigger (Strong)", "Burn Trigger (Magic)", "Burn Trigger (Divine)", "Magic Trigger (Wand)", "Magic Trigger (Reflected)", "Fireball Trigger (Reflected)", "Sword Trigger (Any)",
	"Sword Trigger (White +)", "Sword Trigger (Magic +)", "Sword Trigger (Master)", "Sword Beam Trigger (Any)", "Sword Beam Trigger (White +)", "Sword Beam Trigger (Magic +)", "Sword Beam Trigger (Master)", "Hookshot Trigger",
	"Wand Trigger", "Hammer Trigger", "Strike Trigger", "Block Hole (Block -> Next)", "Fairy Ring (Magic)", "Fairy Ring (All)", "Trigger -> Self Only", "Trigger -> Self, Secret Tiles",
	"No Enemies", "No Ground Enemies", "Script 01", "Script 02", "Script 03", "Script 04", "Script 05", "Raft Bounce",
	 "Pushed", "Script 06", "Script 07", "Script 08", "Script 09", "Script 10", "Script 11", "Script 12",
	"Script 13", "Script 14", "Script 15", "Script 16", "Script 17", "Script 18", "Script 19", "Script 20",
	"Script 21", "Script 22", "Script 23", "Script 24", "Script 25", "Script 26", "Script 27", "Script 28",
	"Script 29", "Script 30", "Script 31", "-Freeze Screen (Unimplemented)", "-Freeze Screen, Except FFCs (Unimplemented)", "-Freeze FFCs Only (Unimplemented)", "-Trigger LW_SCRIPT1 (Unimplemented)", "-Trigger LW_SCRIPT2 (Unimplemented)",
	"-Trigger LW_SCRIPT3 (Unimplemented)", "-Trigger LW_SCRIPT4 (Unimplemented)", "-Trigger LW_SCRIPT5 (Unimplemented)", "-Trigger LW_SCRIPT6 (Unimplemented)", "-Trigger LW_SCRIPT7 (Unimplemented)", "-Trigger LW_SCRIPT8 (Unimplemented)", "-Trigger LW_SCRIPT9 (Unimplemented)", "-Trigger LW_SCRIPT10 (Unimplemented)",
	"Script 32", "Script 33", "Script 34", "Script 35", "Script 36", "Script 37", "Script 38", "Script 39",
	"Script 40", "Script 41", "Script 42", "Script 43", "Script 44", "Script 45", "Script 46", "Script 47",
	"Script 48", "Sideview Ladder", "Sideview Platform","Spawn No Enemies","Spawn All Enemies","Secrets->Next","No Mirroring","Unsafe Ground (Sens)","Unsafe Ground","-mf169", "-mf170","-mf171","-mf172","-mf173","-mf174","-mf175","-mf176","-mf177","-mf178","-mf179",
	"-mf180","-mf181","-mf182","-mf183","-mf184","-mf185","-mf186","-mf187","-mf188","-mf189", "-mf190","-mf191","-mf192","-mf193","-mf194","-mf195","-mf196","-mf197","-mf198","-mf199",
	"-mf200","-mf201","-mf202","-mf203","-mf204","-mf205","-mf206","-mf207","-mf208","-mf209", "-mf210","-mf211","-mf212","-mf213","-mf214","-mf215","-mf216","-mf217","-mf218","-mf219",
	"-mf220","-mf221","-mf222","-mf223","-mf224","-mf225","-mf226","-mf227","-mf228","-mf229", "-mf230","-mf231","-mf232","-mf233","-mf234","-mf235","-mf236","-mf237","-mf238","-mf239",
	"-mf240","-mf241","-mf242","-mf243","-mf244","-mf245","-mf246","-mf247","-mf248","-mf249", "-mf250","-mf251","-mf252","-mf253","-mf254",
	"-Extended (Extended Flag Editor)"
};

const char *g_ctype_help[up30::cMAX] =
{
	"",
	"The Hero is warped via Tile Warp A if they step on the bottom half of this combo.",
	"The Hero marches down into this combo and is warped via Tile Warp A if they step on this. The combo's tile will be drawn above the Hero during this animation.",
	"",
	"",
	"",
	"Raft paths must begin on a Dock-type combo. (Use the Raft combo flag to create raft paths.)",
	"", //cUNDEF
	"A Bracelet is not needed to push this combo, but it can't be pushed until the enemies are cleared from the screen.",
	"A Bracelet is needed to push this combo. The screen's Undercombo will appear beneath it when it is pushed aside.",
	"A Bracelet is needed to push this combo, and it can't be pushed until the enemies are cleared from the screen.",
	"If the 'Statues Shoot Fire' Screen Data flag is checked, an invisible fireball shooting enemy is spawned on this combo.",
	"If the 'Statues Shoot Fire' Screen Data flag is checked, an invisible fireball shooting enemy is spawned on this combo.",
	"The Hero's movement speed is reduced while they walk on this combo. Enemies will not be affected.",
	"While the Hero is standing on top of this, they will be moved upward at 2 pixels per 3 frames (or some dir at a custom-set speed), until they collide with a solid combo.",
	"While the Hero is standing on top of this, they will be moved downward at 2 pixels per 3 frames (or some dir at a custom-set speed), until they collide with a solid combo.",
	"While the Hero is standing on top of this, they will be moved leftward at 2 pixels per 3 frames (or some dir at a custom-set speed), until they collide with a solid combo.",
	"While the Hero is standing on top of this, they will be moved rightward at 2 pixels per 3 frames (or some dir at a custom-set speed), until they collide with a solid combo.",
	"The Hero is warped via Tile Warp A if they swim on this combo. Otherwise, this is identical to Water.",
	"The Hero is warped via Tile Warp A if they dive on this combo. Otherwise, this is identical to Water.",
	"If this combo is solid, the Ladder and Hookshot can be used to cross over it. It only permits the Ladder if its on Layer 0.",
	"This activates Screen Secrets when the bottom half of this combo is stepped on, but it does not set the screen's 'Secret' Screen State.",
	"This activates Screen Secrets when the bottom half of this combo is stepped on, and sets the screen's 'Secret' Screen State, making the secrets permanent.",
	"", // Unused
	"When stabbed or slashed with a Sword, this combo changes into the screen's Undercombo.",
	"Identical to Slash, but an item from Item Drop Set 12 is created when this combo is slashed.",
	"A Bracelet with a Push Combo Level of 2 is needed to push this combo. Otherwise, this is identical to Push (Heavy).",
	"A Bracelet with a Push Combo Level of 2 is needed to push this combo. Otherwise, this is identical to Push (Heavy, Wait).",
	"When hit by a Hammer, this combo changes into the next combo in the list.",
	"If this combo is struck by the Hookshot, the Hero is pulled towards the combo.",
	"", //cHSBRIDGE (Unimplemented)
	// Damage Combos
	"",
	"",
	"",
	"",
	// Anyway...
	"If the 'Statues Shoot Fire' Screen Data flag is checked, an invisible fireball shooting enemy is spawned on this combo.",
	"This flag is obsolete. It behaves identically to Combo Flag 32, Trap (Horizontal, Line of Sight).",
	"This flag is obsolete. It behaves identically to Combo Flag 33, Trap (Vertical, Line of Sight).",
	"This flag is obsolete. It behaves identically to Combo Flag 34, Trap (4-Way, Line of Sight).",
	"This flag is obsolete. It behaves identically to Combo Flag 35, Trap (Horizontal, Constant).",
	"This flag is obsolete. It behaves identically to Combo Flag 36, Trap (Vertical Constant).",
	"The Hero is warped via Tile Warp A if they touch any part of this combo, but their on-screen position remains the same. Ground enemies can't enter.",
	"If this combo is solid, the Hookshot can be used to cross over it.",
	"This combo's tile is drawn between layers 3 and 4 if it is placed on layer 0.",
	"Flying enemies (Keese, Peahats, Moldorms, Patras, Fairys, Digdogger, Manhandla, Ghinis, Gleeok heads) can't fly over or appear on this combo.",
	"Wand magic and enemy magic that hits  this combo is reflected 180 degrees, and becomes 'reflected magic'.",
	"Wand magic and enemy magic that hits  this combo is reflected 90 degrees, and become 'reflected magic'.",
	"Wand magic and enemy magic that hits  this combo is reflected 90 degrees, and become 'reflected magic'.",
	"Wand magic and enemy magic that hits  this combo is duplicated twice, causing three shots to be fired in three directions.",
	"Wand magic and enemy magic that hits  this combo is duplicated thrice, causing four shots to be fired from each direction.",
	"Wand magic and enemy magic that hits this combo is destroyed.",
	"The Hero marches up into this combo and is warped via Tile Warp A if they step on this. The combo's tile will be drawn above the Hero during this animation.",
	"The combo's tile changes depending on the Hero's position relative to the combo. It uses eight tiles per animation frame.",
	"Identical to Eyeball (8-Way A), but the angles at which the tile will change are offset by 22.5 degrees (pi/8 radians).",
	"Tektites cannot jump through or appear on this combo.",
	"Identical to Slash->Item, but when it is slashed, Bush Leaves sprites are drawn and the 'Tall Grass slashed' sound plays.",
	"Identical to Slash->Item, but when it is slashed, Flower Clippings sprites are drawn and the 'Tall Grass slashed' sound plays.",
	"Identical to Slash->Item, but when it is slashed, Grass Clippings sprites are drawn and the 'Tall Grass slashed' sound plays.",
	"Ripples sprites are drawn on the Hero when they walk on this combo. Also, Quake Hammer pounds are nullified by this combo.",
	"",
	"Identical to Lock Block, but if any other Lock Blocks are opened on the same screen, this changes to the next combo.",
	"",
	"Identical to Lock Block (Boss), but if any other Boss Lock Blocks are opened on the same screen, this changes to the next combo.",
	"If this combo is solid, the Ladder can be used to cross over it. Only works on layer 0.",
	"",
	//Chests
	"", "", "", "", "", "",
	"If the Hero touches this, the Screen States are cleared, and the Hero is re-warped back into the screen, effectively resetting the screen entirely.",
	"Press the 'Start' button when the Hero is standing on the bottom of this combo, and the Save menu appears. Best used with the Save Point->Continue Here Screen Flag.",
	"Identical to Save Point, but the Quit option is also available in the menu.",
	"The Hero marches down into this combo and is warped via Tile Warp B if they step on this. The combo's tile will be drawn above the Hero during this animation.",
	"The Hero marches down into this combo and is warped via Tile Warp C if they step on this. The combo's tile will be drawn above the Hero during this animation.",
	"The Hero marches down into this combo and is warped via Tile Warp D if they step on this. The combo's tile will be drawn above the Hero during this animation.",
	"The Hero is warped via Tile Warp B if they step on the bottom half of this combo.",
	"The Hero is warped via Tile Warp C if they step on the bottom half of this combo.",
	"The Hero is warped via Tile Warp D if they step on the bottom half of this combo.",
	"The Hero is warped via Tile Warp B if they touch any part of this combo, but their on-screen position remains the same. Ground enemies can't enter.",
	"The Hero is warped via Tile Warp C if they touch any part of this combo, but their on-screen position remains the same. Ground enemies can't enter.",
	"The Hero is warped via Tile Warp D if they touch any part of this combo, but their on-screen position remains the same. Ground enemies can't enter.",
	"The Hero marches up into this combo and is warped via Tile Warp B if they step on this. The combo's tile will be drawn above the Hero during this animation.",
	"The Hero marches up into this combo and is warped via Tile Warp C if they step on this. The combo's tile will be drawn above the Hero during this animation.",
	"The Hero marches up into this combo and is warped via Tile Warp D if they step on this. The combo's tile will be drawn above the Hero during this animation.",
	"The Hero is warped via Tile Warp B if they swim on this combo. Otherwise, this is identical to Water.",
	"The Hero is warped via Tile Warp C if they swim on this combo. Otherwise, this is identical to Water.",
	"The Hero is warped via Tile Warp D if they swim on this combo. Otherwise, this is identical to Water.",
	"The Hero is warped via Tile Warp B if they dive on this combo. Otherwise, this is identical to Water.",
	"The Hero is warped via Tile Warp C if they dive on this combo. Otherwise, this is identical to Water.",
	"The Hero is warped via Tile Warp D if they dive on this combo. Otherwise, this is identical to Water.",
	"Identical to Stairs [A], but the Tile Warp used (A, B, C, or D) is chosen at random. Use this only in screens where all four Tile Warps are defined.",
	"Identical to Direct Warp [A], but the Tile Warp used (A, B, C, or D) is chosen at random. Use this only in screens where all four Tile Warps are defined.",
	"As soon as this combo appears on the screen, Side Warp A is activated. This is best used with secret combos or combo cycling.",
	"As soon as this combo appears on the screen, Side Warp B is activated. This is best used with secret combos or combo cycling.",
	"As soon as this combo appears on the screen, Side Warp C is activated. This is best used with secret combos or combo cycling.",
	"As soon as this combo appears on the screen, Side Warp D is activated. This is best used with secret combos or combo cycling.",
	"Identical to Auto Side Warp [A], but the Side Warp used (A, B, C, or D) is chosen at random. Use this only in screens where all four Side Warps are defined.",
	"Identical to Stairs [A], but the Hero will be warped as soon as they touch the edge of this combo.",
	"Identical to Stairs [B], but the Hero will be warped as soon as they touch the edge of this combo.",
	"Identical to Stairs [C], but the Hero will be warped as soon as they touch the edge of this combo.",
	"Identical to Stairs [D], but the Hero will be warped as soon as they touch the edge of this combo.",
	"Identical to Stairs [Random], but the Hero will be warped as soon as they touch the edge of this combo.",
	"Identical to Step->Secrets (Temporary), but Screen Secrets are activated as soon as the Hero touches the edge of this combo.",
	"Identical to Step->Secrets (Permanent), but Screen Secrets are activated as soon as the Hero touches the edge of this combo.",
	"When the Hero steps on this combo, it will change into the next combo in the list.",
	"Identical to Step->Next, but if other instances of this particular combo are stepped on, this also changes to the next combo in the list.",
	"When the Hero steps on this combo, each of the Step->Next combos on screen will change to the next combo after them in the list.",
	"When the Hero steps on a Step->Next (All) type combo, this will change into the next combo in the list.",
	"Enemies cannot enter or appear on this combo.",
	"Level 1 Hero arrows that hit this combo are destroyed. Enemy arrows are unaffected.",
	"Level 1 or 2 Hero arrows that hit this combo are destroyed. Enemy arrows are unaffected.",
	"All Hero arrows that hit this combo are destroyed. Enemy arrows are unaffected.",
	"Level 1 Hero boomerangs bounce off this combo. Enemy boomerangs are unaffected.",
	"Level 1 or 2 Hero boomerangs bounce off this combo. Enemy boomerangs are unaffected.",
	"All Hero boomerangs bounce off this combo. Enemy boomerangs are unaffected.",
	"The Hero's sword beams or enemy sword beams that hit this combo are destroyed.",
	"All weapons that hit this combo are either destroyed, or bounce off.",
	"Enemy fireballs and reflected fireballs that hit this combo are destroyed.",
	// More damage
	"", "", "",
	"", //Unused
	"A Spinning Tile immediately appears on this combo, using the combo's tile to determine its sprite. The combo then changes to the next in the list.",
	"", // Unused
	"While this combo is on the screen, all action is frozen, except for FFC animation and all scripts. Best used in conjunction with Changer FFCs or scripts.",
	"While this combo is on the screen, FFCs and FFC scripts will be frozen. Best used in conjunction with combo cycling, screen secrets or global scripts.",
	"Enemies that don't fly or jump cannot enter or appear on this combo.",
	"Identical to Slash, but instead of changing into the Undercombo when slashed, this changes to the next combo in the list.",
	"Identical to Slash (Item), but instead of changing into the Undercombo when slashed, this changes to the next combo in the list.",
	"Identical to Bush, but instead of changing into the Undercombo when slashed, this changes to the next combo in the list.",
	// Continuous variation
	"Identical to Slash, but if slashing this combo changes it to another slash-affected combo, then that combo will also change.",
	"Identical to Slash->Item, but if slashing this combo changes it to another slash-affected combo, then that combo will also change.",
	"Identical to Bush, but if slashing this combo changes it to another slash-affected combo, then that combo will also change.",
	"Identical to Flowers, but if slashing this combo changes it to another slash-affected combo, then that combo will also change.",
	"Identical to Tall Grass, but if slashing this combo changes it to another slash-affected combo, then that combo will also change.",
	"Identical to Slash->Next, but if slashing this combo changes it to another slash-affected combo, then that combo will also change.",
	"Identical to Slash->Next (Item), but if slashing this combo changes it to another slash-affected combo, then that combo will also change.",
	"Identical to Bush->Next, but if slashing this combo changes it to another slash-affected combo, then that combo will also change.",
	"Identical to Eyeball (8-Way A), but only the four cardinal directions/sets of tiles are used (up, down, left and right, respectively).",
	"Identical to Tall Grass, but instead of changing into the Undercombo when slashed, this changes to the next combo in the list.",
	// Script types
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.", //1
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.",
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.",
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.",
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.", //5
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.",
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.",
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.",
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.",
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.", //10
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.",
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.",
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.",
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.",
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.", //15
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.",
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.",
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.",
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.",
	"This type has no built-in effect, but can be given special significance with ZASM or ZScript.", //20
	//Generic
	"Generic combos can be configured to do a wide variety of things based on attributes.",
	"Pitfall combos act as either bottomless pits or warps, including a fall animation.",
	"Step->Effects combos can cause SFX, and also act like a landmine, spawning a weapon.",
	"Bridge combos can be used to block combos under them from having an effect.",
	"",
	"",
	"Switchblock combos change based on switch states toggled by Switch combos. They can also change"
		" the combo at the same position on any layer.",
	""
};

const char *g_trigflag_names[185] =
{
    "Sword",
    "Sword Beam",
    "Boomerang",
    "Bomb Boom",
    "Super Bomb Boom",
    "Placed Bomb",
    "Placed Super Bomb",
    "Arrow",
    "Fire",
    "Whistle",
    "Bait",
    "Wand",
    "Magic",
    "Wind",
    "Refl. Magic",
    "Refl. Fireball",
    "Refl. Rock",
    "Hammer",
    "Reset Anim",
    "Invert Prox. Req",
    "Btn: Top",
    "Btn: Bottom",
    "Btn: Left",
    "Btn: Right",
    "Invertminmax",
    "Step->",
    "Step-> (Sensitive)",
    "Shutter->",
    "->ComboType Effects",
    "Onlygentrig",
    "Kill Triggering Weapon",
    "Fireball",
    "Hookshot",
    "Sparkle",
    "Byrna",
    "Refl. Beam",
    "Stomp",
    "Custom Weapon 1",
    "Custom Weapon 2",
    "Custom Weapon 3",
    "Custom Weapon 4",
    "Custom Weapon 5",
    "Custom Weapon 6",
    "Custom Weapon 7",
    "Custom Weapon 8",
    "Custom Weapon 9",
    "Custom Weapon 10",
    "Always Triggered",
    "Triggers Secrets",
    "Invert Item Req",
    "Consume Item Req",
    "Require >=",
    "Require <",
    "Consume Amount",
    "Consume w/o trig",
    "Light On->",
    "Light Off->",
    "Push->",
    "Lens On->",
    "Lens Off->",
    "Arrow",
    "Boomerang",
    "Sword",
    "Rock",
    "Custom Weapon 1",
    "Custom Weapon 2",
    "Custom Weapon 3",
    "Custom Weapon 4",
    "Custom Weapon 5",
    "Custom Weapon 6",
    "Custom Weapon 7",
    "Custom Weapon 8",
    "Custom Weapon 9",
    "Custom Weapon 10",
    "Magic",
    "Bomb Blast",
    "SBomb Blast",
    "Lit Bomb",
    "Lit SBomb",
    "Fire Trail",
    "Flame",
    "Wind",
    "Flame 2",
    "Spawns Special Item",
    "Trigger ExState after item pickup",
    "Trigger ExState after enemy kill",
    "Spawned Item auto-collects",
    "Enemies->",
    "Secrets->",
    "Thrown",
    "Quake Hammer",
    "S. Quake Hammer",
    "Normal Fire",
    "Strong Fire",
    "Magic Fire",
    "Divine Fire",
    "LevelState->",
    "->LevelState",
    "GlobalState->",
    "->GlobalState",
    "Kill Enemies",
    "Clear Enemies",
    "Clear LWeapons",
    "Clear EWeapons",
    "Ignite Weapon (Normal)",
    "Ignite Weapon (Strong)",
    "Ignite Weapon (Magic)",
    "Ignite Weapon (Divine)",
    "Weapon Separate Triggers",
    "Contributes To TrigGroup",
    "TrigGroup Less->",
    "TrigGroup Greater->",
    "Pushed->",
    "Dive->",
    "Dive-> (Sensitive)",
    "Refl. Arrow",
    "Refl. Fire",
    "Refl. Fire 2",
    "Toggle Darkness",
    "Req. Darkness",
    "Req. No Darkness",
    "Require All",
    "Require Not All",
    "->Set",
    "->Unset",
    "->ClearTint",
    "Only Grounded Weapons",
    "Apply Discount",
    "Screen Loads->",
    "Clear ExState on trigger",
    "Clear ExDoor on trigger",
    "Req. Player Standing",
    "Req. Player Not Standing",
    "Invert Z Req",
    "...Gradual",
    "...Is Percent",
    "Copycat doesn't trigger this",
    "Land Here->",
    "Land Anywhere->",
    "Set X (Abs)",
    "Set X (Rel)",
    "Set Y (Abs)",
    "Set Y (Rel)",
    "Set Z (Abs)",
    "ComboType Causes->",
    "Req Jump >=",
    "Req Jump <=",
    "Req X >=",
    "Req X <=",
    "...Relative",
    "Req Y >=",
    "Req Y <=",
    "...Relative",
    "Force Vx:",
    "Force Vy:",
    "...Unignite Instead",
    "Cancel Further Triggers",
    "Set Gravity",
    "Revert Gravity",
    "Block (Same Layer)->",
    "Block (Any Layer)->",
    "Req. On Screen",
    "Req. Off Screen",
    "Loop",
    "No Restart",
    "Kill",
    "Absolute",
    "Absolute",
    "Swim->",
    "Swim-> (Sensitive)",
    "Respawn Here",
    "Reset Respawn",
    "Screen Unloads->",
    "Use S. Hitbox",
    "Square Area",
    "Partial Consume",
    "Map View Only",
    "Not Map View",
    "Cause",
    "Require",
    "Require Not",
    "->Set",
    "->Unset",
    "->Large Copycat",
    "Large Copycat->"
};

// fmt::format's "{}" placeholders, filled in order
std::string zq_arg(const char *s) { return s ? s : ""; }
std::string zq_arg(int v) { return std::to_string(v); }

template<typename... A>
std::string zq_fmt(const char *f, A... a)
{
    const std::string args[] = { zq_arg(a)... };
    std::string out;
    size_t n = 0;

    for(const char *p = f; *p; ++p)
    {
        if(p[0] == '{' && p[1] == '}')
        {
            if(n < sizeof(args) / sizeof(args[0]))
                out += args[n++];
            ++p;
        }
        else
            out += *p;
    }

    return out;
}

bool zq255_rule(int id)
{
    const size_t b = (size_t)id / 8;
    return b < zq255.rules.size() && (zq255.rules[b] & (1 << (id % 8))) != 0;
}

// Inside up30 so its names hide 2.53's of the same spelling (cSLOPE isn't 2.53's, cNONE is both).
void combo_labels(const Q30Combo &x, std::string *l_attribute, std::string *h_attribute, std::string *l_flag,
                  std::string *h_flag)
{
#define FL(fl) (x.usrflags & (fl))
    for(size_t q = 0; q < 24; ++q)
    {
        l_attribute[q] = "Attributes[" + std::to_string(q) + "]:";
        h_attribute[q].clear();
        if(q > 15) continue;
        l_flag[q] = "Flags[" + std::to_string(q) + "]";
        h_flag[q].clear();
    }
    switch(x.type) //Label names
	{
		case cSTAIR: case cSTAIRB: case cSTAIRC: case cSTAIRD: case cSTAIRR:
		case cSWIMWARP: case cSWIMWARPB: case cSWIMWARPC: case cSWIMWARPD:
		case cDIVEWARP: case cDIVEWARPB: case cDIVEWARPC: case cDIVEWARPD:
		case cPIT: case cPITB: case cPITC: case cPITD: case cPITR:
		case cAWARPA: case cAWARPB: case cAWARPC: case cAWARPD: case cAWARPR:
		case cSWARPA: case cSWARPB: case cSWARPC: case cSWARPD: case cSWARPR:
		{
			l_attribute[8] = "Sound:";
			h_attribute[8] = "SFX to play during the warp";
			break;
		}
		case cSLOPE:
		{
			l_attribute[16] = "X Offset 1:";
			h_attribute[16] = "X Offset of the starting point of the slope line.";
			l_attribute[17] = "Y Offset 1:";
			h_attribute[17] = "Y Offset of the starting point of the slope line.";
			l_attribute[18] = "X Offset 2:";
			h_attribute[18] = "X Offset of the ending point of the slope line.";
			l_attribute[19] = "Y Offset 2:";
			h_attribute[19] = "Y Offset of the ending point of the slope line.";
			l_attribute[0] = "Slipperiness:";
			h_attribute[0] = "Pixels per frame to slide down the slope in sideview.";
			l_flag[0] = "Is Stairs";
			h_flag[0] = "You must hold up or be in the air for this slope to be active from the top. Bottom is always passthrough.";
			l_flag[1] = "Pass through bottom";
			h_flag[1] = "Disables the slope's collision from below, allowing sprites to pass through from the bottom.";
			l_flag[2] = "Pass through top";
			h_flag[2] = "Disables the slope's collision from above, allowing sprites to pass through from the top.";
			l_flag[3] = "Pass through left side";
			h_flag[3] = "Disables the slope's collision from the left, allowing sprites to pass through from the left side."
				"\nThis flag is not needed if you have either pass through top or pass through bottom checked.";
			l_flag[4] = "Pass through right side";
			h_flag[4] = "Disables the slope's collision from the right, allowing sprites to pass through from the right side."
				"\nThis flag is not needed if you have either pass through top or pass through bottom checked.";
			l_flag[5] = "Can fall through";
			h_flag[5] = "Treats the slope like a Sideview Platform Flag, allowing you to fall through it depending on quest rules.";
			break;
		}
		case cTRIGNOFLAG: case cSTRIGNOFLAG:
		case cTRIGFLAG: case cSTRIGFLAG:
		{
			l_attribute[8] = "Sound:";
			h_attribute[8] = "SFX to play when activated";
			break;
		}
		case cSTEP: case cSTEPSAME: case cSTEPALL:
		{
			l_flag[0] = "Heavy";
			h_flag[0] = "Requires Heavy Boots to activate";
			l_attribute[8] = "Sound:";
			h_attribute[8] = "SFX to play when activated";
			l_attribute[9] = "Req. Item";
			h_attribute[9] = "Item ID that must be owned in order to activate. If '0', no item is required.";
			break;
		}
		case cWATER:
		{
			l_flag[0] = "Is Lava";
			h_flag[0] = "If a liquid is Lava, it uses a different drowning sprite, and only flippers with the"
				" 'Can Swim In Lava' flag set will apply.";
			l_flag[1] = "Modify HP (Passive)";
			h_flag[1] = "If checked, the Hero's HP will change over time while in the liquid"
				" (either healing or damaging).";
			l_flag[2] = "Solid is Land";
			h_flag[2] = "Solid areas of the combo are treated as non-solid land";
			l_flag[3] = "Solid is Shallow Liquid";
			h_flag[3] = "Solid areas of the combo are treated as non-solid Shallow Liquid combo";
			l_attribute[0] = "Drown Damage:";
			h_attribute[0] = "The amount of damage dealt when drowning, in HP points. If negative, drowning will heal the Hero.";
			l_attribute[3] = "Inv. Frames";
			h_attribute[3] = "The duration, in frames, the player will be invincible after drowning."
				" If 0, the default of 48 will be used instead. If < 0, 0 will be used instead.";
			l_attribute[4] = "Stun Frames";
			h_attribute[4] = "The duration, in frames, the player will be stunned after drowning.";
			l_attribute[8] = "Flipper Level:";
			h_attribute[8] = "The minimum level flippers required to swim in the water. Flippers of lower level will have no effect.";
			l_attribute[12] = "Drown SFX:";
			h_attribute[12] = "The SFX played when drowning";
			l_attribute[13] = "Splash SFX:";
			h_attribute[13] = "SFX ID to play when walking on the liquid/shallow liquid portion of this combo. Only plays if walking and not swimming.";
			l_attribute[14] = "Ripple Sprite:";
			h_attribute[14] = "Sprite ID to show when walking on the liquid/shallow liquid portion of this combo. Only plays if walking and not swimming.";
			if(FL(cflag2)) //Modify HP
			{
				l_flag[4] = "Rings affect HP Mod";
				h_flag[4] = "Ring items defense reduces damage from HP Mod";
				l_flag[5] = "Mod SFX only on HP change";
				h_flag[5] = "Only play the HP Mod SFX when HP actually changes";
				l_flag[6] = "Damage causes hit anim";
				h_flag[6] = "HP Mod Damage activates the hit animation and invincibility frames";
				l_attribute[1] = "HP Modification:";
				h_attribute[1] = "How much HP should be modified by (negative for damage)";
				l_attribute[2] = "HP Mod SFX:";
				h_attribute[2] = "What SFX should play when HP is modified";
				l_attribute[9] = "HP Delay:";
				h_attribute[9] = "The number of frames between HP modifications";
				l_attribute[10] = "Req Itemclass:";
				h_attribute[10] = "If non-zero, an itemclass number which, if owned, will prevent HP modification.";
				l_attribute[11] = "Req Itemlevel:";
				h_attribute[11] = "A minimum item level to go with 'Req Itemclass'.";
			}
			break;
		}
		case cSHALLOWWATER:
		{
			l_flag[1] = "Modify HP (Passive)";
			h_flag[1] = "If checked, the Hero's HP will change over time while in the liquid"
				" (either healing or damaging).";
			if (zq255_rule(50 * 8 + 1))
			{
				l_attribute[8] = "Splash Sound";
				h_attribute[8] = "SFX ID to play when stepping in the shallow liquid";
			}
			else
			{
				l_attribute[13] = "Splash Sound";
				h_attribute[13] = "SFX ID to play when stepping in the shallow liquid";
			}
			l_attribute[14] = "Ripple Sprite:";
			h_attribute[14] = "Sprite ID to show when stepping in the shallow liquid";
			if(FL(cflag2)) //Modify HP
			{
				l_flag[4] = "Rings affect HP Mod";
				h_flag[4] = "Ring items defense reduces damage from HP Mod";
				l_flag[5] = "Mod SFX only on HP change";
				h_flag[5] = "Only play the HP Mod SFX when HP actually changes";
				l_flag[6] = "Damage causes hit anim";
				h_flag[6] = "HP Mod Damage activates the hit animation and invincibility frames";
				l_attribute[1] = "HP Modification:";
				h_attribute[1] = "How much HP should be modified by (negative for damage)";
				l_attribute[2] = "HP Mod SFX:";
				h_attribute[2] = "What SFX should play when HP is modified";
				l_attribute[9] = "HP Delay:";
				h_attribute[9] = "The number of frames between HP modifications";
				l_attribute[10] = "Req Itemclass:";
				h_attribute[10] = "If non-zero, an itemclass number which, if owned, will prevent HP modification.";
				l_attribute[11] = "Req Itemlevel:";
				h_attribute[11] = "A minimum item level to go with 'Req Itemclass'.";
			}
			break;
		}
		case cARMOS:
		{
			l_flag[0] = "Specify";
			h_flag[0] = "If checked, attributes are used to specify enemy IDs. Otherwise, the lowest"
				" enemy ID with the armos flag checked will be spawned.";
			if(FL(cflag1))
			{
				l_flag[1] = "Random";
				h_flag[1] = "Randomly choose between two enemy IDs (50/50)";
				if(FL(cflag2))
				{
					l_attribute[8] = "Enemy 1:";
					h_attribute[8] = "The first enemy ID, 50% chance of being spawned";
					l_attribute[9] = "Enemy 2:";
					h_attribute[9] = "The second enemy ID, 50% chance of being spawned";
				}
				else
				{
					l_attribute[8] = "Enemy:";
					h_attribute[8] = "The enemy ID to be spawned";
				}
			}
			l_flag[3] = "Use Large Combo";
			h_flag[3] = "Use the connected combos via the 'Large Combo' general tab feature as connected statues,"
				" spawning the enemy with it's hitbox centered on the center of the 'Large Combo'.";
			if (!FL(cflag4))
			{
				l_flag[2] = "Use Large Enemy";
				h_flag[2] = "If the specified enemy is larger than 1x1 tile, attempt to use armos combos that take up its' size";
			}
			break;
		}
		case cBSGRAVE:
			// fall through
		case cGRAVE:
		{
			l_flag[0] = "Specify";
			h_flag[0] = "If checked, attributes are used to specify enemy IDs. Otherwise, the lowest"
				" enemy ID with the grave flag checked will be spawned.";
			if(FL(cflag1))
			{
				l_flag[1] = "Random";
				h_flag[1] = "Randomly choose between two enemy IDs (50/50)";
				if(FL(cflag2))
				{
					l_attribute[8] = "Enemy 1:";
					h_attribute[8] = "The first enemy ID, 50% chance of being spawned";
					l_attribute[9] = "Enemy 2:";
					h_attribute[9] = "The second enemy ID, 50% chance of being spawned";
				}
				else
				{
					l_attribute[8] = "Enemy:";
					h_attribute[8] = "The enemy ID to be spawned";
				}
			}
			l_flag[3] = "Use Large Combo";
			h_flag[3] = "Use the connected combos via the 'Large Combo' general tab feature as connected graves,"
				" spawning the enemy with it's hitbox centered on the center of the 'Large Combo'.";
			break;
		}
		case cCVUP: case cCVDOWN: case cCVLEFT: case cCVRIGHT:
		{
			l_flag[0] = "Stunned while moving";
			h_flag[0] = "While the conveyor is moving the Hero, they are 'stunned'."
				" Weapons that are affected by conveyors will have their step speed set to 0.";
			l_flag[1] = "Custom Speed";
			h_flag[1] = zq_fmt("Uses a custom speed/direction via attributes. If disabled, moves at 2 pixels every 3 frames in the {}ward direction.", dirstr[x.type-cCVUP]);
			l_flag[2] = "Force Dir";
			h_flag[2] = "Forces the Hero to face in the conveyor's direction";
			l_flag[4] = "Heavy Boots Disable";
			h_flag[4] = "If the Hero has boots with the 'heavy' flag, the conveyor will not push them.";
			l_flag[5] = "Force Walk";
			h_flag[5] = "The Hero will walk the speed/dir of the conveyor, unable to walk against it. Requires Newer Hero Movement." + std::string();
			l_flag[6] = "Affects in air";
			h_flag[6] = "Moves sprites that are in the z-axis";
			if(FL(cflag2)) //Custom speed
			{
				l_attribute[0] = "X Speed:";
				h_attribute[0] = "Pixels moved in the X direction per rate frames.\nNegative is Left, Positive is Right";
				l_attribute[1] = "Y Speed:";
				h_attribute[1] = "Pixels moved in the Y direction per rate frames\nNegative is Up, Positive is Down";
				l_attribute[8] = "Rate:";
				h_attribute[8] = "Every this many frames the conveyor moves by the set speeds. If set to 0, acts as if set to 1.";
			}
			if(!FL(cflag6)) //Force Walk
			{
				l_flag[3] = "Smart Corners";
				h_flag[3] = "Uses the half-combo-grid to help avoid getting stuck on corners";
			}
			break;
		}
		case cTALLGRASS: case cTALLGRASSTOUCHY: case cTALLGRASSNEXT:
		{
			l_flag[0] = "Decoration Sprite";
			h_flag[0] = "Spawn a decoration when slashed";
			if(FL(cflag1))
			{
				l_flag[9] = "Use Clippings Sprite";
				h_flag[9] = "Use a system clipping sprite instead of a Sprite Data sprite";
				if(FL(cflag10))
				{
					l_attribute[8] = "Clipping Sprite:";
					h_attribute[8] = "0 and 1 = Bush Leaves, 2 = Flowers, 3 = Grass";
				}
				else
				{
					l_attribute[8] = "Sprite:";
					h_attribute[8] = "Sprite Data sprite ID to display as a clipping";
				}
			}
			l_flag[1] = "Set Dropset";
			h_flag[1] = "Allows specifying the dropset to use as an attribute";
			l_flag[2] = "Custom Slash SFX";
			h_flag[2] = "Specify a custom slash SFX";
			l_attribute[11] = "Walking Sound:";
			h_attribute[11] = "The SFX to play when the Hero walks through this combo. If 0, no sound is played.";
			l_attribute[14] = "Grass Sprite";
			h_attribute[14] = "The sprite to show when the Hero walks through this combo.";
			if(FL(cflag2))
			{
				l_flag[10] = "Specific Item";
				h_flag[10] = "Drop a specific item instead of an item from a dropset";
				if(FL(cflag11))
				{
					l_attribute[9] = "Item:";
					h_attribute[9] = "The item ID to drop";
				}
				else
				{
					l_attribute[9] = "Dropset:";
					h_attribute[9] = "The dropset to select a drop item from";
				}
			}
			if(FL(cflag3))
			{
				l_attribute[10] = "Slash Sound:";
				h_attribute[10] = "The SFX to play when slashed";
			}
			break;
		}
		case cBUSH: case cBUSHTOUCHY: case cFLOWERS: case cSLASHNEXTTOUCHY:
		case cSLASHITEM: case cSLASHNEXTITEMTOUCHY:
		case cSLASHNEXTITEM: case cBUSHNEXT: case cSLASHITEMTOUCHY:
		case cFLOWERSTOUCHY: case cBUSHNEXTTOUCHY:
		{
			l_flag[0] = "Decoration Sprite";
			h_flag[0] = "Spawn a decoration when slashed";
			if(FL(cflag1))
			{
				l_flag[9] = "Use Clippings Sprite";
				h_flag[9] = "Use a system clipping sprite instead of a Sprite Data sprite";
				if(FL(cflag10))
				{
					l_attribute[8] = "Clipping Sprite:";
					h_attribute[8] = "0 and 1 = Bush Leaves, 2 = Flowers, 3 = Grass";
				}
				else
				{
					l_attribute[8] = "Sprite:";
					h_attribute[8] = "Sprite Data sprite ID to display as a clipping";
				}
			}
			l_flag[1] = "Set Dropset";
			h_flag[1] = "Allows specifying the dropset to use as an attribute";
			l_flag[2] = "Custom SFX";
			h_flag[2] = "Specify a custom slash SFX";
			if(FL(cflag2))
			{
				l_flag[10] = "Specific Item";
				h_flag[10] = "Drop a specific item instead of an item from a dropset";
				if(FL(cflag11))
				{
					l_attribute[9] = "Item:";
					h_attribute[9] = "The item ID to drop";
				}
				else
				{
					l_attribute[9] = "Dropset:";
					h_attribute[9] = "The dropset to select a drop item from";
				}
			}
			if(FL(cflag3))
			{
				l_attribute[10] = "Slash Sound:";
				h_attribute[10] = "The SFX to play when slashed";
			}
			break;
		}
		case cDAMAGE1: case cDAMAGE2: case cDAMAGE3: case cDAMAGE4:
		case cDAMAGE5: case cDAMAGE6: case cDAMAGE7:
		{
			l_flag[0] = "Custom Damage";
			h_flag[0] = "Uses custom damage amount";
			l_flag[1] = "No Knockback";
			h_flag[1] = "Does not knock the Hero back when damaging them if checked. Otherwise, knocks the Hero in the direction"
				" opposite the one they face.";
			if(FL(cflag1))
			{
				l_attribute[0] = "Damage:";
				h_attribute[0] = "The amount of damage, in HP, to deal. Negative amounts heal."
					"\nFor healing, the lowest healing amount combo you are standing on takes effect."
					"\nFor damage, the greatest amount takes priority unless 'Quest->Options->Combos->Lesser Damage Combos Take Priority' is checked.";
			}
			break;
		}
		case cLOCKBLOCK:
		{
			l_flag[0] = "Use Item";
			h_flag[0] = "Allow an item in your inventory to unlock the block";
			if(FL(cflag1))
			{
				l_flag[4] = "Eat Item";
				h_flag[4] = "Consume the required item instead of simply requiring its presence";
				if(FL(cflag5))
				{
					l_attribute[8] = "Consumed Item";
					h_attribute[8] = "The Item ID to open the lock block. Consumed.";
				}
				else
				{
					l_attribute[8] = "Held Item";
					h_attribute[8] = "The Item ID to open the lock block. Not consumed.";
				}
				l_flag[1] = "Require Item";
				h_flag[1] = "Only the required item can open this block (instead of ALSO allowing a key)";
			}
			if(!(FL(cflag1)&&FL(cflag2)))
			{
				l_attribute[0] = "Amount:";
				if(FL(cflag4))
					h_attribute[0] = "The amount of the arbitrary counter required to open this block";
				else
					h_attribute[0] = "The amount of keys required to open this block";
				l_flag[3] = "Counter";
				h_flag[3] = "If checked, uses an arbitrary counter instead of keys";
				if(FL(cflag4))
				{
					l_attribute[9] = "Counter:";
					h_attribute[9] = "The counter to use to open this block";
					l_flag[7] = "No Drain";
					h_flag[7] = "Requires the counter have the amount, but do not consume from it";
					if(!FL(cflag8))
					{
						l_flag[5] = "Thief";
						h_flag[5] = "Consumes from counter even if you don't have enough";
					}
				}
			}
		}
		// fall through
		case cBOSSLOCKBLOCK:
		{
			l_flag[8] = "Can't use from top";
			h_flag[8] = "Cannot be activated standing to the top side if checked";
			l_flag[9] = "Can't use from bottom";
			h_flag[9] = "Cannot be activated standing to the bottom side if checked";
			l_flag[10] = "Can't use from left";
			h_flag[10] = "Cannot be activated standing to the left side if checked";
			l_flag[11] = "Can't use from right";
			h_flag[11] = "Cannot be activated standing to the right side if checked";
			l_flag[12] = "Display prompt combo";
			h_flag[12] = "Displays a prompt combo when able to interact\n"
				"Must set: Combo, Xoffset, Yoffset, CSet";
			l_attribute[10] = "Button:";
			h_attribute[10] = "Sum all the buttons you want to be usable:\n(A=1, B=2, L=4, R=8, Ex1=16, Ex2=32, Ex3=64, Ex4=128)\n"
				"If no buttons are selected, walking into the block will activate it.";
			l_attribute[11] = "Unlock Sound:";
			h_attribute[11] = "The sound to play when unlocking the block";
			if(FL(cflag13))
			{
				l_attribute[1] = "Prompt Combo";
				h_attribute[1] = "Combo to display as a 'prompt'";
				l_attribute[2] = "Locked Prompt Combo";
				h_attribute[2] = "Combo to display as a 'prompt', if you are not currently able to"
					" open it. If 0, the normal prompt will be used instead.";
				l_attribute[16] = "Prompt Xoffset";
				h_attribute[16] = "X offset from Hero's position for the prompt to display at";
				l_attribute[17] = "Prompt Yoffset";
				h_attribute[17] = "Y offset from Hero's position for the prompt to display at";
				l_attribute[12] = "Prompt CSet";
				h_attribute[12] = "CSet to draw the prompt in";
			}
			
			l_attribute[3] = "Locked String:";
			h_attribute[3] = "If non-0, plays the specified string on attempting to open and failing.\n"
				"1+: Use specified string\n"
				"-1: Use screen string\n"
				"-2: Use screen catchall as string\n"
				"-10 to -17: Use Screen->D[0] to [7] as string";
		}
		// fall through
		case cLOCKBLOCK2:
		case cBOSSLOCKBLOCK2:
		{
			std::string ss_str;
			switch(x.type)
			{
				case cLOCKBLOCK: case cLOCKBLOCK2: ss_str = "Lockblock"; break;
				case cBOSSLOCKBLOCK: case cBOSSLOCKBLOCK2: ss_str = "Boss Lockblock"; break;
			}
			l_flag[15] = "Use ExtraState";
			h_flag[15] = "If checked, the 'extra state' specified in the attributes"
				" will be used instead of the usual '" + ss_str + "' screen state.";
			if(FL(cflag16))
			{
				l_attribute[13] = "ExtraState";
				h_attribute[13] = "Which ExtraState (0 to 31) to use instead of the usual '"
					+ ss_str + "' screen state.";
			}
			break;
		}
		case cLOCKEDCHEST:
		{
			l_flag[0] = "Use Item";
			h_flag[0] = "Allow an item in your inventory to unlock the chest";
			if(FL(cflag1))
			{
				l_flag[4] = "Eat Item";
				h_flag[4] = "Consume the required item instead of simply requiring its presence";
				if(FL(cflag5))
				{
					l_attribute[8] = "Consumed Item";
					h_attribute[8] = "The Item ID to open the chest. Consumed.";
				}
				else
				{
					l_attribute[8] = "Held Item";
					h_attribute[8] = "The Item ID to open the chest. Not consumed.";
				}
				l_flag[1] = "Require Item";
				h_flag[1] = "Only the required item can open this chest (instead of ALSO allowing a key)";
			}
			if(!(FL(cflag1)&&FL(cflag2)))
			{
				l_attribute[0] = "Amount:";
				if(FL(cflag4))
					h_attribute[0] = "The amount of the arbitrary counter required to open this chest";
				else
					h_attribute[0] = "The amount of keys required to open this chest";
				l_flag[3] = "Counter";
				h_flag[3] = "If checked, uses an arbitrary counter instead of keys";
				if(FL(cflag4))
				{
					l_attribute[9] = "Counter:";
					h_attribute[9] = "The counter to use to open this block";
					l_flag[7] = "No Drain";
					h_flag[7] = "Requires the counter have the amount, but do not consume from it";
					if(!FL(cflag8))
					{
						l_flag[5] = "Thief";
						h_flag[5] = "Consumes from counter even if you don't have enough";
					}
				}
			}
		}
		// fall through
		case cBOSSCHEST:
		{
			if(FL(cflag13)) //Prompt flag
			{
				l_attribute[2] = "Locked Prompt Combo";
				h_attribute[2] = "Combo to display as a 'prompt', if you are not currently able to"
					" open it. If 0, the normal prompt will be used instead.";
			}
			
			l_attribute[3] = "Locked String:";
			h_attribute[3] = "If non-0, plays the specified string on attempting to open and failing.\n"
				"1+: Use specified string\n"
				"-1: Use screen string\n"
				"-2: Use screen catchall as string\n"
				"-10 to -17: Use Screen->D[0] to [7] as string";
		}
		// fall through
		case cCHEST:
		{
			l_flag[8] = "Can't use from top";
			h_flag[8] = "Cannot be activated standing to the top side if checked";
			l_flag[9] = "Can't use from bottom";
			h_flag[9] = "Cannot be activated standing to the bottom side if checked";
			l_flag[10] = "Can't use from left";
			h_flag[10] = "Cannot be activated standing to the left side if checked";
			l_flag[11] = "Can't use from right";
			h_flag[11] = "Cannot be activated standing to the right side if checked";
			l_flag[12] = "Display prompt combo";
			h_flag[12] = "Displays a prompt combo when able to interact\n"
				"Must set: Combo, Xoffset, Yoffset, CSet";
			l_attribute[10] = "Button:";
			h_attribute[10] = "Sum all the buttons you want to be usable:\n(A=1, B=2, L=4, R=8, Ex1=16, Ex2=32, Ex3=64, Ex4=128)\n"
				"If no buttons are selected, walking into the chest will activate it.";
			l_attribute[11] = "Open Sound:";
			h_attribute[11] = "The sound to play when opening the chest";
			if(FL(cflag13))
			{
				l_attribute[1] = "Prompt Combo";
				h_attribute[1] = "Combo to display as a 'prompt'";
				l_attribute[16] = "Prompt Xoffset";
				h_attribute[16] = "X offset from Hero's position for the prompt to display at";
				l_attribute[17] = "Prompt Yoffset";
				h_attribute[17] = "Y offset from Hero's position for the prompt to display at";
				l_attribute[12] = "Prompt CSet";
				h_attribute[12] = "CSet to draw the prompt in";
			}
			l_attribute[18] = "Contained Item";
			h_attribute[18] = zq_fmt("The item contained by the chest."
				"\n0-{}: Use specified item ID\n"
				"-1: Use screen special item (catchall)\n"
				"-10 to -17: Use Screen->D[0] to [7] as item ID", MAXITEMS-1);
			l_flag[6] = "Use SItem State";
			h_flag[6] = "Link contained item to 'Special Item' screen state.";
		}
		// fall through
		case cCHEST2:
		case cLOCKEDCHEST2:
		case cBOSSCHEST2:
		{
			std::string ss_str;
			switch(x.type)
			{
				case cCHEST: case cCHEST2: ss_str = "Chest"; break;
				case cLOCKEDCHEST: case cLOCKEDCHEST2: ss_str = "Locked Chest"; break;
				case cBOSSCHEST: case cBOSSCHEST2: ss_str = "Boss Chest"; break;
			}
			l_flag[15] = "Use ExtraState";
			h_flag[15] = "If checked, the 'extra state' specified in the attributes"
				" will be used instead of the usual '" + ss_str + "' screen state.";
			if(FL(cflag16))
			{
				l_attribute[13] = "ExtraState";
				h_attribute[13] = "Which ExtraState (0 to 31) to use instead of the usual '"
					+ ss_str + "' screen state.";
			}
			break;
		}
		case cSIGNPOST:
		{
			l_flag[8] = "Can't use from top";
			h_flag[8] = "Cannot be activated standing to the top side if checked";
			l_flag[9] = "Can't use from bottom";
			h_flag[9] = "Cannot be activated standing to the bottom side if checked";
			l_flag[10] = "Can't use from left";
			h_flag[10] = "Cannot be activated standing to the left side if checked";
			l_flag[11] = "Can't use from right";
			h_flag[11] = "Cannot be activated standing to the right side if checked";
			l_flag[12] = "Display prompt combo";
			h_flag[12] = "Displays a prompt combo when able to interact\n"
				"Must set: Combo, Xoffset, Yoffset, CSet";
			l_attribute[10] = "Button:";
			h_attribute[10] = "Sum all the buttons you want to be usable:\n(A=1, B=2, L=4, R=8, Ex1=16, Ex2=32, Ex3=64, Ex4=128)\n"
				"If no buttons are selected, walking into the signpost will activate it.";
			l_attribute[0] = "String:";
			h_attribute[0] = "1+: Use specified string\n"
				"-1: Use screen string\n"
				"-2: Use screen catchall as string\n"
				"-10 to -17: Use Screen->D[0] to [7] as string";
			if(FL(cflag13))
			{
				l_attribute[1] = "Prompt Combo";
				h_attribute[1] = "Combo to display as a 'prompt'";
				l_attribute[16] = "Prompt Xoffset";
				h_attribute[16] = "X offset from Hero's position for the prompt to display at";
				l_attribute[17] = "Prompt Yoffset";
				h_attribute[17] = "Y offset from Hero's position for the prompt to display at";
				l_attribute[12] = "Prompt CSet";
				h_attribute[12] = "CSet to draw the prompt in";
			}
			break;
		}
		case cBUTTONPROMPT:
		{
			l_attribute[0] = "Prompt Combo";
			h_attribute[0] = "Combo to display as a 'prompt'";
			l_attribute[16] = "Prompt Xoffset";
			h_attribute[16] = "X offset from Hero's position for the prompt to display at";
			l_attribute[17] = "Prompt Yoffset";
			h_attribute[17] = "Y offset from Hero's position for the prompt to display at";
			l_attribute[8] = "Prompt CSet";
			h_attribute[8] = "CSet to draw the prompt in";
			break;
		}
		case cCUSTOMBLOCK:
		{
			l_attribute[8] = "Block SFX";
			h_attribute[8] = "SFX to play when blocking a weapon";
			break;
		}
		case cSHOOTER:
		{
			l_attribute[8] = "Shot SFX:";
			h_attribute[8] = "SFX to play when shooting a weapon";
			l_attribute[9] = "Weapon Type:";
			h_attribute[9] = "The LWeapon or EWeapon ID to be shot";
			l_attribute[10] = "Sprite:";
			h_attribute[10] = "The sprite of the spawned weapon";
			//byte[3] : multishot shot count
			//byte[4] : unblockable
			//byte[5] : script
			l_attribute[14] = "Parent Item:";
			h_attribute[14] = "The item ID to use as the 'parent item' of the weapon. Only used for LWeapons. 0 = no parent."
				"\nThis affects various attributes of certain lweapons, such as a bomb's fuse.";
			//short[0],[1] : Rate
			l_attribute[18] = "Damage:";
			h_attribute[18] = "The damage of the spawned weapon";
			
			//bute[0] : Angle/Dir
			//bute[1] : Prox Limit
			//bute[3] : Multishot Spread
			l_attribute[2] = "Step Speed:";
			h_attribute[2] = "The speed of the weapon, in 100ths px/frame";
			
			l_flag[0] = "Angular";
			h_flag[0] = "Specify an angle (in degrees) instead of a direction (8dir)";
			l_flag[1] = "Variable Rate";
			h_flag[1] = "Fires at a varying rate instead of a constant rate";
			l_flag[2] = "Instant Shot";
			h_flag[2] = "Shoots when the timer starts, rather than ends";
			l_flag[3] = "Stops by Hero Proximity";
			h_flag[3] = "If the Hero is within the specified number of pixels, the shooter will be unable to shoot.";
			l_flag[4] = "'Custom Weapons' are LWeapons";
			h_flag[4] = "If a 'Custom Weapon' ID is used, it will be treated as an LWeapon with this checked, and an EWeapon otherwise.";
			l_flag[5] = "Auto-rotate sprite";
			h_flag[5] = "Attempt to rotate the sprite to match the weapon's angle";
			l_flag[6] = "Multi-Shot";
			h_flag[6] = "Shoot multiple weapons at once";
			l_flag[7] = "Boss Fireball";
			h_flag[7] = "If a fireball weapon type is used, it will be considered a 'boss' fireball.";
			l_flag[9] = "Apply Misc Weapon Data";
			h_flag[9] = "The combo's 'Misc Weapon Data' will be applied to the weapon(s) after they are shot.";
			if(FL(cflag1)) //Angular
			{
				l_attribute[0] = "Angle (Degrees)";
				h_attribute[0] = "If between 0 and 360, acts as an angle in degrees."
					"\nUse '-1' to aim at the Hero (4-dir)"
					"\nUse '-2' to aim at the Hero (8-dir)"
					"\nUse '-3' to aim at the Hero (angular)";
			}
			else
			{
				l_attribute[0] = "Direction";
				h_attribute[0] = "A direction from 0 to 7. 0 = up, 1 = down, etc.";
			}
			if(FL(cflag2)) //Variable rate
			{
				l_attribute[16] = "Lower Fire Rate:";
				h_attribute[16] = "If lower than the 'Upper Fire Rate', the combo will fire between the two rates. (in frames)";
				l_attribute[17] = "Upper Fire Rate:";
				h_attribute[17] = "If higher than the 'Lower Fire Rate', the combo will fire between the two rates. (in frames)";
			}
			else
			{
				l_attribute[16] = "Fire Rate:";
				h_attribute[16] = "Combo fires every this many frames (0 = don't fire)";
			}
			if(FL(cflag4)) //Stops by Hero Proximity
			{
				l_attribute[1] = "Proximity Limit";
				h_attribute[1] = "If the Hero is at least this close (in pixels)"
					" to the combo, the combo will fail to shoot.";
				l_flag[8] = "Invert Proximity";
				h_flag[8] = "If checked, the combo will fail to shoot if the"
					" Hero is FARTHER than specified.";
			}
			if(FL(cflag7)) //Multi Shot
			{
				l_attribute[11] = "Shot Count";
				h_attribute[11] = "How many shots (min 1) to fire";
				l_attribute[3] = "Shot Spread";
				h_attribute[3] = "Angle (in degrees) between each weapon (0 to 360)";
			}
			if(FL(cflag10)) // misc weapon data
				;
			else
			{
				// Misc Weapon Data overwrites whatever would be set here
				l_attribute[12] = "Unblockable:";
				h_attribute[12] = "Sum the following values to create a flagset:"
					"\n1: Bypass 'Block' defense"
					"\n2: Bypass 'Ignore' defense"
					"\n4: Bypass enemy/Hero shield blocking"
					"\n8: Bypass Hero shield reflecting";
				l_attribute[13] = "Script:";
				h_attribute[13] = "LWeapon or EWeapon script ID to attach to the fired weapons."
					"\nNote that there is no way to supply InitD to such scripts.";
			}
			break;
		}
		case cCUTSCENETRIG:
		{
			l_flag[0] = "End Cutscene";
			h_flag[0] = "If checked, triggering this combo with ComboType Effects will end any active cutscene.";
			if(!FL(cflag1))
			{
				l_flag[1] = "Disable F6";
				h_flag[1] = "The cutscene activated by this combo will not allow F6";
				l_attribute[0] = "Allowed Buttons";
				h_attribute[0] = "A bitwise flagset of the buttons that are allowed. Accessed *in LONG ENTRY MODE*:"
					"\nUp=1,Down=2,Left=4,Right=8,A=16,B=32,Start=64,L=128,"
					"\nR=256,Map=512,Ex1=1024,Ex2=2048,Ex3=4096,Ex4=8192,"
					"\nStickUp=16384,StickDown=32768,StickLeft=65536,StickRight=131072"
					"\nAdd the values of whichever buttons you would like to ALLOW during the cutscene.";
				l_attribute[8] = "Error SFX";
				h_attribute[8] = "If >0, SFX played when trying to press a disabled button";
			}
			break;
		}
		case cCUTSCENEEFFECT:
		{
			for(size_t q = 0; q < 24; ++q)
			{
				h_attribute[q] = "Use the Wizard to set up this combo.";
				if(q > 15) continue;
				h_flag[q] = "Use the Wizard to set up this combo.";
			}
			break;
		}
		case cSLASHNEXT:
		{
			l_flag[0] = "Decoration Sprite";
			h_flag[0] = "Spawn a decoration when slashed";
			if(FL(cflag1))
			{
				l_flag[9] = "Use Clippings Sprite";
				h_flag[9] = "Use a system clipping sprite instead of a Sprite Data sprite";
				if(FL(cflag10))
				{
					l_attribute[8] = "Clipping Sprite:";
					h_attribute[8] = "0 and 1 = Bush Leaves, 2 = Flowers, 3 = Grass";
				}
				else
				{
					l_attribute[8] = "Sprite:";
					h_attribute[8] = "Sprite Data sprite ID to display as a clipping";
				}
			}
			l_flag[2] = "Custom SFX";
			h_flag[2] = "Specify a custom slash SFX";
			if(FL(cflag3))
			{
				l_attribute[10] = "Slash Sound:";
				h_attribute[10] = "The SFX to play when slashed";
			}
			break;
		}
		case cSCRIPT1: case cSCRIPT2: case cSCRIPT3: case cSCRIPT4: case cSCRIPT5:
		case cSCRIPT6: case cSCRIPT7: case cSCRIPT8: case cSCRIPT9: case cSCRIPT10:
		case cSCRIPT11: case cSCRIPT12: case cSCRIPT13: case cSCRIPT14: case cSCRIPT15:
		case cSCRIPT16: case cSCRIPT17: case cSCRIPT18: case cSCRIPT19: case cSCRIPT20:
		{
			l_flag[8] = "Generic";
			h_flag[8] = "Attributes/flags act like the Generic combo type.";
			if(!(FL(cflag9))) //Generic flag not set
				break;
		}
		// fall through
		case cTRIGGERGENERIC:
		{
			l_flag[0] = "Decoration Sprite";
			h_flag[0] = "Spawn a decoration when activated";
			if(FL(cflag1))
			{
				l_flag[9] = "Use Clippings Sprite";
				h_flag[9] = "Use a system clipping sprite instead of a Sprite Data sprite";
				if(FL(cflag10))
				{
					l_attribute[8] = "Clipping Sprite:";
					h_attribute[8] = "0 and 1 = Bush Leaves, 2 = Flowers, 3 = Grass";
				}
				else
				{
					l_attribute[8] = "Sprite:";
					h_attribute[8] = "Sprite Data sprite ID to display as a decoration";
				}
			}
			l_flag[1] = "Drop Item";
			h_flag[1] = "Drop an item when activated";
			if(FL(cflag2)) //Drop item
			{
				l_flag[10] = "Specific Item";
				h_flag[10] = "Drop a specific item instead of an item from a dropset";
				if(FL(cflag11))
				{
					l_attribute[9] = "Item:";
					h_attribute[9] = "The item ID to drop";
				}
				else
				{
					l_attribute[9] = "Dropset:";
					h_attribute[9] = "The dropset to select a drop item from";
				}
			}
			l_flag[3] = "Change Combo";
			h_flag[3] = "Become the next combo in the combo list when activated";
			if(FL(cflag4))
			{
				l_flag[11] = "Undercombo";
				h_flag[11] = "Becomes the screen undercombo instead of next combo in the list";
				if(!FL(cflag12))
				{
					l_flag[4] = "Continuous Trigger";
					h_flag[4] = "When changing to a new combo, if the new combo is a generic combo with continuous flag set, skip it";
				}
			}
			l_flag[7] = "Kill Wpn";
			h_flag[7] = "Destroy the weapon that activates this combo";
			l_flag[13] = "Drop Enemy";
			h_flag[13] = "Spawn an Enemy when activated";
			if(FL(cflag14))
			{
				l_attribute[12] = "Enemy ID";
				h_attribute[12] = "The Enemy ID to spawn";
			}
			
			l_flag[6] = "Trigger Singular Secret";
			h_flag[6] = "Triggers a single secret flag temporarily";
			if(FL(cflag7|cflag4))
			{
				l_attribute[10] = "SFX:";
				switch(FL(cflag7|cflag4))
				{
					case cflag4:
						h_attribute[10] = "SFX to play when changing combo.";
						break;
					case cflag7:
						h_attribute[10] = "SFX to play when activating singular secret.";
						break;
					case cflag4|cflag7:
						h_attribute[10] = "SFX to play when activating singular secret or changing combo";
						break;
				}
			}
			if(FL(cflag7))
			{
				l_attribute[11] = "Singular Secret:";
				h_attribute[11] = "Which single secret combo to activate, using the 'SECCMB_' constants from 'include/std_zh/std_constants.zh'";
			}
			if(FL(cflag14)) //Drop Enemy flag
			{
				l_flag[12] = "No Poof";
				h_flag[12] = "Skip spawn poof for dropped enemy";
			}
			l_flag[5] = "Room Item";
			h_flag[5] = "Drop the room's Special Item on activation"
				" (room does not need to be 'Special Item' type, the Catchall value will be used)";
			break;
		}
		case cPITFALL:
		{
			l_flag[0] = "Warp";
			h_flag[0] = "Warp to another screen using a tile warp when falling";
			l_flag[2] = "Damage is Percent";
			h_flag[2] = "The damage amount is a percentage of the Hero's max life";
			l_flag[3] = "Allow Ladder";
			h_flag[3] = "A ladder with 'Can step over pitfalls' checked can step over this combo";
			l_flag[4] = "No Pull";
			h_flag[4] = "Don't suck in the Hero at all";
			l_attribute[0] = "Damage:";
			h_attribute[0] = "The amount of damage, in HP, to take when falling. Negative values heal.";
			l_attribute[3] = "Inv. Frames";
			h_attribute[3] = "The duration, in frames, the player will be invincible after falling."
				" If 0, the default will be used instead (48 if damage >0, 0 otherwise)."
				" If < 0, 0 will be used instead.";
			l_attribute[4] = "Stun Frames";
			h_attribute[4] = "The duration, in frames, the player will be stunned after falling.";
			l_attribute[8] = "Fall SFX:";
			h_attribute[8] = "The SFX to play when falling";
			if(FL(cflag1)) //Warp enabled
			{
				l_flag[1] = "Direct Warp";
				h_flag[1] = "The warp keeps the Hero at the same x/y position";
				l_attribute[9] = "TileWarp ID";
				h_attribute[9] = "0 = A, 1 = B, 2 = C, 3 = D";
			}
			if(!(FL(cflag5))) //"No Pull"
			{
				l_attribute[10] = "Pull Sensitivity:";
				h_attribute[10] = "Pull the Hero 1 pixel every this many frames.\n"
					"If set to 0, pulls 2 pixels every frame.";
			}
			break;
		}
		case cSTEPSFX:
		{
			l_flag[0] = "Landmine (Step->Wpn)";
			h_flag[0] = "Spawns a weapon when activated, and by default advances to the next combo in the combo list.";
			l_attribute[8] = "Sound:";
			h_attribute[8] = "SFX to play when stepped on";
			if(FL(cflag1)) //Landmine
			{
				l_flag[1] = "Script weapon IDs spawn LWeapons";
				h_flag[1] = "Script weapon IDs for 'Weapon Type' are EWeapons by default; if checked, they will be LWeapons instead.";
				l_flag[2] = "Don't Advance";
				h_flag[2] = "If checked, the combo will not advance to the next combo when activated."
					" This may cause the landmine to activate multiple times in a row.";
				l_flag[3] = "Direct Damage Script LW / Sparkles";
				h_flag[3] = "If the weapon type is a Script weapon and 'Script Weapon IDs spawn LWeapons' is checked, or the weapon type is"
					" a sparkle type, it will immediately damage the Hero (knocking them back none).";
				l_flag[9] = "Apply Misc Weapon Data";
				h_flag[9] = "The combo's 'Misc Weapon Data' will be applied to the weapon after it spawns.";
				l_attribute[0] = "Damage:";
				h_attribute[0] = "The damage value for the spawned weapon. If this is < 1, it will default to 4 damage.";
				l_attribute[9] = "Weapon Type:";
				h_attribute[9] = "The weapon type to spawn. Script1-10 weapon types are eweapons by default."
					" If 0 or invalid, uses an enemy bomb type as a default.";
				l_attribute[10] = "Weapon Dir:";
				h_attribute[10] = "Direction for the weapon. 0-7 are the standard dirs, 8+ selects a random dir.";
				l_attribute[11] = "Wpn Sprite:";
				h_attribute[11] = "The 'Sprite Data' sprite to use for the spawned weapon. Only valid if 1 to 255.";
				l_attribute[12] = "Parent Item:";
				h_attribute[12] = "The item ID to use as the 'parent item' of the weapon. Only used for LWeapons. 0 = no parent."
					"\nThis affects various attributes of certain lweapons, such as a bomb's fuse.";
			}
			break;
		}
		case cCSWITCH:
		{
			l_flag[0] = "Kill Wpn";
			h_flag[0] = "Destroy the weapon that activates the combo";
			l_flag[7] = "Skip Cycle on Screen Entry";
			h_flag[7] = "Combo cycle the switch combo on screen entry, to skip any switching animation";
			
			l_attribute[0] = "Combo Change:";
			h_attribute[0] = "Value to add to the combo ID when activated";
			l_attribute[1] = "CSet Change:";
			h_attribute[1] = "Value to add to the cset when activated";
			l_attribute[9] = "SFX:";
			h_attribute[9] = "SFX to play when activated";
			l_flag[10] = "Global State";
			h_flag[10] = "Use a global state instead of a level-based state.";
			if(FL(cflag11)) //Global State
			{
				l_attribute[8] = "State Num:";
				h_attribute[8] = "Range 0-255 inclusive, which of the global switch states to activate from";
				l_attribute[2] = "Timed State";
				h_attribute[2] = "If > 0, the state will revert after this many frames."
					" If used, the state will NOT remain through save/load."
					" Also, timed switches can only turn the switch state 'on', cannot toggle it back 'off'."
					" Hitting the switch while already on will reset the timer.";
			}
			else
			{
				l_attribute[8] = "State Num:";
				h_attribute[8] = "Range 0-31 inclusive, which of the level's switch states to activate from";
			}
			break;
		}
		case cCSWITCHBLOCK:
		{
			l_flag[0] = "Change L0"; l_flag[1] = "Change L1";
			l_flag[2] = "Change L2"; l_flag[3] = "Change L3";
			l_flag[4] = "Change L4"; l_flag[5] = "Change L5";
			l_flag[6] = "Change L6";
			h_flag[0] = "Changes the combo on layer 0 in the same pos as this combo when activated.";
			h_flag[1] = "Changes the combo on layer 1 in the same pos as this combo when activated.";
			h_flag[2] = "Changes the combo on layer 2 in the same pos as this combo when activated.";
			h_flag[3] = "Changes the combo on layer 3 in the same pos as this combo when activated.";
			h_flag[4] = "Changes the combo on layer 4 in the same pos as this combo when activated.";
			h_flag[5] = "Changes the combo on layer 5 in the same pos as this combo when activated.";
			h_flag[6] = "Changes the combo on layer 6 in the same pos as this combo when activated.";
			l_flag[7] = "Skip Cycle on Screen Entry";
			h_flag[7] = "Combo cycle the switch combo on screen entry, to skip any rising/falling animation";
			l_flag[8] = "Allow walk-on-top";
			h_flag[8] = "Allows the Hero to walk along solid switchblocks if they are on them";
			l_attribute[0] = "Combo Change:";
			h_attribute[0] = "Value to add to the combo ID when activated";
			l_attribute[1] = "CSet Change:";
			h_attribute[1] = "Value to add to the cset when activated";
			if(FL(cflag9)) //Allow walk-on-top
			{
				l_flag[9] = "-8px DrawYOffset";
				h_flag[9] = "If enabled, when the Hero stands atop the block (solid area), the Hero's DrawYOffset is decremented by 8."
					" When the Hero leaves the block, the DrawYOffset is incremented back by 8.";
				l_attribute[2] = "Z-value:";
				h_attribute[2] = "A Z-height for the block, allowing you to jump atop it, and from block to block."
					" If set to 0, acts as infinitely tall.";
				l_attribute[3] = "Step Height:";
				h_attribute[3] = "The Z amount below the block's Z-height that you can jump atop it from. This allows"
					" for 'walking up stairs' type effects.";
			}
			l_flag[10] = "Global State";
			h_flag[10] = "Use a global state instead of a level-based state.";
			if(FL(cflag11)) //Global State
			{
				l_attribute[8] = "State Num:";
				h_attribute[8] = "Range 0-255 inclusive, which of the global switch states to activate from";
			}
			else
			{
				l_attribute[8] = "State Num:";
				h_attribute[8] = "Range 0-31 inclusive, which of the level's switch states to activate from";
			}
			break;
		}
		case cTORCH:
		{
			l_attribute[8] = "Radius:";
			h_attribute[8] = "The radius of light, in pixels, to light up in dark rooms.";
			l_attribute[9] = "Shape:";
			h_attribute[9] = "The shape of light. 0=circle, 1=cone, 2=square";
			l_attribute[10] = "Direction:";
			h_attribute[10] = "The direction the torch is facing, for direction-requiring shapes like 'cone'.";
			l_attribute[11] = "Offset:";
			h_attribute[11] = "An offset for the source of the light, in pixels."
				"\nPositive offsets are in the specified direction,"
				" negative offsets are in the opposite direction.";
			break;
		}
		case cSPOTLIGHT:
		{
			l_flag[0] = "Use Tiles instead of Colors";
			h_flag[0] = "Uses a set of tiles in a preset order, instead of a set of 3 colors, to represent the light beam.";
			l_flag[1] = "Works on FFCs";
			h_flag[1] = "This spotlight can fire a beam from FFCs, off-the-grid";
			l_attribute[8] = "Dir:";
			h_attribute[8] = "0-3 = Up,Down,Left,Right\n4-7 = Unused (For Now)\n8 = at the ground";
			l_attribute[12] = "Trigger Set:";
			h_attribute[12] = "0-32; if 0 will activate any targets, otherwise only activates matching targets";
			if(FL(cflag1))
			{
				l_attribute[0] = "Start Tile:";
				h_attribute[0] = "Tiles in order: Ground, Up, Down, Left, Right, U+L, U+R, D+L, D+R, U+D, L+R, D+L+R, U+L+R, U+D+R, U+D+L, U+D+L+R";
				l_attribute[9] = "CSet (0-11):";
				h_attribute[9] = "CSet for the light beam graphic";
			}
			else
			{
				l_attribute[9] = "Inner Color:";
				h_attribute[9] = "One of the colors used to generate the light beam graphic";
				l_attribute[10] = "Middle Color:";
				h_attribute[10] = "One of the colors used to generate the light beam graphic";
				l_attribute[11] = "Outer Color:";
				h_attribute[11] = "One of the colors used to generate the light beam graphic";
			}
			if(FL(cflag2))
			{
				l_attribute[13] = "Beam Width (FFC)";
				h_attribute[13] = "The 'width' of the beam. This is only used when the spotlight"
					" is on an FFC, and is used to center the beam and determine the hitbox."
					"\nDoes not affect the visual in any way. If < 1, uses '8' as a default.";
			}
			break;
		}
		case cLIGHTTARGET:
		{
			l_flag[0] = "Lit Version";
			h_flag[0] = "If checked, reverts to previous combo when not hit by a spotlight."
				"\nIf unchecked, becomes the next combo when hit by a spotlight.";
			l_flag[1] = "Invert";
			h_flag[1] = "If checked, counts as activated when light is NOT hitting it.";
			l_flag[2] = "Blocks Light";
			h_flag[2] = "Light that hits will activate, but not pass, the target";
			l_attribute[12] = "Trigger Set:";
			h_attribute[12] = "0-32; if 0 will be activated by any beams, otherwise only by matching beams";
			break;
		}
		case cSWITCHHOOK:
		{
			l_flag[0] = "Only swap with Combo 0";
			h_flag[0] = "The switch will fail if this combo would be swapped with a non-zero combo";
			l_flag[1] = "Swap Placed Flags";
			h_flag[1] = "Placed flags on the same layer and position as this combo will be swapped along with the combo";
			l_flag[2] = "Break upon swap";
			h_flag[2] = "The combo will 'break' upon swapping, displaying a break sprite and potentially"
				" dropping an item. Instead of swapping with the combo under the Hero,"
				" it will be replaced by the screen's Undercombo.";
			l_flag[6] = "Counts as 'pushblock'";
			h_flag[6] = "This combo counts as a 'pushblock' for purposes of switching it onto"
				" block triggers/holes, though this does not allow it to be pushed"
				" (unless a push flag is placed on it).";
			l_attribute[8] = "Hook Level:";
			h_attribute[8] = "The minimum level of SwitchHook that can swap this combo";
			if(FL(cflag3)) //break info
			{
				l_attribute[9] = "Break Sprite:";
				h_attribute[9] = "Sprite Data sprite ID to display when broken";
				l_attribute[10] = "Break SFX:";
				h_attribute[10] = "SFX to be played when broken";
				l_flag[3] = "Drop Item";
				h_flag[3] = "Will drop an item upon breaking.";
				l_flag[5] = "Next instead of Undercombo";
				h_flag[5] = "Replace with the Next combo instead of the screen's Undercombo";
				if(FL(cflag4))
				{
					l_flag[4] = "Specific Item";
					h_flag[4] = "Drop a specific item instead of an item from a dropset";
					if(FL(cflag5))
					{
						l_attribute[11] = "Item:";
						h_attribute[11] = "The item ID to drop";
					}
					else
					{
						l_attribute[11] = "Dropset:";
						h_attribute[11] = "The dropset to select a drop item from";
					}
				}
			}
			break;
		}
		case cSAVE: case cSAVE2:
		{
			l_flag[0] = "Restores Life";
			h_flag[0] = "If checked, restores life up to the percentage listed in attributes.";
			l_flag[1] = "Restores Magic";
			h_flag[1] = "If checked, restores magic up to the percentage listed in attributes.";
			if(FL(cflag1))
			{
				l_attribute[8] = "Life Percentage";
				h_attribute[8] = "Restore life up to this percentage, if it is lower.";
			}
			if(FL(cflag2))
			{
				l_attribute[9] = "Magic Percentage";
				h_attribute[9] = "Restore magic up to this percentage, if it is lower.";
			}
			l_attribute[10] = "Save Menu";
			h_attribute[10] = "If >0, saving will use the specified Save Menu instead of the default engine save menu.";
			break;
		}
		case cPUSHBLOCK:
		{
			l_flag[0] = "Pushable Up";
			h_flag[0] = "Can be pushed in the 'Up' direction";
			l_flag[1] = "Pushable Down";
			h_flag[1] = "Can be pushed in the 'Down' direction";
			l_flag[2] = "Pushable Left";
			h_flag[2] = "Can be pushed in the 'Left' direction";
			l_flag[3] = "Pushable Right";
			h_flag[3] = "Can be pushed in the 'Right' direction";
			l_flag[4] = "Separate Directions";
			h_flag[4] = "Different push counts for different directions";
			l_flag[5] = "Enemies First (Wait)";
			h_flag[5] = "Cannot be pushed until the enemies have been cleared from the screen";
			l_flag[6] = "Icy Block";
			h_flag[6] = "When pushed, keeps sliding until it hits a barrier.";
			l_flag[7] = "Opposites Cancel";
			h_flag[7] = "Pushing the block in a direction opposite to one it has already been pushed in"
				" 'cancels' the previous push, instead of being a new push,"
				" for purposes of the max number of pushes.";
			l_flag[8] = "0 limit is none";
			h_flag[8] = "Any direction with a push limit of '0' will be unable to be pushed"
				" (as opposed to being pushable an infinite number of times).";
			l_flag[9] = "Ignores Icy Floor";
			h_flag[9] = "Does not slide on icy floors";
			l_flag[10] = "Obeys Gravity";
			h_flag[10] = "Falls down in sideview until it hits the floor (when pushed, NOT passively)";
			
			l_attribute[16] = "Push Speed";
			h_attribute[16] = "If 0 or less, uses the default push speed of 0.5 pixels per frame."
				" Otherwise, uses this speed as a step value (1/100ths pixel per frame).";
			l_attribute[8] = "Heavy Level";
			h_attribute[8] = "If >0, a bracelet of at least this level is required to push this block.";
			l_attribute[9] = "Push SFX";
			h_attribute[9] = "The SFX to play when the block is pushed.";
			l_attribute[10] = "Stop SFX";
			h_attribute[10] = "The SFX to play when the block stops moving.";
			if(FL(cflag5))
			{
				l_attribute[12] = "Times Pushable (Up):";
				h_attribute[12] = "How many times the block can be pushed Up before it can no"
					" longer be pushed up. '0' either means no pushing or infinite pushing, depending on the '0 limit is none' flag.";
				l_attribute[13] = "Times Pushable (Down):";
				h_attribute[13] = "How many times the block can be pushed Down before it can no"
					" longer be pushed down. '0' either means no pushing or infinite pushing, depending on the '0 limit is none' flag.";
				l_attribute[14] = "Times Pushable (Left):";
				h_attribute[14] = "How many times the block can be pushed Left before it can no"
					" longer be pushed left. '0' either means no pushing or infinite pushing, depending on the '0 limit is none' flag.";
				l_attribute[15] = "Times Pushable (Right):";
				h_attribute[15] = "How many times the block can be pushed Right before it can no"
					" longer be pushed right. '0' either means no pushing or infinite pushing, depending on the '0 limit is none' flag.";
			}
			else
			{
				l_attribute[12] = "Times Pushable:";
				h_attribute[12] = "How many times the block can be pushed before it clicks into place."
					" '0' either means no pushing or infinite pushing, depending on the '0 limit is none' flag.";
			}
			break;
		}
		case cICY:
		{
			l_flag[0] = "Slides Blocks";
			h_flag[0] = "Pushable blocks pushed onto this combo will"
				" slide past it, if nothing blocks their way.";
			l_flag[1] = "Slides Hero";
			h_flag[1] = "The Hero will slip and slide on the ice. Requires Newer Hero Movement." + std::string();
			if(FL(cflag2))
			{
				l_attribute[8] = "Start Speed Percentage";
				h_attribute[8] = "The percentage of the Hero's movement speed to carry onto the ice when first stepping onto it.";
				l_attribute[9] = "Entry Leeway Frames";
				h_attribute[9] = "For this many frames after entering the ice, the Hero will have additional traction. The traction gradually decreases over time until the frames are up.";
				l_attribute[0] = "Acceleration";
				h_attribute[0] = "Speed gained when holding a direction, in pixels per frame.";
				l_attribute[1] = "Deceleration";
				h_attribute[1] = "Speed lost when not holding a direction, in pixels per frame.";
				l_attribute[2] = "Max Speed";
				h_attribute[2] = "The highest speed that can be reached, in pixels per frame.";
			}
			break;
		}
		case cMIRRORNEW:
		{
			l_attribute[8+up] = "Up Reflect";
			h_attribute[8+up] = "Weapons/light beams facing up (coming from below) will move in this direction."
				" Light beams will not work with diagonals."
				"\n0 = up, 1 = down, 2 = left, 3 = right"
				"\n4 = up-left, 5 = up-right, 6 = down-left, 7 = down-right, 8 = block";
			l_attribute[8+down] = "Down Reflect";
			h_attribute[8+down] = "Weapons/light beams facing down (coming from above) will move in this direction."
				" Light beams will not work with diagonals."
				"\n0 = up, 1 = down, 2 = left, 3 = right"
				"\n4 = up-left, 5 = up-right, 6 = down-left, 7 = down-right, 8 = block";
			l_attribute[8+left] = "Left Reflect";
			h_attribute[8+left] = "Weapons/light beams facing left (coming from the right) will move in this direction."
				" Light beams will not work with diagonals."
				"\n0 = up, 1 = down, 2 = left, 3 = right"
				"\n4 = up-left, 5 = up-right, 6 = down-left, 7 = down-right, 8 = block";
			l_attribute[8+right] = "Right Reflect";
			h_attribute[8+right] = "Weapons/light beams facing right (coming from the left) will move in this direction."
				" Light beams will not work with diagonals."
				"\n0 = up, 1 = down, 2 = left, 3 = right"
				"\n4 = up-left, 5 = up-right, 6 = down-left, 7 = down-right, 8 = block";
			l_attribute[8+l_up] = "Up-Left Reflect";
			h_attribute[8+l_up] = "Weapons facing up-left (coming from down-right) will move in this direction."
				"\n0 = up, 1 = down, 2 = left, 3 = right"
				"\n4 = up-left, 5 = up-right, 6 = down-left, 7 = down-right, 8 = block";
			l_attribute[8+r_up] = "Up-Right Reflect";
			h_attribute[8+r_up] = "Weapons facing up-right (coming from down-left) will move in this direction."
				"\n0 = up, 1 = down, 2 = left, 3 = right"
				"\n4 = up-left, 5 = up-right, 6 = down-left, 7 = down-right, 8 = block";
			l_attribute[8+l_down] = "Down-Left Reflect";
			h_attribute[8+l_down] = "Weapons facing down-left (coming from up-right) will move in this direction."
				"\n0 = up, 1 = down, 2 = left, 3 = right"
				"\n4 = up-left, 5 = up-right, 6 = down-left, 7 = down-right, 8 = block";
			l_attribute[8+r_down] = "Down-Right Reflect";
			h_attribute[8+r_down] = "Weapons facing down-right (coming from up-left) will move in this direction."
				"\n0 = up, 1 = down, 2 = left, 3 = right"
				"\n4 = up-left, 5 = up-right, 6 = down-left, 7 = down-right, 8 = block";
		}
		// fall through
		case cMIRROR: case cMIRRORSLASH: case cMIRRORBACKSLASH: case cMAGICPRISM: case cMAGICPRISM4:
		{
			l_attribute[0] = "Reflect Flags";
			h_attribute[0] = "Sum of values for each type of weapon that should be reflected. If '0', reflects everything.";
			break;
		}
		case cCRUMBLE:
		{
			l_attribute[8] = "Crumble Type";
			h_attribute[8] = "0 = Reset (Timer resets when stepped off of, can also change combo)"
				"\n1 = Cumulative (Timer does not reset, just pauses when stepped off of)"
				"\n2 = Inevitable (Timer keeps going even if stepped off of)";
			l_flag[0] = "Continuous";
			h_flag[0] = "If the next combo is a Crumbling combo of the 'Inevitable' type, it will continue crumbling automatically.";
			l_attribute[9] = "Crumble Sensitivity";
			h_attribute[9] = "This many pixels of leeway are given. 0 = fully sensitive."
				"\nIf leeway exceeds combo/ffc size, no crumbling will occur.";
			l_attribute[16] = "Crumble Time";
			h_attribute[16] = "The time, in frames, before the combo crumbles into the next combo in the combo list.";
			if((x.attributes[8] / 10000) == CMBTY_CRUMBLE_RESET)
			{
				l_attribute[17] = "Reset Change";
				h_attribute[17] = "If non-zero, the combo will change by this amount (ex. 1 = Next, -1 = Prev)"
					" when the Hero steps off of the combo before it finishes crumbling. (Might be used to reset a crumble animation)";
			}
			break;
		}
	}
#undef FL
}

} // namespace up30
} // namespace

int q30_trigflag_count() { return 185; }

const char *q30_trigflag_name(int f)
{
    return f >= 0 && f < 185 ? up30::g_trigflag_names[f] : "";
}

int q30_ctype_count(bool is30) { return is30 ? (int)up30::cMAX : 183; }
int q30_mflag_count(bool is30) { return is30 ? (int)up30::mfMAX : 256; }

const char *q30_ctype_name(int t)
{
    return t >= 0 && t < (int)up30::cMAX ? up30::g_ctype_names[t] : "";
}

const char *q30_ctype_help(int t)
{
    return t >= 0 && t < (int)up30::cMAX && up30::g_ctype_help[t] ? up30::g_ctype_help[t] : "";
}

const char *q30_mflag_name(int f)
{
    return f >= 0 && f < (int)up30::mfMAX ? up30::g_mflag_names[f] : "";
}

void q30_combo_labels(const Q30Combo &x, std::string *l_attribute, std::string *h_attribute, std::string *l_flag,
                      std::string *h_flag)
{
    up30::combo_labels(x, l_attribute, h_attribute, l_flag, h_flag);
}
