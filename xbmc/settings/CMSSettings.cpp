/*
 *      Copyright (C) 2013 Team XBMC
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

#include <algorithm>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include <float.h>

#include "CMSSettings.h"
#include "dialogs/GUIDialogFileBrowser.h"
#include "dialogs/GUIDialogYesNo.h"
#include "guilib/GraphicContext.h"
#include "guilib/gui3d.h"
#include "guilib/LocalizeStrings.h"
#include "guilib/StereoscopicsManager.h"
#include "settings/AdvancedSettings.h"
#include "settings/lib/Setting.h"
#include "settings/Settings.h"
#include "storage/MediaManager.h"
#include "threads/SingleLock.h"
#include "utils/log.h"
#include "utils/StringUtils.h"
#include "utils/Variant.h"
#include "utils/XMLUtils.h"
#include "windowing/WindowingFactory.h"

CCMSSettings::CCMSSettings()
{
  m_CmsMode = CmsModeOff;
  m_Cms3dLut = "";
}

CCMSSettings::~CCMSSettings()
{ }

CCMSSettings& CCMSSettings::Get()
{
  static CCMSSettings sCMSSettings;
  return sCMSSettings;
}

bool CCMSSettings::Load(const TiXmlNode *settings)
{
  if (settings == NULL)
    return false;

  CSingleLock lock(m_critical);
  const TiXmlElement *pElement = settings->FirstChildElement("cms");
  if (pElement != NULL)
  {
    if (!XMLUtils::GetInt(pElement, "cmsmode", m_CmsMode, CmsModeOff, CmsModeProfile))
      m_CmsMode = CmsModeOff;
    if (!XMLUtils::GetString(pElement, "cms3dlut", m_Cms3dLut))
      m_Cms3dLut = "";
  }

  return true;
}

bool CCMSSettings::Save(TiXmlNode *settings) const
{
  if (settings == NULL)
    return false;

  CSingleLock lock(m_critical);
  // default video settings
  TiXmlElement cmsSettingsNode("cms");
  TiXmlNode *pNode = settings->InsertEndChild(cmsSettingsNode);
  if (pNode == NULL)
    return false;

  XMLUtils::SetInt(pNode, "cmsmode", m_CmsMode);
  XMLUtils::SetString(pNode, "cms3dlut", m_Cms3dLut);

  return true;
}

void CCMSSettings::OnSettingAction(const CSetting *setting)
{
  if (setting == NULL)
    return;

  const std::string &settingId = setting->GetId();
  if (settingId == "cms.cms3dlut")
  {
    std::string path = ((CSettingString*)setting)->GetValue();
    VECSOURCES shares;
    g_mediaManager.GetLocalDrives(shares);
    if (CGUIDialogFileBrowser::ShowAndGetFile(shares, ".3dlut", g_localizeStrings.Get(16042), path))
    {
      ((CSettingString*)setting)->SetValue(path);
    }
  }
}
