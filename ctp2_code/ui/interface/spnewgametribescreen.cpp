//----------------------------------------------------------------------------
//
// Project      : Call To Power 2
// File type    : C++ source
// Description  : Single player new game civ selection screen
// Id           : $Id$
//
//----------------------------------------------------------------------------
//
// Disclaimer
//
// THIS FILE IS NOT GENERATED OR SUPPORTED BY ACTIVISION.
//
// This material has been developed at apolyton.net by the Apolyton CtP2
// Source Code Project. Contact the authors at ctp2source@apolyton.net.
//
//----------------------------------------------------------------------------
//
// Compiler flags
//
// USE_SELECT_BARBARIANS    : make Barbarians selectable
// USE_SHOW_BARBARIANS      : make Barbarians visible
//
//----------------------------------------------------------------------------
//
// Modifications from the original Activision code:
//
// - Memory leaks repaired.
// - Replaced old civ selection button bank by list box. (2-Jan-2008 Martin Gühmann)
//
//----------------------------------------------------------------------------

#include "c3.h"
#include "c3window.h"
#include "c3_popupwindow.h"
#include "c3_button.h"
#include "c3_listitem.h"
#include "c3_dropdown.h"
#include "c3_static.h"
#include "c3slider.h"
#include "c3ui.h"
#include "aui_radio.h"
#include "aui_switchgroup.h"
#include "aui_uniqueid.h"

#include "profileDB.h"

#include "spnewgamewindow.h"
#include "spnewgamediffscreen.h"

#include "ctp2_listbox.h"
#include "ctp2_listitem.h"
#include "ctp2_Static.h"
#include "DifficultyRecord.h"
#include "RiskRecord.h"

#include "keypress.h"

//original tribe screen includes

#include "c3.h"                     // Precompiled header
#include "spnewgametribescreen.h"   // Own declarations: consistency check

#include "aui_blitter.h"
#include "aui_Factory.h"
#include "aui_image.h"
#include "aui_radio.h"
#include "aui_surface.h"
#include "aui_switchgroup.h"
#include "aui_uniqueid.h"
#include "c3_button.h"
#include "c3_dropdown.h"
#include "c3_listitem.h"
#include "c3_popupwindow.h"
#include "c3_static.h"
#include "c3slider.h"
#include "c3textfield.h"
#include "c3ui.h"
#include "c3window.h"
#include "Civilisation.h"
#include "CivilisationRecord.h"
#include "CivPaths.h"               // g_civPaths
#include "gamefile.h"
#include "keypress.h"
#include "player.h"                 // g_player
#include "profileDB.h"              // g_theProfileDB
#include "SelItem.h"
#include "spnewgamewindow.h"
#include "StrDB.h"                  // g_theStringDB
#include "textradio.h"
#include <map>                      // std::map
#include <string>                   // std::string
#include <vector>                   // std::vector
//end includes

extern C3UI             *g_c3ui;
extern ProfileDB        *g_theProfileDB;
extern sint32               g_isCheatModeOn;

c3_PopupWindow              *g_spNewGameTribeScreen  = NULL; //added
static ctp2_ListBox         *s_CivListBox            = NULL;
//original gender buttons
static c3_Static			*s_leaderNameStatic		 = NULL;
static C3TextField			*s_leaderNameTextField	 = NULL;
static aui_SwitchGroup		*s_maleFemaleSwitchGroup = NULL;
static GENDER				s_gender;
sint32  const               INDEX_TRIBE_INVALID = -1;
aui_Radio					*s_maleRadio = NULL;
aui_Radio					*s_femaleRadio = NULL;
sint32						s_tribeIndex    = INDEX_TRIBE_INVALID;
sint32						s_skipIndex = 0;

// Civ list flags: the diplomacy flags, shrunk once and kept until the screen is cleaned up.
// Keyed by file name because a scenario can load another civilisation database.
static sint32 const         k_FLAG_THUMBNAIL_WIDTH   = 40;
static sint32 const         k_FLAG_THUMBNAIL_HEIGHT  = 24;
static std::map<std::string, aui_Surface *> s_flagThumbnails;

// Averages the source pixels behind each thumbnail pixel: the flags are 250x150, and the
// blitter's nearest-neighbour stretch loses their thin stripes at this size.
static aui_Surface * spnewgametribescreen_createFlagThumbnail(MBCHAR const * fileName)
{
	MBCHAR path[_MAX_PATH];
	if (!g_civPaths->FindFile(C3DIR_PICTURES, fileName, path, true))
		return NULL;

	aui_Image * image = g_c3ui->LoadImage(fileName);
	if (!image)
		return NULL;

	aui_Surface * thumbnail = NULL;
	aui_Surface * source    = image->TheSurface();
	AUI_ERRCODE   errcode   = AUI_ERRCODE_OK;

	if (source && source->BitsPerPixel() == 16)
	{
		thumbnail = aui_Factory::new_Surface(errcode, k_FLAG_THUMBNAIL_WIDTH, k_FLAG_THUMBNAIL_HEIGHT);
	}

	uint8 * sourceBuffer = NULL;
	uint8 * targetBuffer = NULL;
	bool    done         = false;

	if (thumbnail && AUI_SUCCESS(errcode)
	 && thumbnail->BitsPerPixel() == 16
	 && thumbnail->PixelFormat() == source->PixelFormat()
	 && source->Lock(NULL, (LPVOID *)&sourceBuffer, 0) == AUI_ERRCODE_OK)
	{
		if (thumbnail->Lock(NULL, (LPVOID *)&targetBuffer, 0) == AUI_ERRCODE_OK)
		{
			bool const   is565     = source->PixelFormat() == AUI_SURFACE_PIXELFORMAT_565;
			sint32 const redShift  = is565 ? 11 : 10;
			uint32 const greenMask = is565 ? 0x3f : 0x1f;

			for (sint32 y = 0; y < k_FLAG_THUMBNAIL_HEIGHT; ++y)
			{
				sint32 const top    = y * source->Height() / k_FLAG_THUMBNAIL_HEIGHT;
				sint32 const bottom = (y + 1) * source->Height() / k_FLAG_THUMBNAIL_HEIGHT;
				uint16 *     target = (uint16 *)(targetBuffer + y * thumbnail->Pitch());

				for (sint32 x = 0; x < k_FLAG_THUMBNAIL_WIDTH; ++x)
				{
					sint32 const left  = x * source->Width() / k_FLAG_THUMBNAIL_WIDTH;
					sint32 const right = (x + 1) * source->Width() / k_FLAG_THUMBNAIL_WIDTH;
					uint32       red   = 0;
					uint32       green = 0;
					uint32       blue  = 0;

					for (sint32 sy = top; sy < bottom; ++sy)
					{
						uint16 const * row = (uint16 const *)(sourceBuffer + sy * source->Pitch());
						for (sint32 sx = left; sx < right; ++sx)
						{
							red   += (row[sx] >> redShift) & 0x1f;
							green += (row[sx] >> 5) & greenMask;
							blue  += row[sx] & 0x1f;
						}
					}

					uint32 const count = (right - left) * (bottom - top);
					bool const   edge  = x == 0 || y == 0
					                  || x == k_FLAG_THUMBNAIL_WIDTH - 1
					                  || y == k_FLAG_THUMBNAIL_HEIGHT - 1;

					target[x] = edge ? 0 : (uint16)
						((((red + count / 2) / count) << redShift)
						 | (((green + count / 2) / count) << 5)
						 | ((blue + count / 2) / count));
				}
			}

			thumbnail->Unlock(targetBuffer);
			done = true;
		}

		source->Unlock(sourceBuffer);
	}

	g_c3ui->UnloadImage(image);

	if (!done)
	{
		delete thumbnail;
		thumbnail = NULL;
	}

	return thumbnail;
}

static AUI_ERRCODE spnewgametribescreen_drawFlag(ctp2_Static * control, aui_Surface * surface, RECT & rect, Cookie cookie)
{
	sint32 const tribe = cookie.m_sin32Type;
	if (tribe < 0 || tribe >= g_theCivilisationDB->NumRecords())
		return AUI_ERRCODE_OK;

	MBCHAR const * fileName = g_theStringDB->GetNameStr(g_theCivilisationDB->Get(tribe)->GetEmissaryPhotoMale());
	if (!fileName)
		return AUI_ERRCODE_OK;

	std::map<std::string, aui_Surface *>::iterator found = s_flagThumbnails.find(fileName);
	if (found == s_flagThumbnails.end())
	{
		// A civilisation without a usable flag is remembered too, so it is only looked for once.
		found = s_flagThumbnails.insert(std::make_pair(std::string(fileName), spnewgametribescreen_createFlagThumbnail(fileName))).first;
	}

	if (!found->second)
		return AUI_ERRCODE_OK;

	RECT source = { 0, 0, k_FLAG_THUMBNAIL_WIDTH, k_FLAG_THUMBNAIL_HEIGHT };
	return g_c3ui->TheBlitter()->Blt(surface, rect.left, rect.top, found->second, &source, k_AUI_BLITTER_FLAG_COPY);
}

sint32 spnewgametribescreen_getTribeIndex( void )
{
	return s_tribeIndex;
}

void spnewgametribescreen_setTribeIndex( sint32 index, MBCHAR *lname )
{

	if (index < 0)
	{
		s_tribeIndex = INDEX_TRIBE_INVALID;
		return;
	}

	//s_CivListBox->SelectItem(index);

	for(sint32 i = 0; i < s_CivListBox->NumItems(); ++i)
	{
		ctp2_ListItem *item = (ctp2_ListItem *)s_CivListBox->GetItemByIndex(i);
		if(index == item->GetUserDataSint32())
		{
			s_CivListBox->SelectItem(i);
		}
	}

	s_tribeIndex = index;

	sint32 const    playerIndex         =
	    g_selected_item ? g_selected_item->GetVisiblePlayer() : 1;
	bool const      shouldSetProfileDB  =
	    !g_isCheatModeOn ||
	     (g_player[playerIndex] &&
	      g_player[playerIndex]->IsHuman()
	     );

	if (shouldSetProfileDB)
		g_theProfileDB->SetCivIndex(index);

	StringId civString = g_theCivilisationDB->Get(index)->GetPluralCivName();

	if (shouldSetProfileDB)
		g_theProfileDB->SetCivName((MBCHAR *)g_theStringDB->GetNameStr(civString));

	s_gender = s_maleRadio->GetState() ? GENDER_MALE : GENDER_FEMALE;
	if (shouldSetProfileDB)
	{
		g_theProfileDB->SetGender(s_gender);
	}

	if ( lname )
	{
		s_leaderNameTextField->SetFieldText( lname );

		if (shouldSetProfileDB)
			g_theProfileDB->SetLeaderName( lname );

		spnewgamescreen_setPlayerName( lname );
	}
	else
	{

		StringId nameString = g_theCivilisationDB->Get(index)->GetLeaderNameMale();

		if ( s_maleRadio->GetState() )
			nameString = g_theCivilisationDB->Get(index)->GetLeaderNameMale();
		else
			nameString = g_theCivilisationDB->Get(index)->GetLeaderNameFemale();

		s_leaderNameTextField->SetFieldText(g_theStringDB->GetNameStr(nameString) );

		if (shouldSetProfileDB)
			g_theProfileDB->
				SetLeaderName((MBCHAR *)g_theStringDB->GetNameStr(nameString));

		spnewgamescreen_setPlayerName(g_theStringDB->GetNameStr(nameString));
	}
}

sint32	spnewgametribescreen_displayMyWindow( Cookie cookie, BOOL edit )
{
	sint32 retval = g_spNewGameTribeScreen ? 0 : spnewgametribescreen_Initialize();

	AUI_ERRCODE auiErr = g_c3ui->AddWindow(g_spNewGameTribeScreen);
	Assert(auiErr == AUI_ERRCODE_OK);

	keypress_RegisterHandler(g_spNewGameTribeScreen);

	if ( edit && !(g_isScenario && g_startInfoType == STARTINFOTYPE_NOLOCS))
		s_leaderNameTextField->Enable( TRUE );
	else
		s_leaderNameTextField->Enable( FALSE );

	g_spNewGameTribeScreen->Ok()->SetActionFuncAndCookie( g_spNewGameTribeScreen->Ok()->GetActionFunc(), cookie );

	return retval;
}

sint32 spnewgametribescreen_removeMyWindow(uint32 action, MBCHAR *lname)
{
	if ( action != (uint32)AUI_BUTTON_ACTION_EXECUTE ) return 0;

	ctp2_ListItem *item = (ctp2_ListItem *)s_CivListBox->GetSelectedItem();
	sint32 index   = item->GetUserDataSint32();

	if ( lname )
		s_leaderNameTextField->GetFieldText( lname, 100 );

	spnewgametribescreen_setTribeIndex( index, lname );

	AUI_ERRCODE auiErr = g_c3ui->RemoveWindow(g_spNewGameTribeScreen->Id());
	Assert( auiErr == AUI_ERRCODE_OK );

	keypress_RemoveHandler(g_spNewGameTribeScreen);
	spnewgamescreen_update();

	return 1;
}

AUI_ERRCODE spnewgametribescreen_Initialize( aui_Control::ControlActionCallback *callback )
{
	AUI_ERRCODE errcode = AUI_ERRCODE_OK;
	MBCHAR		windowBlock[ k_AUI_LDL_MAXBLOCK + 1 ];
	MBCHAR		controlBlock[ k_AUI_LDL_MAXBLOCK + 1 ];
	s_tribeIndex = INDEX_TRIBE_INVALID;
	if ( g_spNewGameTribeScreen ) {
		if(callback) {
			g_spNewGameTribeScreen->Ok()->SetActionFuncAndCookie(callback, nullptr);
		}
		return AUI_ERRCODE_OK;
	}

	strcpy(windowBlock, "SPNewGameTribeScreen");

	{
		g_spNewGameTribeScreen = new c3_PopupWindow( &errcode, aui_UniqueId(), windowBlock, 16, AUI_WINDOW_TYPE_FLOATING, false);
		Assert( AUI_NEWOK(g_spNewGameTribeScreen, errcode) );
		if ( !AUI_NEWOK(g_spNewGameTribeScreen, errcode) ) return errcode;

		g_spNewGameTribeScreen->Resize(g_spNewGameTribeScreen->Width(),g_spNewGameTribeScreen->Height());
		g_spNewGameTribeScreen->GrabRegion()->Resize(g_spNewGameTribeScreen->Width(),g_spNewGameTribeScreen->Height());
		g_spNewGameTribeScreen->SetStronglyModal(TRUE);
	}

	if ( !callback ) callback = spnewgametribescreen_backPress;

	sprintf( controlBlock, "%s.%s", windowBlock, "Name" );
	g_spNewGameTribeScreen->AddTitle( controlBlock );
	g_spNewGameTribeScreen->AddClose( callback );

	s_CivListBox = (ctp2_ListBox *)aui_Ldl::BuildHierarchyFromRoot("SPNewGameTribeScreen.CivBox");

	Assert(s_CivListBox);

	spnewgametribescreen_addAllTribes();

	sprintf( controlBlock, "%s.%s", windowBlock, "leadernamestatic" );
	s_leaderNameStatic = new c3_Static(&errcode, aui_UniqueId(), controlBlock );

	sprintf( controlBlock, "%s.%s", windowBlock, "leadernametextfield" );
	s_leaderNameTextField = new C3TextField(&errcode, aui_UniqueId(), controlBlock );

	sprintf( controlBlock, "%s.%s", windowBlock, "malefemaleswitchgroup" );
	s_maleFemaleSwitchGroup = new aui_SwitchGroup( &errcode, aui_UniqueId(), controlBlock );

	sprintf( controlBlock, "%s.%s", windowBlock, "malefemaleswitchgroup.maleradio" );
	s_maleRadio = new aui_Radio(&errcode, aui_UniqueId(), controlBlock, spnewgametribescreen_malePress );

	sprintf( controlBlock, "%s.%s", windowBlock, "malefemaleswitchgroup.femaleradio" );
	s_femaleRadio = new aui_Radio(&errcode, aui_UniqueId(), controlBlock, spnewgametribescreen_femalePress );

	if(g_isScenario && g_startInfoType == STARTINFOTYPE_NOLOCS)
	{
		s_leaderNameTextField->Enable(FALSE);
		s_maleRadio->Enable(FALSE);
		s_femaleRadio->Enable(FALSE);
	}
	else
	{
		s_leaderNameTextField->Enable(TRUE);
		s_maleRadio->Enable(TRUE);
		s_femaleRadio->Enable(TRUE);
	}

	errcode = aui_Ldl::SetupHeirarchyFromRoot( windowBlock );
	Assert( AUI_SUCCESS(errcode) );

	s_CivListBox->SetForceSelect(TRUE);
	s_CivListBox->SetMultiSelect(FALSE);
	s_CivListBox->SetActionFuncAndCookie(spnewgametribescreen_switchPress, nullptr);

	return AUI_ERRCODE_OK;
}

//----------------------------------------------------------------------------
//
// Name       : spnewgametribescreen_Cleanup
//
// Description: Release the memory of the screen.
//
// Parameters : -
//
// Globals    : g_spNewGameTribeScreen
//              s_barbarianThreat
//              s_riskBox
//              s_skillLevel
//              s_CivListBox
//              s_riskListBox
//
// Returns    : AUI_ERRCODE : always AUI_ERRCODE_OK
//
// Remark(s)  : -
//
//----------------------------------------------------------------------------
AUI_ERRCODE spnewgametribescreen_Cleanup()
{
	if (g_spNewGameTribeScreen)
	{
		g_c3ui->RemoveWindow(g_spNewGameTribeScreen->Id());
		keypress_RemoveHandler(g_spNewGameTribeScreen);

		allocated::clear(s_CivListBox);
		allocated::clear(s_leaderNameStatic);
		allocated::clear(s_leaderNameTextField);
		allocated::clear(s_maleFemaleSwitchGroup);
		allocated::clear(s_maleRadio);
		allocated::clear(s_femaleRadio);
		allocated::clear(g_spNewGameTribeScreen);

		for (std::map<std::string, aui_Surface *>::iterator it = s_flagThumbnails.begin(); it != s_flagThumbnails.end(); ++it)
		{
			delete it->second;
		}
		s_flagThumbnails.clear();
	}

	return AUI_ERRCODE_OK;
}

//original code for gender, etc

void spnewgametribescreen_getLeaderName(MBCHAR *lname)
{
	s_leaderNameTextField->GetFieldText(lname, 100);
}

GENDER spnewgametribescreen_getGender(void)
{
	return s_gender;
}

//action buttons
void spnewgametribescreen_acceptPress(aui_Control *control, uint32 action, uint32 data, Cookie cookie)
{
	spnewgametribescreen_removeMyWindow(action);
}

void spnewgametribescreen_cancelPress(aui_Control *control, uint32 action, uint32 data, Cookie cookie)
{
	spnewgametribescreen_removeMyWindow(action);
}

void spnewgametribescreen_backPress(aui_Control *control, uint32 action, uint32 data, Cookie cookie)
{
	const sint32 size = 100;
	MBCHAR lname[ size + 1 ];
	spnewgametribescreen_getLeaderName( lname );

	spnewgametribescreen_removeMyWindow(action, lname);
}

void spnewgametribescreen_switchPress(aui_Control *control, uint32 action, uint32 data, Cookie cookie)
{
	//this is for the text boxes and updated once selected
	if ( action != (uint32)AUI_LISTBOX_ACTION_SELECT ) return;

	ctp2_ListItem *item = (ctp2_ListItem *)s_CivListBox->GetSelectedItem();
	sint32 civ = item->GetUserDataSint32();
	StringId nameString;

	if ( s_maleRadio->GetState() )
		nameString = g_theCivilisationDB->Get(civ)->GetLeaderNameMale();
	else
		nameString = g_theCivilisationDB->Get(civ)->GetLeaderNameFemale();

	s_leaderNameTextField->SetFieldText( g_theStringDB->GetNameStr(nameString) );
}

void spnewgametribescreen_malePress(aui_Control *control, uint32 action, uint32 data, Cookie cookie)
{
	if ( action != (uint32)AUI_SWITCH_ACTION_ON ) return;

	ctp2_ListItem *item = (ctp2_ListItem *)s_CivListBox->GetSelectedItem();

	if(item)
	{
		sint32 civ = item->GetUserDataSint32();

		StringId const  nameString  = g_theCivilisationDB->Get(civ)->GetLeaderNameMale();

		s_leaderNameTextField->SetFieldText(g_theStringDB->GetNameStr(nameString));
	}
}

void spnewgametribescreen_femalePress(aui_Control *control, uint32 action, uint32 data, Cookie cookie)
{
	if ( action != (uint32)AUI_SWITCH_ACTION_ON ) return;

	ctp2_ListItem *item = (ctp2_ListItem *)s_CivListBox->GetSelectedItem();

	if(item)
	{
		sint32 civ = item->GetUserDataSint32();

		StringId const  nameString  = g_theCivilisationDB->Get(civ)->GetLeaderNameFemale();

		s_leaderNameTextField->SetFieldText(g_theStringDB->GetNameStr(nameString));
	}
}

void spnewgametribescreen_clearTribes()
{
	s_CivListBox->Clear();
}

void spnewgametribescreen_addTribeNoDuplicate(sint32 tribe)
{
	for(sint32 i = 0; i < s_CivListBox->NumItems(); i++)
	{
		ctp2_ListItem *item = (ctp2_ListItem *)s_CivListBox->GetItemByIndex(i);
		if(tribe == item->GetUserDataSint32())
		{
			return;
		}
	}

	spnewgametribescreen_addTribe(tribe);

	s_CivListBox->SortByColumn(0, true);
}

void spnewgametribescreen_addTribe(sint32 tribe)
{
	if (tribe < 0)
		return;

#if defined(USE_SHOW_BARBARIANS) || defined(USE_SELECT_BARBARIANS)
	// Always create a list item
#else
	if (PLAYER_INDEX_VANDALS == tribe) // PLAYER_INDEX_VANDALS obsolete
	{
		s_skipIndex = tribe;
	}
	else
#endif
	{
		ctp2_ListItem *item = (ctp2_ListItem *)aui_Ldl::BuildHierarchyFromRoot("CivItem");
		Assert(item);
		if(!item)
			return;

		item->SetUserData(tribe);

		// The list box makes each direct child of an item a column, so the name and flag share a box.
		ctp2_Static *box = (ctp2_Static *)item->GetChildByIndex(0);
		Assert(box);
		if(!box)
			return;

		ctp2_Static *text = (ctp2_Static *)box->GetChildByIndex(0);
		Assert(text);
		if(!text)
			return;

		text->SetText(g_theStringDB->GetNameStr(g_theCivilisationDB->Get(tribe)->GetPluralCivName()));  //singular? // Maybe change to civ name and then we can change it to Roman Empire, instead of having the boring name Romans
		ctp2_Static *flag = (ctp2_Static *)box->GetChildByIndex(1);
		Assert(flag);
		if(flag)
		{
			flag->SetDrawCallbackAndCookie(spnewgametribescreen_drawFlag, tribe);
		}

		item->SetCompareCallback(spnewgametribescreen_CompareItems);
		s_CivListBox->AddItem(item);
	}
}

void spnewgametribescreen_removeTribe(sint32 tribe)
{
	for(sint32 i = s_CivListBox->NumItems() - 1; i >= 0; i--)
	{
		ctp2_ListItem *item = (ctp2_ListItem *)s_CivListBox->GetItemByIndex(i);
		if(tribe == item->GetUserDataSint32())
		{
			s_CivListBox->RemoveItemByIndex(i);
			delete item;
			return;
		}
	}
}

void spnewgametribescreen_addAllTribes()
{
	for(sint32 i = 0; i < g_theCivilisationDB->NumRecords(); ++i)
	{
		spnewgametribescreen_addTribe(i);
	}

	s_CivListBox->SortByColumn(0, true);
}

sint32 spnewgametribescreen_CompareItems(ctp2_ListItem *item1, ctp2_ListItem *item2, sint32 column)
{
	ctp2_Static *text1 = (ctp2_Static *)item1->GetChildByIndex(0)->GetChildByIndex(0);
	ctp2_Static *text2 = (ctp2_Static *)item2->GetChildByIndex(0)->GetChildByIndex(0);

	return _stricoll(text1->GetText(), text2->GetText());
}
