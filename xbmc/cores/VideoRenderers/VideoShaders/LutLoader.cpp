#include "config.h"
#include "utils/log.h"
#include "filesystem/File.h"
#include "settings/Settings.h"

#include <boost/algorithm/clamp.hpp>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "LutLoader.h"

using namespace XFILE;

#if defined(HAVE_LCMS2)
#include "lcms2.h"
#include "lcms2_plugin.h"

namespace ba = boost::algorithm;

// FIXME: rename to source profile; allow choosing wp, primaries and gamma
cmsHPROFILE gammaprofile(cmsCIEXYZ blackpoint, float brightness, float contrast)
{
  float gamma = 2.4;
  double bkipow = brightness * pow(blackpoint.Y, 1.0/gamma);
  double wtipow = contrast * 1.0;
  double lift = bkipow / (wtipow - bkipow);
  double gain = pow(wtipow - bkipow, gamma);

  const int tablesize = 1024;
  cmsFloat32Number gammatable[tablesize];
  for (int i=0; i<tablesize; i++)
  {
    gammatable[i] = gain * pow(((double) i)/(tablesize-1) + lift, gamma);
  }

  cmsToneCurve*  Gamma = cmsBuildTabulatedToneCurveFloat(0,
      tablesize,
      gammatable);
  cmsToneCurve*  Gamma3[3];
  cmsHPROFILE hProfile;
  cmsCIExyY whitepoint = { 0.3127, 0.3290, 1.0 };
  cmsCIExyYTRIPLE primaries = {
      0.640, 0.330, 1.0,
      0.300, 0.600, 1.0,
      0.150, 0.060, 1.0 };

  Gamma3[0] = Gamma3[1] = Gamma3[2] = Gamma;
  hProfile = cmsCreateRGBProfile(&whitepoint,
      &primaries,
      Gamma3);
  cmsFreeToneCurve(Gamma);
  return hProfile;
}

bool loadICC(const std::string filename, float **CLUT, int *CLUTsize)
{
    cmsHPROFILE hProfile;
    cmsHTRANSFORM hTransform;
    int lutsamples;

    // FIXME - device link filename based on colorspace in flags
    hProfile = cmsOpenProfileFromFile(
        filename.c_str(),
        "r");
    if (!hProfile)
    {
      CLog::Log(LOGERROR, "ICC profile not found\n");
      return false;
    }

    if (cmsGetDeviceClass(hProfile) == cmsSigDisplayClass)
    {
      CLog::Log(LOGNOTICE, "got display profile: %s\n", filename.c_str());
      // check black point
      cmsCIEXYZ blackpoint = { 0, 0, 0};
      if (cmsDetectBlackPoint(&blackpoint, hProfile, INTENT_PERCEPTUAL, 0))
      {
        CLog::Log(LOGDEBUG, "black point: %f\n", blackpoint.Y);
      }

      // create input profile (monitor to simulate)
      cmsHPROFILE inputprofile = gammaprofile(blackpoint, 1.0, 1.0);

      // create the transform
      hTransform = cmsCreateTransform(inputprofile, TYPE_RGB_FLT,
          hProfile, TYPE_RGB_FLT,
          INTENT_PERCEPTUAL, 0);
      cmsCloseProfile(inputprofile);
    }
    else if (cmsGetDeviceClass(hProfile) == cmsSigLinkClass)
    {
      hTransform = cmsCreateMultiprofileTransform(&hProfile,
          1,
          TYPE_RGB_FLT,
          TYPE_RGB_FLT,
          INTENT_PERCEPTUAL,
          0);
    }
    else
    {
      CLog::Log(LOGERROR, "unsupported profile type\n");
      return false;
    }

#define LUT_RESOLUTION 65

    lutsamples = LUT_RESOLUTION * LUT_RESOLUTION * LUT_RESOLUTION * 3;
    *CLUTsize = LUT_RESOLUTION;
    *CLUT = (float*)malloc(lutsamples * sizeof(float));

    cmsFloat32Number input[3*LUT_RESOLUTION];
    cmsFloat32Number output[3*LUT_RESOLUTION];

// #define videoToPC(x) ( ba::clamp((((x)*255)-16)/219,0,1) )
// #define PCToVideo(x) ( (((x)*219)+16)/255 )
#define videoToPC(x) ( x )
#define PCToVideo(x) ( x )
    for (int bIndex=0; bIndex<LUT_RESOLUTION; bIndex++) {
      for (int gIndex=0; gIndex<LUT_RESOLUTION; gIndex++) {
        for (int rIndex=0; rIndex<LUT_RESOLUTION; rIndex++) {
          input[rIndex*3+0] = videoToPC(rIndex / (LUT_RESOLUTION-1.0));
          input[rIndex*3+1] = videoToPC(gIndex / (LUT_RESOLUTION-1.0));
          input[rIndex*3+2] = videoToPC(bIndex / (LUT_RESOLUTION-1.0));
        }
        int index = (bIndex*LUT_RESOLUTION*LUT_RESOLUTION + gIndex*LUT_RESOLUTION)*3;
        cmsDoTransform(hTransform, input, output, LUT_RESOLUTION);
        for (int i=0; i<LUT_RESOLUTION*3; i++) {
          (*CLUT)[index+i] = PCToVideo(output[i]);
        }
      }
    }

#if 1 // debug 3dLUT greyscale
    for (int y=0; y<LUT_RESOLUTION; y+=1)
    {
      int index = 3*(y*LUT_RESOLUTION*LUT_RESOLUTION + y*LUT_RESOLUTION + y);
      CLog::Log(LOGDEBUG, "  %d (%d): %d %d %d\n",
          (int)round(y * 255 / (LUT_RESOLUTION-1.0)), y,
          (int)round(255*(*CLUT)[index+0]),
          (int)round(255*(*CLUT)[index+1]),
          (int)round(255*(*CLUT)[index+2]));
    }
#endif

    cmsCloseProfile(hProfile);
    return true;
}

#else

bool loadICC(const std::string filename, float **CLUT, int *CLUTsize)
{
    CLog::Log(LOGERROR, "No ICC profile support (requires lcms2)\n");
    return false;
}
#endif

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

bool load3DLUT(const std::string filename, float **CLUT, int *CLUTsize)
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

    CLog::Log(LOGNOTICE, "%s: 3DLUT file looks ok so far: %s", __FUNCTION__, filename.c_str());

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

    return true; // FIXME: false until implemented
}

int loadLUT(unsigned flags,
    float **CLUT,
    int *CLUTsize)
{

    // TODO: profile selection logic
    //
    // - select colorspace based on video flags (see CONF_FLAGS_COLPRI_MASK)
    // - allow user to override colorspace per video?
    // - allow user to select gamma?
    // - look for matching 3dlut
    // - look for matching icc device link
    // - look for a display profile
    // - fall back to an identity matrix and a warning message?

    // TODO: move icc file handling to a separate function

    int cmsMode = CSettings::Get().GetInt("videoscreen.colormanagement");

    if (cmsMode == CMS_MODE_3DLUT)
    {
      if (load3DLUT(CSettings::Get().GetString("videoscreen.cms3dlut"), CLUT, CLUTsize))
          return 0;
    }
    else if (cmsMode == CMS_MODE_PROFILE)
    {
      if (loadICC(CSettings::Get().GetString("videoscreen.displayprofile"), CLUT, CLUTsize))
        return 0;
    }

    return 1;
}
