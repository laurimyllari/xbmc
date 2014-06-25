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
#include "settings/AdvancedSettings.h"
#include "windowing/WindowingFactory.h"
#include "utils/log.h"
#if defined(HAS_GL) || defined(HAS_GLES)
#include "utils/GLUtils.h"
#endif
#include "cores/VideoRenderers/RenderFormats.h"
#include "LUTShader.h"
#if defined(HAVE_LIBLCMS2)
#include "lutloader.h"
#endif
#include "dither.h"

#include <string>
#include <sstream>

#if 0 // keeping this for possible future profile linking support
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
#endif

#if defined(HAS_GL) || HAS_GLES == 2

using namespace Shaders;
using namespace std;

//////////////////////////////////////////////////////////////////////
// BaseLUTGLSLShader - base class for GLSL LUT shaders
//////////////////////////////////////////////////////////////////////

BaseLUTGLSLShader::BaseLUTGLSLShader(bool rect, unsigned flags, ERenderFormat format, bool stretch)
{
  m_width      = 1;
  m_height     = 1;
  m_field      = 0;
  m_flags      = flags;
  m_format     = format;

  m_black      = 0.0f;
  m_contrast   = 1.0f;

  m_stretch = 0.0f;

  // shader attribute handles
  m_hYTex    = -1;
  m_hUTex    = -1;
  m_hVTex    = -1;
  m_hStretch = -1;
  m_hStep    = -1;
  m_hCLUT    = -1;
  m_hOutLUTR = -1;
  m_hOutLUTG = -1;
  m_hOutLUTB = -1;

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
    CLog::Log(LOGERROR, "GL: BaseLUTGLSLShader - unsupported format %d", m_format);

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
    CLog::Log(LOGERROR, "GL: BaseLUTGLSLShader - unsupported format %d", m_format);

  VertexShader()->LoadSource("yuv2rgb_vertex_gles.glsl", m_defines);
#endif

  CLog::Log(LOGDEBUG, "GL: BaseLUTGLSLShader: defines:\n%s", m_defines.c_str());
}

void BaseLUTGLSLShader::OnCompiledAndLinked()
{
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
  m_hStretch = glGetUniformLocation(ProgramHandle(), "m_stretch");
  m_hStep    = glGetUniformLocation(ProgramHandle(), "m_step");
  VerifyGLState();
}

bool BaseLUTGLSLShader::OnEnabled()
{
  // set shader attributes once enabled
  glUniform1i(m_hYTex, 0);
  glUniform1i(m_hUTex, 1);
  glUniform1i(m_hVTex, 2);
  glUniform1f(m_hStretch, m_stretch);
  glUniform2f(m_hStep, 1.0 / m_width, 1.0 / m_height);

#if HAS_GLES == 2
  glUniformMatrix4fv(m_hProj,  1, GL_FALSE, m_proj);
  glUniformMatrix4fv(m_hModel, 1, GL_FALSE, m_model);
  glUniform1f(m_hAlpha, m_alpha);
#endif
  VerifyGLState();
  return true;
}

//////////////////////////////////////////////////////////////////////
// LUTProgressiveShader - LUT with no deinterlacing
// Use for weave deinterlacing / progressive
//////////////////////////////////////////////////////////////////////

LUTProgressiveShader::LUTProgressiveShader(bool rect, unsigned flags, ERenderFormat format, bool stretch)
  : BaseLUTGLSLShader(rect, flags, format, stretch)
{
#ifdef HAS_GL
  PixelShader()->LoadSource("lut_basic.glsl", m_defines);
#elif HAS_GLES == 2
#error 3dlut not yet implemented for GLES2
  PixelShader()->LoadSource("lut_basic_gles.glsl", m_defines);
#endif
  m_tDitherTex = 0;
}

void LUTProgressiveShader::OnCompiledAndLinked()
{
  float *CLUT, *outluts;
  int CLUTsize, outlutsize;
  CheckAndFreeTextures();

  // load LUTs
  if ( loadLUT(m_flags, &CLUT, &CLUTsize, &outluts, &outlutsize) )
  {
    CLog::Log(LOGERROR, "GL: LUTProgressiveShader: Error loading the LUT");
    return;
  }

  glGenTextures(1, &m_tDitherTex);
  glActiveTexture(GL_TEXTURE3);
  if ( m_tDitherTex <= 0 )
  {
    CLog::Log(LOGERROR, "GL: LUTProgressiveShader: Error creating dither texture");
    return;
  }
  glBindTexture(GL_TEXTURE_2D, m_tDitherTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_R16, dither_size, dither_size, 0, GL_RED,
GL_SHORT, dither_matrix);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  glActiveTexture(GL_TEXTURE0);
  VerifyGLState();

  glGenTextures(1, &m_tCLUTTex);
  glActiveTexture(GL_TEXTURE4);
  if ( m_tCLUTTex <= 0 )
  {
    CLog::Log(LOGERROR, "GL: LUTProgressiveShader: Error creating 3DLUT texture");
    return;
  }
  glBindTexture(GL_TEXTURE_3D, m_tCLUTTex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB32F, CLUTsize, CLUTsize, CLUTsize, 0, GL_RGB,
GL_FLOAT, CLUT);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glActiveTexture(GL_TEXTURE0);
  VerifyGLState();

  glGenTextures(1, &m_tOutLUTR);
  glActiveTexture(GL_TEXTURE5);
  if ( m_tOutLUTR <= 0 )
  {
    CLog::Log(LOGERROR, "GL: LUTProgressiveShader: Error creating output 1DLUT texture");
    return;
  }
  glBindTexture(GL_TEXTURE_1D, m_tOutLUTR);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glTexImage1D(GL_TEXTURE_1D, 0, GL_RED, outlutsize, 0, GL_RED,
GL_FLOAT, outluts+0*outlutsize);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glActiveTexture(GL_TEXTURE0);
  VerifyGLState();

  glGenTextures(1, &m_tOutLUTG);
  glActiveTexture(GL_TEXTURE6);
  if ( m_tOutLUTG <= 0 )
  {
    CLog::Log(LOGERROR, "GL: LUTProgressiveShader: Error creating output 1DLUT texture");
    return;
  }
  glBindTexture(GL_TEXTURE_1D, m_tOutLUTG);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glTexImage1D(GL_TEXTURE_1D, 0, GL_RED, outlutsize, 0, GL_RED,
GL_FLOAT, outluts+1*outlutsize);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glActiveTexture(GL_TEXTURE0);
  VerifyGLState();

  glGenTextures(1, &m_tOutLUTB);
  glActiveTexture(GL_TEXTURE7);
  if ( m_tOutLUTB <= 0 )
  {
    CLog::Log(LOGERROR, "GL: LUTProgressiveShader: Error creating output 1DLUT texture");
    return;
  }
  glBindTexture(GL_TEXTURE_1D, m_tOutLUTB);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
  glTexImage1D(GL_TEXTURE_1D, 0, GL_RED, outlutsize, 0, GL_RED,
GL_FLOAT, outluts+2*outlutsize);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glActiveTexture(GL_TEXTURE0);
  VerifyGLState();

  m_hCLUT = glGetUniformLocation(ProgramHandle(), "m_CLUT");
  m_hOutLUTR = glGetUniformLocation(ProgramHandle(), "m_OutLUTR");
  m_hOutLUTG = glGetUniformLocation(ProgramHandle(), "m_OutLUTG");
  m_hOutLUTB = glGetUniformLocation(ProgramHandle(), "m_OutLUTB");
  m_hDither = glGetUniformLocation(ProgramHandle(), "m_dither");
  m_hDitherQuant = glGetUniformLocation(ProgramHandle(), "m_ditherquant");
  m_hDitherSize = glGetUniformLocation(ProgramHandle(), "m_dithersize");
  VerifyGLState();

  free(CLUT);
  free(outluts);

  BaseLUTGLSLShader::OnCompiledAndLinked();
}

bool LUTProgressiveShader::OnEnabled()
{
  if (!BaseLUTGLSLShader::OnEnabled())
    return false;

  // set texture units
  glUniform1i(m_hDither, 3);
  glUniform1i(m_hCLUT, 4);
  glUniform1i(m_hOutLUTR, 5);
  glUniform1i(m_hOutLUTG, 6);
  glUniform1i(m_hOutLUTB, 7);
  VerifyGLState();

  // bind textures
  glActiveTexture(GL_TEXTURE3);
  glBindTexture(GL_TEXTURE_2D, m_tDitherTex);
  glActiveTexture(GL_TEXTURE4);
  glBindTexture(GL_TEXTURE_3D, m_tCLUTTex);
  glActiveTexture(GL_TEXTURE5);
  glBindTexture(GL_TEXTURE_1D, m_tOutLUTR);
  glActiveTexture(GL_TEXTURE6);
  glBindTexture(GL_TEXTURE_1D, m_tOutLUTG);
  glActiveTexture(GL_TEXTURE7);
  glBindTexture(GL_TEXTURE_1D, m_tOutLUTB);
  glActiveTexture(GL_TEXTURE0);
  VerifyGLState();

  // dither settings
  glUniform1f(m_hDitherQuant, 255.0); // (1<<depth)-1
  VerifyGLState();
  glUniform2f(m_hDitherSize, dither_size, dither_size);
  VerifyGLState();
  return true;
}

void LUTProgressiveShader::Free()
{
  CheckAndFreeTextures();
  BaseLUTGLSLShader::Free();
}

void LUTProgressiveShader::CheckAndFreeTextures()
{
  if (m_tDitherTex)
  {
    glDeleteTextures(1, &m_tDitherTex);
    m_tDitherTex = 0;
  }
  if (m_tCLUTTex)
  {
    glDeleteTextures(1, &m_tCLUTTex);
    m_tCLUTTex = 0;
  }
  if (m_tOutLUTR)
  {
    glDeleteTextures(1, &m_tOutLUTR);
    m_tOutLUTR = 0;
  }
  if (m_tOutLUTG)
  {
    glDeleteTextures(1, &m_tOutLUTG);
    m_tOutLUTG = 0;
  }
  if (m_tOutLUTB)
  {
    glDeleteTextures(1, &m_tOutLUTB);
    m_tOutLUTB = 0;
  }
}

//////////////////////////////////////////////////////////////////////
// LUTBobShader - LUT with Bob deinterlacing
//////////////////////////////////////////////////////////////////////

LUTBobShader::LUTBobShader(bool rect, unsigned flags, ERenderFormat format)
  : BaseLUTGLSLShader(rect, flags, format, false)
{
  m_hStepX = -1;
  m_hStepY = -1;
  m_hField = -1;
#ifdef HAS_GL
  PixelShader()->LoadSource("lut_bob.glsl", m_defines);
#elif HAS_GLES == 2
  PixelShader()->LoadSource("lut_bob_gles.glsl", m_defines);
#endif
}

void LUTBobShader::OnCompiledAndLinked()
{
  BaseLUTGLSLShader::OnCompiledAndLinked();
  m_hStepX = glGetUniformLocation(ProgramHandle(), "m_stepX");
  m_hStepY = glGetUniformLocation(ProgramHandle(), "m_stepY");
  m_hField = glGetUniformLocation(ProgramHandle(), "m_field");
  VerifyGLState();
}

bool LUTBobShader::OnEnabled()
{
  if(!BaseLUTGLSLShader::OnEnabled())
    return false;

  glUniform1i(m_hField, m_field);
  glUniform1f(m_hStepX, 1.0f / (float)m_width);
  glUniform1f(m_hStepY, 1.0f / (float)m_height);
  VerifyGLState();
  return true;
}

#endif // HAS_GL
