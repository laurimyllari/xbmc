#include "config.h"
#include "utils/log.h"
#include "filesystem/SpecialProtocol.h"

#include <boost/algorithm/clamp.hpp>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "LutLoader.h"

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
      CLog::Log(LOGDEBUG, "got display profile\n");
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
    long fileVersion;          // file format version number (currently "1")
    char programName[32];      // name of the program that created the file
    long long programVersion;  // version number of the program that created the file
    long inputBitDepth[3];     // input bit depth per component (Y,Cb,Cr or R,G,B)
    long inputColorEncoding;   // input color encoding standard
    long outputBitDepth;       // output bit depth for all components (valid values are 8, 16 and 32)
    long outputColorEncoding;  // output color encoding standard
    long parametersFileOffset; // number of bytes between the beginning of the file and array parametersData
    long parametersSize;       // size in bytes of the array parametersData
    long lutFileOffset;        // number of bytes between the beginning of the file and array lutData
    long lutCompressionMethod; // type of compression used if any (0 = none, ...)
    long lutCompressedSize;    // size in bytes of the array lutData inside the file, whether compressed or not
    long lutUncompressedSize;  // true size in bytes of the array lutData when in memory for usage (outside the file)
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

    const std::string profileBase = "special://profile/display/";
    std::string profileName = "rec709.icc";

    if (!loadICC(CSpecialProtocol::TranslatePath(profileBase + profileName), CLUT, CLUTsize))
        return 1;

    return 0;
}
