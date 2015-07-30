#pragma once

class CColorManager
{
public:
  /*!
   \brief Access the global color management system
   \return the global instance
   */
  static CColorManager& Get();

  /*!
   \brief Get a 3D LUT for video color correction
   \param primaries video primaries
   \param token pointer to a color manager configuration token
   \param clutsize pointer to CLUT resolution
   \param clutdata pointer to CLUT data
   \return true on success, false otherwise
   */
  // GetVideo3dLut


  // CheckConfiguration

private:
  // private constructor, use the Get() method to access an instance
  CColorManager();
  virtual ~CColorManager();

  /*! \brief Check .3dlut file validity
   \param filename full path and filename
   \return true if the file can be loaded, false otherwise
   */
  // Probe3dLut

  /*! \brief Load a .3dlut file
   \param filename full path and filename
   \return ??
   */
  // Load3dLut


#ifdef HAVE_LCMS2
  // ProbeIccDisplayProfile

  // ProbeIccDeviceLink (?)


  /* \brief Load an ICC display profile
   \param filename full path and filename
   \return display profile (cmsHPROFILE)
   */
  // LoadIccDisplayProfile

  /* \brief Load an ICC device link
   \param filename full path and filename
   \return device link (cmsHTRANSFORM)
   */
  // LoadIccDeviceLink (?)


  // create a gamma curve


  // create a source profile


  /* \brief Create 3D LUT
   Samples a cmsHTRANSFORM object to create a 3D LUT of specified resolution
   \param transform cmsHTRANSFORM object to sample
   \param resolution size of the 3D LUT to create
   \param clut pointer to LUT data
   */
  // Create3dLut


#endif // HAVE_LCMS2

  // current configuration:
  // video primaries
  // clut size
  // clut data
  // token
  // (compare the following to system settings to see if configuration is still valid)
  // cms mode
  // 3dlut file
  // icc profile
  // display parameters (gamma, input/output offset, primaries, whitepoint?, intent)
}


