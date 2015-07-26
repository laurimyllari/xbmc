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
#include "settings/MediaSettings.h"
#include "settings/Settings.h"
#include "settings/lib/Setting.h"
#include "settings/lib/SettingsManager.h"
#include "utils/log.h"
#include "utils/URIUtils.h"
#include "video/VideoDatabase.h"
#include "utils/Variant.h"

#include <vector>

#define SETTING_VIDEO_CMSMODE             "video.cmsmode"
#define SETTING_VIDEO_CMS3DLUT            "video.cms3dlut"

CGUIDialogCMSSettings::CGUIDialogCMSSettings()
    : CGUIDialogSettingsManualBase(WINDOW_DIALOG_CMS_OSD_SETTINGS, "VideoOSDSettings.xml"),
      m_viewModeChanged(false)
{ }

CGUIDialogCMSSettings::~CGUIDialogCMSSettings()
{ }

void CGUIDialogCMSSettings::OnSettingChanged(const CSetting *setting)
{
  if (setting == NULL)
    return;

  CGUIDialogSettingsManualBase::OnSettingChanged(setting);

  CVideoSettings &videoSettings = CMediaSettings::Get().GetCurrentVideoSettings();

  const std::string &settingId = setting->GetId();
  if (settingId == SETTING_VIDEO_CMSMODE)
    videoSettings.m_CmsMode = static_cast<const CSettingInt*>(setting)->GetValue();
}

void CGUIDialogCMSSettings::OnSettingAction(const CSetting *setting)
{
  if (setting == NULL)
    return;

  CGUIDialogSettingsManualBase::OnSettingChanged(setting);

  const std::string &settingId = setting->GetId();
}

void CGUIDialogCMSSettings::Save()
{
  if (CProfilesManager::Get().GetMasterProfile().getLockMode() != LOCK_MODE_EVERYONE &&
      !g_passwordManager.CheckSettingLevelLock(::SettingLevelExpert))
    return;

}

void CGUIDialogCMSSettings::SetupView()
{
  CGUIDialogSettingsManualBase::SetupView();

  SetHeading(13395);
}

void CGUIDialogCMSSettings::InitializeSettings()
{
  CGUIDialogSettingsManualBase::InitializeSettings();

  CSettingCategory *category = AddCategory("audiosubtitlesettings", -1);
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

  CVideoSettings &videoSettings = CMediaSettings::Get().GetCurrentVideoSettings();
  
  StaticIntegerSettingOptions entries;

  // color management settings
  // entries.clear();
  entries.push_back(std::make_pair(16039, CmsModeOff));
  entries.push_back(std::make_pair(16042, CmsMode3dLut));
#ifdef HAVE_LCMS2
  entries.push_back(std::make_pair(16043, CmsModeProfile));
#endif
  AddSpinner(groupColorManagement, SETTING_VIDEO_CMSMODE, 36554, 0, videoSettings.m_CmsMode, entries);
  AddList(groupColorManagement, SETTING_VIDEO_CMS3DLUT, 36555, 0, videoSettings.m_Cms3dLut, Cms3dLutsFiller, 36555);
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
  std::string current3dlut = CSettings::Get().GetString("videoscreen.default3dlut");
  if (!current3dlut.empty())
    current3dlut = URIUtils::GetDirectory(current3dlut);
  XFILE::CDirectory::GetDirectory(current3dlut, items, ".3dlut");

  for (int i = 0; i < items.Size(); i++)
  {
    list.push_back(make_pair(items[i]->GetLabel(), items[i]->GetLabel()));
  }
}
