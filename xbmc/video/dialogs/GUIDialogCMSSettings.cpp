/*
 *      Copyright (C) 2005-2014 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

// FIXME: clean up includes
#include "system.h"
#include "FileItem.h"
#include "GUIDialogCMSSettings.h"
#include "GUIPassword.h"
#include "addons/Skin.h"
#ifdef HAS_VIDEO_PLAYBACK
#include "cores/VideoRenderers/RenderManager.h"
#endif
#include "dialogs/GUIDialogYesNo.h"
#include "filesystem/Directory.h"
#include "filesystem/File.h"
#include "guilib/GUIWindowManager.h"
#include "profiles/ProfilesManager.h"
#include "settings/Settings.h"
#include "settings/lib/Setting.h"
#include "settings/lib/SettingsManager.h"
#include "utils/log.h"
#include "utils/URIUtils.h"
#include "video/VideoDatabase.h"
#include "utils/Variant.h"

#include <vector>

#define SETTING_VIDEO_CMSMODE             "videoscreen.colormanagement"
#define SETTING_VIDEO_CMS3DLUT            "videoscreen.cms3dlut"

CGUIDialogCMSSettings::CGUIDialogCMSSettings()
    : CGUIDialogSettingsManualBase(WINDOW_DIALOG_CMS_OSD_SETTINGS, "VideoOSDSettings.xml")
{ }

CGUIDialogCMSSettings::~CGUIDialogCMSSettings()
{ }

void CGUIDialogCMSSettings::SetupView()
{
  CGUIDialogSettingsManualBase::SetupView();

  SetHeading(13395);
}

void CGUIDialogCMSSettings::InitializeSettings()
{
  CGUIDialogSettingsManualBase::InitializeSettings();

  CSettingCategory *category = AddCategory("cmssettings", -1);
  if (category == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogCMSSettings: unable to setup settings");
    return;
  }

  // get all necessary setting groups
  CSettingGroup *groupColorManagement = AddGroup(category);
  if (groupColorManagement == NULL)
  {
    CLog::Log(LOGERROR, "CGUIDialogCMSSettings: unable to setup settings");
    return;
  }

  bool usePopup = g_SkinInfo->HasSkinFile("DialogSlider.xml");

  StaticIntegerSettingOptions entries;

  // color management settings
  entries.clear();
  entries.push_back(std::make_pair(16039, 0 /* CMS_MODE_OFF */)); // FIXME: get from CMS class
  entries.push_back(std::make_pair(16042, 1 /* CMS_MODE_3DLUT */ ));
#ifdef HAVE_LCMS2
  entries.push_back(std::make_pair(16043, 2 /* CMS_MODE_PROFILE */));
#endif
  int currentMode = CSettings::Get().GetInt("videoscreen.colormanagement");
  AddSpinner(groupColorManagement, SETTING_VIDEO_CMSMODE, 36554, 0, currentMode, entries);
  std::string current3dLUT = CSettings::Get().GetString("videoscreen.cms3dlut");
  AddList(groupColorManagement, SETTING_VIDEO_CMS3DLUT, 36555, 0, current3dLUT, Cms3dLutsFiller, 36555);
}

void CGUIDialogCMSSettings::OnSettingChanged(const CSetting *setting)
{
  if (setting == NULL)
    return;

  CGUIDialogSettingsManualBase::OnSettingChanged(setting);

  const std::string &settingId = setting->GetId();
  if (settingId == SETTING_VIDEO_CMSMODE)
    CSettings::Get().SetInt(SETTING_VIDEO_CMSMODE, static_cast<int>(static_cast<const CSettingInt*>(setting)->GetValue()));
  else if (settingId == SETTING_VIDEO_CMS3DLUT)
    CSettings::Get().SetString(SETTING_VIDEO_CMS3DLUT, static_cast<std::string>(static_cast<const CSettingString*>(setting)->GetValue()));
}

bool CGUIDialogCMSSettings::OnBack(int actionID)
{
  Save();
  return CGUIDialogSettingsBase::OnBack(actionID);
}

void CGUIDialogCMSSettings::Save()
{
  CLog::Log(LOGINFO, "CGUIDialogCMSSettings: Save() called");
  CSettings::Get().Save();
}

void CGUIDialogCMSSettings::Cms3dLutsFiller(
    const CSetting *setting,
    std::vector< std::pair<std::string, std::string> > &list,
    std::string &current,
    void *data)
{
  // get 3dLut directory from settings
  CFileItemList items;

  // list .3dlut files
  std::string current3dlut = CSettings::Get().GetString("videoscreen.cms3dlut");
  if (!current3dlut.empty())
    current3dlut = URIUtils::GetDirectory(current3dlut);
  XFILE::CDirectory::GetDirectory(current3dlut, items, ".3dlut");

  for (int i = 0; i < items.Size(); i++)
  {
    list.push_back(make_pair(items[i]->GetLabel(), items[i]->GetPath()));
  }
}
