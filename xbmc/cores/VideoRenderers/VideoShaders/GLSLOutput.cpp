/*
 *      Copyright (c) 2007 d4rk
 *      Copyright (C) 2007-2013 Team XBMC
 *      Copyright (C) 2015 Lauri Mylläri
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
#include "GLSLOutput.h"
#include "windowing/WindowingFactory.h"
#include "utils/log.h"
#if defined(HAS_GL) || defined(HAS_GLES)
#include "utils/GLUtils.h"
#endif

#include "dither.h"
#include "LutLoader.h"

using namespace Shaders;

GLSLOutput::GLSLOutput(int texunit, unsigned videoflags)
{
  // set member variable initial values
  m_1stTexUnit = texunit;
  m_uDither = m_1stTexUnit+0;
  m_uCLUT = m_1stTexUnit+1;
  m_flags = videoflags;

  //   textures
  m_tDitherTex  = 0;
  m_tCLUTTex  = 0;

  //   shader attribute handles
  m_hDither      = -1;
  m_hDitherQuant = -1;
  m_hDitherSize  = -1;
  m_hCLUT        = -1;

  m_dither = g_Windowing.UseDithering();
  m_ditherDepth = g_Windowing.DitherDepth();
  m_fullRange = !g_Windowing.UseLimitedColor();
  m_3DLUT = g_Windowing.Use3DLUT();
}

std::string GLSLOutput::GetDefines()
{
  std::string defines = "#define XBMC_OUTPUT 1\n";
  if (m_dither) defines += "#define XBMC_DITHER 1\n";
  if (m_fullRange) defines += "#define XBMC_FULLRANGE 1\n";
  if (m_3DLUT) defines += "#define XBMC_3DLUT 1\n";
  return defines;
}

void GLSLOutput::OnCompiledAndLinked(GLuint programHandle)
{
  float *CLUT;
  int CLUTsize;

  FreeTextures();

  // get uniform locations
  //   dithering
  if (m_dither) {
    m_hDither      = glGetUniformLocation(programHandle, "m_dither");
    m_hDitherQuant = glGetUniformLocation(programHandle, "m_ditherquant");
    m_hDitherSize  = glGetUniformLocation(programHandle, "m_dithersize");
  }
  //   3DLUT
  if (m_3DLUT) {
    m_hCLUT        = glGetUniformLocation(programHandle, "m_CLUT");
  }

  if (m_dither) {
    // TODO: create a dither pattern

    // create a dither texture
    glGenTextures(1, &m_tDitherTex);
    if ( m_tDitherTex <= 0 )
    {
      CLog::Log(LOGERROR, "Error creating dither texture");
      return;
    }
    // bind and set texture parameters
    glActiveTexture(GL_TEXTURE0 + m_uDither);
    glBindTexture(GL_TEXTURE_2D, m_tDitherTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // load dither texture data
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16, dither_size, dither_size, 0, GL_RED, GL_UNSIGNED_SHORT, dither_matrix);
  }

  if (m_3DLUT) {
    // load 3DLUT
    // TODO: move to a helper class, provide video primaries for LUT selection
    if ( loadLUT(m_flags, &CLUT, &CLUTsize) )
    {
      CLog::Log(LOGERROR, "Error loading the LUT");
      return;
    }

    // create 3DLUT texture
    glGenTextures(1, &m_tCLUTTex);
    glActiveTexture(GL_TEXTURE0 + m_uCLUT);
    if ( m_tCLUTTex <= 0 )
    {
      CLog::Log(LOGERROR, "Error creating 3DLUT texture");
      return;
    }

    // bind and set 3DLUT texture parameters
    glBindTexture(GL_TEXTURE_3D, m_tCLUTTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    // load 3DLUT data
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB32F, CLUTsize, CLUTsize, CLUTsize, 0, GL_RGB, GL_FLOAT, CLUT);
    free(CLUT);
  }

  glActiveTexture(GL_TEXTURE0);

  VerifyGLState();
}

bool GLSLOutput::OnEnabled()
{

  if (m_dither) {
    // set texture units
    glUniform1i(m_hDither, m_uDither);
    VerifyGLState();

    // bind textures
    glActiveTexture(GL_TEXTURE0 + m_uDither);
    glBindTexture(GL_TEXTURE_2D, m_tDitherTex);
    glActiveTexture(GL_TEXTURE0);
    VerifyGLState();

    // dither settings
    glUniform1f(m_hDitherQuant, (1<<m_ditherDepth)-1.0);
    VerifyGLState();
    glUniform2f(m_hDitherSize, dither_size, dither_size);
    VerifyGLState();
  }

  if (m_3DLUT) {
    // set texture units
    glUniform1i(m_hCLUT, m_uCLUT);
    VerifyGLState();

    // bind textures
    glActiveTexture(GL_TEXTURE0 + m_uCLUT);
    glBindTexture(GL_TEXTURE_3D, m_tCLUTTex);
    glActiveTexture(GL_TEXTURE0);
    VerifyGLState();
  }

  VerifyGLState();
  return true;
}

void GLSLOutput::OnDisabled()
{
  // disable textures
  if (m_dither) {
    glActiveTexture(GL_TEXTURE0 + m_uDither);
    glDisable(GL_TEXTURE_2D);
  }
  if (m_3DLUT) {
    glActiveTexture(GL_TEXTURE0 + m_uCLUT);
    glDisable(GL_TEXTURE_3D);
  }
  glActiveTexture(GL_TEXTURE0);
  VerifyGLState();
}

void GLSLOutput::Free()
{
  FreeTextures();
}

void GLSLOutput::FreeTextures()
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
}

