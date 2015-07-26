#pragma once
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

#include <map>
#include <set>
#include <vector>

#include "guilib/Resolution.h"
#include "settings/lib/ISettingCallback.h"
#include "settings/lib/ISubSettings.h"
#include "threads/CriticalSection.h"
#include "utils/Observer.h"

class TiXmlNode;

typedef enum {
  CmsModeOff  = 0,
  CmsMode3dLut,
  CmsModeProfile
} CmsMode;

class CCMSSettings : public ISettingCallback, public ISubSettings,
                         public Observable
{
public:
  static CCMSSettings& Get();

  virtual bool Load(const TiXmlNode *settings);
  virtual bool Save(TiXmlNode *settings) const;

  virtual void OnSettingAction(const CSetting *setting);

  int m_CmsMode;
  std::string m_Cms3dLut;

protected:
  CCMSSettings();
  CCMSSettings(const CCMSSettings&);
  CCMSSettings& operator=(CCMSSettings const&);
  virtual ~CCMSSettings();

private:
  CCriticalSection m_critical;
};
