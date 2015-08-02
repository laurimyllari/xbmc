#include <math.h>
#include <string>

#include "ColorManager.h"
#include "filesystem/File.h"
#include "settings/Settings.h"
#include "utils/log.h"

using namespace XFILE;

CColorManager &CColorManager::Get()
{
  static CColorManager s_colorManager;
  return s_colorManager;
}

CColorManager::CColorManager()
{
  curVideoPrimaries = 0;
  curClutSize = 0;
  curCmsToken = 0;
  curCmsMode = 0;
  cur3dlutFile = "";
  curIccProfile = "";
}

CColorManager::~CColorManager()
{
}

bool CColorManager::IsEnabled()
{
  //TODO: check that the configuration is valid here (files exist etc)

  return CSettings::Get().GetInt("videoscreen.colormanagement") != CMS_MODE_OFF;
}

bool CColorManager::GetVideo3dLut(int primaries, int *cmsToken, int *clutSize, float **clutData)
{
  switch (CSettings::Get().GetInt("videoscreen.colormanagement"))
  {
  case CMS_MODE_3DLUT:
    cur3dlutFile = CSettings::Get().GetString("videoscreen.cms3dlut");
    if (!Load3dLut(cur3dlutFile, clutData, clutSize))
      return false;
    // set current state
    curVideoPrimaries = primaries;
    curClutSize = *clutSize;
    *cmsToken = ++curCmsToken;
    curCmsMode = CMS_MODE_3DLUT;
    return true;

  case CMS_MODE_PROFILE:


  case CMS_MODE_OFF:
  default:
    return false;
  }
}

bool CColorManager::CheckConfiguration(int cmsToken)
{
  if (cmsToken != curCmsToken)
    return false;
  if (curCmsMode != CSettings::Get().GetInt("videoscreen.colormanagement"))
    return false;   // CMS mode has changed
  switch (curCmsMode)
  {
  case CMS_MODE_3DLUT:
    if (cur3dlutFile != CSettings::Get().GetString("videoscreen.cms3dlut"))
      return false; // different 3dlut file selected
    break;
  case CMS_MODE_PROFILE:
    if (curIccProfile != CSettings::Get().GetString("videoscreen.displayprofile"))
      return false; // different ICC profile selected
    // TODO: check other parameters
    break;
  default:
    CLog::Log(LOGERROR, "%s: unexpected CMS mode: %d", __FUNCTION__, curCmsMode);
    return false;
  }
  return true;
}



// madvr 3dlut file format support
struct H3DLUT
{
    char signature[4];         // file signature; must be: '3DLT'
    uint32_t fileVersion;          // file format version number (currently "1")
    char programName[32];      // name of the program that created the file
    uint64_t programVersion;  // version number of the program that created the file
    uint32_t inputBitDepth[3];     // input bit depth per component (Y,Cb,Cr or R,G,B)
    uint32_t inputColorEncoding;   // input color encoding standard
    uint32_t outputBitDepth;       // output bit depth for all components (valid values are 8, 16 and 32)
    uint32_t outputColorEncoding;  // output color encoding standard
    uint32_t parametersFileOffset; // number of bytes between the beginning of the file and array parametersData
    uint32_t parametersSize;       // size in bytes of the array parametersData
    uint32_t lutFileOffset;        // number of bytes between the beginning of the file and array lutData
    uint32_t lutCompressionMethod; // type of compression used if any (0 = none, ...)
    uint32_t lutCompressedSize;    // size in bytes of the array lutData inside the file, whether compressed or not
    uint32_t lutUncompressedSize;  // true size in bytes of the array lutData when in memory for usage (outside the file)
    // This header is followed by the char array 'parametersData', of length 'parametersSize',
    // and by the array 'lutDataxx', of length 'lutCompressedSize'.
};

bool CColorManager::Probe3dLut(const std::string filename)
{
    struct H3DLUT header;
    CFile lutFile;

    if (!lutFile.Open(filename))
    {
        CLog::Log(LOGERROR, "%s: Could not open 3DLUT file: %s", __FUNCTION__, filename.c_str());
        return false;
    }

    if (lutFile.Read(&header, sizeof(header)) < sizeof(header))
    {
        CLog::Log(LOGERROR, "%s: Could not read 3DLUT header: %s", __FUNCTION__, filename.c_str());
        return false;
    }

    if ( !(header.signature[0]=='3'
                && header.signature[1]=='D'
                && header.signature[2]=='L'
                && header.signature[3]=='T') )
    {
        CLog::Log(LOGERROR, "%s: Not a 3DLUT file: %s", __FUNCTION__, filename.c_str());
        return false;
    }

    if ( header.fileVersion != 1
            || header.lutCompressionMethod != 0
            || header.inputColorEncoding != 0
            || header.outputColorEncoding != 0 )
    {
        CLog::Log(LOGERROR, "%s: Unsupported 3DLUT file: %s", __FUNCTION__, filename.c_str());
        return false;
    }

    lutFile.Close();
    return true;
}

bool CColorManager::Load3dLut(const std::string filename, float **CLUT, int *CLUTsize)
{
    struct H3DLUT header;
    CFile lutFile;

    if (!lutFile.Open(filename))
    {
        CLog::Log(LOGERROR, "%s: Could not open 3DLUT file: %s", __FUNCTION__, filename.c_str());
        return false;
    }

    if (lutFile.Read(&header, sizeof(header)) < sizeof(header))
    {
        CLog::Log(LOGERROR, "%s: Could not read 3DLUT header: %s", __FUNCTION__, filename.c_str());
        return false;
    }

    int rSize = 1 << header.inputBitDepth[0];
    int gSize = 1 << header.inputBitDepth[1];
    int bSize = 1 << header.inputBitDepth[2];

    if ( !((rSize == gSize) && (rSize == bSize)) )
    {
        CLog::Log(LOGERROR, "%s: Different channel resolutions unsupported: %s", __FUNCTION__, filename.c_str());
        return false;
    }

    int lutsamples = rSize * gSize * bSize * 3;
    *CLUTsize = rSize; // TODO: assumes cube
    *CLUT = (float*)malloc(lutsamples * sizeof(float));

    lutFile.Seek(header.lutFileOffset, SEEK_SET);

    for (int rIndex=0; rIndex<rSize; rIndex++) {
        for (int gIndex=0; gIndex<gSize; gIndex++) {
            uint16_t input[bSize*3];
            lutFile.Read(input, 3*bSize*sizeof(uint16_t));
            int index = (rIndex + gIndex*rSize)*3;
            for (int bIndex=0; bIndex<bSize; bIndex++) {
                (*CLUT)[index+bIndex*rSize*gSize*3+0] = input[bIndex*3+2]/65535.0;
                (*CLUT)[index+bIndex*rSize*gSize*3+1] = input[bIndex*3+1]/65535.0;
                (*CLUT)[index+bIndex*rSize*gSize*3+2] = input[bIndex*3+0]/65535.0;
            }
        }
    }

    lutFile.Close();

#if 1 // debug 3dLUT greyscale
    for (int y=0; y<rSize; y+=1)
    {
      int index = 3*(y*rSize*rSize + y*rSize + y);
      CLog::Log(LOGDEBUG, "  %d (%d): %d %d %d\n",
          (int)round(y * 255 / (rSize-1.0)), y,
          (int)round(255*(*CLUT)[index+0]),
          (int)round(255*(*CLUT)[index+1]),
          (int)round(255*(*CLUT)[index+2]));
    }
#endif

    return true;
}
