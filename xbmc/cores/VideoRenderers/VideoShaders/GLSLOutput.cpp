/*
 *      Copyright (C) 2015 Lauri Mylläri
 *      http://kodi.tv
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

#include "utils/log.h"

#include "GLSLOutput.h"
#include "dither.h"

using namespace Shaders;

GLSLOutput::GLSLOutput()
{
  // TODO: set member variable initial values
  m_1stTexUnit = 4;
  m_uDither = m_1stTexUnit+0;

  //   textures
  m_tDitherTex  = 0;

  //   shader attribute handles
  m_hDither      = -1;
  m_hDitherQuant = -1;
  m_hDitherSize  = -1;

  // TODO: set defines for shader
  // can I figure out dither etc settings here? gamma encode? 3dLUT?
  m_dither = true; // hardcode dithering for now
  m_fullRange = true; // hardcode fullrange for now
}

std::string GLSLOutput::GetDefines()
{
  std::string defines = "#define XBMC_OUTPUT 1\n";
  if (m_dither) defines += "#define XBMC_DITHER 1\n";
  if (m_fullRange) defines += "#define XBMC_FULLRANGE 1\n";
  return defines;
}

void GLSLOutput::OnCompiledAndLinked(GLuint programHandle)
{
  FreeTextures();

  // TODO: get uniform locations
  //   dithering
  if (m_dither) {
    m_hDither      = glGetUniformLocation(programHandle, "m_dither");
    m_hDitherQuant = glGetUniformLocation(programHandle, "m_ditherquant");
    m_hDitherSize  = glGetUniformLocation(programHandle, "m_dithersize");
  }

  if (m_dither) {
    // TODO: create a dither pattern
    // TODO: create a dither texture
    glGenTextures(1, &m_tDitherTex);
    if ( m_tDitherTex <= 0 )
    {
      CLog::Log(LOGERROR, "Error creating dither texture");
      return;
    }
    // TODO: bind and set texture parameters
    glActiveTexture(GL_TEXTURE0 + m_uDither);
    glBindTexture(GL_TEXTURE_2D, m_tDitherTex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    // TODO: load dither texture data
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16, dither_size, dither_size, 0, GL_RED, GL_SHORT, dither_matrix);
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
    glUniform1f(m_hDitherQuant, 255.0); // (1<<depth)-1
    VerifyGLState();
    glUniform2f(m_hDitherSize, dither_size, dither_size);
    VerifyGLState();
  }

  VerifyGLState();
  return true;
}

void GLSLOutput::OnDisabled()
{
  // TODO: disable textures
  glActiveTexture(GL_TEXTURE0 + m_uDither);
  glDisable(GL_TEXTURE_2D);
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
}

