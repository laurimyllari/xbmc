
// FIXME: make into a ColorManagement class

enum CMS_MODE
{
  CMS_MODE_OFF,
  CMS_MODE_3DLUT,
  CMS_MODE_PROFILE,
  CMS_MODE_COUNT
};

int loadLUT(unsigned flags,
    float **CLUT,
    int *CLUTsize);
