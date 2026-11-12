// Generated from upstream ZQuestClassic (3.0 Nightly, commit ef321f8) by tools/zq_gen_item_names.py: item
// class, counter, weapon and enemy class names, item class help, and the item editor's labels
// for each class (itemeditor.cpp loadinfo). Upstream's numbers, which 2.55 and 3.0 quests
// use. Regenerate rather than edit by hand.

#include "precompiled.h" //always first

#include <string>

#include "zq_item_names.h"
#include "zq_combo_names.h"

namespace
{
namespace up30
{
enum // used for gamedata ITEMS
{
	// 0
	itype_sword, itype_brang, itype_arrow, itype_candle, itype_whistle,
	itype_bait, itype_letter, itype_potion, itype_wand, itype_ring,
	itype_wallet, itype_amulet, itype_shield, itype_bow, itype_raft,
	itype_ladder, itype_book, itype_magickey, itype_bracelet, itype_flippers,
	// 20
	itype_boots, itype_hookshot, itype_lens, itype_hammer, itype_divinefire,
	itype_divineescape, itype_divineprotection, itype_bomb, itype_sbomb, itype_clock,
	itype_key, itype_magiccontainer, itype_triforcepiece, itype_map, itype_compass,
	itype_bosskey, itype_quiver, itype_lkey, itype_cbyrna, itype_rupee,
	// 40
	itype_arrowammo, itype_fairy, itype_magic, itype_heart, itype_heartcontainer,
	itype_heartpiece, itype_killem, itype_bombammo, itype_bombbag, itype_rocs,
	itype_hoverboots, itype_spinscroll,itype_crossscroll, itype_quakescroll,itype_whispring,
	itype_chargering, itype_perilscroll, itype_wealthmedal,itype_heartring,itype_magicring,
	// 60
	itype_spinscroll2, itype_quakescroll2, itype_agony, itype_stompboots, itype_whimsicalring,
	itype_perilring, itype_misc,
	// 67
	itype_custom1, itype_custom2, itype_custom3, itype_custom4, itype_custom5,
	itype_custom6, itype_custom7, itype_custom8, itype_custom9, itype_custom10,
	itype_custom11, itype_custom12, itype_custom13, itype_custom14, itype_custom15,
	itype_custom16, itype_custom17, itype_custom18, itype_custom19, itype_custom20,
	// 87
	itype_bowandarrow, itype_letterpotion,
	itype_last, 
	itype_script1 = 256, //Scripted Weapons
	itype_script2, itype_script3, itype_script4, itype_script5, itype_script6, itype_script7, itype_script8, itype_script9, itype_script10,
	itype_icerod, itype_atkring, itype_lantern, itype_pearl, itype_bottle, itype_bottlefill, itype_bugnet,
	itype_mirror, itype_switchhook, itype_itmbundle, itype_progressive_itm, itype_note, itype_refill,
	itype_liftglove, itype_gravity_boots, itype_gravity_up_boots, itype_gravity_down_boots,
	itype_cooldown_ring, itype_enemy_ambush,
	/*
	itype_templast,
	itype_ether, itype_bombos, itype_quake, 
	itype_powder,
	itype_trowel,
	itype_instrument,
	itype_sword180,
	itype_sword_gb,
	itype_firerod,
	itype_scripted_001 = 400, 
	itype_scripted_002,
	itype_scripted_003,
	itype_scripted_004,
	itype_scripted_005,
	itype_scripted_006,
	itype_scripted_007,
	itype_scripted_008,
	itype_scripted_009,
	itype_scripted_010,
	*/

	
	itype_maxusable,
	itype_max=512
};

enum
{
	crNONE = -1,
	crLIFE, crMONEY, crBOMBS, crARROWS, crMAGIC,
	crKEYS, crSBOMBS, crCUSTOM1, crCUSTOM2, crCUSTOM3,
	crCUSTOM4, crCUSTOM5, crCUSTOM6, crCUSTOM7, crCUSTOM8,
	crCUSTOM9, crCUSTOM10, crCUSTOM11, crCUSTOM12, crCUSTOM13,
	crCUSTOM14, crCUSTOM15, crCUSTOM16, crCUSTOM17, crCUSTOM18,
	crCUSTOM19, crCUSTOM20, crCUSTOM21, crCUSTOM22, crCUSTOM23,
	crCUSTOM24, crCUSTOM25, crCUSTOM26, crCUSTOM27, crCUSTOM28,
	crCUSTOM29, crCUSTOM30, crCUSTOM31, crCUSTOM32, crCUSTOM33,
	crCUSTOM34, crCUSTOM35, crCUSTOM36, crCUSTOM37, crCUSTOM38,
	crCUSTOM39, crCUSTOM40, crCUSTOM41, crCUSTOM42, crCUSTOM43,
	crCUSTOM44, crCUSTOM45, crCUSTOM46, crCUSTOM47, crCUSTOM48,
	crCUSTOM49, crCUSTOM50, crCUSTOM51, crCUSTOM52, crCUSTOM53,
	crCUSTOM54, crCUSTOM55, crCUSTOM56, crCUSTOM57, crCUSTOM58,
	crCUSTOM59, crCUSTOM60, crCUSTOM61, crCUSTOM62, crCUSTOM63,
	crCUSTOM64, crCUSTOM65, crCUSTOM66, crCUSTOM67, crCUSTOM68,
	crCUSTOM69, crCUSTOM70, crCUSTOM71, crCUSTOM72, crCUSTOM73,
	crCUSTOM74, crCUSTOM75, crCUSTOM76, crCUSTOM77, crCUSTOM78,
	crCUSTOM79, crCUSTOM80, crCUSTOM81, crCUSTOM82, crCUSTOM83,
	crCUSTOM84, crCUSTOM85, crCUSTOM86, crCUSTOM87, crCUSTOM88,
	crCUSTOM89, crCUSTOM90, crCUSTOM91, crCUSTOM92, crCUSTOM93,
	crCUSTOM94, crCUSTOM95, crCUSTOM96, crCUSTOM97, crCUSTOM98,
	crCUSTOM99, crCUSTOM100, MAX_COUNTERS
};

enum
{
    // 0
    wNone,wSword,wBeam,wBrang,
    wBomb,wSBomb,wLitBomb,wLitSBomb,
    // 8
    wArrow,wFire,wWhistle,wBait,
    wWand,wMagic,wCatching,wWind,
    // 16
    wRefMagic,wRefFireball,wRefRock, wHammer,
    wHookshot, wHSHandle, wHSChain, wSSparkle,
    // 24
    wFSparkle, wSmack, wPhantom, wCByrna,
	//28
    wRefBeam, wStomp,
	//30
    lwMax,
    // Dummy weapons - must be between lwMax and wEnemyWeapons!
	//31
    wScript1, wScript2, wScript3, wScript4,
	//35
    wScript5, wScript6, wScript7, wScript8,
	//39
    wScript9, wScript10, wIce, wFlame, //ice rod, fire rod
    wSound, // -Z: sound + defence split == digdogger, sound + one hit kill == pols voice -Z
	wThrown, wPot, //Thrown pot or rock -Z //Just gonna use a single 'wThrown' -Em
	wLit, //Lightning or Electric -Z
	wBombos, wEther, wQuake,// -Z
	wSword180, wSwordLA,
	//52
	wBugNet, wRefArrow, wRefFire, wRefFire2,
    // Enemy weapons
    wEnemyWeapons=128,
    //129
    ewFireball,ewArrow,ewBrang,ewSword,
    ewRock,ewMagic,ewBomb,ewSBomb,
    //137
    ewLitBomb,ewLitSBomb,ewFireTrail,ewFlame,
    ewWind,ewFlame2,ewFlame2Trail,
    //145
    ewIce,ewFireball2,
    wMax
};

enum
{
    eeGUY=0, eeWALK,
    eeSHOOT/*DEPRECATED*/,
    eeTEK, eeLEV, eePEAHAT, eeZORA, eeROCK,
	//8
    eeGHINI, eeARMOS/*DEPRECATED*/, eeKEESE, eeGEL/*DEPRECATED*/, eeZOL/*DEPRECATED*/, eeROPE/*DEPRECATED*/, eeGORIYA/*DEPRECATED*/, eeTRAP,
	//16
    eeWALLM, eeBUBBLE/*DEPRECATED*/, eeVIRE/*DEPRECATED*/, eeLIKE/*DEPRECATED*/, eePOLSV/*DEPRECATED*/, eeWIZZ, eeAQUA, eeMOLD,
	//24
    eeDONGO, eeMANHAN, eeGLEEOK, eeDIG, eeGHOMA, eeLANM, eePATRA, eeGANON,
	//32
    eePROJECTILE, eeGELTRIB/*DEPRECATED*/, eeZOLTRIB/*DEPRECATED*/, eeVIRETRIB/*DEPRECATED*/, eeKEESETRIB/*DEPRECATED*/, eeSPINTILE, eeNONE,
	//39
    eeFAIRY, eeFIRE, eeOTHER, eeMAX250, //eeFire is Other (Floating), eeOther is Other in the Editor.
    eeSCRIPT01, eeSCRIPT02, eeSCRIPT03, eeSCRIPT04, eeSCRIPT05, eeSCRIPT06, eeSCRIPT07, eeSCRIPT08, eeSCRIPT09, eeSCRIPT10,
    eeSCRIPT11, eeSCRIPT12, eeSCRIPT13, eeSCRIPT14, eeSCRIPT15, eeSCRIPT16, eeSCRIPT17, eeSCRIPT18, eeSCRIPT19, eeSCRIPT20,
    eeFFRIENDLY01, eeFFRIENDLY02, eeFFRIENDLY03, eeFFRIENDLY04, eeFFRIENDLY05, eeFFRIENDLY06, eeFFRIENDLY07, eeFFRIENDLY08,
    eeFFRIENDLY09, eeFFRIENDLY10,
    eeMAX
};

enum
{	//These are the COMBO flags. -Z
	//0
	mfNONE, 	mfPUSHUD, 	mfPUSH4, 	mfWHISTLE, 	mfANYFIRE, 
	//5
	mfARROW, 	mfBOMB, 	mfFAIRY, 	mfRAFT, 	mfARMOS_SECRET, 
	//10
	mfARMOS_ITEM, 	mfSBOMB, 	mfRAFT_BRANCH, 	mfDIVE_ITEM,	mfLENSMARKER, 
	//15
	mfZELDA,	mfSECRETS01, 	mfSECRETS02, 	mfSECRETS03, 	mfSECRETS04,       
	//20
	mfSECRETS05, 	mfSECRETS06, 	mfSECRETS07, 	mfSECRETS08,	mfSECRETS09, 
	//25
	mfSECRETS10, 	mfSECRETS11, 	mfSECRETS12,	mfSECRETS13, 	mfSECRETS14, 
	//30
	mfSECRETS15, 	mfSECRETS16,	mfTRAP_H, 	mfTRAP_V, 	mfTRAP_4, 
	//35
	mfTRAP_LR, 	mfTRAP_UD,	mfENEMY0, 	mfENEMY1, 	mfENEMY2, 
	//40
	mfENEMY3, 	mfENEMY4,	mfENEMY5, 	mfENEMY6, 	mfENEMY7, 
	//45
	mfENEMY8, 	mfENEMY9,	mfPUSHLR, 	mfPUSHU, 	mfPUSHD, 
	//50
	mfPUSHL, 	mfPUSHR,	mfPUSHUDNS, 	mfPUSHLRNS, 	mfPUSH4NS, 
	//55
	mfPUSHUNS,	mfPUSHDNS, 	mfPUSHLNS, 	mfPUSHRNS, 	mfPUSHUDINS,
	//60
	mfPUSHLRINS, 	mfPUSH4INS, 	mfPUSHUINS,	mfPUSHDINS, 	mfPUSHLINS, 
	//65
	mfPUSHRINS,	mfBLOCKTRIGGER,	mfNOBLOCKS, 	mfBRANG, 	mfMBRANG,
	//70
	mfFBRANG, 	mfSARROW, 	mfGARROW, 	mfSTRONGFIRE, 	mfMAGICFIRE, 
	//75
	mfDIVINEFIRE,	mfWANDMAGIC, 	mfREFMAGIC, 	mfREFFIREBALL, 	mfSWORD, 
	//80
	mfWSWORD,	mfMSWORD, 	mfXSWORD, 	mfSWORDBEAM, 	mfWSWORDBEAM, 
	//85
	mfMSWORDBEAM,	mfXSWORDBEAM, 	mfHOOKSHOT, 	mfWAND, 	mfHAMMER, 
	//90
	mfSTRIKE, 	mfBLOCKHOLE,	mfMAGICFAIRY, 	mfALLFAIRY, 	mfSINGLE, 
	//95
	mfSINGLE16,	mfNOENEMY, 	mfNOGROUNDENEMY, mfSCRIPT1, 	mfSCRIPT2, 
	//100
	mfSCRIPT3, 	mfSCRIPT4, 	mfSCRIPT5,	mfRAFT_BOUNCE, 	mfPUSHED,
	//105
	mfSCRIPT6, 	mfSCRIPT7, 	mfSCRIPT8, 	mfSCRIPT9, 	mfSCRIPT10, 
	//110
	mfSCRIPT11, 	mfSCRIPT12, 	mfSCRIPT13, 	mfSCRIPT14, 	mfSCRIPT15,
	//115
	mfSCRIPT16, 	mfSCRIPT17, 	mfSCRIPT18, 	mfSCRIPT19, 	mfSCRIPT20,
	//120
	mfPITHOLE, 	mfPITFALLFLOOR,	mfLAVA, 	mfICE, 		mfICEDAMAGE, 
	//125
	mfDAMAGE1, 	mfDAMAGE2, 	mfDAMAGE4, 	mfDAMAGE8, 	mfDAMAGE16, 
	//130
	mfDAMAGE32,	mfFREEZEALL, 	mfFREZEALLANSFFCS, mfFREEZEFFCSOLY, mfSCRITPTW1TRIG, 
	//135
	mfSCRITPTW2TRIG, mfSCRITPTW3TRIG, mfSCRITPTW4TRIG, mfSCRITPTW5TRIG, mfSCRITPTW6TRIG, 
	//140
	mfSCRITPTW7TRIG, mfSCRITPTW8TRIG, mfSCRITPTW9TRIG, mfSCRITPTW10TRIG, mfTROWEL, 
	//145
	mfTROWELNEXT, mfTROWELSPECIALITEM,mfSLASHPOT, 	mfLIFTPOT,	mfLIFTORSLASH, 
	//150
	mfLIFTROCK, 	mfLIFTROCKHEAVY, mfDROPITEM, 	mfSPECIALITEM, 	mfDROPKEY, 
	//155
	mfDROPLKEY, 	mfDROPCOMPASS, 	mfDROPMAP, 	mfDROPBOSSKEY, mfSPAWNNPC, 
	//160
	mfSWITCHHOOK, 	mfSIDEVIEWLADDER, mfSIDEVIEWPLATFORM, mfNOENEMYSPAWN, mfENEMYALL,
	//165
	mfSECRETSNEXT, mfNOMIRROR, mfUNSAFEGROUND_SENS, mfUNSAFEGROUND, mf169, mf170, mf171, mf172, mf173, mf174,
    mf175, mf176, mf177, mf178, mf179, mf180, mf181, mf182, mf183, mf184, mf185, mf186, mf187, 
    mf188, mf189, mf190, mf191, mf192, mf193, mf194, mf195, mf196, mf197, mf198, mf199, mf200,
    mf201, mf202, mf203, mf204, mf205, mf206, mf207, mf208, mf209, mf210, mf211, mf212, mf213,
    mf214, mf215, mf216, mf217, mf218, mf219, mf220, mf221, mf222, mf223, mf224, mf225, mf226, 
    mf227, mf228, mf229, mf230, mf231, mf232, mf233, mf234, mf235, mf236, mf237, mf238, mf239,
    mf240, mf241, mf242, mf243, mf244, mf245, mf246, mf247, mf248, mf249, mf250, mf251, mf252, mf253, mf254, 
    mfEXTENDED, mfMAX //256 for array sizing
    
    //mfSLASHPOTSPECIALITEM, //stack flags to make a drop or special item
     //mfLIFTPOTSPECIAL, 
};

enum
{
    edNORMAL, // : IMPLEMENTED : Take damage (or stun)
    edHALFDAMAGE, // : IMPLEMENTED : Take half damage
    edQUARTDAMAGE, // : IMPLEMENTED : Take 0.25 damage
    edSTUNONLY, // : IMPLEMENTED : Stun instead of taking damage.
    edSTUNORCHINK, // : IMPLEMENTED : If damage > 0, stun instead. Else, bounce off.
    edSTUNORIGNORE, // : IMPLEMENTED : If damage > 0, stun instead. Else, ignore.
    edCHINKL1, // : IMPLEMENTED : Bounces off, plays SFX_CHINK
    edCHINKL2, // : IMPLEMENTED : Bounce off unless damage >= 2
    edCHINKL4, //: IMPLEMENTED : Bounce off unless damage >= 4
    edCHINKL6, // : IMPLEMENTED : Bounce off unless damage >= 6
    edCHINKL8, // : IMPLEMENTED : Bounce off unless damage >= 8
    edCHINK, // : IMPLEMENTED : Bounces off, plays SFX_CHINK
    edIGNOREL1, // : IMPLEMENTED : Ignore unless damage > 1.
    edIGNORE, // : IMPLEMENTED : Do Nothing
    ed1HKO, // : IMPLEMENTED : One-hit knock-out
	edCHINKL10, //: IMPLEMENTED : If damage is less than 10
	ed2x, // : IMPLEMENTED : Double damage.
	ed3x, // : IMPLEMENTED : Triple Damage.
	ed4x, // : IMPLEMENTED : 4x damage.
	edHEAL, // : IMPLEMENTED : Gain the weapon damage in HP.
	edTRIGGERSECRETS, // : IMPLEMENTED : Triggers screen secrets.
	edFREEZE, //Freeze solid
	edMSG_NOT_ENABLED, //A message for 'The following are not yet enabled.
	edMSG_LINE, //An entry for the hiriz line in THE zq PULLDOWN
	edLEVELDAMAGE, //Damage * item level
	edLEVELREDUCTION, //Damage / item level
	
	edSPLIT, //: IMPLEMENTED : causes the enemy to split if it has a split attribute
	edREPLACE, //replaced by next in list?
	edLEVELCHINK2, //If item level is < 2: This needs a weapon variable that is set by 
	edLEVELCHINK3, //If item level is < 3: the item that generates it (itemdata::level stored to
	edLEVELCHINK4, //If item level is < 4: weapon::level, or something similar; then a check to
	edLEVELCHINK5, //If item level is < 5: read weapon::level in hit detection. 
	edSHOCK, //buzz blob
	edEXPLODESMALL, //: IMPLEMENTED : ew bombblast
	edEXPLODELARGE, //: IMPLEMENTED : super bomb blast
	edSTONE, //deadrock
	
	edBREAKSHIELD, //break the enemy shield
	edRESTORESHIELD, //if the enemy had a shield, reset it
	edSPECIALDROP, //but where to define it?
	edINCREASECOUNTER, //but where to define the counter
	edREDUCECOUNTER, //same problem
	edEXPLODEHARMLESS, //: IMPLEMENTED : boss death explosion; needs different sprites?
	edKILLNOSPLIT, //If sufficient damage to kill it, a splitting enemy just dies.
	edTRIBBLE, //Tribbles on hit. 
	edFIREBALL, //Makes a 1x1 fireball; Z3 Gibdo
	edFIREBALLLARGE, //Makes a 3x3  Z3 Gibdo for large npcs. 
	edSUMMON, //: IMPLEMENTED : Summons a number of enemies as defined by the summon properties of the npc. 
	//edSAVE, edRETRY, edCRASHZC // Sanity Check Required. -Z
	edWINGAME, //Wand of Gamelon. 
	edJUMP, //Z3 stalfos
	edEATHERO, //-G //Is this practical? We need specisal npc mvoement for it. -Z
	edSHOWMESSAGE, //Shows a ZString when hit. e.g., Z3 Ganon
	edSWITCH, //Switch places with the Hero, as a switchhook does
	
    edLAST
};

enum { nswapDEC, nswapHEX, nswapLDEC, nswapLHEX, nswapBOOL, nswapMAX };

// item_flags (items.h): the generic flags sit between named ones
enum : unsigned
{
    item_flag1 = 1u << 4, item_flag2 = 1u << 5, item_flag3 = 1u << 10, item_flag4 = 1u << 11, item_flag5 = 1u << 12,
    item_flag6 = 1u << 13, item_flag7 = 1u << 14, item_flag8 = 1u << 15, item_flag9 = 1u << 16, item_flag10 = 1u << 17,
    item_flag11 = 1u << 18, item_flag12 = 1u << 19, item_flag13 = 1u << 20, item_flag14 = 1u << 21, item_flag15 = 1u << 22
};

const char g_itype_names[itype_max][255] = 
{ 
	"Swords", "Boomerangs", "Arrows", "Candles", "Whistles",
	"Bait", "Letters", "Potions", "Wands", "Rings", 
	"Wallets", "Amulets", "Shields", "Bows", "Rafts",
	"Ladders", "Books", "Magic Keys", "Bracelets", "Flippers", 
	"Boots", "Hookshots", "Lenses", "Hammers", "Divine Fire", 
	"Divine Escape", "Divine Protection", "Bombs", "Super Bombs", "Clocks", 
	"Keys", "Magic Containers", "Triforce Pieces", "Maps", "Compasses", 
	"Boss Keys", "Quivers", "Level Keys", "Canes of Byrna", "Rupees", 
	"Arrow Ammo", "Fairies", "Magic", "Hearts", "Heart Containers", 
	"Heart Pieces", "Kill All Enemies", "Bomb Ammo", "Bomb Bags", "Roc Items", 
	"Hover Boots", "Scroll: Spin Attack", "Scroll: Cross Beams", "Scroll: Quake Hammer","Whisp Rings", 
	"Charge Rings", "Scroll: Peril Beam", "Wealth Medals", "Heart Rings", "Magic Rings", 
	"Scroll: Hurricane Spin", "Scroll: Super Quake","Stones of Agony", "Stomp Boots", "Whimsical Rings", 
	"Peril Rings", "Non-gameplay Items", "zz067", "zz068", "zz069",
	"zz070", "zz071", "zz072", "zz073", "zz074",
	"zz075", "zz076", "zz077", "zz078", "zz079",
	"zz080", "zz081", "zz082", "zz083", "zz084",
	"zz085", "zz086", "Bow and Arrow (Subscreen Only)", "Letter or Potion (Subscreen Only)",
	"zz089", "zz090", "zz091", "zz092", "zz093", "zz094", "zz095", "zz096",
	"zz097", "zz098", "zz099", "zz100", "zz101", "zz102", "zz103", "zz104",
	"zz105", "zz106", "zz107", "zz108", "zz109", "zz110", "zz111", "zz112",
	"zz113", "zz114", "zz115", "zz116", "zz117", "zz118", "zz119", "zz120",
	"zz121", "zz122", "zz123", "zz124", "zz125", "zz126", "zz127", "zz128",
	"zz129", "zz130", "zz131", "zz132", "zz133", "zz134", "zz135", "zz136",
	"zz137", "zz138", "zz139", "zz140", "zz141", "zz142", "zz143", "zz144",
	"zz145", "zz146", "zz147", "zz148", "zz149", "zz150", "zz151", "zz152",
	"zz153", "zz154", "zz155", "zz156", "zz157", "zz158", "zz159", "zz160",
	"zz161", "zz162", "zz163", "zz164", "zz165", "zz166", "zz167", "zz168",
	"zz169", "zz170", "zz171", "zz172", "zz173", "zz174", "zz175", "zz176",
	"zz177", "zz178", "zz179", "zz180", "zz181", "zz182", "zz183", "zz184",
	"zz185", "zz186", "zz187", "zz188", "zz189", "zz190", "zz191", "zz192",
	"zz193", "zz194", "zz195", "zz196", "zz197", "zz198", "zz199", "zz200",
	"zz201", "zz202", "zz203", "zz204", "zz205", "zz206", "zz207", "zz208",
	"zz209", "zz210", "zz211", "zz212", "zz213", "zz214", "zz215", "zz216",
	"zz217", "zz218", "zz219", "zz220", "zz221", "zz222", "zz223", "zz224",
	"zz225", "zz226", "zz227", "zz228", "zz229", "zz230", "zz231", "zz232",
	"zz233", "zz234", "zz235", "zz236", "zz237", "zz238", "zz239", "zz240",
	"zz241", "zz242", "zz243", "zz244", "zz245", "zz246", "zz247", "zz248",
	"zz249", "zz250", "zz251", "zz252", "zz253", "zz254", "zz255",
	"Custom Weapon 01", "Custom Weapon 02", "Custom Weapon 03", "Custom Weapon 04", "Custom Weapon 05",
	"Custom Weapon 06", "Custom Weapon 07", "Custom Weapon 08", "Custom Weapon 09", "Custom Weapon 10",
	"Ice Rod", "Attack Ring", "Lanterns", "Pearls", "Bottles", "Bottle Fillers", "Bug Nets", "Mirrors",
	"SwitchHooks", "Item Bundle", "Progressive Item", "Note", "Refiller",
	"Lift Glove", "Gravity Boots", "Gravity Up Boots", "Gravity Down Boots",
	"Cooldown Ring", "Enemy Ambush"
};

const char *g_itype_help[itype_max] =
{
	"The Hero's standard weapon. When wielded, it can stab, slash and fire beams. It is used to perform several scroll techniques, too.",
	"When wielded, it flies out, hurting enemies, and collecting items, before returning to the Hero. Can be thrown diagonally. Can optionally drop damaging sparkles.",
	"When wielded, it flies and collects items before hitting an enemy. Requires the Bow. Expends either 1 rupee or 1 arrow ammo. Can drop damaging sparkles.",
	"When wielded, a damaging flame drifts out, which lights dark screens until it expires. Can optionally have wand-style stab and slash sprites.",
	"When wielded, plays strange music and summons a whirlwind that warps you to the Warp Ring locations. Can also dry Water combos on specific screens.",
	"When wielded, drops bait that attracts Walking Enemies depending on their Hunger stat. Removed when used in 'Feed The Goriya' room types.",
	"When wielded in 'Potion Shop' room types, activates the shop and all other Potion Shops in the quest. Is overridden by Potions if you have them.",
	"When wielded, the Hero regains hearts and/or magic. Can also cure jinxes, depending on the Quest Rules.",
	"When wielded, shoots damaging magic. The magic is affected by your current Magic Book item. Can also damage enemies by stabbing and slashing.",
	"Divides the damage that the Hero takes, and changes their palette. If a magic cost is set, the Hero loses magic when they takes damage, and the item is disabled without magic.",
	"Can provide infinite rupees to the Hero, or provide a regenerating supply of rupees. Typically also set to increase their Rupee max.",
	"Makes invisible enemies visible. Currently does not affect Ganon.",
	"When the Hero isn't wielding an item, this deflects or reflects enemy projectiles from the front.",
	"Required to wield the Arrow. This affects the speed of the arrow fired. The Action settings are not used.",
	"Allows the Hero to traverse Raft Paths. When at a Raft Branch combo flag, hold the arrow keys to decide which path the raft will take.",
	"Used to cross Water combos and certain combo types. If Four-Way > 1, the Hero can step sideways off the ladder.",
	"Affects the sprite and damage of the magic shot by the Wand. If 'Fire Magic' is set, a flame is created at the place where the magic stops.",
	"Provides unlimited keys in a specific dungeon level, or all dungeon levels up to a point. The Action settings are not used.",
	"Allows the Hero to push Heavy or Very Heavy push block combos. Can be limited to one push per screen. The Action settings are not used.",
	"Allows the Hero to swim in Water combos. The Power and Action settings are not used.",
	"Prevents damage from certain Damage combos. Can require magic to use, which is drained as the Hero touches the combos.",
	"When wielded, shoots a hook and chain that collects items and hurt enemies, before retracting back to the Hero. Can grab onto various combos.",
	"When wielded, restricts your vision and reveals certain combo flags, as well as hiding or showing certain layers on a screen.",
	"When wielded, pounds and breaks Walking Enemies' shields. It is used to perform the Quake Hammer and Super Quake techniques, too.",
	"When wielded, casts a spell which sends out a wide ring of flames from the Hero. The Hero is invincible while casting the spell.",
	"When wielded, teleports the Hero to the Continue screen of the current DMap.",
	"When wielded, casts a spell which surrounds the Hero with a magic shield that nullifies all damage taken until it expires.",
	"When wielded, places a bomb which explodes to momentarily hurt foes. Expends 1 bomb ammo. Remote bombs only explode when you press the button again after placing.",
	"Similar to Bomb, but has a much larger blast radius, and expends 1 super bomb ammo.",
	"When collected, freezes most enemies and makes the Hero invincible for a limited time.",
	"No built-in effect, but is typically set to increase your Key count by 1 when collected.",
	"No built-in effect, but is typically set to increase your maximum Magic by 32 when collected.",
	"When collected, enables the Triforce for the current dungeon level and plays a cutscene. May warp the Hero out using the current screen's Side Warp A.",
	"When collected, enables the Subscreen Map for the current dungeon level.",
	"When collected, enables the Compass for the current dungeon level.",
	"When collected, enables the Boss Key for the current dungeon level, letting the Hero unlock Boss Lock Blocks, Boss Chest combos and Boss doors.",
	"Can provide infinite arrow ammo to the Hero, or provide a regenerating supply of ammo. Typically also set to increase their arrow ammo max.",
	"When collected, increases the Level-Specific Key count for the current dungeon level. These keys are used in place of normal keys if possible.",
	"When wielded, creates one or more beams that circle the Hero. Beams can be dismissed by pressing the button again. Can optionally have wand-style stab and slash sprites.",
	"No built-in effect, but is typically set to increase your Rupee count when collected.",
	"No built-in effect, but is typically set to increase your Arrow ammo when collected.",
	"Flies around the screen at a certain speed. When collected, the Hero regains hearts and/or magic. Can also cure jinxes, depending on the Quest Rules.",
	"No built-in effect, but is typically set to increase your Magic when collected.",
	"No built-in effect, but is typically set to restore the Hero's hearts when collected.",
	"No built-in effect, but is typically set to increase the Hero's max. health when collected.",
	"When collected, increases the Hero's Heart Piece count by 1. If the 'Per HC' amount (in Init Data) is reached, the Hero's max. health is increased by 1 heart (as specified by the lowest-ID 'Heart Container' item.",
	"When collected, all beatable enemies on the screen are instantly and silently killed.",
	"No built-in effect, but is typically set to increase the Hero's bomb ammo when collected.",
	"Can provide infinite bomb ammo to the Hero, or provide a regenerating supply of ammo. Typically also set to increase their bomb ammo max.",
	"When wielded, the Hero jumps into the air through the 'Z-axis', with an initial Jump speed of 0.8 times the Height Multiplier.",
	"When the Hero is at the apex of a jump, they hovers in the air for a specified time. In sideview, can be dismissed by pressing Down.",
	"When wielding the Sword, the Hero can hold the button to tap solid combos to find bombable locations and release to spin the sword around him.",
	"When performing a Spin Attack with a sword that can fire beams, four beams are released from the sword during each spin.",
	"When wielding the Hammer, the Hero can hold the button and release to pound for extra damage, and stunning most nearby ground enemies.",
	"Reduces the duration of jinxes given by certain enemies, or (if Divisor is 0) prevents them entirely. The Action settings are not used.",
	"Reduces the time it takes to charge the Spin Attack/Quake Hammer (Charging) and Hurricane Spin/Super Quake (Magic C.) abilities.",
	"Enables the Sword to fire sword beams when the Hero's health is below a certain amount. The Action settings are not used.",
	"In Shop room types, shop prices are multiplied by the Discount Amount, making the items cheaper.",
	"Gradually restores the Hero's health in certain quantities over a certain duration. The Action settings are not used.",
	"Gradually restores the Hero's magic in certain quantities over a certain duration. Can also provide infinite magic to the Hero. The Action settings are unused.",
	"After charging the Spin Attack, holding down the button longer enables a stronger attack with faster and more numerous spins.",
	"After charging the Quake Hammer, holding down the button longer enables a stronger attack which stuns more enemies for longer.",
	"The Hero's sprite faintly vibrates when they stands on or near secret-triggering combo flags. Sensitivity increases the distance at which it works.",
	"If the Hero, while jumping, lands on an enemy, that enemy takes a certain amount of damage, instead of damaging the Hero.",
	"The Sword, Wand, and Hammer will occasionally do increased damage in a single strike.",
	"Divides the damage that the Hero takes when their health is below a certain level.",
	"These items have no built-in effect. They will not be dropped in an Item Drop Set.",
	//ic67 to 86, custom IC
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", //ic87
	"Displays the bow and arrow together as a single item. No item should use this class; it is intended for use in subscreens only.",
	"Represents either the letter or a potion, whichever is available at the moment. No item should use this class; It is intended for use in subscreens only.",
	"This item class is reserved for future versions of ZC", //ic_last, 89
	//90
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", 
	"", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "", "",
	"", "", "", "", "",

	//256 == script01
	"This item class is used to create LW_SCRIPT_01 type lweapons with properties defined by the Item Editor.",
	//257 == script02
	"This item class is used to create LW_SCRIPT_02 type lweapons with properties defined by the Item Editor.",
	//258 == script03
	"This item class is used to create LW_SCRIPT_03 type lweapons with properties defined by the Item Editor.",
	//258 == script04
	"This item class is used to create LW_SCRIPT_04 type lweapons with properties defined by the Item Editor.",
	//259 == script05
	"This item class is used to create LW_SCRIPT_05 type lweapons with properties defined by the Item Editor.",
	//260 == script06
	"This item class is used to create LW_SCRIPT_06 type lweapons with properties defined by the Item Editor.",
	//261 == script07
	"This item class is used to create LW_SCRIPT_07 type lweapons with properties defined by the Item Editor.",
	//262 == script08
	"This item class is used to create LW_SCRIPT_08 type lweapons with properties defined by the Item Editor.",
	//263 == script09
	"This item class is used to create LW_SCRIPT_09 type lweapons with properties defined by the Item Editor.",
	//264 == script10
	"This item class is used to create LW_SCRIPT_10 type lweapons with properties defined by the Item Editor.",
	//265
	"This item class is reserved for the Ice Rod in a future version of ZC", //icerod
	"The Sword, Wand, and Hammer deal more damage. The multiplier applies before the bonus damage.", //Atkring
	"Emanates light passively from the Hero in dark rooms.", //Lantern
	"Prevents the Hero from becoming a Bunny on certain dmaps.", //Pearls
	"Can be filled with bottle contents, and trigger their effects.\n"
		"The drawn tile of the bottle's animation is offset by its AFrames (min 1)"
		" times the value of its' contents (empty = 0, so no offset).", //Bottle
	"Fills the first empty bottle you have with a particular content", //Bottlefill
	//270
	"Can be swung, does not collide with enemies. Can catch fairies to put in bottles.", //Bugnet
	"Either warps to the continue point or warps to another dmap at the same screen location,"
		" depending on dmap settings. May or may not leave a return portal."
		"\nSet the DMap Flag 'Mirror Continues instead of Warping' to make the mirror return"
		" the Hero to their continue point on that dmap."
		"\nOtherwise, set the 'Mirror DMap' to the ID of the dmap you wish to warp to.", //Mirrors
	"Acts similarly to the hookshot, but swaps the Hero with certain blocks, or enemies.",
	"When collected, gives the Hero a number of other items together.",
	"Displays itself graphically as the next item not owned in a set of items."
		" Acts as picking up that item when collected.\n\nIf an item increases"
		" the max of a counter, but won't be able to due to the 'But Not Above...'"
		" field, it is considered 'owned', in addition to any 'Equipment Item'"
		" that has been collected.\nHeart pieces check the counter max increase"
		" of the heart container they grant.", //Progressive
	"When used, displays a string onscreen. Especially useful when combined"
		" with string control codes.", //Note
	"When used, can refill up to 5 counters, and optionally cure sword jinxes.",
	"When used, attempts to lift something in front of the Hero, OR throws the"
		" already lifted object.", // Lift Glove
	"Modifies the player's gravity while owned.", // Gravity Boots
	"Modifies the player's gravity when holding 'Up' in sideview.", // Gravity Up Boots
	"Modifies the player's gravity when holding 'Down' in sideview.", // Gravity Down Boots
	"Modifies the cooldown of all items with cooldowns.", // Cooldown Ring
	"When this item spawns, instead replaces it with a number of enemies.", // Enemy Ambush
};

const char g_weap_names[wMax][255] =
{
	"(None)","Sword","Sword Beam","Boomerang","Bomb Blast",
	"Super Bomb Blast","Bomb","Super Bomb","Arrow","Fire",
	//10
	"Whistle","Bait","-Melee Handle","Magic","-Catching",
	"Wind","Reflected Magic","Reflected Fireball","Reflected Rock","Hammer",
	//20
	"Hookshot","-Hookshot Handle","-Hookshot Chain","Sparkle","Fire Sparkle",
	"-Smack","-Phantom","Byrna Beam","Reflected Beam","Stomp Boots",
	//30
	"-lwMax","Custom Weapon 1","Custom Weapon 2","Custom Weapon 3","Custom Weapon 4",
	"Custom Weapon 5","Custom Weapon 6","Custom Weapon 7","Custom Weapon 8","Custom Weapon 9",
	//40
	"Custom Weapon 10","-Ice","-Flame","-Sound","Thrown",
	"-Pot","-Lit","-Med1","-Med2","-Med3",
	//50
	"-Sword180","-SwordLA","-Bug Net","Reflected Arrow","Reflected Fire 1",
	"Reflected Fire 2","","","","",
	//60
	"","","","","","","","","","",
	//70
	"","","","","","","","","","",
	//80
	"","","","","","","","","","",
	//90
	"","","","","","","","","","",
	//100
	"","","","","","","","","","",
	//110
	"","","","","","","","","","",
	//120
	"","","","","",
	"","","","-wEnemyWeapons","Fireball",
	//130
	"Arrow","Boomerang","Sword","Rock","Magic",
	"Bomb Blast","Super Bomb Blast","Bomb","Super Bomb","Fire Trail",
	//140
	"Fire 1","Wind","Fire 2","-Fire Trail 2","-Ice",
	"Fireball (Rising)"
	//wMax
};

const char g_ctr_names[MAX_COUNTERS][255] =
{
	"Life","Rupees","Bombs","Arrows","Magic",
	"Keys","Super Bombs","Custom 1","Custom 2","Custom 3",
	"Custom 4","Custom 5","Custom 6","Custom 7","Custom 8",
	"Custom 9","Custom 10","Custom 11","Custom 12","Custom 13",
	"Custom 14","Custom 15","Custom 16","Custom 17","Custom 18",
	"Custom 19","Custom 20","Custom 21","Custom 22","Custom 23",
	"Custom 24","Custom 25", "Custom 26", "Custom 27", "Custom 28",
	"Custom 29", "Custom 30", "Custom 31", "Custom 32", "Custom 33",
	"Custom 34", "Custom 35", "Custom 36", "Custom 37", "Custom 38",
	"Custom 39", "Custom 40", "Custom 41", "Custom 42", "Custom 43",
	"Custom 44", "Custom 45", "Custom 46", "Custom 47", "Custom 48",
	"Custom 49", "Custom 50", "Custom 51", "Custom 52", "Custom 53",
	"Custom 54", "Custom 55", "Custom 56", "Custom 57", "Custom 58",
	"Custom 59", "Custom 60", "Custom 61", "Custom 62", "Custom 63",
	"Custom 64", "Custom 65", "Custom 66", "Custom 67", "Custom 68",
	"Custom 69", "Custom 70", "Custom 71", "Custom 72", "Custom 73",
	"Custom 74", "Custom 75", "Custom 76", "Custom 77", "Custom 78",
	"Custom 79", "Custom 80", "Custom 81", "Custom 82", "Custom 83",
	"Custom 84", "Custom 85", "Custom 86", "Custom 87", "Custom 88",
	"Custom 89", "Custom 90", "Custom 91", "Custom 92", "Custom 93",
	"Custom 94", "Custom 95", "Custom 96", "Custom 97", "Custom 98",
	"Custom 99", "Custom 100"
};

const char g_etype_names[eeMAX][255] =
{
	"-Guy", "Walking Enemy", "-DEPRECATED", "Tektite", "Leever", "Peahat", "Zora", "Rock", "Ghini", "-DEPRECATED",
	"Keese", "-DEPRECATED", "-DEPRECATED", "-DEPRECATED", "-DEPRECATED", "Trap", "Wall Master", "-DEPRECATED", "-DEPRECATED", "-DEPRECATED",
	"-DEPRECATED", "Wizzrobe", "Aquamentus", "Moldorm", "Dodongo", "Manhandla", "Gleeok", "Digdogger", "Ghoma", "Lanmola", "Patra",
	"Ganon", "Projectile Shooter", "-DEPRECATED", "-DEPRECATED", "-DEPRECATED", "-DEPRECATED", "Spin Tile", "(None)", "-Fairy", "Other (Floating)",
	"Other", "-OLDMAX",
	"Custom 01","Custom 02","Custom 03","Custom 04","Custom 05","Custom 06","Custom 07","Custom 08","Custom 09","Custom 10",
	"Custom 11","Custom 12","Custom 13","Custom 14","Custom 15","Custom 16","Custom 17","Custom 18","Custom 19","Custom 20",
	"Friendly NPC 01","Friendly NPC 02","Friendly NPC 03","Friendly NPC 04","Friendly NPC 05",
	"Friendly NPC 06","Friendly NPC 07","Friendly NPC 08","Friendly NPC 09","Friendly NPC 10"
};

std::string zq_arg(const char *s) { return s ? s : ""; }
std::string zq_arg(const std::string &s) { return s; }
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

std::string ordinal(int n)
{
    const char *suf = "th";
    if(n % 100 < 11 || n % 100 > 13)
    {
        if(n % 10 == 1) suf = "st";
        else if(n % 10 == 2) suf = "nd";
        else if(n % 10 == 3) suf = "rd";
    }
    return std::to_string(n) + suf;
}

struct NameVal { const char *name; int value; };
const NameVal g_defenses[] =
{
    { "(None)", edNORMAL },
    { "1/2 Damage", edHALFDAMAGE },
    { "1/4 Damage", edQUARTDAMAGE },
    { "Stun", edSTUNONLY },
    { "Stun Or Block", edSTUNORCHINK },
    { "Stun Or Ignore", edSTUNORIGNORE },
    { "Block If < 1", edCHINKL1 },
    { "Block If < 2", edCHINKL2 },
    { "Block If < 4", edCHINKL4 },
    { "Block If < 6", edCHINKL6 },
    { "Block If < 8", edCHINKL8 },
    { "Block", edCHINK },
    { "Ignore If < 1", edIGNOREL1 },
    { "Ignore", edIGNORE },
    { "One-Hit-Kill", ed1HKO },
    { "Block if Power < 10", edCHINKL10 },
    { "Double Damage", ed2x },
    { "Triple Damage", ed3x },
    { "Quadruple Damage", ed4x },
    { "Enemy Gains HP = Damage", edHEAL },
    { "Trigger Screen Secrets", edTRIGGERSECRETS },
    { "Split", edSPLIT },
    { "Transform", edREPLACE },
    { "Bomb Explosion", edEXPLODESMALL },
    { "Superbomb Explosion", edEXPLODELARGE },
    { "Harmless Explosion", edEXPLODEHARMLESS },
    { "Summon", edSUMMON },
    { "Switch w/ Player", edSWITCH }
};
const NameVal g_anims[] =
{
    { "(None)", 0 },
    { "Flip", 1 },
    { "-Unused", 2 },
    { "2-Frame", 3 },
    { "-Unused", 4 },
    { "Octorok (NES)", 5 },
    { "Tektite (NES)", 6 },
    { "Leever (NES)", 7 },
    { "Walker", 8 },
    { "Zora (NES)", 9 },
    { "Zora (4-Frame)", 10 },
    { "Ghini", 11 },
    { "Armos (NES)", 12 },
    { "Rope", 13 },
    { "Wall Master (NES)", 14 },
    { "Wall Master (4-Frame)", 15 },
    { "Darknut (NES)", 16 },
    { "Vire", 17 },
    { "3-Frame", 18 },
    { "Wizzrobe (NES)", 19 },
    { "Aquamentus", 20 },
    { "Dodongo (NES)", 21 },
    { "Manhandla", 22 },
    { "Gleeok", 23 },
    { "Digdogger", 24 },
    { "Gohma", 26 },
    { "Lanmola", 26 },
    { "2-Frame Flying", 27 },
    { "4-Frame 4-Dir + Tracking", 28 },
    { "4-Frame 8-Dir + Tracking", 29 },
    { "4-Frame 4-Dir + Firing", 30 },
    { "4-Frame 4-Dir", 31 },
    { "4-Frame 8-Dir + Firing", 32 },
    { "Armos (4-Frame)", 33 },
    { "4-Frame Flying 4-Dir", 34 },
    { "4-Frame Flying 8-Dir", 35 },
    { "-Unused", 36 },
    { "4-Frame 8-Dir Big", 37 },
    { "Tektite (4-Frame)", 38 },
    { "3-Frame 4-Dir", 39 },
    { "2-Frame 4-Dir", 40 },
    { "Leever (4-Frame)", 41 },
    { "2-Frame 4-Dir + Tracking", 42 },
    { "Wizzrobe (4-Frame)", 43 },
    { "Dodongo (4-Frame)", 44 },
    { "Dodongo BS (4-Frame)", 45 },
    { "4-Frame Flying 8-Dir + Firing", 46 },
    { "4-Frame Flying 4-Dir + Firing", 47 },
    { "4-Frame", 48 },
    { "Ganon", 49 },
    { "2-Frame Big", 50 },
    { "4-Frame 8-Dir Big + Tracking", 51 },
    { "4-Frame 4-Dir Big + Tracking", 52 },
    { "4-Frame 8-Dir Big + Firing", 53 },
    { "4-Frame 4-Dir Big", 54 },
    { "4-Frame 4-Dir Big + Firing", 55 }
};

const char *weap_name(int w) { return w >= 0 && w < (int)wMax ? g_weap_names[w] : ""; }

void item_labels(int type, unsigned flags, Q30ItemLabels &inf)
{
#define _SET(mem, str, helpstr) do { inf.mem = str; inf.h_##mem = helpstr; } while(false)
#define FLAG(val) (flags & item_flag##val)
#define SWAPTYPE(indx, val) inf.misc_input_type[indx] = val
    std::string classname(q30_itemclass_name(type));
    switch(type)
	{
		case itype_fairy:
		{
			_SET(misc[2], "Step Speed:", "The movement speed of the fairy, in 100ths of pixel/frame");
			_SET(misc[3], "Bottle Fill:", "What bottle type to fill an empty bottle with if caught using a Bug Net");
			_SET(flag[0], "Life is Percent", "HP Regained is a percentage out of max HP");
			_SET(flag[1], "Magic is Percent", "MP Regained is a percentage out of max MP");
			if(FLAG(1))
				_SET(misc[0], "% HP Regained:", "Percentage of max life restored when collected.");
			else
				_SET(misc[0], "HP Regained:", "Life (in points) restored when collected.");
			if(FLAG(2))
				_SET(misc[1], "% MP Regained:", "Percentage of max magic restored when collected.");
			else
				_SET(misc[1], "MP Regained:", "Magic (in points) restored when collected.");
			_SET(actionsnd[0], "Item Drop Sound:", "Plays SFX when dropped");
			break;
		}
		case itype_triforcepiece:
		{
			_SET(misc[0], "Cutscene MIDI:", "If non-zero, overrides the cutscene midi");
			_SET(misc[1], "Cutscene Type (0-1):", "If >0, uses the 'big mcguffin' cutscene style");
			_SET(misc[2], "Second Collect Sound:", "A second sound to play on pickup, played only if flag 'Play Second SFX' is checked.");
			_SET(misc[3], "Custom Cutscene Duration", "If nonzero, overrides the duration of the cutscene, in frames");
			_SET(misc[4], "Custom Refill Frame", "If non-zero, changes the timing of the mcguffin's life refill.");
			_SET(flag[0], "Side Warp Out", "Warp out using sidewarp A upon completion");
			_SET(flag[2], "Removes Sword Jinxes", "Heal sword jinxes on pickup");
			_SET(flag[3], "Removes Item Jinxes", "Heal item jinxes on pickup");
			_SET(flag[4], "Removes Shield Jinxes", "Heal shield jinxes on pickup");
			_SET(flag[7], "Ownable", "Can be owned; this is normally handled by 'Equipment Item', but not for mcguffin piece items.");
			_SET(flag[8], "Don't Dismiss Messages", "If not checked, the cutscene clears screen strings");
			_SET(flag[9], "Cutscene Interrupts Action Script", "The action script, if running on collection, is paused for the duration of the cutscene.");
			_SET(flag[10], "Don't Affect Music", "If not checked, music is stopped for the cutscene");
			_SET(flag[11], "No Cutscene", "Skip the cutscene animation");
			_SET(flag[12], "Run Action Script on Collection", "Item action script runs when it is collected");
			_SET(flag[13], "Play Second SFX", "Plays the 'Second Collect Sound' SFX on pickup");
			_SET(flag[14], "Don't Play MIDI", "Don't play a cutscene midi if checked");
			_SET(actionsnd[0], "Collect Sound:", "First sound played on pickup");
			break;
		}
		case itype_shield:
		{
			_SET(misc[0], "Block Flags:", "(Rock=1, Arrow=2, BRang=4, Fireball=8, Sword=16, Magic=32, Flame=64, Script(All)=128, Fireball2=256, Lightbeam=512,\n"
				"Script1=1024, Script2=2048, Script3=4096, Script4=8192, Script5=16384, Script6=32768, Script7=65536, Script8=131072, Script9=262144, Script10=524288)\n"
				"Sum all of the values you want to apply. Weapons and lightbeams with their flags set will be blocked from in front of the Hero.");
			_SET(misc[1], "Reflect Flags:", "(Rock=1, Fireball=8, Sword=16, Magic=32, Flame=64, Script(All)=128, Fireball2=256, Lightbeam=512,\n"
				"Script1=1024, Script2=2048, Script3=4096, Script4=8192, Script5=16384, Script6=32768, Script7=65536, Script8=131072, Script9=262144, Script10=524288)\n"
				"Sum all of the values you want to apply. Weapons and lightbeams with their flags set will be reflected from in front of the Hero.\n"
				"Weapons are only reflected if their value is in both 'Block Flags' and 'Reflect Flags'.");
			_SET(actionsnd[0], "De/Reflection Sound:", "Plays when the shield successfully blocks or reflects a weapon");
			
			_SET(flag[0], "Protects Front", "The shield will protect the front side of the Hero");
			_SET(flag[1], "Protects Back", "The shield will protect the back side of the Hero");
			_SET(flag[2], "Protects Left", "The shield will protect the left side of the Hero");
			_SET(flag[3], "Protects Right", "The shield will protect the right side of the Hero");
			_SET(flag[8], "Active Use", "If enabled, the shield only protects the Hero (and provides its' LTM)"
				" while it is equipped to a button that is being held.");
			if(FLAG(9))
			{
				_SET(actionsnd[1], "Activation Sound:", "Plays when the shield button is pressed");
				_SET(flag[4], "Inactive Front", "Protects the front when button is NOT held");
				_SET(flag[5], "Inactive Back", "Protects the back when button is NOT held");
				_SET(flag[6], "Inactive Left", "Protects the left when button is NOT held");
				_SET(flag[7], "Inactive Right", "Protects the right when button is NOT held");
				_SET(misc[4], "Button", "If 0, the item must be equipped to a button to use it.\n"
					"Otherwise, any of the specified buttons will activate the glove, even when not equipped to a button.\n"
					"Sum all the buttons you want to be usable:\n(A=1, B=2, L=4, R=8, Ex1=16, Ex2=32, Ex3=64, Ex4=128)");
				_SET(misc[5], "Inactive PTM", "Hero Tile Modifier to use while shield is inactive");
				_SET(flag[9], "Change Speed", "Change the Hero's walking speed while the shield is active");
				_SET(flag[10], "Lock Direction", "When the shield is activated, lock the Hero's direction until"
					" it is released.");
				if(FLAG(10))
				{
					_SET(misc[6], "Speed Percentage", "A percentage multiplier for the Hero's movement speed."
						" A negative value will give that amount *more* speed; i.e. '-100' is the same as '200'.");
					_SET(misc[7], "Speed Bonus", "A step value (in 100ths of a pixel per frame) to be added to the"
						" Hero's speed.");
				}
			}
			break;
		}
		case itype_agony:
		{
			_SET(power, "Sensitivity:", "The radius in which flags are checked for");
			_SET(misc[0], "Vibration Frequency", "0-58, higher = faster vibrations");
			break;
		}
		case itype_wealthmedal:
		{
			_SET(misc[0], "Discount Amount:", "If 'Discount is Percent' is checked, this is the percentage of the price that will still be charged.\n"
				"else, this many rupees are ADDED to the price (use negative to subtract)");
			_SET(flag[0], "Discount Is Percent", "Makes the discount percentage-based");
			break;
		}
		case itype_cbyrna: //!TODO Help Text
		{
			inf.power = "Beam Damage:";
			inf.misc[0] = "Beam Speed Divisor:";
			inf.misc[1] = "Orbit Radius:";
			inf.misc[2] = "Number Of Beams:";
			inf.misc[3] = "Melee Damage:";
			inf.misc[9] = "Slash Sound:";
			inf.flag[0] = "Penetrates Enemies";
			_SET(flag[1], "Invincible Hero", "Make the Hero invincible from damage while the beam is out.");
			inf.flag[2] = "No Melee Attack";
			inf.flag[3] = "Can Slash";
			inf.flag[5] = "Melee Weapons Use Magic Cost";
			inf.flag[6] = "Doesn't Pick Up Dropped Items";
			inf.flag[7] = "Flip Right-Facing Slash";
			inf.wpn[0] = "Stab Sprite:";
			inf.wpn[1] = "Slash Sprite:";
			inf.wpn[2] = "Beam Sprite:";
			inf.wpn[3] = "Sparkle Sprite:";
			inf.wpn[4] = "Damaging Sparkle Sprite:";
			inf.actionsnd[0] = "Orbit Sound:";
			if (FLAG(2))
			{
				_SET(flag[4], "Invincibility Stops Knockback",
					"The Hero's invincibility stops not only damage, but also knockback/iframes.");
			}
			break;
		}
		case itype_whispring:
		{
			_SET(power, "Jinx Divisor:", "Amount to divide jinx duration by.\n"
				"If 'Perm. Jinx Are Temp' is checked, perm jinxes last for 150/[divisor] frames."
				" If divisor is 0, Hero is immune to affected jinxes.\n"
				"When picked up, regardless of flags, perm jinxes matching the Jinx Type become 150-frame temp jinxes.");
			_SET(misc[0], "Jinx Type:", "Sum all of the values you want to apply. 1 = sword, 2 = item, 4 = shield");
			_SET(flag[0], "Perm. Jinx Are Temp.", "Perm jinxes inflicted instead are temp for 150/[divisor] frames");
			break;
		}
		case itype_ladder:
		{
			_SET(power, "Four-Way:", "If >1, the Hero can exit the ladder in all 4 directions.\n"
				"If <=1, the Hero can only exit parallel to the direction they entered from.");
			_SET(flag[0], "Can Ladder Over Pitfalls", "Can be used to cross Pitfall combos");
			break;
		}
		case itype_magickey:
		{
			_SET(power, "Dungeon Level:", "What dungeon level to grant infinite keys for");
			_SET(flag[0], "Lesser D. Levels Also", "If checked, lower levels are also affected");
			break;
		}
		case itype_wallet:
		{
			_SET(power, "Infinite Supply:", "If >0, grants infinite money");
			_SET(misc[0], "Increase Amount:", "How much money regenerate over time from having this");
			_SET(misc[1], "Delay Duration:", "How often money regenerate from having this, in frames");
			break;
		}
		case itype_quiver:
		{
			_SET(power, "Infinite Supply:", "If >0, grants infinite arrows");
			_SET(misc[0], "Increase Amount:", "How many arrows regenerate over time from having this");
			_SET(misc[1], "Delay Duration:", "How often arrows regenerate from having this, in frames");
			break;
		}
		case itype_bombbag:
		{
			_SET(power, "Infinite Supply:", "If >0, grants infinite bombs");
			_SET(misc[0], "Increase Amount:", "How many bombs regenerate over time from having this");
			_SET(misc[1], "Delay Duration:", "How often bombs regenerate from having this, in frames");
			_SET(flag[0], "Super Bombs Also", "Also regenerates S. Bombs");
			break;
		}
		case itype_quakescroll:
		case itype_quakescroll2:
		{
			_SET(power, "Damage Multiplier:", "Multiplier of the hammer's damage to deal");
			_SET(misc[0], "Stun Duration:", "Duration, in frames, to stun enemies for.");
			_SET(misc[1], "Stun Radius:", "Radius, in pixels, to stun enemies in.");
			_SET(actionsnd[0], "Quake Sound:", "Sound played when the hammer is swung");
			_SET(actionsnd[1], "Charge Sound:", "Sound played when the hammer is charged");
			_SET(flag[0], "Pay on swing", "The use cost will be consumed when the charge is"
				" unleashed, instead of when it is charged up.");
			break;
		}
		case itype_perilscroll: //!TODO Help Text
		{
			inf.misc[0] = "Maximum Hearts:";
			break;
		}
		case itype_spinscroll:
		case itype_spinscroll2:
		{
			_SET(power, "Damage Multiplier:", "Multiplier of the sword's damage to deal");;
			_SET(misc[0], "Number of Spins:", "Number of times to spin the sword");
			_SET(actionsnd[0], "Spinning Sound:", "The sound to play while spinning");
			_SET(actionsnd[1], "Charge Sound:", "The sound to play when charged");
			_SET(flag[0], "Pay on swing", "The use cost will be consumed when the charge is"
				" unleashed, instead of when it is charged up.");
			break;
		}
		case itype_clock:
		{
			_SET(misc[0], "Duration:", "How long the invincibility lasts, in frames. 0 = infinite.");
			_SET(flag[0], "Active-Use", "If enabled, activates on use as an 'Equipment Item' instead"
				" of on pickup.");
			_SET(flag[1], "Can't trigger while active", "If enabled, will not activate while a clock"
				" is already active.");
			_SET(actionsnd[0], "Activation Sound", "SFX to play when the effect activates");
			break;
		}
		case itype_killem:
		{
			_SET(flag[0], "Active-Use", "If enabled, activates on use as an 'Equipment Item' instead"
				" of on pickup.");
			_SET(actionsnd[0], "Activation Sound", "SFX to play when the effect activates");
			break;
		}
		case itype_refill:
		{
			for(auto q = 0; q < 5; ++q)
			{
				std::string numstr = std::to_string(q + 1);
				std::string ordstr = ordinal(q + 1);
				_SET(misc[q], "Counter " + numstr, "The " + ordstr + " counter that will be refilled when activating the item."
					"\nIf <0, acts as 'no counter'.");
				_SET(misc[q+5], "Amount " + numstr, "How much the " + ordstr + " counter will be refilled by.");
			}
			_SET(flag[0], "Cures Sword Jinx", "When activated, cures sword jinx effects");
			_SET(flag[1], "Gradual Refill", "Counters refill gradually instead of immediately.");
			_SET(actionsnd[0], "Activation Sound", "SFX to play when the item is used");
			break;
		}
		case itype_liftglove:
		{
			_SET(misc[0], "Button", "If 0, the item must be equipped to a button to use it.\n"
				"Otherwise, any of the specified buttons will activate the glove, even when not equipped to a button.\n"
				"Sum all the buttons you want to be usable:\n(A=1, B=2, L=4, R=8, Ex1=16, Ex2=32, Ex3=64, Ex4=128)");
			_SET(misc[1], "Throw Step", "The speed, in 1/100ths pixel per frame, of the thrown object");
			_SET(misc[2], "Throw Jump", "The jump value, in 1/100ths pixel per frame, of the thrown object");
			_SET(actionsnd[0], "Lift Sound", "SFX to play when an object is lifted,"
				"\nand does not have its' own lift sound.");
			_SET(actionsnd[1], "Throw Sound", "SFX to play when an object is thrown");
			_SET(flag[0], "Fake Z Throw", "Throws weapons in the fakez axis instead of the z axis");
			_SET(flag[1], "Lift In Water", "Allows lifting while swimming");
			_SET(flag[2], "Disable Shield", "Makes the shield not protect from projectiles, in the same way as when the Hero is attacking");
			_SET(flag[3], "Disable Item Use", "Stop the use of items unrelated to lifting while an object is lifted");
			_SET(flag[4], "Drop When Hit", "Held object is dropped when receiving damage");
			break;
		}
		case itype_gravity_boots:
		{
			_SET(misc[0], "Passive Gravity", "The player's Gravity while they own this item, in px/frame^2");
			_SET(misc[1], "Passive TerminalV", "The player's Terminal Velocity while they own this item, in px/frame");
			_SET(flag[0], "Sideview Only", "The change will only apply while in Sideview");
			SWAPTYPE(0, nswapDEC);
			SWAPTYPE(1, nswapDEC);
			break;
		}
		case itype_gravity_up_boots:
		{
			_SET(misc[0], "Up Gravity", "The player's Gravity while in sideview"
				" and holding the Up direction, in px/frame^2");
			_SET(misc[1], "Up TerminalV", "The player's Terminal Velocity while in sideview"
				" and holding the Up direction, in px/frame");
			SWAPTYPE(0, nswapDEC);
			SWAPTYPE(1, nswapDEC);
			break;
		}
		case itype_gravity_down_boots:
		{
			_SET(misc[0], "Down Gravity", "The player's Gravity while in sideview"
				" and holding the Down direction, in px/frame^2");
			_SET(misc[1], "Down TerminalV", "The player's Terminal Velocity while in sideview"
				" and holding the Down direction, in px/frame");
			SWAPTYPE(0, nswapDEC);
			SWAPTYPE(1, nswapDEC);
			break;
		}
		case itype_cooldown_ring:
		{
			_SET(misc[0], "Add (before)", "Add this many frames to all item cooldowns, before multiplication and division. (Use negatives to subtract)");
			_SET(misc[1], "Multiply", "Multiply all cooldowns by this. (Yes, 0 works)");
			_SET(misc[2], "Divide", "Divide all cooldowns by this. If '0', treated as '1'.");
			_SET(misc[3], "Add (after)", "Add this many frames to all item cooldowns, after multiplication and division. (Use negatives to subtract)");
			_SET(flag[0], "Affect 0-cooldown Items", "Only if checked will items with '0' as their item editor cooldown be affected.");
			_SET(flag[1], "Strictly Decrease", "If the result would make the cooldown longer, it is ignored.");
			_SET(flag[2], "Strictly Increase", "If the result would make the cooldown shorter, it is ignored.");
			break;
		}
		case itype_magicring:
		{
			_SET(power, "Infinite Magic:", "If >0, grants infinite magic");
			_SET(misc[0], "Increase Amount:", "How much magic regenerate over time from having this");
			_SET(misc[1], "Delay Duration:", "How often magic regenerate from having this, in frames");
			break;
		}
		case itype_heartring:
		{
			_SET(misc[0], "Increase Amount:", "How much life regenerate over time from having this");
			_SET(misc[1], "Delay Duration:", "How often life regenerate from having this, in frames");
			break;
		}
		case itype_chargering: //!TODO Help Text
		{
			inf.misc[0] = "Charging Duration:";
			inf.misc[1] = "Magic C. Duration:";
			break;
		}
		case itype_hoverboots: //!TODO Help Text
		{
			inf.misc[0] = "Hover Duration:";
			inf.flag[0] = "Timer only resets on landing";
			inf.wpn[0] = "Halo Sprite:";
			inf.actionsnd[0] = "Hovering Sound:";
			break;
		}
		case itype_rocs:
		{
			_SET(misc[0], "Extra Jumps:", "The number of times this item can be used in mid-air"
				" without landing.");
			_SET(misc[1], "Button", "If 0, the item must be equipped to a button to use it.\n"
				"Otherwise, any of the specified buttons will jump, even when not equipped to a button.\n"
				"Sum all the buttons you want to be usable:\n(A=1, B=2, L=4, R=8, Ex1=16, Ex2=32, Ex3=64, Ex4=128)");
			if(FLAG(1))
				_SET(power, "Jump Power:", "The Hero will jump with a force of 'power'");
			else _SET(power, "Jump Power:", "The Hero will jump with a force of '(power*80)+160)'");
			if(FLAG(2))
				_SET(misc[2], "Held Gravity", "Gravity applied to Link when this item is held. Used for variable jumps"
					" such as holding the button to jump higher. Value is divided by 100 (314 would be equal to 3.14).");
			if(FLAG(5))
				_SET(misc[3], "Held Terminal Velocity", "Max fall speed of Link when this item is held. Useful for floating down while holding jump.");
			_SET(misc[4], "Coyote Time", "Number of frames after leaving a ledge via non-jump that you can still jump");
			if(FLAG(6))
			{
				_SET(misc[5], "Jump Release Decay", "This much will be subtracted from the player's Jump each frame after the button is released."
					" (This will not cause it to go below 0. If this is set to 0, the player's jump will be fully neutralized instead.)");
				SWAPTYPE(5, nswapDEC);
			}
			_SET(flag[0], "Jump is Power/100", "If enabled, the Hero jumps with a force"
				" of 'power' instead of '(power*80)+160'");
			_SET(flag[1], "Hold to change Hero gravity", "If enabled, holding the button this item is attached to will change"
				" the Hero's gravity to the value specified in attributes[2]");
			_SET(flag[4], "Hold to change Hero terminal velocity", "If enabled, holding the button this item is attached to will change"
				" the Hero's terminal velocity (max fall speed) to the value specified in attributes[3]");
			if(FLAG(2))
				_SET(flag[2], "Held Gravity doesn't affect downward momentum", "If enabled, 'Hold to change Hero gravity' will"
					" not affect the Hero when falling.");
			if(FLAG(2))
				_SET(flag[3], "Held Gravity doesn't affect upward momentum", "If enabled, 'Hold to change Hero gravity' will"
					" not affect the Hero when rising.");
			_SET(flag[5], "Decay On Release", "If enabled, the player's jump speed drops after the item button is released.");
			inf.actionsnd[0] = "Jumping Sound:";
			break;
		}
		case itype_sbomb: //!TODO Help Text
		{
			inf.power = "Damage:";
			inf.misc[0] = "Fuse Duration (0 = Remote):";
			inf.misc[1] = "Max. On Screen:";
			inf.misc[2] = "Damage to Hero:";
			_SET(misc[6], "Boom Radius", "If 0, uses a classic boxy hitbox- otherwise uses a circular radius of this many pixels." + std::string());
			inf.flag[1] = "Explosion Hurts Hero";
			_SET(flag[2], "Stops Movement on Landing", "If the weapon lands due to gravity, its step will be set to 0.");
			_SET(flag[3], "Auto-Lift", "If the Hero owns a Lift Glove, place the bomb directly in the Hero's hands.");
			inf.wpn[0] = "Bomb Sprite:";
			inf.wpn[1] = "Explosion Sprite:";
			inf.actionsnd[0] = "Explosion Sound:";
			break;
		}
		case itype_bomb: //!TODO Help Text
		{
			inf.power = "Damage:";
			inf.misc[0] = "Fuse Duration (0 = Remote):";
			inf.misc[1] = "Max. On Screen:";
			inf.misc[2] = "Damage to Hero:";
			_SET(misc[6], "Boom Radius", "If 0, uses a classic boxy hitbox- otherwise uses a circular radius of this many pixels." + std::string());
			inf.flag[0] = "Use 1.92 Timing";
			inf.flag[1] = "Explosion Hurts Hero";
			_SET(flag[3], "Auto-Lift", "If the Hero owns a Lift Glove, place the bomb directly in the Hero's hands.");
			inf.wpn[0] = "Bomb Sprite:";
			inf.wpn[1] = "Explosion Sprite:";
			inf.actionsnd[0] = "Explosion Sound:";
			break;
		}
		case itype_divineprotection: //!TODO Help Text
		{
			inf.misc[0] = "Duration:";
			inf.flag[0] = "Rocket Flickers";
			inf.flag[1] = "Translucent Rocket";
			inf.flag[2] = "Translucent Shield";
			inf.flag[3] = "Shield Flickers";
			_SET(flag[4], "Invincibility Stops Knockback",
				"The Hero's invincibility stops not only damage, but also knockback/iframes.");
			inf.wpn[0] = "Left Rocket Sprite:";
			inf.wpn[1] = "L. Rocket Return Sprite:";
			inf.wpn[2] = "L. Rocket Sparkle Sprite:";
			inf.wpn[3] = "L. Return Sparkle Sprite:";
			inf.wpn[4] = "Shield Sprite (2x2, Over):";
			inf.wpn[5] = "Right Rocket Sprite:";
			inf.wpn[6] = "R. Rocket Return Sprite:";
			inf.wpn[7] = "R. Rocket Sparkle Sprite:";
			inf.wpn[8] = "R. Return Sparkle Sprite:";
			inf.wpn[9] = "Shield Sprite (2x2, Under):";
			inf.actionsnd[0] = "Shield Sound:";
			break;
		}
		case itype_divineescape:
		{
			_SET(misc[0], "Warp Animation (0-2):",
				"0 = Scatter into dust particles"
				"\n1 = Rise into the sky as color 0x00 particles"
				"\n2 = Rise into the sky as a mix of color 0x12 and 0x01 particles");
			_SET(actionsnd[0], "Warp Sound:", "This sound plays when the Hero turns into particles");
			_SET(flag[0], "Continue acts as F6->Continue",
				"Activates F6->Continue instead of just 'restarting the level'.");
			break;
		}
		case itype_divinefire:
		{
			_SET(power, "Damage:", "The amount of damage dealt by the flames");
			_SET(misc[0], "Number of Flames:", "The number of flames to shoot in a circle");
			_SET(misc[1], "Circle Width:", "The diameter of the circle");
			_SET(flag[1], "Don't Provide Light", "The flames will not emit light");
			_SET(flag[2], "Falls in Sideview", "The flames will obey gravity");
			_SET(flag[4], "Temporary Light", "The flames light will only light the room temporarily (Old dark rooms)");
			_SET(flag[8], "Counts as Strong Fire", zq_fmt("The flames will trigger '{}' flags",q30_mflag_name(mfSTRONGFIRE)));
			_SET(flag[9], "Counts as Magic Fire", zq_fmt("The flames will trigger '{}' flags",q30_mflag_name(mfMAGICFIRE)));
			_SET(flag[10], "Counts as Divine Fire", zq_fmt("The flames will trigger '{}' flags",q30_mflag_name(mfDIVINEFIRE)));
			_SET(wpn[0], "Rocket Up Sprite:", "The sprite for the up rocket");
			_SET(wpn[1], "Rocket Down Sprite:", "The sprite for the down rocket");
			_SET(wpn[2], "R. Up Sparkle Sprite:", "The sparkle sprite for the up rocket");
			_SET(wpn[3], "R. Down Sparkle Sprite:", "The sparkle sprite for the down rocket");
			_SET(wpn[4], "Flame Sprite:", "The sprite of the flame weapons");
			_SET(actionsnd[0], "Ring Sound:", "The sound that plays when the ring of fire appears");
			break;
		}
		case itype_hammer: //!TODO Help Text
		{
			inf.power = "Damage:";
			inf.flag[2] = "No Melee Attack";
			_SET(flag[0], "No Air Triggers", "Don't trigger combos/secret flags while the hammer is 'in the air'.");
			inf.wpn[0] = "Hammer Sprite:";
			inf.wpn[1] = "Smack Sprite:";
			inf.actionsnd[0] = "Pound Sound:";
			inf.flag[6] = "Doesn't Pick Up Dropped Items";
			break;
		}
		case itype_lens: //!TODO Help Text
		{
			inf.misc[0] = "Lens Width:";
			inf.flag[0] = "Show Hints";
			_SET(flag[1], "Hide Secret Combos", "Does not reveal 'Secret Combos' if checked");
			_SET(flag[2], "No X-Ray for Items", "Will not show items hidden under armos/dive flags/inside chests");
			inf.flag[3] = "Show Raft Paths";
			inf.flag[4] = "Show Invis. Enemies";
			_SET(flag[5], "Triggers Lens Trigflag", "Interacts with the 'Lens On->' and 'Lens Off->' combo trigger flags");
			inf.actionsnd[0] = "Activation Sound:";
			break;
		}
		case itype_hookshot:
		{
			inf.power = "Damage:";
			_SET(misc[0], "Chain Length:", "Max length, in tiles, of the hookshot chain");
			_SET(misc[1], "Chain Links:", "Max number of chain link sprites to display (less = a more stretched chain)");
			_SET(flag[0], "No Handle Damage", "The handle does not collide with enemies");
			_SET(flag[1], "Allow Diagonal", "The hookshot can be fired diagonally. (EXPERIMENTAL)");
			_SET(flag[3], "Pick Up Anything", "Any collided item will be grabbed, not just dropped items");
			_SET(flag[4], "Drags Items", "Collected items are dragged towards the Hero");
			_SET(flag[5], "Hits Enemy Projectiles", "If checked, the weapon can collide with projectiles, either blocking or reflecting them.");
			_SET(flag[6], "Picks Up Keys", "Will pick up Key type items");
			
			inf.wpn[0] = "Tip Sprite:";
			inf.wpn[1] = "Chain Sprite (H):";
			inf.wpn[2] = "Chain Sprite (V):";
			inf.wpn[3] = "Handle Sprite:";
			inf.wpn[4] = "Diagonal Tip Sprite:";
			inf.wpn[5] = "Diagonal Handle Sprite:";
			inf.wpn[6] = "Diagonal Chain Sprite:";
			_SET(actionsnd[0], "Firing Sound:", "Sound plays while the hookshot extends");
			if(FLAG(6))
			{
				_SET(misc[2], "Block Flags:",
					"(Rock=1, Arrow=2, BRang=4, Fireball=8, Sword=16, Magic=32, Flame=64, Script(All)=128, Firebal2=256,\n"
					"Script1=1024, Script2=2048, Script3=4096, Script4=8192, Script5=16384, Script6=32768, Script7=65536, Script8=131072, Script9=262144, Script10=524288)\n"
					"Sum all of the values you want to apply. Weapons with their flags set will be blocked by the weapon.");
				_SET(misc[3], "Reflect Flags:",
					"(Rock=1, Fireball=8, Sword=16, Magic=32, Flame=64, Script(All)=128, Firebal2=256,\n"
					"Script1=1024, Script2=2048, Script3=4096, Script4=8192, Script5=16384, Script6=32768, Script7=65536, Script8=131072, Script9=262144, Script10=524288)\n"
					"Sum all of the values you want to apply. Weapons with their flags set will be reflected by the weapon.\n"
					"Weapons are only reflected if their value is in both 'Block Flags' and 'Reflect Flags'.");
			}
			break;
		}
		case itype_switchhook:
		{
			inf.power = "Damage:";
			_SET(misc[0], "Chain Length:", "Max length, in tiles, of the switchhook chain");
			_SET(misc[1], "Chain Links:", "Max number of chain link sprites to display (less = a more stretched chain)");
			_SET(misc[4], "Switch FX Type:", "The visual effect to use when switching."
				"\n0 = Poof, 1 = Flicker, 2 = Rise/Fall");
			_SET(flag[0], "No Handle Damage", "The handle does not collide with enemies");
			_SET(flag[1], "Allow Diagonal", "The switchhook can be fired diagonally. (EXPERIMENTAL)");
			_SET(flag[3], "Pick Up Anything", "Any collided item will be grabbed, not just dropped items");
			_SET(flag[5], "Hits Enemy Projectiles", "If checked, the weapon can collide with projectiles, either blocking or reflecting them.");
			_SET(flag[6], "Picks Up Keys", "Will pick up Key type items");
			_SET(flag[7], "Cost on Swap", "The cost of the item is only paid upon swapping successfully.");
			_SET(flag[8], "Swaps Items", "Items that would be collected are instead swapped with the Hero.");
			if(!FLAG(9))
				_SET(flag[4], "Drags Items", "Collected items are dragged towards the Hero");
			
			inf.wpn[0] = "Tip Sprite:";
			inf.wpn[1] = "Chain Sprite (H):";
			inf.wpn[2] = "Chain Sprite (V):";
			inf.wpn[3] = "Handle Sprite:";
			inf.wpn[4] = "Diagonal Tip Sprite:";
			inf.wpn[5] = "Diagonal Handle Sprite:";
			inf.wpn[6] = "Diagonal Chain Sprite:";
			_SET(actionsnd[0], "Firing Sound:", "Sound plays while the hookshot extends");
			_SET(actionsnd[1], "Switch Sound:", "Sound plays when a switch occurs");
			if(FLAG(6))
			{
				_SET(misc[2], "Block Flags:",
					"(Rock=1, Arrow=2, BRang=4, Fireball=8, Sword=16, Magic=32, Flame=64, Script(All)=128, Firebal2=256,\n"
					"Script1=1024, Script2=2048, Script3=4096, Script4=8192, Script5=16384, Script6=32768, Script7=65536, Script8=131072, Script9=262144, Script10=524288)\n"
					"Sum all of the values you want to apply. Weapons with their flags set will be blocked by the weapon.");
				_SET(misc[3], "Reflect Flags:",
					"(Rock=1, Fireball=8, Sword=16, Magic=32, Flame=64, Script(All)=128, Firebal2=256,\n"
					"Script1=1024, Script2=2048, Script3=4096, Script4=8192, Script5=16384, Script6=32768, Script7=65536, Script8=131072, Script9=262144, Script10=524288)\n"
					"Sum all of the values you want to apply. Weapons with their flags set will be reflected by the weapon.\n"
					"Weapons are only reflected if their value is in both 'Block Flags' and 'Reflect Flags'.");
			}
			break;
		}
		case itype_itmbundle:
		{
			for(auto q = 0; q < 10; ++q)
				_SET(misc[q], "Item " + std::to_string(q+1) + ":", "If > -1, an item ID to 'collect' when you collect this item bundle.");
			_SET(flag[0], "Run Pickup Scripts", "Run the collect script of bundled items.");
			break;
		}
		case itype_progressive_itm:
		{
			for(auto q = 0; q < 10; ++q)
				_SET(misc[q], "Item " + std::to_string(q+1) + ":", "If > -1, an item ID in the progressive order.");
			break;
		}
		case itype_note:
		{
			_SET(misc[0], "String:", "The string to display when this item is used.");
			_SET(actionsnd[0], "Opening Sound", "SFX to play when the note is opened");
			break;
		}
		case itype_boots:
		{
			_SET(power, "Protection Power:", "Protects against damage combos that deal up to 16*power points of damage.");
			_SET(flag[0], "Not Solid Combos", "Does not protect against solid damage combos");
			_SET(flag[1], "Heavy", "Some combo settings interact with this flag.\n"
				"Unlike most items, this applies as long as you have a heavy " + classname + " item,"
				" even if it is not the highest level " + classname + " item.");
			_SET(flag[2], "No Damage Protection", "These " + classname + " will not protect you"
				" from Damage Combos at all.");
			break;
		}
		case itype_bracelet:
		{
			_SET(power, "Push Combo Level:", "If 1 or higher, can push 'Heavy' blocks. If 2 or higher, can push 'Very Heavy' blocks.");
			_SET(flag[0], "Limited Per Screen", "Can only use a set number of times per screen");
			if(FLAG(1))
			{
				_SET(misc[2], "Uses Per Screen", "How many times this bracelet can be used per screen. Minimum 1.");
			}
			break;
		}
		case itype_book: //!TODO Help Text
		{
			inf.power = "M. Damage:";
			inf.wpn[0] = "Magic Sprite:";
			inf.wpn[1] = "Projectile Sprite:";
			
			inf.flag[0] = "Fire Magic";
			if(FLAG(1))
			{
				inf.misc[3] = "Fire Damage";
				inf.flag[2] = "Fire Doesn't Hurt Hero";
				inf.flag[4] = "Temporary Light";
				inf.actionsnd[0] = "Fire Sound:";
				_SET(flag[8], "Counts as Strong Fire", zq_fmt("The flames will trigger '{}' flags",q30_mflag_name(mfSTRONGFIRE)));
				_SET(flag[9], "Counts as Magic Fire", zq_fmt("The flames will trigger '{}' flags",q30_mflag_name(mfMAGICFIRE)));
				_SET(flag[10], "Counts as Divine Fire", zq_fmt("The flames will trigger '{}' flags",q30_mflag_name(mfDIVINEFIRE)));
			}
			
			inf.flag[1] = "Override Wand Damage";
			if(FLAG(2))
				inf.misc[0] = "W. Damage:";
			
			inf.flag[3] = "Override Wand SFX";
			if(FLAG(4))
				inf.misc[1] = "Wand Sound";
			
			inf.flag[5] = "Replace Magic Step";
			if(FLAG(6))
				inf.misc[2] = "M. Step";
			break;
		}
		case itype_ring:
		{
			_SET(misc[0], "Hero Sprite Pal:", "The Sprite Palette row to load into CSet 6");
			_SET(flag[0], "Affects Damage Combos", "Whether or not the ring reduces damage from damage combos.");
			_SET(flag[1], "Percentage Multiplier", "Changes the 'Damage Divisor' to a 'Damage % Mult'"
				", allowing more precise control over the defense granted.");
			if(FLAG(2))
				_SET(power, "Damage % Mult:", "The percentage to multiply the damage by. A negative value"
					" will deal that amount *more* damage; i.e. '-100' is the same as '200'.");
			else
				_SET(power, "Damage Divisor:", "The number to divide the damage by. A divisor of 0"
					" makes the Hero take no damage at all.");
			break;
		}
		case itype_wand:
		{
			inf.power = "Damage:";
			_SET(flag[0], "Allow Magic w/o Book", "If unchecked, the projectile will only fire if a Book is owned.");
			_SET(flag[2], "No Melee Attack", "If checked, the item's melee swing will not damage enemies.");
			_SET(flag[3], "Can Slash", "If the item's melee attack slashes, instead of stabbing.");
			_SET(flag[4], "No Melee Cooldown", "Allows spamming the item's melee attack");
			_SET(flag[5], "Melee Weapons Use Cost", "The cost will be charged when using the item itself, not spawning the projectile."
				" If unchecked, the item itself can be used even without the cost, it will simply not spawn a projectile.");
			_SET(flag[6], "Doesn't Pick Up Dropped Items", "If checked, the melee swing will not collect items.");
			_SET(flag[7], "Flip Right-Facing Slash", "If checked the right-facing slash will slash down-right instead of up-right.");
			_SET(wpn[0], "Stab Sprite:", "Sprite used by the item itself");
			_SET(wpn[1], "Slash Sprite:", "Sprite used by the item itself");
			_SET(wpn[2], "Projectile Sprite:", "Sprite used by the projectile");
			_SET(actionsnd[0], "Firing Sound:", "Sound played when firing a projectile");
			break;
		}
		case itype_bait: //!TODO Help Text
		{
			_SET(flag[1], "Only for feeding", "The bait cannot be placed except when feeding a hungry NPC");
			_SET(flag[2], "Don't remove when feeding", "The item will not be removed from the inventory upon feeding a hungry NPC.");
			_SET(flag[3], "Only require cost when feeding", "The item's Use Cost will only be checked when feeding hungry NPCs, not when luring enemies.");
			if(!FLAG(4))
				_SET(flag[4], "Only charge cost when feeding", "The item's Use Cost will only be charged when feeding hungry NPCs, not when luring enemies."
					"\nIt will still be CHECKED when luring enemies.");
			if(!FLAG(2)) //Not only for feeding
			{
				_SET(misc[0], "Duration:", "The duration, in frames, the bait sits on the ground for when luring enemies");
				_SET(misc[1], "Bait Range (0 = infinite)", "The range, in pixels, to lure enemies in");
				_SET(flag[0], "Repels enemies", "Repel enemies instead of luring them");
			}
			inf.wpn[0] = "Bait Sprite:";
			inf.actionsnd[0] = "Baiting Sound:";
			break;
		}
		case itype_potion:
		{
			_SET(flag[0], "Life is Percent", "HP Regained is a percentage out of max HP");
			_SET(flag[1], "Magic is Percent", "MP Regained is a percentage out of max MP");
			_SET(flag[2], "Removes Sword Jinxes", "Heal sword jinxes on use");
			_SET(flag[3], "Removes Item Jinxes", "Heal item jinxes on use");
			_SET(flag[4], "Removes Shield Jinxes", "Heal shield jinxes on use");
			
			if(FLAG(1))
				_SET(misc[0], "% HP Regained:", "Percentage of max life restored when collected.");
			else
				_SET(misc[0], "HP Regained:", "Life (in points) restored when collected.");
			if(FLAG(2))
				_SET(misc[1], "% MP Regained:", "Percentage of max magic restored when collected.");
			else
				_SET(misc[1], "MP Regained:", "Magic (in points) restored when collected.");
			
			break;
		}
		case itype_whistle:
		{
			auto windname = weap_name(wWind);
			auto whistlename = weap_name(wWhistle);
			_SET(misc[0], zq_fmt("{} Direction:", windname), zq_fmt("Direction the {} faces. 0 = Up, 1 = Down, 2 = Left, 3 = Right, 4 = Opposite the Hero", windname));
			_SET(misc[1], "Warp Ring:", "Which warp ring to use when warping the Hero");
			if(FLAG(2))
				_SET(misc[4], zq_fmt("{} Damage", whistlename), zq_fmt("The damage of the {} to anything that hears it.",whistlename));
			_SET(flag[0], "One W.Wind Per Scr.", zq_fmt("Only allow a {} to be summoned once per screen.", windname));
			_SET(flag[1], "Has Damage", zq_fmt("If checked, the {0} weapon will deal the damage set in the '{0} Damage' attribute.",whistlename));
			_SET(flag[2], zq_fmt("{} Reflects off Prism/Mirror Combos", windname), zq_fmt("If checked, {} weapons will reflect off of Mirrors and be duplicated by Prisms.", windname));
			inf.wpn[0] = zq_fmt("{} Sprite:", windname);
			_SET(actionsnd[0], "Music Sound:", "The musical tune played");
			break;
		}
		case itype_candle:
		{
			inf.power = "Damage:";
			_SET(misc[1], "Max Fires On Screen", "If < 1, defaults to 1");
			_SET(misc[3], "Step Speed", "The step speed of the created fire weapon, where 100 = 1px/frame. Default 50.");
			_SET(flag[0], "Limited Per Screen", "Can only use a set number of times per screen");
			_SET(flag[1], "Don't Provide Light", "Does not light up dark rooms");
			inf.flag[2] = "Fire Doesn't Hurt Hero";
			_SET(flag[3], "Can Slash", "The candle slashes instead of stabs");
			inf.flag[7] = "Flip Right-Facing Slash";
			_SET(flag[8], "Counts as Strong Fire", zq_fmt("The flames will trigger '{}' flags",q30_mflag_name(mfSTRONGFIRE)));
			_SET(flag[9], "Counts as Magic Fire", zq_fmt("The flames will trigger '{}' flags",q30_mflag_name(mfMAGICFIRE)));
			_SET(flag[10], "Counts as Divine Fire", zq_fmt("The flames will trigger '{}' flags",q30_mflag_name(mfDIVINEFIRE)));
			_SET(wpn[0], "Stab Sprite:", "The sprite for the candle stabbing");
			_SET(wpn[1], "Slash Sprite:", "The sprite for the candle slashing");
			_SET(wpn[2], "Fire Sprite:", "The sprite for the candle fire");
			inf.actionsnd[0] = "Firing Sound:";
			if(FLAG(1))
			{
				_SET(misc[2], "Uses Per Screen", "How many times this candle can be used per screen. Minimum 1.");
			}
			if(!FLAG(2))
			{
				_SET(flag[4], "Temporary Light", "In Old-style Dark Rooms, only keeps the room lit while it exists.");
			}
			if(!FLAG(3))
			{
				_SET(misc[0], "Damage to Hero:", "Damage value to be used for self-damage");
			}
			break;
		}
		case itype_arrow: //!TODO Help Text
		{
			inf.power = "Damage:";
			inf.misc[0] = "Duration (0 = Infinite):";
			_SET(misc[1], "Max Arrows On Screen", "If < 1, defaults to 2");
			inf.flag[0] = "Penetrate Enemies";
			inf.flag[1] = "Allow Item Pickup";
			inf.flag[3] = "Pick Up Anything";
			inf.flag[6] = "Picks Up Keys";
			inf.actionsnd[0] = "Firing Sound:";
			_SET(wpn[0], "Arrow Sprite:", "The sprite used for the arrow weapon."
				" Should contain an up-facing sprite followed by a right-facing sprite.");
			inf.wpn[1] = "Sparkle Sprite:";
			inf.wpn[2] = "Damaging Sparkle Sprite:";
			break;
		}
		case itype_brang: //!TODO Help Text
		{
			inf.power = "Damage:";
			_SET(misc[0], "Duration:", "The amount of frames the boomerang flys before returning.\n0 = Infinite");
			inf.misc[2] = "Block Flags:";
			inf.misc[3] = "Reflect Flags:";
			inf.flag[0] = "Corrected Animation";
			inf.flag[1] = "Directional Sprites";
			inf.flag[2] = "Do Not Return";
			inf.flag[3] = "Pick Up Anything";
			inf.flag[4] = "Drags Items";
			inf.flag[5] = "Reflects Enemy Projectiles";
			inf.flag[6] = "Picks Up Keys";
			_SET(flag[7], "Counts as Fire", zq_fmt("The flames will trigger '{}' flags",q30_mflag_name(mfANYFIRE)));
			_SET(flag[8], "Counts as Strong Fire", zq_fmt("The flames will trigger '{}' flags",q30_mflag_name(mfSTRONGFIRE)));
			_SET(flag[9], "Counts as Magic Fire", zq_fmt("The flames will trigger '{}' flags",q30_mflag_name(mfMAGICFIRE)));
			_SET(flag[10], "Counts as Divine Fire", zq_fmt("The flames will trigger '{}' flags",q30_mflag_name(mfDIVINEFIRE)));
			
			inf.wpn[0] = "Boomerang Sprite:";
			inf.wpn[1] = "Sparkle Sprite:";
			inf.wpn[2] = "Damaging Sparkle Sprite:";
			inf.actionsnd[0] = "Spinning Sound:";
			break;
		}
		case itype_sword: //!TODO Help Text
		{
			inf.power = "Damage:";
			inf.misc[0] = "Beam Hearts:";
			inf.misc[1] = "Beam Damage:";
			inf.flag[0] = "Beam Hearts Is Percent";
			inf.flag[1] = "Beam Damage Is Percent";
			inf.flag[2] = "Beam Penetrates Enemies";
			inf.flag[3] = "Can Slash";
			inf.flag[4] = "No Melee Cooldown";
			inf.flag[5] = "Melee Weapons Use Magic Cost";
			inf.flag[6] = "Doesn't Pick Up Dropped Items";
			inf.flag[7] = "Flip Right-Facing Slash";
			inf.flag[8] = "Sword Beams Reflect off Prism/Mirror Combos";
			inf.flag[9] = "Walk slowly while charging";
			inf.wpn[0] = "Stab Sprite:";
			inf.wpn[1] = "Slash Sprite:";
			inf.wpn[2] = "Beam Sprite:";
			inf.actionsnd[0] = "Slash/Stab Sound";
			inf.actionsnd[1] = "Beam Sound";
			break;
		}
		case itype_whimsicalring:
		{
			_SET(power, "Power Bonus:", "How much power to add to strikes from the Sword, Wand, and Hammer."
				"\nThis is added before Attack Rings take effect, and also before the hero damage multiplier.");
			_SET(misc[0], "Chance (1 in n):", "There is a '1 in n' chance of the bonus power being added (min 1)");
			_SET(actionsnd[0], "Whimsy Sound:", "This sound plays when the bonus power is successfully applied.");
			break;
		}
		case itype_perilring:
		{
			_SET(misc[0], "Maximum Hearts:", "Takes effect when below this number of hearts");
			inf.flag[0] = "Affects Damage Combos";
			inf.flag[1] = "Percentage Multiplier";
			if(FLAG(2))
				inf.power = "Damage % Mult:";
			else
				inf.power = "Damage Divisor:";
			break;
		}
		case itype_stompboots:
		{
			inf.power = "Damage:";
			_SET(flag[0], "Can bounce off enemies", "If enabled, damaging an enemy with this will cause the Hero to bounce upwards."
				" The jump speed gained is set by Attributes[0]");
			if(FLAG(1))
				_SET(misc[0], "Bounce Power:", "Amount of jump power gained from a bounce."
					" This value is equal to setting Hero->Jump by value divided by 100");
			_SET(misc[1], "Block Flags:", "(Rock=1, Arrow=2, BRang=4, Fireball=8, Sword=16, Magic=32, Flame=64, Script(All)=128, Firebal2=256,\n"
				"Script1=1024, Script2=2048, Script3=4096, Script4=8192, Script5=16384, Script6=32768, Script7=65536, Script8=131072, Script9=262144, Script10=524288)\n"
				"Sum all of the values you want to apply. Weapons with their flags set will be blocked if the Hero lands on them.");
			break;
		}
		case itype_bow:
		{
			_SET(power, "Arrow Speed Mod:", "Arrow moves at 3 pixels per frame, multiplied by '(1+n)/2', where 'n' is this value.");
			break;
		}
		case itype_script1: case itype_script2: case itype_script3: case itype_script4:
		case itype_script5: case itype_script6: case itype_script7: case itype_script8:
		case itype_script9: case itype_script10:
		{
			_SET(misc[0], "Step Speed:", "The speed, in 100ths of pixel/frame");
			_SET(flag[0], "No GFX Flip", "If checked, the sprite is not flipped based on direction.");
			_SET(wpn[0], "Weapon Sprite", "The sprite the weapon uses. Will be flipped/rotated based on direction, unless 'No GFX Flip' is checked");
			break;
		}
		case itype_custom1: case itype_custom2: case itype_custom3: case itype_custom4:
		case itype_custom5: case itype_custom6: case itype_custom7: case itype_custom8:
		case itype_custom9: case itype_custom10:
		{
			break;
		}
		case itype_icerod:
		{
			_SET(misc[0], "Step Speed:", "The speed, in 100ths of pixel/frame");
			_SET(flag[0], "No GFX Flip", "If checked, the sprite is not flipped based on direction.");
			break;
		}
		case itype_flippers:
		{
			_SET(flag[0], "No Diving", "If checked, these flippers cannot dive.");
			_SET(flag[1], "Cancellable Diving", "Pressing B will immediately cancel a dive");
			_SET(flag[2], "Can Swim in Lava", "These flippers will be able to swim in liquid marked as 'Lava'.");
			
			_SET(misc[0], "Dive Length:", "Length, in frames, of the dive.");
			_SET(misc[1], "Dive Cooldown:", "Cooldown, in frames, between coming up from a dive and being able to dive again.");
			_SET(misc[2], "Dive Button", "If 0, diving will not be possible.\n"
				"Otherwise, any of the specified buttons will dive.\n"
				"Sum all the buttons you want to be usable:\n(A=1, B=2, L=4, R=8, Ex1=16, Ex2=32, Ex3=64, Ex4=128)");
			break;
		}
		case itype_raft:
		{
			_SET(misc[0], "Speed Modifier:", "Valid values -8 to 5."
				"\nIf positive, move 2^n pixels per frame."
				"\nIf negative, move 1 pixel per 2^n frames."
				"\nA value of 0 will stop the raft dead. This is useless unless combined with scripts.");
			break;
		}
		case itype_atkring:
		{
			_SET(misc[0], "Bonus Power", "This amount is *added* to the power, *after* the multiplier.");
			_SET(misc[1], "Power Multiplier", "The power is multiplied by this value.");
			break;
		}
		case itype_lantern:
		{
			_SET(misc[0], "Shape", "What shape to use for the light area emitted.\n"
				"0 = circular, 1 = cone in front, 2 = square");
			_SET(misc[1], "Range", "The range, in pixels, of the light.");
			_SET(misc[2], "Offset", "An offset for the source of the light, in pixels."
				"\nPositive offsets are in the direction the player is facing,"
				" negative offsets are in the opposite direction.");
			_SET(flag[0], "No Light 'Wave'", "The light cast from this item is not affected by the 'light wave' settings");
			break;
		}
		case itype_bottle:
		{
			_SET(misc[0], "Slot:", "Which slot this bottle item is attached to. Valid values: 0-255.");
			break;
		}
		case itype_bottlefill:
		{
			_SET(misc[0], "Contents:", "What contents to place in an empty bottle when picked up");
			break;
		}
		case itype_bugnet:
		{
			_SET(flag[0], "Can't catch fairies", "If checked, no longer catches fairies"
				" it collides with");
			_SET(flag[1], "Right-handed", "Swaps swing direction of the weapon");
			_SET(actionsnd[0], "Swing Sound", "Sound played when swinging the net");
			break;
		}
		case itype_mirror:
		{
			_SET(misc[0], "Warp Effect", "What warp effect to use during the warp to another dmap.\n"
				"0=None, 1=Zap, 2=Wave, 3=Blackscr, 4=OpenWipe");
			_SET(misc[1], "Cont. Warp Effect", "What warp effect to use during the warp to the continue point.\n"
				"0=None, 1=Zap, 2=Wave, 3=Blackscr, 4=OpenWipe");
			_SET(flag[0], "Place Return Portal", "If checked, places a return portal when"
				" mirroring to a new dmap");
			_SET(flag[1], "Continue acts as F6->Continue",
				"When used on a dmap with 'Mirror Continues instead of Warping' checked, "
				"activates F6->Continue instead of Divine Escape effect if enabled.");
			_SET(wpn[0], "Portal Sprite", "Sprite of the Return Portal");
			_SET(actionsnd[0], "Warp Sound", "Sound played for the warp to a new dmap");
			_SET(actionsnd[1], "Continue Sound", "Sound played for a continue warp");
			break;
		}
		case itype_lkey:
		{
			_SET(flag[0], "Specific Level", "If checked, grants a key for a specific level, instead of the 'current' level");
			if(FLAG(1))
				_SET(misc[0], "Key Level", "The level to grant a key for");
			break;
		}
		case itype_enemy_ambush:
		{
			_SET(flag[0], "On Pickup", "If checked, waits until being picked up to spawn the enemies."
				" Otherwise, enemies are spawned immediately when the item exists.");
			_SET(flag[1], "No Randomness", "If checked, spawns enemies starting at the top of the list, in order."
				" Otherwise, spawns enemies picked randomly from the list.");
			_SET(misc[0], "Count", "How many enemies to spawn.");
			for (int q = 0; q < 5; ++q)
				_SET(misc[q+5], zq_fmt("Enemy {}", q+1), "An enemy ID to spawn. Skipped if invalid.");
			break;
		}
	}
#undef _SET
#undef FLAG
#undef SWAPTYPE
}

void guy_labels(int type, const long *attributes, std::string *l_attribute, std::string *h_attribute, std::string *l_bflag,
                std::string *h_bflag)
{
    (void)attributes;
    for (int q = 0; q < 32; ++q)
		l_attribute[q] = zq_fmt("Misc Attr. {}:", q + 1);
	l_bflag[0] = "Enemy is Completely Invisible";
	l_bflag[1] = "Item Specified in Attributes 13 Dispels Invisibility";
	l_bflag[2] = "Doesn't always return";
	l_bflag[3] = "Draw Invisible as Cloaked";
	l_bflag[4] = "BFlags[4]";
	l_bflag[5] = "BFlags[5]";
	l_bflag[6] = "BFlags[6]";
	l_bflag[7] = "BFlags[7]";
	l_bflag[8] = "BFlags[8]";
	l_bflag[9] = "BFlags[9]";
	l_bflag[10] = "BFlags[10]";
	l_bflag[11] = "Toggle Move Offscreen";
	l_bflag[12] = "Fast Drawing";
	l_bflag[13] = "Ignore Sideview Ladders/Platforms";
	l_bflag[14] = "Move Off-Grid (WIP)";
	l_bflag[15] = "Render Cloaked Instead of VISIBLE";

	//These need to be moved else where to be honest
	l_attribute[12] = "This Item Dispels Invisibility:";
	l_attribute[14] = "Transform Effect:";
	l_attribute[15] = "Transform Into Enemy:";
	l_attribute[16] = "Transform Inv Time:";
	
	switch(type)
	{
		case eeWALK:
		{
			l_attribute[0] = "Shot Type:";
			l_attribute[1] = "Death Type:";
			l_attribute[2] = "Death/Shot Attr. 1:";
			l_attribute[3] = "Death/Shot Attr. 2:";
			l_attribute[4] = "Death/Shot Attr. 3:";
			l_attribute[5] = "Extra Shots:";
			l_attribute[6] = "Touch Effects:";
			l_attribute[7] = "Effect Strength:";
			l_attribute[8] = "Walk Style:";
			l_attribute[9] = "Walk Attribute:";
			l_bflag[4] = "Split in Place";
			break;
		}
		case eeGLEEOK:
		{
			l_attribute[0] = "Heads:";
			l_attribute[1] = "Head HP:";
			l_attribute[2] = "Shot Type:";
			l_attribute[3] = "Unused";
			l_attribute[4] = "Neck Segments:";
			l_attribute[5] = "Neck Offset 1:";
			l_attribute[6] = "Neck Offset 2:";
			l_attribute[7] = "Head Offset";
			l_attribute[8] = "Flying Head Offset";
			l_attribute[9] = "Unused";
			l_bflag[2] = "BFlags[2]";
			l_bflag[4] = "Obeys Spawn Points";
			l_bflag[5] = "Center Spawn X Point";
			l_bflag[6] = "Heads Block Projectiles";
			break;
		}
		case eeDIG:
		{
			l_attribute[0] = "Enemy 1 ID:";
			l_attribute[1] = "Enemy 2 ID:";
			l_attribute[2] = "Enemy 3 ID:";
			l_attribute[3] = "Enemy 4 ID:";
			l_attribute[4] = "Enemy 1 Qty:";
			l_attribute[5] = "Enemy 2 Qty:";
			l_attribute[6] = "Enemy 3 Qty:";
			l_attribute[7] = "Enemy 4 Qty:";
			l_attribute[8] = "Unused";
			l_attribute[9] = "Type";
			l_bflag[2] = "BFlags[2]";
			break;
		}
		case eePATRA:
		{
			l_attribute[0] = "Outer Eyes:";
			l_attribute[1] = "Inner Eyes:";
			l_attribute[2] = "Eyes' HP:";
			l_attribute[3] = "Eye Movement:";
			l_attribute[4] = "Shooters:";
			l_attribute[5] = "Pattern Odds:";
			l_attribute[6] = "Pattern Cycles:";
			l_attribute[7] = "Eye Offset:";
			l_attribute[8] = "Eye CSet:";
			l_attribute[9] = "Type:";
			l_attribute[17] = "Shot chance (1/N per frame):";
			l_attribute[18] = "Firing cooldown:";
			l_attribute[19] = "Inner Eyes Firing Pattern:";
			l_attribute[20] = "Warning Spins:";
			l_attribute[21] = "Stays Still:";
			l_attribute[22] = "Outer Ring Loss Speed Boost:";
			l_attribute[23] = "Inner Ring Loss Speed Boost:";
			l_attribute[24] = "Can Fire:";
			l_attribute[25] = "Can Expand:";
			l_attribute[26] = "Inner Eye HP:";
			l_attribute[27] = "Center Eye Firing Pattern:";
			l_attribute[28] = "Outer Eye Radius:";
			l_attribute[29] = "Inner Eye Radius:";
			l_attribute[30] = "Outer Eye Expanded Radius:";
			l_attribute[31] = "Inner Eye Expanded Radius:";
			l_bflag[2] = "Has Firing Animation";
			l_bflag[4] = "Obeys Spawn Points";
			l_bflag[5] = "Slow down when Firing";
			l_bflag[6] = "Don't attack when expanding";
			l_bflag[7] = "Don't expand when spawned";
			l_bflag[8] = "Pattern Odds reset when Expanding";
			l_bflag[9] = "Don't 'expand' without orbiters";
			break;
		}
		case eePROJECTILE:
		{
			l_attribute[0] = "Shot Type:";
			l_attribute[2] = "Shot Attribute 1:";
			l_attribute[3] = "Shot Attribute 2:";
			break;
		}
		case eeGHOMA:
		{
			l_attribute[0] = "Shot Type:";
			l_bflag[2] = "BFlags[2]";
			l_bflag[4] = "Obeys Spawn Points";
			break;
		}
		case eeAQUA:
		{
			l_attribute[0] = "Side:";
			l_bflag[2] = "BFlags[2]";
			l_bflag[4] = "Obeys Spawn Points";
			break;
		}
		case eeMANHAN:
		{
			l_attribute[0] = "Frame Rate:";
			l_attribute[1] = "Size";
			l_bflag[2] = "BFlags[2]";
			break;
		}
		case eeLANM:
		{
			l_attribute[0] = "Segments:";
			l_attribute[1] = "Segment Lag:";
			l_attribute[2] = "Item per segment:";
			l_bflag[2] = "BFlags[2]";
			l_bflag[4] = "Obeys Spawn Points";
			break;
		}
		case eeMOLD:
		{
			l_attribute[0] = "Segments:";
			l_attribute[1] = "Item per segment:";
			l_bflag[2] = "BFlags[2]";
			l_bflag[4] = "Obeys Spawn Points";
			break;
		}
		case eeWIZZ:
		{
			l_attribute[0] = "Walk Style:";
			l_attribute[1] = "Shot Type:";
			l_attribute[2] = "Shot Attr. 1:";
			l_attribute[3] = "Solid Combos OK:";
			l_attribute[4] = "Teleport Delay:";
			l_attribute[5] = "Shot Attr. 2:";
			l_attribute[6] = "Shot Attr. 3:";
			l_bflag[4] = "Old Windrobe Teleport";
			break;
		}
		case eeDONGO:
		{
			l_attribute[9] = "Type:";
			l_bflag[2] = "BFlags[2]";
			break;
		}
		case eeKEESE:
		{
			l_attribute[0] = "Walk Style:";
			l_attribute[1] = "Death Type:";
			l_attribute[2] = "Enemy Id:";
			l_attribute[13] = "Landing Chance (1/N):",
			l_attribute[14] = "Landing Cooldown:",
			l_attribute[15] = "Halt Duration:";
			l_attribute[16] = "Acceleration Frame Interval";
			l_attribute[17] = "Acceleration Step Modifier:";
			l_attribute[18] = "Spawn Step:";
			l_attribute[19] = "Tribble Timer";
			break;
		}
		case eeTEK:
		{
			l_attribute[0] = "1/N Jump Start:";
			l_attribute[1] = "1/N Jump Cont.:";
			l_attribute[2] = "Jump Z Velocity:";
			break;
		}
		case eeLEV:
		{
			l_attribute[0] = "Emerge style:";
			l_attribute[1] = "Submerged CSet:";
			l_attribute[2] = "Emerge step:";
			break;
		}
		case eeWALLM:
		{
			l_attribute[0] = "Fixed Distance";
			break;
		}
		case eeTRAP:
		{
			l_attribute[0] = "Direction:";
			l_attribute[1] = "Move Style:";
			break;
		}
		case eeROCK:
		{
			l_attribute[9] = "Size:";
			
			break;
		}
		case eeNONE:
		{
			l_attribute[9] = "Boss Death Trigger:";

			break;
		}
		case eeGHINI: case eePEAHAT: //TODO DEPRECIATE ME
		{
			l_attribute[0] = "Stop On Pits";
			l_attribute[13] = "Landing Chance (1/N):",
			l_attribute[14] = "Landing Cooldown:",
			l_attribute[15] = "Halt Duration:";
			l_attribute[16] = "Acceleration Factor:";
			break;
		}
		case eeZORA:
		{
			l_bflag[4] = "Lava Only";
			l_bflag[5] = "Lava and Water";
			l_bflag[6] = "Shallow Water";
			l_bflag[7] = "Doesn't regenerate health";
			break;
		}
		case eeSPINTILE:
		{
			//no attributes or flags either (yet)
			break;
		}
		case eeGANON:
		{
			l_attribute[13] = "Death Shot Item ID"; //TODO MOVE ME TO MISC ZERO AND ADD ME TO DEFAULT DATA FOR OLD QUEST
			break;
		}
		default: //script and friendly families
		{
			break;
		}
	}
}

enum
{
    aNONE, aFLIP, aUNUSED1, a2FRM, aUNUSED2,
    aOCTO, aTEK, aLEV, aWALK, aZORA,
    aNEWZORA, aGHINI, aARMOS, aROPE, aWALLM,
    aNEWWALLM, aDWALK, aVIRE, a3FRM, aWIZZ,
    aAQUA, aDONGO, aMANHAN, aGLEEOK, aDIG,
    aGHOMA, aLANM, a2FRMPOS, a4FRM4EYE,a4FRM8EYE,
    a4FRM4DIRF, a4FRM4DIR, a4FRM8DIRF, aARMOS4, a4FRMPOS4DIR,
    a4FRMPOS8DIR, aUNUSED3, a4FRM8DIRB, aNEWTEK, a3FRM4DIR,
    a2FRM4DIR, aNEWLEV, a2FRM4EYE, aNEWWIZZ, aNEWDONGO,
    aDONGOBS, a4FRMPOS8DIRF, a4FRMPOS4DIRF, a4FRMNODIR, aGANON, a2FRMB, 
    a4FRM8EYEB, a4FRM4EYEB, a4FRM8DIRFB, a4FRM4DIRB, a4FRM4DIRFB, aMAX
};

enum { e1tNORMAL, e1tEACHTILE, e1tCONSTANT, e1tHOMINGBRANG=2, e1tFAST, e1tSLANT, e1t3SHOTS, e1t4SHOTS, e1t5SHOTS, e1t3SHOTSFAST, e1tFIREOCTO, e1t8SHOTS, e1tSUMMON, e1tSUMMONLAYER, e1tLAST };

enum { e2tNORMAL, e2tSPLITHIT, e2tSPLIT, e2tFIREOCTO, e2tBOMBCHU, e2tTRIBBLE, e2tLAST };

enum { e7tNORMAL, e7tTEMPJINX, e7tPERMJINX, e7tUNJINX, e7tTAKEMAGIC, e7tTAKERUPEES, e7tDRUNK,
// all from this point involve engulfing
       e7tEATITEMS, e7tEATMAGIC, e7tEATRUPEES, e7tEATHURT,
// all from this point involve dragging
       e7tWALLMASTER, e7tLAST
     };

enum { e9tNORMAL, e9tROPE, e9tVIRE, e9tPOLSVOICE, e9tARMOS,
// remainder unimplemented
       e9tLEEVER, e9tZ3LEEVER, e9tZ3WALK, e9tZ3STALFOS, e9tLAST
     };

const Q30NameVal g_el_list_walkmisc1[] =
{
    { "1 Shot", 0 },
    { "1 (End-Halt)", 1 },
    { "Rapid-Fire", 2 },
    { "1 (Fast)", 3 },
    { "1 (Slanted)", 4 },
    { "3 Shots", 5 },
    { "4 Shots", 6 },
    { "5 Shots", 7 },
    { "3 (Fast)", 8 },
    { "Breath", 9 },
    { "8 Shots", 10 },
    { "Summon", 11 },
    { "Summon (Layer)", 12 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_walkmisc2[] =
{
    { "Normal", 0 },
    { "Split On Hit", 1 },
    { "Split On Death", 2 },
    { "8 Shots", 3 },
    { "Explode", 4 },
    { "Tribble", 5 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_walkmisc7[] =
{
    { "None", 0 },
    { "Temp. Jinx", 1 },
    { "Perm. Jinx", 2 },
    { "Cure Jinx", 3 },
    { "Lose Magic", 4 },
    { "Lose Rupees", 5 },
    { "Drunk", 6 },
    { "Eat (Items)", 7 },
    { "Eat (Magic)", 8 },
    { "Eat (Rupees)", 9 },
    { "Eat (Damage)", 10 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_walkmisc9[] =
{
    { "Normal", 0 },
    { "Rope", 1 },
    { "Vire", 2 },
    { "Pols Voice", 3 },
    { "Armos", 4 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_gleeokmisc3[] =
{
    { "1 Shot", 0 },
    { "Breath", 1 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_gohmamisc1[] =
{
    { "1 Shot", 0 },
    { "3 Shots", 0 },
    { "Breath", 2 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_manhandlamisc2[] =
{
    { "1x1 (4 Heads)", 0 },
    { "2x2 (8 Heads)", 1 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_aquamisc1[] =
{
    { "Right (Facing Left)", 0 },
    { "Left (Facing Right)", 1 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_patramisc4[] =
{
    { "Big Circle", 0 },
    { "Oval", 1 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_patramisc5[] =
{
    { "None", 0 },
    { "Center Eye", 1 },
    { "Inner Eyes", 2 },
    { "Inner + Center", 3 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_patramisc10[] =
{
    { "1x1 Patra", 0 },
    { "2x2 Patra", 1 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_patramisc20[] =
{
    { "Random (Any)", 0 },
    { "Random (Single)", 1 },
    { "Barrage", 2 },
    { "Ring", 3 },
    { "Stream", 4 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_patramisc22[] =
{
    { "Never", 0 },
    { "When Expanding", 1 },
    { "Expand + Warning", 2 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_patramisc25[] =
{
    { "Always", 0 },
    { "When Vulnerable", 1 },
    { "When Invulnerable", 2 },
    { "When Behind 1 Layer", 3 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_patramisc26[] =
{
    { "Always", 0 },
    { "With Outer Ring", 1 },
    { "Without Outer Ring", 2 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_patramisc28[] =
{
    { "1 Shot", 0 },
    { "1 (Fast)", 1 },
    { "3 Shots", 2 },
    { "3 (Fast)", 3 },
    { "5 Shots", 4 },
    { "5 (Fast)", 5 },
    { "4 (Cardinal)", 6 },
    { "4 (Diagonal)", 7 },
    { "4 (Card/Diag Random)", 8 },
    { "8 Shots", 9 },
    { "Breath", 10 },
    { "Stream", 11 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_dodongomisc10[] =
{
    { "NES", 0 },
    { "BS-Zelda", 1 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_digdoggermisc10[] =
{
    { "Digdogger", 0 },
    { "Kid", 1 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_wizzrobemisc1[] =
{
    { "Teleport", 0 },
    { "Phase", 1 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_wizzrobemisc2[] =
{
    { "1 shot", 0 },
    { "8 shot", 1 },
    { "Summon", 2 },
    { "Summon (Layer)", 3 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_keesemisc1[] =
{
    { "Keese", 0 },
    { "Bat", 1 },
    { "Keese(Fast)", 2 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_keesemisc2[] =
{
    { "Normal", 0 },
    { "Tribble", 1 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_trapmisc1[] =
{
    { "4-Way", 0 },
    { "Horizontal", 1 },
    { "Vertical", 2 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_trapmisc2[] =
{
    { "Line Of Sight", 0 },
    { "Constant", 1 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_leevermisc1[] =
{
    { "Hero's path", 0 },
    { "In place", 1 },
    { "Hero's path + Random", 2 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_rockmisc10[] =
{
    { "1x1", 0 },
    { "2x2", 1 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_yesnomisc[] =
{
    { "No", 0 },
    { "Yes", 1 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_noyesmisc[] =
{
    { "Yes", 0 },
    { "No", 1 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_null[] =
{
    { "NOPE", 0 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_spawntype[] =
{
    { "Puff", 0 },
    { "Flicker", 1 },
    { "Instant", 2 },
    { NULL, 0 }
};

const Q30NameVal g_el_list_deathtype[] =
{
    { "Normal", 0 },
    { "Explode", 1 },
    { "Eight Shards", 2 },
    { "Robot Master", 3 },
    { "Disintegrate", 4 },
    { NULL, 0 }
};

const Q30NameVal g_el_enemies[] = { { NULL, 0 } };     // the quest's enemies

std::string anim_text(int v)
{
    for(size_t k = 0; k < sizeof(g_anims) / sizeof(g_anims[0]); ++k)
        if(g_anims[k].value == v)
            return g_anims[k].name;

    return std::string();
}

// loadEnemyType: the attributes shown as dropdowns for the class
void guy_dropdowns(int type, const Q30NameVal **lists)
{
    switch (type)
	{
	case eeWALK:
		lists[0] = g_el_list_walkmisc1;
		lists[1] = g_el_list_walkmisc2;
		lists[6] = g_el_list_walkmisc7;
		lists[8] = g_el_list_walkmisc9;
		break;
	case eeGLEEOK:
		lists[2] = g_el_list_gleeokmisc3;
		break;
	case eeDIG:
		lists[0] = g_el_enemies;
		lists[1] = g_el_enemies;
		lists[2] = g_el_enemies;
		lists[3] = g_el_enemies;
		lists[9] = g_el_list_digdoggermisc10;
		break;
	case eePATRA:
		lists[3] = g_el_list_patramisc4;
		lists[4] = g_el_list_patramisc5;
		lists[9] = g_el_list_patramisc10;
		lists[19] = g_el_list_patramisc20;
		lists[21] = g_el_list_patramisc22;
		lists[24] = g_el_list_patramisc25;
		lists[25] = g_el_list_patramisc26;
		lists[27] = g_el_list_patramisc28;
		break;
	case eePROJECTILE:
		lists[0] = g_el_list_walkmisc1;
		break;
	case eeGHOMA:
		lists[0] = g_el_list_gohmamisc1;
		break;
	case eeAQUA:
		lists[0] = g_el_list_aquamisc1;
		break;
	case eeMANHAN:
		lists[1] = g_el_list_manhandlamisc2;
		break;
	case eeLANM:
		lists[2] = g_el_list_yesnomisc;
		break;
	case eeMOLD:
		lists[1] = g_el_list_yesnomisc;
		break;
	case eeWIZZ:
		lists[0] = g_el_list_wizzrobemisc1;
		lists[1] = g_el_list_wizzrobemisc2;
		lists[3] = g_el_list_yesnomisc;
		break;
	case eeDONGO:
		lists[9] = g_el_list_dodongomisc10;
		break;
	case eeKEESE: //FLOATER
		lists[0] = g_el_list_keesemisc1;
		lists[1] = g_el_list_keesemisc2;
		lists[2] = g_el_enemies;
		break;
	case eeLEV:
		lists[0] = g_el_list_leevermisc1;
		break;
	case eeWALLM:
		lists[0] = g_el_list_noyesmisc;
		break;
	case eeTRAP:
		lists[0] = g_el_list_trapmisc1;
		lists[1] = g_el_list_trapmisc2;
		break;
	case eeROCK:
		lists[9] = g_el_list_rockmisc10;
		break;
	case eeNONE: //(Boss Death Triggers)
		lists[9] = g_el_list_yesnomisc;
		break;
	default:
		break;
	}
}

// loadAttributes: labels and dropdowns that follow other attributes' values
void guy_depends(int type, const long *attributes, std::string *l_attribute, std::string *h_attribute, const Q30NameVal **lists)
{
    switch(type)
	{
		case eeWALK:
		{
			static const int dependent_attribs[] = {2,3,4,7,9};
			for(auto q : dependent_attribs)
			{
				l_attribute[q] = "Unused:";
				h_attribute[q] = "";
			}
			const Q30NameVal *dl[12] = { NULL };
			
			if (attributes[0] == e1tSUMMON)
			{
				l_attribute[2] = "Summon Enemy ID:";
				h_attribute[2] = "Enemy to Summon";
				l_attribute[3] = "Summon Enemy Count:";
				h_attribute[3] = "Between 1 and N enemies will spawn each time the summoner summons. Minimum 1.";
				l_attribute[4] = "Max Summons:";
				h_attribute[4] = "Max summons before the enemy stops summoning. If 0, defaults to 41."
					" If 'Old Summoner Counts' is checked, any enemy of the summoned ID counts."
					" Otherwise, all enemies actually summoned by this enemy count."
					+ std::string();
				dl[2] = g_el_enemies;
			}
			else if (attributes[0] == e1tSUMMONLAYER)
			{
				l_attribute[3] = "Summon Enemy Count:";
				h_attribute[3] = "Between 1 and N enemies will spawn each time the summoner summons."
					" Minimum 1. If 0, will default to 3.";
				l_attribute[4] = "Max Summons:";
				h_attribute[4] = "Max summons before the enemy stops summoning. If 0, defaults to 41."
					" If 'Old Summoner Counts' is checked, all enemies on screen count."
					" Otherwise, all enemies actually summoned by this enemy count."
					+ std::string();
			}
			else
			{
				switch(attributes[1])
				{
					case e2tTRIBBLE:
						dl[2] = g_el_enemies;
						l_attribute[2] = "Enemy ID:";
						h_attribute[2] = "Enemy to Tribble into (transforming if not killed within a set time).";
						l_attribute[3] = "Enemy Count:";
						h_attribute[3] = "Tribble into this many copies of the specified enemy.";
						l_attribute[4] = "Tribble Time:";
						h_attribute[4] = "Tribbles into specified enemy after this many frames. If 0, defaults to 256 frames.";
						break;
					case e2tSPLITHIT:
					case e2tSPLIT:
						dl[2] = g_el_enemies;
						l_attribute[2] = "Enemy ID:";
						h_attribute[2] = "Enemy to Split into.";
						l_attribute[3] = "Enemy Count:";
						h_attribute[3] = "Split into this many copies of the specified enemy.";
						break;
					case e2tFIREOCTO:
					case e2tBOMBCHU:
						l_attribute[2] = "Weapon Offset:";
						h_attribute[2] = "This number is added to the enemy's 'Weapon' dropdown value to get the weapon used for the death effect. Yes this is confusing.";
						l_attribute[3] = "Weapon Dmg:";
						h_attribute[3] = "Damage of the weapon(s) spawned by the death effect";
						break;
				}
			}
			
			switch(attributes[6])
			{
				case e7tTEMPJINX:
				case e7tPERMJINX:
				case e7tUNJINX:
				{
					l_attribute[7] = "Jinx Type(s):";
					
					h_attribute[7] = "Bits: 1 sword jinx, 2 item jinx, 4 shield jinx.";
					break;
				}
				case e7tTAKEMAGIC:
				case e7tTAKERUPEES:
				{
					l_attribute[7] = "Amount:";
					break;
				}
				case e7tDRUNK:
				{
					l_attribute[7] = "Drunk Time:";
					h_attribute[7] = "Time, in frames, the effect lasts";
					break;
				}
				case e7tEATITEMS:
				case e7tEATMAGIC:
				case e7tEATRUPEES:
				{
					l_attribute[7] = "Delay:";
					h_attribute[7] = "Time, in frames, between each eating";
					break;
				}
				case e7tEATHURT:
				{
					l_attribute[7] = "Reserved:";
					break;
				}
			}
			
			switch(attributes[8])
			{
				case e9tROPE:
					l_attribute[9] = "Charge Step:";
					h_attribute[9] = "Step speed used when the enemy charges forward.";
					break;
				case e9tVIRE:
				case e9tPOLSVOICE:
					l_attribute[9] = "Reserved:";
					break;
				case e9tARMOS:
					l_attribute[9] = "Other Step:";
					h_attribute[9] = zq_fmt("Has a 50% chance to use this step speed instead of the enemy's normal step speed."
						" If the animation style is set to '{}', adds 20 to the tile of the enemy when this step is used.",
						anim_text(aARMOS));
					break;
			}
			
			for(auto q : dependent_attribs) lists[q] = dl[q];
			break;
		}
		case eeWIZZ:
		{
			static const int dependent_attribs[] = {2,5,6};
			for(auto q : dependent_attribs)
			{
				l_attribute[q] = "Unused:";
				h_attribute[q] = "";
			}
			const Q30NameVal *dl[12] = { NULL };
			
			if (attributes[1] == 2) // summon
			{
				l_attribute[2] = "Summon Enemy ID:";
				h_attribute[2] = "Enemy to Summon";
				l_attribute[5] = "Summon Enemy Count:";
				h_attribute[5] = "Between 1 and N enemies will spawn each time the summoner summons."
					" Minimum 1. If 0, will default to 3.";
				l_attribute[6] = "Max Summons:";
				h_attribute[6] = "Max summons before the enemy stops summoning. If 0, defaults to 41."
					" If 'Old Summoner Counts' is checked, any enemy of the summoned ID counts."
					" Otherwise, all enemies actually summoned by this enemy count."
					+ std::string();
				dl[2] = g_el_enemies;
			}
			else if (attributes[1] == 3) // summon layer
			{
				l_attribute[5] = "Summon Enemy Count:";
				h_attribute[5] = "Between 1 and N enemies will spawn each time the summoner summons."
					" Minimum 1. If 0, will default to 3.";
				l_attribute[6] = "Max Summons:";
				h_attribute[6] = "Max summons before the enemy stops summoning. If 0, defaults to 201."
					" If 'Old Summoner Counts' is checked, all enemies on screen count."
					" Otherwise, all enemies actually summoned by this enemy count."
					+ std::string();
			}
			
			for(auto q : dependent_attribs) lists[q] = dl[q];
		}
	}
}

} // namespace up30
} // namespace

int q30_itemclass_count(bool is30) { return is30 ? 285 : 280; }
int q30_enemyclass_count(bool is30) { return is30 ? 73 : 73; }
int q30_counter_count() { return (int)up30::MAX_COUNTERS; }

const char *q30_itemclass_name(int t)
{
    return t >= 0 && t < (int)up30::itype_max ? up30::g_itype_names[t] : "";
}

const char *q30_itemclass_help(int t)
{
    return t >= 0 && t < (int)up30::itype_max && up30::g_itype_help[t] ? up30::g_itype_help[t] : "";
}

const char *q30_counter_name(int c)
{
    if(c < 0)
        return "(None)";
    return c < (int)up30::MAX_COUNTERS ? up30::g_ctr_names[c] : "";
}

const char *q30_enemyclass_name(int e)
{
    return e >= 0 && e < (int)up30::eeMAX ? up30::g_etype_names[e] : "";
}

const char *q30_weapon_type_name(int w)
{
    return up30::weap_name(w);
}

int q30_defense_count() { return 28; }
const char *q30_defense_at(int k, int *value)
{
    if(k < 0 || k >= 28) return NULL;
    if(value) *value = up30::g_defenses[k].value;
    return up30::g_defenses[k].name;
}

int q30_anim_count() { return 56; }
const char *q30_anim_at(int k, int *value)
{
    if(k < 0 || k >= 56) return NULL;
    if(value) *value = up30::g_anims[k].value;
    return up30::g_anims[k].name;
}

void q30_enemy_labels(int type, const long *attributes, std::string *l_attribute, std::string *h_attribute, std::string *l_bflag,
                      std::string *h_bflag)
{
    for(int q = 0; q < 32; ++q)
    {
        l_attribute[q].clear();
        h_attribute[q].clear();
    }

    for(int q = 0; q < 16; ++q)
    {
        l_bflag[q].clear();
        h_bflag[q].clear();
    }

    up30::guy_labels(type, attributes, l_attribute, h_attribute, l_bflag, h_bflag);
    const Q30NameVal *lists[32] = { NULL };
    up30::guy_depends(type, attributes, l_attribute, h_attribute, lists);
}

void q30_enemy_attr_lists(int type, const long *attributes, const Q30NameVal **lists)
{
    for(int q = 0; q < 32; ++q)
        lists[q] = NULL;

    up30::guy_dropdowns(type, lists);
    std::string l[32], h[32];
    up30::guy_depends(type, attributes, l, h, lists);
}

bool q30_attr_list_is_enemies(const Q30NameVal *list)
{
    return list == up30::g_el_enemies;
}

void q30_item_labels(int type, unsigned flags, Q30ItemLabels &out)
{
    out = Q30ItemLabels();
    for(int q = 0; q < 10; ++q)
        out.misc_input_type[q] = up30::nswapLDEC;
    up30::item_labels(type, flags, out);
}
