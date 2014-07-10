#ifndef __YUV2RGB_SHADERS_H__
#define __YUV2RGB_SHADERS_H__

/*
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

#include "guilib/TransformMatrix.h"
#include "cores/VideoRenderers/RenderFormats.h"

void CalculateYUVMatrix(TransformMatrix &matrix
                        , unsigned int  flags
                        , ERenderFormat format
                        , float         black
                        , float         contrast);

#if defined(HAS_GL) || HAS_GLES == 2

#ifndef __GNUC__
#pragma warning( push )
#pragma warning( disable : 4250 )
#endif

#include "guilib/Shader.h"

namespace Shaders {

  class BaseYUV2RGBShader
    : virtual public CShaderProgram
  {
  public:
    virtual ~BaseYUV2RGBShader()      {};
    virtual void SetField(int field)  {};
    virtual void SetWidth(int width)  {};
    virtual void SetHeight(int width) {};

    virtual void SetBlack(float black)          {};
    virtual void SetContrast(float contrast)    {};
    virtual void SetNonLinStretch(float stretch){};
#if HAS_GLES == 2
    virtual GLint GetVertexLoc() { return 0; };
    virtual GLint GetYcoordLoc() { return 0; };
    virtual GLint GetUcoordLoc() { return 0; };
    virtual GLint GetVcoordLoc() { return 0; };

    virtual void SetMatrices(GLfloat *p, GLfloat *m) {};
    virtual void SetAlpha(GLfloat alpha)             {};
#endif
  };


  class BaseYUV2RGBGLSLShader 
    : public BaseYUV2RGBShader
    , public CGLSLShaderProgram
  {
  public:
    BaseYUV2RGBGLSLShader(bool rect, unsigned flags, ERenderFormat format, bool stretch);
   ~BaseYUV2RGBGLSLShader() {}
    virtual void SetField(int field) { m_field  = field; }
    virtual void SetWidth(int w)     { m_width  = w; }
    virtual void SetHeight(int h)    { m_height = h; }

    virtual void SetBlack(float black)           { m_black    = black; }
    virtual void SetContrast(float contrast)     { m_contrast = contrast; }
    virtual void SetNonLinStretch(float stretch) { m_stretch = stretch; }
#if HAS_GLES == 2
    virtual GLint GetVertexLoc() { return m_hVertex; }
    virtual GLint GetYcoordLoc() { return m_hYcoord; }
    virtual GLint GetUcoordLoc() { return m_hUcoord; }
    virtual GLint GetVcoordLoc() { return m_hVcoord; }

    virtual void SetMatrices(GLfloat *p, GLfloat *m) { m_proj = p; m_model = m; }
    virtual void SetAlpha(GLfloat alpha)             { m_alpha = alpha; }
#endif

  protected:
    void OnCompiledAndLinked();
    bool OnEnabled();
    void OnDisabled();
    void Free();
    void CheckAndFreeTextures();

    unsigned m_flags;
    ERenderFormat m_format;
    int   m_width;
    int   m_height;
    int   m_field;

    float m_black;
    float m_contrast;
    float m_stretch;

    string m_defines;

    // textures
    GLuint m_tDitherTex;

    // shader attribute handles
    GLint m_hYTex;
    GLint m_hUTex;
    GLint m_hVTex;
    GLint m_hMatrix;
    GLint m_hStretch;
    GLint m_hStep;
    GLint m_hDither;
    GLint m_hDitherQuant;
    GLint m_hDitherSize;
#if HAS_GLES == 2
    GLint m_hVertex;
    GLint m_hYcoord;
    GLint m_hUcoord;
    GLint m_hVcoord;
    GLint m_hProj;
    GLint m_hModel;
    GLint m_hAlpha;

    GLfloat *m_proj;
    GLfloat *m_model;
    GLfloat  m_alpha;
#endif
  };

#if HAS_GLES != 2       // No ARB Shader when using GLES2.0
  class BaseYUV2RGBARBShader 
    : public BaseYUV2RGBShader
    , public CARBShaderProgram
  {
  public:
    BaseYUV2RGBARBShader(unsigned flags, ERenderFormat format);
   ~BaseYUV2RGBARBShader() {}
    virtual void SetField(int field) { m_field  = field; }
    virtual void SetWidth(int w)     { m_width  = w; }
    virtual void SetHeight(int h)    { m_height = h; }

    virtual void SetBlack(float black)       { m_black    = black; }
    virtual void SetContrast(float contrast) { m_contrast = contrast; }

  protected:
    unsigned m_flags;
    ERenderFormat m_format;
    int   m_width;
    int   m_height;
    int   m_field;

    float m_black;
    float m_contrast;

    // shader attribute handles
    GLint m_hYTex;
    GLint m_hUTex;
    GLint m_hVTex;
  };

  class YUV2RGBProgressiveShaderARB : public BaseYUV2RGBARBShader
  {
  public:
    YUV2RGBProgressiveShaderARB(bool rect=false, unsigned flags=0, ERenderFormat format=RENDER_FMT_NONE);
    void OnCompiledAndLinked();
    bool OnEnabled();
  };
#endif

#if defined(HAVE_LIBLCMS2) && defined(HAS_GL) // no GLES2 support yet
  class Base3dLUTGLSLShader
    : public BaseYUV2RGBShader
    , public CGLSLShaderProgram
  {
  public:
    Base3dLUTGLSLShader(bool rect, unsigned flags, ERenderFormat format, bool stretch);
   ~Base3dLUTGLSLShader() {}
    virtual void SetField(int field) { m_field  = field; }
    virtual void SetWidth(int w)     { m_width  = w; }
    virtual void SetHeight(int h)    { m_height = h; }

    virtual void SetBlack(float black)           { m_black    = black; }
    virtual void SetContrast(float contrast)     { m_contrast = contrast; }
    virtual void SetNonLinStretch(float stretch) { m_stretch = stretch; }
  protected:
    void OnCompiledAndLinked();
    bool OnEnabled();
    void OnDisabled();
    void Free();
    void CheckAndFreeTextures();

    unsigned m_flags;
    ERenderFormat m_format;
    int   m_width;
    int   m_height;
    int   m_field;

    float m_black;
    float m_contrast;
    float m_stretch;

    string m_defines;

    // textures
    GLuint m_tCLUTTex;
    GLuint m_tOutRLUTTex;
    GLuint m_tOutGLUTTex;
    GLuint m_tOutBLUTTex;

    // shader attribute handles
    GLint m_hYTex;
    GLint m_hUTex;
    GLint m_hVTex;
    GLint m_hCLUT;
    GLint m_hOutRLUT;
    GLint m_hOutGLUT;
    GLint m_hOutBLUT;
    GLint m_hStretch;
    GLint m_hStep;
  };

  class Progressive3dLUTShader : public Base3dLUTGLSLShader
  {
  public:
    Progressive3dLUTShader(bool rect=false, unsigned flags=0, ERenderFormat format=RENDER_FMT_NONE, bool stretch = false);
  };

#if 0 // not implemented yet
  class Bob3dLUTShader : public Base3dLUTGLSLShader
  {
  public:
    Bob3dLUTShader(bool rect=false, unsigned flags=0, ERenderFormat format=RENDER_FMT_NONE);
    void OnCompiledAndLinked();
    bool OnEnabled();

    GLint m_hStepX;
    GLint m_hStepY;
    GLint m_hField;
  };
#endif // 0
#endif // HAVE_LIBLCMS2 && HAS_GL

  class YUV2RGBProgressiveShader : public BaseYUV2RGBGLSLShader
  {
  public:
    YUV2RGBProgressiveShader(bool rect=false, unsigned flags=0, ERenderFormat format=RENDER_FMT_NONE, bool stretch = false);
  };

  class YUV2RGBBobShader : public BaseYUV2RGBGLSLShader
  {
  public:
    YUV2RGBBobShader(bool rect=false, unsigned flags=0, ERenderFormat format=RENDER_FMT_NONE);
    void OnCompiledAndLinked();
    bool OnEnabled();

    GLint m_hStepX;
    GLint m_hStepY;
    GLint m_hField;
  };

} // end namespace

#ifndef __GNUC__
#pragma warning( pop )
#endif
#endif

#endif //__YUV2RGB_SHADERS_H__
