#include <boost/algorithm/clamp.hpp>
#include <math.h>
#include <string>

#include "config.h"
#include "ColorManager.h"
#include "filesystem/File.h"
#include "settings/Settings.h"
#include "utils/log.h"

using namespace XFILE;
namespace ba = boost::algorithm;

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

bool CColorManager::GetVideo3dLut(int primaries, int *cmsToken, int *clutSize, uint16_t **clutData)
{
  switch (CSettings::Get().GetInt("videoscreen.colormanagement"))
  {
  case CMS_MODE_3DLUT:
    CLog::Log(LOGDEBUG, "ColorManager: CMS_MODE_3DLUT\n");
    cur3dlutFile = CSettings::Get().GetString("videoscreen.cms3dlut");
    if (!Load3dLut(cur3dlutFile, clutData, clutSize))
      return false;
    curCmsMode = CMS_MODE_3DLUT;
    break;

  case CMS_MODE_PROFILE:
    CLog::Log(LOGDEBUG, "ColorManager: CMS_MODE_PROFILE\n");
#if defined(HAVE_LCMS2)
    {
      bool changed = false;
      // check if display profile is not loaded, or has changed
      if (curIccProfile != CSettings::Get().GetString("videoscreen.displayprofile"))
      {
        changed = true;
        // free old profile if there is one
        if (m_hProfile)
          cmsCloseProfile(m_hProfile);
        // load profile
        m_hProfile = LoadIccDisplayProfile(CSettings::Get().GetString("videoscreen.displayprofile"));
        if (!m_hProfile)
          return false;
        // detect blackpoint
        if (cmsDetectBlackPoint(&m_blackPoint, m_hProfile, INTENT_PERCEPTUAL, 0))
        {
          CLog::Log(LOGDEBUG, "black point: %f\n", m_blackPoint.Y);
        }
        curIccProfile = CSettings::Get().GetString("videoscreen.displayprofile");
      }
      // create gamma curve
      cmsToneCurve* gammaCurve;
      // TODO: gamma paremeters
      gammaCurve =
        CreateToneCurve(CMS_TRC_INPUT_OFFSET, 2.2, m_blackPoint);

      // create source profile
      // TODO: primaries and whitepoint selection
      cmsHPROFILE sourceProfile =
        CreateSourceProfile(CMS_PRIMARIES_BT709, gammaCurve, 0);

      // link profiles
      // TODO: intent selection, switch output to 16 bits?
      cmsHTRANSFORM deviceLink =
        cmsCreateTransform(sourceProfile, TYPE_RGB_FLT,
            m_hProfile, TYPE_RGB_FLT,
            INTENT_ABSOLUTE_COLORIMETRIC, 0);

      // sample the transformation
      *clutSize = 64;
      Create3dLut(deviceLink, clutData, clutSize);

      // free gamma curve, source profile and transformation
      cmsDeleteTransform(deviceLink);
      cmsCloseProfile(sourceProfile);
      cmsFreeToneCurve(gammaCurve);
    }

    curCmsMode = CMS_MODE_PROFILE;
    break;
#else   //defined(HAVE_LCMS2)
    return false;
#endif  //defined(HAVE_LCMS2)

  case CMS_MODE_OFF:
    CLog::Log(LOGDEBUG, "ColorManager: CMS_MODE_OFF\n");
    return false;
  default:
    CLog::Log(LOGDEBUG, "ColorManager: unknown CMS mode\n");
    return false;
  }

  // set current state
  curVideoPrimaries = primaries;
  curClutSize = *clutSize;
  *cmsToken = ++curCmsToken;
  return true;
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

bool CColorManager::Load3dLut(const std::string filename, uint16_t **CLUT, int *CLUTsize)
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
    *CLUT = (uint16_t*)malloc(lutsamples * sizeof(uint16_t));

    lutFile.Seek(header.lutFileOffset, SEEK_SET);

    for (int rIndex=0; rIndex<rSize; rIndex++) {
        for (int gIndex=0; gIndex<gSize; gIndex++) {
            uint16_t input[bSize*3];
            lutFile.Read(input, 3*bSize*sizeof(uint16_t));
            int index = (rIndex + gIndex*rSize)*3;
            for (int bIndex=0; bIndex<bSize; bIndex++) {
                (*CLUT)[index+bIndex*rSize*gSize*3+0] = input[bIndex*3+2];
                (*CLUT)[index+bIndex*rSize*gSize*3+1] = input[bIndex*3+1];
                (*CLUT)[index+bIndex*rSize*gSize*3+2] = input[bIndex*3+0];
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
          (int)round((*CLUT)[index+0]/256),
          (int)round((*CLUT)[index+1]/256),
          (int)round((*CLUT)[index+2]/256));
    }
#endif

    return true;
}



#if defined(HAVE_LCMS2)
// ICC profile support

cmsHPROFILE CColorManager::LoadIccDisplayProfile(const std::string filename)
{
    cmsHPROFILE hProfile;

    hProfile = cmsOpenProfileFromFile(filename.c_str(), "r");
    if (!hProfile)
    {
      CLog::Log(LOGERROR, "ICC profile not found\n");
    }
    return hProfile;
}


cmsToneCurve* CColorManager::CreateToneCurve(CMS_TRC_TYPE gammaType, float gammaValue, cmsCIEXYZ blackPoint)
{
  const int tableSize = 1024;
  cmsFloat32Number gammaTable[tableSize];

  switch (gammaType)
  {
  case CMS_TRC_INPUT_OFFSET:
    // calculate gamma to match effective gamma provided, then fall through to bt.1886
    {
      double effectiveGamma = gammaValue;
      double gammaLow = effectiveGamma;  // low limit for infinite contrast ratio
      double gammaHigh = 3.2;            // high limit for 2.4 gamma on 200:1 contrast ratio
      double gammaGuess = 0.0;
#define TARGET(gamma) (pow(0.5, (gamma)))
#define GAIN(bkpt, gamma) (pow(1-pow((bkpt), 1/(gamma)), (gamma)))
#define LIFT(bkpt, gamma) (pow((bkpt), 1/(gamma)) / (1-pow((bkpt), 1/(gamma))))
#define HALFPT(bkpt, gamma) (GAIN(bkpt, gamma)*pow(0.5+LIFT(bkpt, gamma), gamma))
      for (int i=0; i<3; i++)
      {
        // calculate 50% output for gammaLow and gammaHigh, compare to target 50% output
        gammaGuess = gammaLow + (gammaHigh-gammaLow)
            * ((HALFPT(blackPoint.Y, gammaLow)-TARGET(effectiveGamma))
                / (HALFPT(blackPoint.Y, gammaLow)-HALFPT(blackPoint.Y, gammaHigh)));
        if (HALFPT(blackPoint.Y, gammaGuess) < TARGET(effectiveGamma))
        {
          // guess is too high
          // move low limit half way to guess
          gammaLow = gammaLow + (gammaGuess-gammaLow)/2;
          // set high limit to guess
          gammaHigh = gammaGuess;
        }
        else
        {
          // guess is too low
          // set low limit to guess
          gammaLow = gammaGuess;
          // move high limit half way to guess
          gammaHigh = gammaHigh + (gammaGuess-gammaLow)/2;
        }
      }
      gammaValue = gammaGuess;
      CLog::Log(LOGINFO, "calculated technical gamma %0.3f (50%% target %0.4f, output %0.4f)\n",
        gammaValue,
        TARGET(effectiveGamma),
        HALFPT(blackPoint.Y, gammaValue));
#undef TARGET
#undef GAIN
#undef LIFT
#undef HALFPT
    }
    // fall through to bt.1886 with calculated technical gamma
  case CMS_TRC_BT1886:
    {
      double bkipow = pow(blackPoint.Y, 1.0/gammaValue);
      double wtipow = 1.0;
      double lift = bkipow / (wtipow - bkipow);
      double gain = pow(wtipow - bkipow, gammaValue);
      for (int i=0; i<tableSize; i++)
      {
        gammaTable[i] = gain * pow(((double) i)/(tableSize-1) + lift, gammaValue);
      }
    }
    break;
  case CMS_TRC_OUTPUT_OFFSET:

  case CMS_TRC_ABSOLUTE:

  default:
    CLog::Log(LOGERROR, "gamma type not implemented yet\n");
  }

  cmsToneCurve* result = cmsBuildTabulatedToneCurveFloat(0,
      tableSize,
      gammaTable);
  return result;
}


cmsHPROFILE CColorManager::CreateSourceProfile(CMS_PRIMARIES primaries, cmsToneCurve *gamma, int whitepoint)
{
  cmsToneCurve*  Gamma3[3];
  cmsHPROFILE hProfile;
  cmsCIExyY whiteCoords = { 0.3127, 0.3290, 1.0 };
  cmsCIExyYTRIPLE primaryCoords = {
      0.640, 0.330, 1.0,
      0.300, 0.600, 1.0,
      0.150, 0.060, 1.0 };

  Gamma3[0] = Gamma3[1] = Gamma3[2] = gamma;
  hProfile = cmsCreateRGBProfile(&whiteCoords,
      &primaryCoords,
      Gamma3);
  return hProfile;
}


bool CColorManager::Create3dLut(cmsHTRANSFORM transform, uint16_t **clutData, int *clutSize)
{
    const int lutResolution = *clutSize;
    int lutsamples = lutResolution * lutResolution * lutResolution * 3;
    *clutData = (uint16_t*)malloc(lutsamples * sizeof(uint16_t));

    cmsFloat32Number input[3*lutResolution];
    cmsFloat32Number output[3*lutResolution];

#define videoToPC(x) ( ba::clamp((((x)*255)-16)/219,0,1) )
#define PCToVideo(x) ( (((x)*219)+16)/255 )
// #define videoToPC(x) ( x )
// #define PCToVideo(x) ( x )
    for (int bIndex=0; bIndex<lutResolution; bIndex++) {
      for (int gIndex=0; gIndex<lutResolution; gIndex++) {
        for (int rIndex=0; rIndex<lutResolution; rIndex++) {
          input[rIndex*3+0] = videoToPC(rIndex / (lutResolution-1.0));
          input[rIndex*3+1] = videoToPC(gIndex / (lutResolution-1.0));
          input[rIndex*3+2] = videoToPC(bIndex / (lutResolution-1.0));
        }
        int index = (bIndex*lutResolution*lutResolution + gIndex*lutResolution)*3;
        cmsDoTransform(transform, input, output, lutResolution);
        for (int i=0; i<lutResolution*3; i++) {
          (*clutData)[index+i] = PCToVideo(output[i]) * 65535;
        }
      }
    }

#if 1 // debug 3dLUT greyscale
    for (int y=0; y<lutResolution; y+=1)
    {
      int index = 3*(y*lutResolution*lutResolution + y*lutResolution + y);
      CLog::Log(LOGDEBUG, "  %d (%d): %d %d %d\n",
          (int)round(y * 255 / (lutResolution-1.0)), y,
          (int)round((*clutData)[index+0]/256),
          (int)round((*clutData)[index+1]/256),
          (int)round((*clutData)[index+2]/256));
    }
#endif

}


#endif //defined(HAVE_LCMS2)
