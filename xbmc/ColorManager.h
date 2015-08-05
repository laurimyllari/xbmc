#pragma once

enum CMS_MODE
{
  CMS_MODE_OFF,
  CMS_MODE_3DLUT,
  CMS_MODE_PROFILE,
  CMS_MODE_COUNT
};

class CColorManager
{
public:
  /*!
   \brief Access the global color management system
   \return the global instance
   */
  static CColorManager& Get();

  /*!
   \brief Check if user has requested color management
   \return true on enabled, false otherwise
   */
  bool IsEnabled();

  /*!
   \brief Get a 3D LUT for video color correction
   \param primaries video primaries (see CONF_FLAGS_COLPRI)
   \param cmsToken pointer to a color manager configuration token
   \param clutSize pointer to CLUT resolution
   \param clutData pointer to CLUT data (caller to free memory afterwards)
   \return true on success, false otherwise
   */
  bool GetVideo3dLut(int primaries, int *cmsToken, int *clutSize, uint16_t **clutData);

  /*!
   \brief Check if a 3D LUT is still valid
   \param cmsToken pointer to a color manager configuration token
   \return true on valid, false if 3D LUT should be reloaded
   */
  bool CheckConfiguration(int cmsToken);

private:
  // private constructor, use the Get() method to access an instance
  CColorManager();
  virtual ~CColorManager();

  /*! \brief Check .3dlut file validity
   \param filename full path and filename
   \return true if the file can be loaded, false otherwise
   */
  bool Probe3dLut(const std::string filename);

  /*! \brief Load a .3dlut file
   \param filename full path and filename
   \param clutSize pointer to CLUT resolution
   \param clutData pointer to CLUT data
   \return true on success, false otherwise
   */
  bool Load3dLut(const std::string filename, uint16_t **clutData, int *clutSize);


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
  int curVideoPrimaries;
  int curClutSize;
  int curCmsToken;
  // (compare the following to system settings to see if configuration is still valid)
  int curCmsMode;
  std::string cur3dlutFile;
  std::string curIccProfile;
  // display parameters (gamma, input/output offset, primaries, whitepoint?, intent)

 
};


