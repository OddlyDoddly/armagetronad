/*

*************************************************************************

ArmageTron -- Just another Tron Lightcycle Game in 3D.
Copyright (C) 2000  Manuel Moos (manuel@moosnet.de)

**************************************************************************

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
  
***************************************************************************

*/

#include "aa_config.h"
#include "tString.h"
#include "tCommandLine.h"

// sg_modernRenderer is defined here and declared extern in rRender.h.
bool sg_modernRenderer = false;
// sg_vulkanRenderer selects the experimental Vulkan backend (HAVE_VULKAN only).
bool sg_vulkanRenderer = false;

tString sg_rendererName = tString("Legacy GL");
tString sg_rendererDebugInfo;
bool    sg_gfxDebug = false;

// --gfx-dbg: show renderer backend + device info in the corner of the menu.
class gGfxDebugCommandLineAnalyzer: public tCommandLineAnalyzer
{
private:
    bool DoAnalyze( tCommandLineParser & parser, int pass ) override
    {
        if (pass > 0)
            return false;

        if ( parser.GetSwitch( "--gfx-dbg", nullptr ) )
        {
            sg_gfxDebug = true;
            return true;
        }
        return false;
    }

    void DoHelp( std::ostream & s ) override
    {
        s << "--gfx-dbg                    : show the active graphics renderer and\n"
          << "                               device debug info in the menu corner\n";
    }
};
static gGfxDebugCommandLineAnalyzer s_gfxDebugAnalyzer;

#ifndef DEDICATED

#define DONTDOIT
#include "rRender.h"
#include "rGL.h"
#include "tMemManager.h"
#include "tError.h"
#include "tConfiguration.h"
#ifdef HAVE_GLEW
#include "gl/gl_backend.h"
#endif
#ifdef HAVE_VULKAN
#include "vk/vk_renderer.h"
#include "rScreen.h"
#endif

static tSettingItem<bool> sg_modernRendererConf("MODERN_RENDERER", sg_modernRenderer);
#ifdef HAVE_VULKAN
static tSettingItem<bool> sg_vulkanRendererConf("VULKAN_RENDERER", sg_vulkanRenderer);
#endif

class glRenderer: public rRenderer{
    GLenum lastPrimitive;
    bool   forceglEnd;

    GLenum lastMatrix;

    void BeginPrimitive(GLenum p, bool forceEnd = false){
        //  glBegin(p);
        //  return;

        if (lastPrimitive != p && lastPrimitive != GL_FALSE)
        {
            glEnd();
            sr_CheckGLError();
        }

        if ( lastPrimitive != p )
        {
            sr_CheckGLError();
            glBegin(p);
        }

        lastPrimitive = p;

        forceglEnd = forceEnd;
    }

    void MatrixMode(GLenum mm){
        //    if (lastMatrix != mm)
        {
            glMatrixMode(mm);
            lastMatrix = mm;
        }
    }

public:
    glRenderer():lastPrimitive(GL_FALSE), forceglEnd(false), lastMatrix(GL_FALSE){
        ChangeFlags(0xffffffff,0);
    };

    virtual ~glRenderer(){};

    virtual void Vertex(REAL x, REAL y){
        glVertex2f(x,y);
    };

    virtual void Vertex(REAL x, REAL y, REAL z){
        glVertex3f(x,y,z);
    }

    virtual void Vertex3(REAL *x){
        glVertex3fv(x);
    }

    virtual void Vertex(REAL x, REAL y, REAL z, REAL w){
        glVertex4f(x,y,z,w);
    }

    virtual void TexCoord(REAL u, REAL v){
        glTexCoord2f(u,v);
    }

    virtual void TexCoord(REAL u, REAL v, REAL w){
        glTexCoord3f(u,v,w);
    }

    virtual void TexCoord(REAL u, REAL v, REAL w, REAL t){
        glTexCoord4f(u,v,w,t);
    };

    virtual void TexVertex(REAL x, REAL y, REAL z,
                           REAL u, REAL v){
        glTexCoord2f(u,v);
        glVertex3f(x,y,z);
    }


    virtual void Color(REAL r, REAL g, REAL b){
        glColor3f(r,g,b);
    };

    virtual void Color(REAL r, REAL g, REAL b,REAL a){
        glColor4f(r,g,b,a);
    };


    virtual void End(bool force=true){
        //    glEnd();
        //    return;

        if ((forceglEnd || force ) && lastPrimitive!=GL_FALSE)
        {
            forceglEnd = false;
            glEnd();
            sr_CheckGLError();
            lastPrimitive = GL_FALSE;
        }
    }

    virtual void BeginLines(){
        BeginPrimitive(GL_LINES);
    };

    virtual void BeginTriangles(){
        BeginPrimitive(GL_TRIANGLES);
    }

    virtual void BeginQuads(){
        BeginPrimitive(GL_QUADS);
    }

    virtual void BeginLineStrip(){
        BeginPrimitive(GL_LINE_STRIP, true);
    };

    virtual void BeginLineLoop(){
        BeginPrimitive(GL_LINE_LOOP, true);
    };

    virtual void BeginTriangleStrip(){
        BeginPrimitive(GL_TRIANGLE_STRIP, true);
    };

    virtual void BeginQuadStrip(){
        BeginPrimitive(GL_QUAD_STRIP, true);
    };

    virtual void BeginTriangleFan(){
        BeginPrimitive(GL_TRIANGLE_FAN, true);
    };

    virtual void Line(REAL x1, REAL y1, REAL z1,
                      REAL x2, REAL y2, REAL z2){
        BeginPrimitive(GL_LINES);
        glVertex3f(x1,y1,z1);
        glVertex3f(x2,y2,z2);
        End();
    }




    virtual void ProjMatrix(){
        End(true);
        MatrixMode(GL_PROJECTION);
    };

    virtual void ModelMatrix(){
        End(true);
        MatrixMode(GL_MODELVIEW);
    };

    virtual void TexMatrix(){
        End(true);
        MatrixMode(GL_TEXTURE);
    };

    virtual void PushMatrix(){
        glPushMatrix();
    };

    virtual void PopMatrix(){
        End(true);
        glPopMatrix();
    };

    virtual void MultMatrix(REAL mdata[4][4]){
        End(true);
        tASSERT(sizeof(REAL) == sizeof(GLfloat));
        REAL * mdat=&mdata[0][0];
        glMultMatrixf(reinterpret_cast<GLfloat *>(mdat));
    };

    virtual void IdentityMatrix(){
        End(true);
        glLoadIdentity();
    };

    virtual void ScaleMatrix(REAL f){
        End(true);
        glScalef(f,f,f);
    };

    virtual void ScaleMatrix(REAL f1, REAL f2, REAL f3){
        End(true);
        glScalef(f1,f2,f3);
    };

    virtual void TranslateMatrix(REAL x1, REAL x2, REAL x3){
        End(true);
        glTranslatef(x1,x2,x3);
    }



    virtual void ReallySetFlag(flag f,bool c){
        GLenum fl = GL_DEPTH_TEST;
        switch (f)
        {
        case ALPHA_BLEND:
            fl = GL_BLEND;
            break;
        case DEPTH_TEST:
            fl = GL_DEPTH_TEST;
            break;
        default:
            break;
        }

        if (c)
            glEnable(fl);
        else
            glDisable(fl);
    };

};

void sr_glRendererInit(){
#ifdef HAVE_VULKAN
    if (sg_vulkanRenderer && vk::VulkanRenderer::IsSupported()) {
        vk::VulkanRenderer * vr = new vk::VulkanRenderer();
        if (vr->init(sr_screen)) {
            sg_rendererName = tString("Vulkan");
            if (sg_gfxDebug)
                sg_rendererDebugInfo = vr->debugInfo();
            return;
        }
        // init failed: the Vulkan-flagged window cannot host a GL context, so
        // this is effectively fatal, but tear down cleanly and fall through.
        delete vr;
        renderer = nullptr;
        sg_vulkanRenderer = false;
    }
#endif
#ifdef HAVE_GLEW
    if (sg_modernRenderer && gl::ModernGLRenderer::IsSupported()) {
        new gl::ModernGLRenderer();
        sg_rendererName = tString("Modern GL");
        return;
    }
#endif
    sg_rendererName = tString("Legacy GL");
    tNEW(glRenderer);
}

#endif
