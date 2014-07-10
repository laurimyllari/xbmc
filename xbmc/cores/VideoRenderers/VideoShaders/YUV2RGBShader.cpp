/*
 *      Copyright (c) 2007 d4rk
 *      Copyright (C) 2007-2013 Team XBMC
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

#include "system.h"
#include "../RenderFlags.h"
#include "YUV2RGBShader.h"
#include "settings/AdvancedSettings.h"
#include "guilib/TransformMatrix.h"
#include "windowing/WindowingFactory.h"
#include "utils/log.h"
#if defined(HAS_GL) || defined(HAS_GLES)
#include "utils/GLUtils.h"
#endif
#include "cores/VideoRenderers/RenderFormats.h"
#if defined(HAVE_LIBLCMS2)
#include "dither.h"
#include "LutLoader.h"
#endif

#include <string>
#include <sstream>

// http://www.martinreddy.net/gfx/faqs/colorconv.faq

//
// Transformation matrixes for different colorspaces.
//
static float yuv_coef_bt601[4][4] = 
{
    { 1.0f,      1.0f,     1.0f,     0.0f },
    { 0.0f,     -0.344f,   1.773f,   0.0f },
    { 1.403f,   -0.714f,   0.0f,     0.0f },
    { 0.0f,      0.0f,     0.0f,     0.0f } 
};

static float yuv_coef_bt709[4][4] =
{
    { 1.0f,      1.0f,     1.0f,     0.0f },
    { 0.0f,     -0.1870f,  1.8556f,  0.0f },
    { 1.5701f,  -0.4664f,  0.0f,     0.0f },
    { 0.0f,      0.0f,     0.0f,     0.0f }
};

static float yuv_coef_ebu[4][4] = 
{
    { 1.0f,      1.0f,     1.0f,     0.0f },
    { 0.0f,     -0.3960f,  2.029f,   0.0f },
    { 1.140f,   -0.581f,   0.0f,     0.0f },
    { 0.0f,      0.0f,     0.0f,     0.0f }
};

static float yuv_coef_smtp240m[4][4] =
{
    { 1.0f,      1.0f,     1.0f,     0.0f },
    { 0.0f,     -0.2253f,  1.8270f,  0.0f },
    { 1.5756f,  -0.5000f,  0.0f,     0.0f },
    { 0.0f,      0.0f,     0.0f,     0.0f }
};

static float** PickYUVConversionMatrix(unsigned flags)
{
  // Pick the matrix.
   
   switch(CONF_FLAGS_YUVCOEF_MASK(flags))
   {
     case CONF_FLAGS_YUVCOEF_240M:
       return (float**)yuv_coef_smtp240m; break;
     case CONF_FLAGS_YUVCOEF_BT709:
       return (float**)yuv_coef_bt709; break;
     case CONF_FLAGS_YUVCOEF_BT601:    
       return (float**)yuv_coef_bt601; break;
     case CONF_FLAGS_YUVCOEF_EBU:
       return (float**)yuv_coef_ebu; break;
   }
   
   return (float**)yuv_coef_bt601;
}

void CalculateYUVMatrix(TransformMatrix &matrix
                        , unsigned int  flags
                        , ERenderFormat format
                        , float         black
                        , float         contrast)
{
  TransformMatrix coef;

  matrix *= TransformMatrix::CreateScaler(contrast, contrast, contrast);
  matrix *= TransformMatrix::CreateTranslation(black, black, black);

  float (*conv)[4] = (float (*)[4])PickYUVConversionMatrix(flags);
  for(int row = 0; row < 3; row++)
    for(int col = 0; col < 4; col++)
      coef.m[row][col] = conv[col][row];
  coef.identity = false;

  // Scale to limited range if so configured, or if 3dLUT is used use
  // limited range internally and convert range later if necessary
  if(g_Windowing.UseLimitedColor() || g_Windowing.Use3dLUT())
  {
    matrix *= TransformMatrix::CreateTranslation(+ 16.0f / 255
                                               , + 16.0f / 255
                                               , + 16.0f / 255);
    matrix *= TransformMatrix::CreateScaler((235 - 16) / 255.0f
                                          , (235 - 16) / 255.0f
                                          , (235 - 16) / 255.0f);
  }

  matrix *= coef;
  matrix *= TransformMatrix::CreateTranslation(0.0, -0.5, -0.5);

  if (!(flags & CONF_FLAGS_YUV_FULLRANGE))
  {
    matrix *= TransformMatrix::CreateScaler(255.0f / (235 - 16)
                                          , 255.0f / (240 - 16)
                                          , 255.0f / (240 - 16));
    matrix *= TransformMatrix::CreateTranslation(- 16.0f / 255
                                               , - 16.0f / 255
                                               , - 16.0f / 255);
  }

  if(format == RENDER_FMT_YUV420P10)
  {
    matrix *= TransformMatrix::CreateScaler(65535.0f / 1023.0f
                                          , 65535.0f / 1023.0f
                                          , 65535.0f / 1023.0f);
  }
}

#if defined(HAS_GL) || HAS_GLES == 2

using namespace Shaders;
using namespace std;

static void CalculateYUVMatrixGL(GLfloat      res[4][4]
                               , unsigned int flags
                               , ERenderFormat format
                               , float        black
                               , float        contrast)
{
  TransformMatrix matrix;
  CalculateYUVMatrix(matrix, flags, format, black, contrast);

  for(int row = 0; row < 3; row++)
    for(int col = 0; col < 4; col++)
      res[col][row] = matrix.m[row][col];

  res[0][3] = 0.0f;
  res[1][3] = 0.0f;
  res[2][3] = 0.0f;
  res[3][3] = 1.0f;
}

//////////////////////////////////////////////////////////////////////
// BaseYUV2RGBGLSLShader - base class for GLSL YUV2RGB shaders
//////////////////////////////////////////////////////////////////////

BaseYUV2RGBGLSLShader::BaseYUV2RGBGLSLShader(bool rect, unsigned flags, ERenderFormat format, bool stretch)
{
  m_width      = 1;
  m_height     = 1;
  m_field      = 0;
  m_flags      = flags;
  m_format     = format;

  m_black      = 0.0f;
  m_contrast   = 1.0f;

  m_stretch = 0.0f;

  // textures
  m_tDitherTex  = 0;
#if USE_3DLUT
  m_tCLUTTex    = 0;
  m_tOutRLUTTex    = 0;
  m_tOutGLUTTex    = 0;
  m_tOutBLUTTex    = 0;
#endif // USE_3DLUT

  // shader attribute handles
  m_hYTex    = -1;
  m_hUTex    = -1;
  m_hVTex    = -1;
  m_hStretch = -1;
  m_hStep    = -1;
  m_hDither      = -1;
  m_hDitherQuant = -1;
  m_hDitherSize  = -1;
#if USE_3DLUT
  m_hCLUT        = -1;
  m_hOutRLUT     = -1;
  m_hOutGLUT     = -1;
  m_hOutBLUT     = -1;
#endif // USE_3DLUT

#ifdef HAS_GL
  if(rect)
    m_defines += "#define XBMC_texture_rectangle 1\n";
  else
    m_defines += "#define XBMC_texture_rectangle 0\n";

  if(g_advancedSettings.m_GLRectangleHack)
    m_defines += "#define XBMC_texture_rectangle_hack 1\n";
  else
    m_defines += "#define XBMC_texture_rectangle_hack 0\n";

  //don't compile in stretch support when it's not needed
  if (stretch)
    m_defines += "#define XBMC_STRETCH 1\n";
  else
    m_defines += "#define XBMC_STRETCH 0\n";

  if (m_format == RENDER_FMT_YUV420P ||
      m_format == RENDER_FMT_YUV420P10 ||
      m_format == RENDER_FMT_YUV420P16)
    m_defines += "#define XBMC_YV12\n";
  else if (m_format == RENDER_FMT_NV12)
    m_defines += "#define XBMC_NV12\n";
  else if (m_format == RENDER_FMT_YUYV422)
    m_defines += "#define XBMC_YUY2\n";
  else if (m_format == RENDER_FMT_UYVY422)
    m_defines += "#define XBMC_UYVY\n";
  else if (RENDER_FMT_VDPAU_420)
    m_defines += "#define XBMC_VDPAU_NV12\n";
  else
    CLog::Log(LOGERROR, "GL: BaseYUV2RGBGLSLShader - unsupported format %d", m_format);

  if(g_Windowing.Use3dLUT())
  {
    CLog::Log(LOGNOTICE, "YUV2RGB: Configuring shader for 3dLUT");
    m_defines += "#define XBMC_USE_3DLUT\n";
    if (!g_Windowing.UseLimitedColor())
    {
      CLog::Log(LOGNOTICE, "YUV2RGB: Configuring shader for full range output");
      m_defines += "#define XBMC_EXPAND_TO_FULLRANGE\n";
    }
  }

  VertexShader()->LoadSource("yuv2rgb_vertex.glsl", m_defines);
#elif HAS_GLES == 2
  m_hVertex = -1;
  m_hYcoord = -1;
  m_hUcoord = -1;
  m_hVcoord = -1;
  m_hProj   = -1;
  m_hModel  = -1;
  m_hAlpha  = -1;
  if (m_format == RENDER_FMT_YUV420P)
    m_defines += "#define XBMC_YV12\n";
  else if (m_format == RENDER_FMT_NV12)
    m_defines += "#define XBMC_NV12\n";
  else
    CLog::Log(LOGERROR, "GL: BaseYUV2RGBGLSLShader - unsupported format %d", m_format);

  VertexShader()->LoadSource("yuv2rgb_vertex_gles.glsl", m_defines);
#endif

  CLog::Log(LOGDEBUG, "GL: BaseYUV2RGBGLSLShader: defines:\n%s", m_defines.c_str());
}

void BaseYUV2RGBGLSLShader::OnCompiledAndLinked()
{
#if USE_3DLUT
  float *CLUT, *outluts;
  int CLUTsize, outlutsize;
#endif // USE_3DLUT

  CheckAndFreeTextures();

#if HAS_GLES == 2
  m_hVertex = glGetAttribLocation(ProgramHandle(),  "m_attrpos");
  m_hYcoord = glGetAttribLocation(ProgramHandle(),  "m_attrcordY");
  m_hUcoord = glGetAttribLocation(ProgramHandle(),  "m_attrcordU");
  m_hVcoord = glGetAttribLocation(ProgramHandle(),  "m_attrcordV");
  m_hProj   = glGetUniformLocation(ProgramHandle(), "m_proj");
  m_hModel  = glGetUniformLocation(ProgramHandle(), "m_model");
  m_hAlpha  = glGetUniformLocation(ProgramHandle(), "m_alpha");
#endif
  m_hYTex    = glGetUniformLocation(ProgramHandle(), "m_sampY");
  m_hUTex    = glGetUniformLocation(ProgramHandle(), "m_sampU");
  m_hVTex    = glGetUniformLocation(ProgramHandle(), "m_sampV");
  m_hMatrix  = glGetUniformLocation(ProgramHandle(), "m_yuvmat");
  m_hStretch = glGetUniformLocation(ProgramHandle(), "m_stretch");
  m_hStep    = glGetUniformLocation(ProgramHandle(), "m_step");
  m_hDither      = glGetUniformLocation(ProgramHandle(), "m_dither");
  m_hDitherQuant = glGetUniformLocation(ProgramHandle(), "m_ditherquant");
  m_hDitherSize  = glGetUniformLocation(ProgramHandle(), "m_dithersize");
#if USE_3DLUT
  m_hCLUT        = glGetUniformLocation(ProgramHandle(), "m_CLUT");
  m_hOutRLUT     = glGetUniformLocation(ProgramHandle(), "m_OutLUTR");
  m_hOutGLUT     = glGetUniformLocation(ProgramHandle(), "m_OutLUTG");
  m_hOutBLUT     = glGetUniformLocation(ProgramHandle(), "m_OutLUTB");
#endif // USE_3DLUT
  VerifyGLState();

  // set up dither texture
  glGenTextures(1, &m_tDitherTex);
  glActiveTexture(GL_TEXTURE3);
  if ( m_tDitherTex <= 0 )
  {
    CLog::Log(LOGERROR, "Error creating dither texture");
    return;
  }
  glBindTexture(GL_TEXTURE_2D, m_tDitherTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R16, dither_size, dither_size, 0, GL_RED, GL_SHORT, dither_matrix);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glActiveTexture(GL_TEXTURE0);
  VerifyGLState();

#if USE_3DLUT
  // load LUTs
  if ( loadLUT(m_flags, &CLUT, &CLUTsize, &outluts, &outlutsize) )
  {
    CLog::Log(LOGERROR, "Error loading the LUT");
    return;
  }

  glGenTextures(1, &m_tCLUTTex);
  glActiveTexture(GL_TEXTURE4);
  if ( m_tCLUTTex <= 0 )
  {
    CLog::Log(LOGERROR, "Error creating 3DLUT texture");
    return;
  }
  glBindTexture(GL_TEXTURE_3D, m_tCLUTTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB32F, CLUTsize, CLUTsize, CLUTsize, 0, GL_RGB, GL_FLOAT, CLUT);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glActiveTexture(GL_TEXTURE0);
  VerifyGLState();

  glGenTextures(1, &m_tOutRLUTTex);
  glActiveTexture(GL_TEXTURE5);
  if ( m_tOutRLUTTex <= 0 )
  {
    CLog::Log(LOGERROR, "Error creating output 1DLUT texture");
    return;
  }
  glBindTexture(GL_TEXTURE_1D, m_tOutRLUTTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glTexImage1D(GL_TEXTURE_1D, 0, GL_RED, outlutsize, 0, GL_RED, GL_FLOAT, outluts+0*outlutsize);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glActiveTexture(GL_TEXTURE0);
  VerifyGLState();

  glGenTextures(1, &m_tOutGLUTTex);
  glActiveTexture(GL_TEXTURE6);
  if ( m_tOutGLUTTex <= 0 )
  {
    CLog::Log(LOGERROR, "Error creating output 1DLUT texture");
    return;
  }
  glBindTexture(GL_TEXTURE_1D, m_tOutGLUTTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glTexImage1D(GL_TEXTURE_1D, 0, GL_RED, outlutsize, 0, GL_RED, GL_FLOAT, outluts+1*outlutsize);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glActiveTexture(GL_TEXTURE0);
  VerifyGLState();

  glGenTextures(1, &m_tOutBLUTTex);
  glActiveTexture(GL_TEXTURE7);
  if ( m_tOutBLUTTex <= 0 )
  {
    CLog::Log(LOGERROR, "Error creating output 1DLUT texture");
    return;
  }
  glBindTexture(GL_TEXTURE_1D, m_tOutBLUTTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glTexImage1D(GL_TEXTURE_1D, 0, GL_RED, outlutsize, 0, GL_RED, GL_FLOAT, outluts+2*outlutsize);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glActiveTexture(GL_TEXTURE0);
  VerifyGLState();

  m_hCLUT = glGetUniformLocation(ProgramHandle(), "m_CLUT");
  m_hOutRLUT = glGetUniformLocation(ProgramHandle(), "m_OutLUTR");
  m_hOutGLUT = glGetUniformLocation(ProgramHandle(), "m_OutLUTG");
  m_hOutBLUT = glGetUniformLocation(ProgramHandle(), "m_OutLUTB");
  VerifyGLState();

  free(CLUT);
  free(outluts);
#endif // USE_3DLUT
}

bool BaseYUV2RGBGLSLShader::OnEnabled()
{
  // set shader attributes once enabled
  glUniform1i(m_hYTex, 0);
  glUniform1i(m_hUTex, 1);
  glUniform1i(m_hVTex, 2);
  glUniform1f(m_hStretch, m_stretch);
  glUniform2f(m_hStep, 1.0 / m_width, 1.0 / m_height);

  GLfloat matrix[4][4];
  CalculateYUVMatrixGL(matrix, m_flags, m_format, m_black, m_contrast);

  glUniformMatrix4fv(m_hMatrix, 1, GL_FALSE, (GLfloat*)matrix);
#if HAS_GLES == 2
  glUniformMatrix4fv(m_hProj,  1, GL_FALSE, m_proj);
  glUniformMatrix4fv(m_hModel, 1, GL_FALSE, m_model);
  glUniform1f(m_hAlpha, m_alpha);
#endif
  VerifyGLState();

  // set texture units
  glUniform1i(m_hDither, 3);
#if USE_3DLUT
  glUniform1i(m_hCLUT, 4);
  glUniform1i(m_hOutRLUT, 5);
  glUniform1i(m_hOutGLUT, 6);
  glUniform1i(m_hOutBLUT, 7);
#endif // USE_3DLUT
  VerifyGLState();

  // bind textures
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, m_tDitherTex);
  glActiveTexture(GL_TEXTURE0);
#if USE_3DLUT
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_3D, m_tCLUTTex);
  glActiveTexture(GL_TEXTURE5);
  glBindTexture(GL_TEXTURE_1D, m_tOutRLUTTex);
  glActiveTexture(GL_TEXTURE6);
  glBindTexture(GL_TEXTURE_1D, m_tOutGLUTTex);
  glActiveTexture(GL_TEXTURE7);
  glBindTexture(GL_TEXTURE_1D, m_tOutBLUTTex);
  glActiveTexture(GL_TEXTURE0);
#endif // USE_3DLUT
  VerifyGLState();

  // dither settings
  glUniform1f(m_hDitherQuant, 255.0); // (1<<depth)-1
  VerifyGLState();
  glUniform2f(m_hDitherSize, dither_size, dither_size);
  VerifyGLState();

  return true;
}

void BaseYUV2RGBGLSLShader::OnDisabled()
{
  glActiveTexture(GL_TEXTURE3);
  glDisable(GL_TEXTURE_2D);
#if USE_3DLUT
  glActiveTexture(GL_TEXTURE4);
  glDisable(GL_TEXTURE_3D);
  glActiveTexture(GL_TEXTURE5);
  glDisable(GL_TEXTURE_1D);
  glActiveTexture(GL_TEXTURE6);
  glDisable(GL_TEXTURE_1D);
  glActiveTexture(GL_TEXTURE7);
  glDisable(GL_TEXTURE_1D);
#endif // USE_3DLUT
  glActiveTexture(GL_TEXTURE0);
  VerifyGLState();
}

void BaseYUV2RGBGLSLShader::Free()
{
  CheckAndFreeTextures();
}

void BaseYUV2RGBGLSLShader::CheckAndFreeTextures()
{
  if (m_tDitherTex)
  {
    glDeleteTextures(1, &m_tDitherTex);
    m_tDitherTex = 0;
  }
#if USE_3DLUT
  if (m_tCLUTTex)
  {
    glDeleteTextures(1, &m_tCLUTTex);
    m_tCLUTTex = 0;
  }
  if (m_tOutRLUTTex)
  {
    glDeleteTextures(1, &m_tOutRLUTTex);
    m_tOutRLUTTex = 0;
  }
  if (m_tOutGLUTTex)
  {
    glDeleteTextures(1, &m_tOutGLUTTex);
    m_tOutGLUTTex = 0;
  }
  if (m_tOutBLUTTex)
  {
    glDeleteTextures(1, &m_tOutBLUTTex);
    m_tOutBLUTTex = 0;
  }
#endif // USE_3DLUT
}

//////////////////////////////////////////////////////////////////////
// BaseYUV2RGBGLSLShader - base class for GLSL YUV2RGB shaders
//////////////////////////////////////////////////////////////////////
#if HAS_GLES != 2	// No ARB Shader when using GLES2.0
BaseYUV2RGBARBShader::BaseYUV2RGBARBShader(unsigned flags, ERenderFormat format)
{
  m_width         = 1;
  m_height        = 1;
  m_field         = 0;
  m_flags         = flags;
  m_format        = format;

  // shader attribute handles
  m_hYTex  = -1;
  m_hUTex  = -1;
  m_hVTex  = -1;
}
#endif

//////////////////////////////////////////////////////////////////////
// Base3dLUTGLSLShader - base class for GLSL 3dLUT shaders
//////////////////////////////////////////////////////////////////////

#if defined(HAVE_LIBLCMS2) && defined(HAS_GL) // no GLES2 support yet
Base3dLUTGLSLShader::Base3dLUTGLSLShader(bool rect, unsigned flags, ERenderFormat format, bool stretch)
{
  m_width      = 1;
  m_height     = 1;
  m_field      = 0;
  m_flags      = flags;
  m_format     = format;

  m_black      = 0.0f;
  m_contrast   = 1.0f;

  m_stretch = 0.0f;

  // textures

  // shader attribute handles
  m_hYTex        = -1;
  m_hUTex        = -1;
  m_hVTex        = -1;
  m_hStretch     = -1;
  m_hStep        = -1;

  if(rect)
    m_defines += "#define XBMC_texture_rectangle 1\n";
  else
    m_defines += "#define XBMC_texture_rectangle 0\n";

  if(g_advancedSettings.m_GLRectangleHack)
    m_defines += "#define XBMC_texture_rectangle_hack 1\n";
  else
    m_defines += "#define XBMC_texture_rectangle_hack 0\n";

  //don't compile in stretch support when it's not needed
  if (stretch)
    m_defines += "#define XBMC_STRETCH 1\n";
  else
    m_defines += "#define XBMC_STRETCH 0\n";

  if (m_format == RENDER_FMT_YUV420P ||
      m_format == RENDER_FMT_YUV420P10 ||
      m_format == RENDER_FMT_YUV420P16)
    m_defines += "#define XBMC_YV12\n";
  else if (m_format == RENDER_FMT_NV12)
    m_defines += "#define XBMC_NV12\n";
  else if (m_format == RENDER_FMT_YUYV422)
    m_defines += "#define XBMC_YUY2\n";
  else if (m_format == RENDER_FMT_UYVY422)
    m_defines += "#define XBMC_UYVY\n";
  else if (RENDER_FMT_VDPAU_420)
    m_defines += "#define XBMC_VDPAU_NV12\n";
  else
    CLog::Log(LOGERROR, "GL: Base3dLUTGLSLShader - unsupported format %d", m_format);

  VertexShader()->LoadSource("yuv2rgb_vertex.glsl", m_defines);

  CLog::Log(LOGDEBUG, "GL: Base3dLUTGLSLShader: defines:\n%s", m_defines.c_str());
}

void Base3dLUTGLSLShader::OnCompiledAndLinked()
{
  CheckAndFreeTextures();

  m_hYTex        = glGetUniformLocation(ProgramHandle(), "m_sampY");
  m_hUTex        = glGetUniformLocation(ProgramHandle(), "m_sampU");
  m_hVTex        = glGetUniformLocation(ProgramHandle(), "m_sampV");
  m_hStretch     = glGetUniformLocation(ProgramHandle(), "m_stretch");
  m_hStep        = glGetUniformLocation(ProgramHandle(), "m_step");
  VerifyGLState();


}

bool Base3dLUTGLSLShader::OnEnabled()
{
  // set shader attributes once enabled
  glUniform1i(m_hYTex, 0);
  glUniform1i(m_hUTex, 1);
  glUniform1i(m_hVTex, 2);
  glUniform1f(m_hStretch, m_stretch);
  glUniform2f(m_hStep, 1.0 / m_width, 1.0 / m_height);

  // set texture units
  VerifyGLState();

  // bind textures
  VerifyGLState();

  return true;
}

void Base3dLUTGLSLShader::OnDisabled()
{
  glActiveTexture(GL_TEXTURE0);
  VerifyGLState();
}

void Base3dLUTGLSLShader::Free()
{
  CheckAndFreeTextures();
}

void Base3dLUTGLSLShader::CheckAndFreeTextures()
{
}

//////////////////////////////////////////////////////////////////////
// Progressive3dLUTShader - 3dLUT with no deinterlacing
// Use for weave deinterlacing / progressive
//////////////////////////////////////////////////////////////////////

Progressive3dLUTShader::Progressive3dLUTShader(bool rect, unsigned flags, ERenderFormat format, bool stretch)
  : Base3dLUTGLSLShader(rect, flags, format, stretch)
{
  PixelShader()->LoadSource("lut_basic.glsl", m_defines);
}


#endif // HAVE_LIBLCMS2 && HAS_GL

//////////////////////////////////////////////////////////////////////
// YUV2RGBProgressiveShader - YUV2RGB with no deinterlacing
// Use for weave deinterlacing / progressive
//////////////////////////////////////////////////////////////////////

YUV2RGBProgressiveShader::YUV2RGBProgressiveShader(bool rect, unsigned flags, ERenderFormat format, bool stretch)
  : BaseYUV2RGBGLSLShader(rect, flags, format, stretch)
{
#ifdef HAS_GL
  PixelShader()->LoadSource("yuv2rgb_basic.glsl", m_defines);
#elif HAS_GLES == 2
  PixelShader()->LoadSource("yuv2rgb_basic_gles.glsl", m_defines);
#endif
}


//////////////////////////////////////////////////////////////////////
// YUV2RGBBobShader - YUV2RGB with Bob deinterlacing
//////////////////////////////////////////////////////////////////////

YUV2RGBBobShader::YUV2RGBBobShader(bool rect, unsigned flags, ERenderFormat format)
  : BaseYUV2RGBGLSLShader(rect, flags, format, false)
{
  m_hStepX = -1;
  m_hStepY = -1;
  m_hField = -1;
#ifdef HAS_GL
  PixelShader()->LoadSource("yuv2rgb_bob.glsl", m_defines);
#elif HAS_GLES == 2
  PixelShader()->LoadSource("yuv2rgb_bob_gles.glsl", m_defines);
#endif
}

void YUV2RGBBobShader::OnCompiledAndLinked()
{
  BaseYUV2RGBGLSLShader::OnCompiledAndLinked();
  m_hStepX = glGetUniformLocation(ProgramHandle(), "m_stepX");
  m_hStepY = glGetUniformLocation(ProgramHandle(), "m_stepY");
  m_hField = glGetUniformLocation(ProgramHandle(), "m_field");
  VerifyGLState();
}

bool YUV2RGBBobShader::OnEnabled()
{
  if(!BaseYUV2RGBGLSLShader::OnEnabled())
    return false;

  glUniform1i(m_hField, m_field);
  glUniform1f(m_hStepX, 1.0f / (float)m_width);
  glUniform1f(m_hStepY, 1.0f / (float)m_height);
  VerifyGLState();
  return true;
}

//////////////////////////////////////////////////////////////////////
// YUV2RGBProgressiveShaderARB - YUV2RGB with no deinterlacing
//////////////////////////////////////////////////////////////////////
#if HAS_GLES != 2	// No ARB Shader when using GLES2.0
YUV2RGBProgressiveShaderARB::YUV2RGBProgressiveShaderARB(bool rect, unsigned flags, ERenderFormat format)
  : BaseYUV2RGBARBShader(flags, format)
{
  m_black      = 0.0f;
  m_contrast   = 1.0f;

  string shaderfile;

  if (m_format == RENDER_FMT_YUYV422)
  {
    if(rect)
      shaderfile = "yuv2rgb_basic_rect_YUY2.arb";
    else
      shaderfile = "yuv2rgb_basic_2d_YUY2.arb";
  }
  else if (m_format == RENDER_FMT_UYVY422)
  {
    if(rect)
      shaderfile = "yuv2rgb_basic_rect_UYVY.arb";
    else
      shaderfile = "yuv2rgb_basic_2d_UYVY.arb";
  }
  else
  {
    if(rect)
      shaderfile = "yuv2rgb_basic_rect.arb";
    else
      shaderfile = "yuv2rgb_basic_2d.arb";
  }

  CLog::Log(LOGDEBUG, "GL: YUV2RGBProgressiveShaderARB: loading %s", shaderfile.c_str());

  PixelShader()->LoadSource(shaderfile);
}

void YUV2RGBProgressiveShaderARB::OnCompiledAndLinked()
{
}

bool YUV2RGBProgressiveShaderARB::OnEnabled()
{
  GLfloat matrix[4][4];
  CalculateYUVMatrixGL(matrix, m_flags, m_format, m_black, m_contrast);

  for(int i=0;i<4;i++)
    glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, i
                               , matrix[0][i]
                               , matrix[1][i]
                               , matrix[2][i]
                               , matrix[3][i]);

  glProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 4,
                               1.0 / m_width, 1.0 / m_height,
                               m_width, m_height);
  return true;
}
#endif
#endif // HAS_GL
