//
// OpenVRML
//
// Copyright (C) 1998-2000  Chris Morley
// 
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// 
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
// 

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef _WIN32
#include <winconfig.h>
#endif

#include "ViewerOpenGL.h"

#include <GL/gl.h>
#include <GL/glu.h>
#include <stdio.h>		// sprintf
#include <math.h>

#include <vrml97/MathUtils.h>
#include <vrml97/System.h>
#include <vrml97/VrmlScene.h>
#include <vrml97/VrmlNodeNavigationInfo.h>
#include <vrml97/VrmlBSphere.h>
#include <vrml97/VrmlFrustum.h>

#include "OpenGLEvent.h"

// Put geometry into display lists.
// If geometry is not in display lists, performance will suffer,
// especially for non-convex geometries. You probably shouldn't
// change these unless you know what you are doing.

#define USE_GEOMETRY_DISPLAY_LISTS 1

// Textures are now done using OGL1.1 bindTexture API rather than
// display lists when this flag is set. Don't define this if you
// are still at OpenGL 1.0 (or get a newer OpenGL).

#define USE_TEXTURE_DISPLAY_LISTS 1


static void
matrix_to_glmatrix(double M[4][4], float GLM[16])
{
  GLM[0]  = M[0][0];
  GLM[1]  = M[1][0];
  GLM[2]  = M[2][0];
  GLM[3]  = M[3][0];
  GLM[4]  = M[0][1];
  GLM[5]  = M[1][1];
  GLM[6]  = M[2][1];
  GLM[7]  = M[3][1];
  GLM[8]  = M[0][2];
  GLM[9]  = M[1][2];
  GLM[10] = M[2][2];
  GLM[11] = M[3][2];
  GLM[12] = M[0][3];
  GLM[13] = M[1][3];
  GLM[14] = M[2][3];
  GLM[15] = M[3][3];
}


//  Construct a viewer for the specified scene. I'm not happy with the
//  mutual dependencies between VrmlScene/VrmlNodes and Viewers...
//  Maybe a static callback function pointer should be passed in rather
//  than a class object pointer. Currently, the scene is used to access
//  the VrmlScene::render() method. Also, the static VrmlScene::update
//  is called from the idle function. A way to pass mouse/keyboard sensor 
//  events back to the scene is also needed.

ViewerOpenGL::ViewerOpenGL(VrmlScene *scene) : Viewer(scene)
{
  d_GLinitialized = false;
  d_blend = true;
  d_lit = true;
  d_texture = true;
  d_wireframe = false;

  // Don't make any GL calls here since the window probably doesn't exist.
  d_nObjects = 0;
  d_nestedObjects = 0;

  d_nSensitive = 0;
  d_overSensitive = 0;
  d_activeSensitive = 0;
  d_selectMode = false;
  d_selectZ = 0.0;

  d_background[0] = d_background[1] = d_background[2] = 0.0;
  d_winWidth = 1;
  d_winHeight = 1;
  for (int i=0; i<MAX_LIGHTS; ++i)
    d_lightInfo[i].lightType = LIGHT_UNUSED;

  d_tess = 0;

  d_scale = 1.0;
  d_translatex = d_translatey = d_translatez = 0.0;
  //d_rotationChanged = true;
  d_rotating = false;
  d_scaling = false;
  d_translating = false;
  trackball(d_curquat, 0.0, 0.0, 0.0, 0.0);
  d_position[0] = d_position[1] = d_position[2] = 0.0;
  d_target[0] = d_target[1] = d_target[2] = 0.0;

  d_orientation[0] = 0;
  d_orientation[1] = 1;
  d_orientation[2] = 0;
  d_orientation[3] = 0;

  d_reportFPS = false;
  d_renderTime = 1.0;
  d_renderTime1 = 1.0;

  d_drawBSpheres = false;
  d_cull = true;

}

ViewerOpenGL::~ViewerOpenGL()
{
#if GLU_VERSION_1_2
  if (d_tess) gluDeleteTess( d_tess );
#endif
}

void ViewerOpenGL::initialize()
{
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);

#if USE_STENCIL_SHAPE
  glEnable(GL_STENCIL_TEST);
  glStencilFunc(GL_ALWAYS, 1, 1);
  glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
#endif

  glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, GL_TRUE);

  // should only trigger this after a non-uniform scale is seen...
  // on my system this is <5% speedup though.
  glEnable(GL_NORMALIZE);

  // blending is only enabled if d_blend and a non-zero transparency value is
  // passed in
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  d_GLinitialized = true;
}

// Call this after each frame for debugging...
static void checkErrors(char *s)
{
  GLenum glerr;
  while ((glerr = glGetError()) != GL_NO_ERROR) {
#ifdef macintosh
    if (glerr != 1285) // This avoids automatic switching between SW and HW
                       // renderers being continuously reported as an error
#endif
      theSystem->error("GL ERROR: %s %s\n", s, gluErrorString(glerr));
 }
}



//
//  beginObject/endObject should correspond to grouping nodes.
//  Group-level scoping for directional lights, anchors, sensors
//  are handled here. Display lists can optionally be created
//  (but the retain flag is just a hint, not guaranteed). Retained
//  objects can be referred to later to avoid duplicating geometry.
//  OpenGL doesn't allow nested objects. The top-down approach of
//  putting entire groups in display lists is faster for static 
//  scenes but uses more memory and means that if anything is changed,
//  the whole object must be tossed.
//  The bottom-up model wraps each piece of geometry in a dlist but
//  requires traversal of the entire scene graph to reference each dlist.
//  The decision about what groups to stuff in an object is punted to
//  the object itself, as it can decide whether it is mutable.
//
//  The OpenGL viewer never puts objects in display lists, so the
//  retain hint is ignored.

Viewer::Object ViewerOpenGL::beginObject(const char *,
					 bool /* retain */
					 )
{
  // Finish setup stuff before first object
  if (1 == ++d_nObjects)
    {
      //cout << "ViewerOpenGL::beginObject():rot/trans:match" << endl;
      // Finish specifying the view (postponed to make Background easier)
      glPushMatrix();
      //glLoadIdentity();
      glMultMatrixf(&d_rotationMatrix[0][0]);                  // M = M * R
      glTranslatef(d_translatex, d_translatey, d_translatez);  // M = M * T = (M * R) * T = R * T
      if (!d_lit) glDisable(GL_LIGHTING);

      //GLfloat actual_gl_mv[16];
      //glGetFloatv(GL_MODELVIEW_MATRIX, actual_gl_mv);
      //for(int i=0; i<4; i++) {
      //for(int j=0; j<4; j++)
      //cout << actual_gl_mv[i+j*4] << " ";
      //cout << endl;
      //}
      //cout << endl;
    }

  ++d_nestedObjects;

  // Increment nesting level for group-scoped lights
  for (int i=0; i<MAX_LIGHTS; ++i)
    if (d_lightInfo[i].lightType == LIGHT_DIRECTIONAL)
      ++d_lightInfo[i].nestingLevel;

  return 0;
}

// End of group scope

void ViewerOpenGL::endObject()
{
  // Decrement nesting level for group-scoped lights and get rid
  // of any defined at this level
  for (int i=0; i<MAX_LIGHTS; ++i)
    if (d_lightInfo[i].lightType == LIGHT_DIRECTIONAL)
      if (--d_lightInfo[i].nestingLevel < 0)
	{
	  glDisable( (GLenum) (GL_LIGHT0 + i) );
	  d_lightInfo[i].lightType = LIGHT_UNUSED;
	}

  if (--d_nestedObjects == 0)
    {
      //cout << "ViewerOpenGL::endObject():pop" << endl;
      glPopMatrix();
    }
}


// These attributes need to be reset per geometry. Any attribute
// modified in a geometry function should be reset here. This is
// called after Appearance/Material has been set. Any attribute
// that can be modified by an Appearance node should not be modified
// here since these settings will be put in dlists with the geometry.

void ViewerOpenGL::beginGeometry()
{
  glPushAttrib( GL_ENABLE_BIT );
}

// This should be called BEFORE ending any dlists containing geometry,
// otherwise, attributes changed in the geometry insertion will impact
// the geometries rendered after the dlist is executed.

void ViewerOpenGL::endGeometry()
{
  glPopAttrib();
  glCullFace( GL_BACK );
  glShadeModel( GL_SMOOTH );

  // if needed...
  glMatrixMode(GL_TEXTURE);
  glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);
}


// Queries

void ViewerOpenGL::getPosition( float *x, float *y, float *z )
{
  //cout << "ViewerOpenGL::getPosition()" << endl;
  GLint viewport[4];
  GLdouble modelview[16], projection[16];
  glGetIntegerv (GL_VIEWPORT, viewport);
  glGetDoublev (GL_MODELVIEW_MATRIX, modelview);
  glGetDoublev (GL_PROJECTION_MATRIX, projection);
  
  GLdouble dx, dy, dz;
  gluUnProject( modelview[12], modelview[13], modelview[14],
		modelview, projection, viewport,
		&dx, &dy, &dz);
  *x = dx;
  *y = dy;
  *z = dz;
}

void ViewerOpenGL::getOrientation( float *orientation )
{
  //cout << "ViewerOpenGL::getOrientation()" << endl;
  GLint viewport[4];
  GLdouble modelview[16], projection[16];
  glGetIntegerv (GL_VIEWPORT, viewport);
  glGetDoublev (GL_MODELVIEW_MATRIX, modelview);
  
  for (int i=0; i<4; ++i)
    for (int j=0; j<4; ++j)
      projection[4*i+j] = (i == j) ? 1.0 : 0.0;

  GLdouble ox, oy, oz;
  gluUnProject( modelview[12], modelview[13], modelview[14],
		modelview, projection, viewport,
		&ox, &oy, &oz);
  GLdouble dx, dy, dz;
  gluUnProject( modelview[12], modelview[13], modelview[14]+1.0,
		modelview, projection, viewport,
		&dx, &dy, &dz);

  // this can all be optimized ...
  float L[3] = { ox - dx, oy - dy, oz - dz };
  float Z[3] = { 0.0, 0.0, -1.0 };
  float V[3];

  Vnorm( L );
  Vcross( V, Z, L );
  if ( FPZERO( V[0]*V[0]+V[1]*V[1] ) )
    {
      orientation[0] = 0.0;
      orientation[1] = 1.0;
      orientation[2] = 0.0;
      orientation[3] = 0.0;
    }
  else
    {
      orientation[0] = V[0];
      orientation[1] = V[1];
      orientation[2] = V[2];
      orientation[3] = acos( Vdot( L, Z ) );
    }
}


// Construct Billboard transformation matrix M  
//
// get rid of this asap: change over to use the transformation matrix
// accumulated during render traversal. -cks
//
void ViewerOpenGL::getBillboardTransformMatrix(float M[4][4],
                                               const float axisOfRotation[3])
{
  GLfloat modelview[16],invert[9];
  glGetFloatv (GL_MODELVIEW_MATRIX, modelview);
  float pos[] = {-modelview[12], - modelview[13], -modelview[14]};
  float v[3],y[3];

  if(InvertMatrix3x3of4x4(modelview,invert))
  {
// Viewer position in local coordinate system
   v[0] = pos[0] * invert[0] + pos[1] * invert[3] + pos[2] * invert[6];
   v[1] = pos[0] * invert[1] + pos[1] * invert[4] + pos[2] * invert[7];
   v[2] = pos[0] * invert[2] + pos[1] * invert[5] + pos[2] * invert[8];
   Vnorm( v );
// Viewer-alignment
  if ( FPZERO(axisOfRotation[0]) &&
       FPZERO(axisOfRotation[1]) &&
       FPZERO(axisOfRotation[2]) )
    {
// Viewer's up vector
	  y[0] = invert[3];
	  y[1] = invert[4];
	  y[2] = invert[5];
      Vnorm( y );
// get x-vector from the cross product of Viewer's
// up vector and billboard-to-viewer vector. 
     float x[3];
     Vcross( x, y, v );
	 Vnorm( x );
     M[0][0] = x[0]; M[0][1] = x[1]; M[0][2] = x[2]; M[0][3] = 0.0;
     M[1][0] = y[0]; M[1][1] = y[1]; M[1][2] = y[2]; M[1][3] = 0.0;
     M[2][0] = v[0]; M[2][1] = v[1]; M[2][2] = v[2]; M[2][3] = 0.0,
     M[3][0] = M[3][1] = M[3][2] = 0.0; M[3][3] = 1.0;
    }

  // use axis of rotation
  else
    {
// axis of rotation will be the y-axis vector
     y[0] = axisOfRotation[0];
     y[1] = axisOfRotation[1];
     y[2] = axisOfRotation[2];
     Vnorm( y );

// Plane defined by the axisOfRotation and billboard-to-viewer vector
     float x[3];
     Vcross( x, y, v );
     Vnorm( x );
// Get Z axis vector from cross product of X and Y
     float z[3];
     Vcross(z, x, y);
// Transform Z axis vector of current coordinate system to new coordinate system.
     float nz[3];
     nz[0] = x[2]; nz[1] = y[2]; nz[2] = z[2];
// calculate the angle by which the Z axis vector of current coordinate system
// has to be rotated around the Y axis to new coordinate system.
     float angle = acos(nz[2])*180/M_PI;
     if(nz[0] > 0) angle = -angle;
     glPushMatrix();
     glLoadIdentity();
     glRotatef(angle,y[0],y[1],y[2]);
     glGetFloatv (GL_MODELVIEW_MATRIX,(float *) M);
     glPopMatrix();	 
  }
  }
}


Viewer::RenderMode ViewerOpenGL::getRenderMode() 
{
  return d_selectMode ? RENDER_MODE_PICK : RENDER_MODE_DRAW; 
}

double ViewerOpenGL::getFrameRate()
{
  return 1.0 / d_renderTime;
}

//

void ViewerOpenGL::resetUserNavigation()
{
  d_translatex = d_translatey = d_translatez = 0.0;
  d_target[0] = d_target[1] = d_target[2] = 0.0;

  trackball(d_curquat, 0.0, 0.0, 0.0, 0.0);
  //d_rotationChanged = true;
  double tmp[4][4];
  Midentity(tmp);
  matrix_to_glmatrix(tmp, (float*)d_rotationMatrix);
  wsPostRedraw();
}


void ViewerOpenGL::getUserNavigation(double M[4][4])
{
  //cout << "ViewerOpenGL::getuserNavigation()" << endl;

  Midentity(M);
  float pos_vec[3];
  pos_vec[0] = d_translatex;
  pos_vec[1] = d_translatey;
  pos_vec[2] = d_translatez;
  double pos_mat[4][4];
  Mtranslation(pos_mat, pos_vec);
  //cout << "(" << d_translatex << "," << d_translatey << "," << d_translatez << ")" << endl;

  double rot_mat[4][4];
  for(int i=0; i<4; i++) // oh good grief.
    for(int j=0; j<4; j++)
      rot_mat[i][j] = d_rotationMatrix[j][i];

  //MM((double[4][4])M, pos_mat, rot_mat);
  MM(M, rot_mat, pos_mat); // M = R * T
  //Mdump(cout, M) << endl;
}

// Generate a normal from 3 indexed points.

static void indexFaceNormal( int i1,
			     int i2,
			     int i3,
			     float *p,
			     float *N)
{
  float V1[3], V2[3];

  Vdiff( V1, &p[i2], &p[i3] );
  Vdiff( V2, &p[i2], &p[i1] );
  Vcross( N, V1, V2 );
}

//
//  Geometry insertion.
//

Viewer::Object ViewerOpenGL::insertBackground(int nGroundAngles,
					      float* groundAngle,
					      float* groundColor,
					      int nSkyAngles,
					      float* skyAngle,
					      float* skyColor,
					      int *whc,
					      unsigned char **pixels) 
{
  float r = 0.0, g = 0.0, b = 0.0, a = 1.0;

  // Clear to last sky color
  if (skyColor != 0)
    {
      r = skyColor[3*nSkyAngles+0];
      g = skyColor[3*nSkyAngles+1];
      b = skyColor[3*nSkyAngles+2];
    }

  GLuint glid = 0;

  // Need to separate the geometry from the transformation so the
  // dlist doesn't have to get rebuilt for every mouse movement...
#if USE_GEOMETRY_DISPLAY_LISTS && 0
  // Don't bother with a dlist if we aren't drawing anything
  if (! d_selectMode &&
      (nSkyAngles > 0 || nGroundAngles > 0 || pixels) )
    {
      glid = glGenLists(1);
      glNewList( glid, GL_COMPILE_AND_EXECUTE );
    }
#endif

  glClearColor( r, g, b, a );
  GLuint mask = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT;
#if USE_STENCIL_SHAPE
  mask |= GL_STENCIL_BUFFER_BIT;
  glClear( mask );
#else
  glClear( mask );

  // Draw the background as big spheres centered at the view position
  if ( ! d_selectMode && (nSkyAngles > 0 || nGroundAngles > 0 || pixels) )
    {
      glDisable( GL_DEPTH_TEST );
      glDisable( GL_LIGHTING );

      glPushMatrix();
      glLoadIdentity();

      // Undo translation
      glTranslatef( d_position[0], d_position[1], d_position[2] );

      // Apply current view rotation
      glMultMatrixf( &d_rotationMatrix[0][0] );

      glScalef(1000.,1000.,1000.);

      // Sphere constants
      const int nCirc = 8;		// number of circumferential slices
      const double cd = 2.0 * M_PI / nCirc;

      double heightAngle0, heightAngle1 = 0.0;
      float *c0, *c1 = skyColor;

      for (int nSky=0; nSky<nSkyAngles; ++nSky)
	{
	  heightAngle0 = heightAngle1;
	  heightAngle1 = skyAngle[nSky];
	  c0 = c1;
	  c1 += 3;

	  double circAngle0, circAngle1 = 0.0;
	  double sha0 = sin(heightAngle0), cha0 = cos(heightAngle0);
	  double sha1 = sin(heightAngle1), cha1 = cos(heightAngle1);
	  double sca0, cca0;
	  double sca1 = sin(circAngle1), cca1 = cos(circAngle1);

	  glBegin( GL_QUADS );
	  for (int nc=0; nc<nCirc; ++nc)
	    {
	      circAngle0 = circAngle1;
	      circAngle1 = (nc+1) * cd;
	      sca0 = sca1;
	      sca1 = sin(circAngle1);
	      cca0 = cca1;
	      cca1 = cos(circAngle1);

	      glColor3fv( c1 );
	      glVertex3f( sha1 * cca0, cha1, sha1 * sca0 );
	      glVertex3f( sha1 * cca1, cha1, sha1 * sca1 );
	      glColor3fv( c0 );
	      glVertex3f( sha0 * cca1, cha0, sha0 * sca1 );
	      glVertex3f( sha0 * cca0, cha0, sha0 * sca0 );
	    }
	  glEnd();
	}

      // Ground
      heightAngle1 = M_PI;
      c1 = groundColor;

      for (int nGround=0; nGround<nGroundAngles; ++nGround)
	{
	  heightAngle0 = heightAngle1;
	  heightAngle1 = M_PI - groundAngle[nGround];
	  c0 = c1;
	  c1 += 3;

	  double circAngle0, circAngle1 = 0.0;
	  double sha0 = sin(heightAngle0), cha0 = cos(heightAngle0);
	  double sha1 = sin(heightAngle1), cha1 = cos(heightAngle1);
	  double sca0, cca0;
	  double sca1 = sin(circAngle1), cca1 = cos(circAngle1);

	  glBegin( GL_QUADS );
	  for (int nc=0; nc<nCirc; ++nc)
	    {
	      circAngle0 = circAngle1;
	      circAngle1 = (nc+1) * cd;
	      sca0 = sca1;
	      sca1 = sin(circAngle1);
	      cca0 = cca1;
	      cca1 = cos(circAngle1);

	      glColor3fv( c1 );
	      glVertex3f( sha1 * cca1, cha1, sha1 * sca1 );
	      glVertex3f( sha1 * cca0, cha1, sha1 * sca0 );
	      glColor3fv( c0 );
	      glVertex3f( sha0 * cca0, cha0, sha0 * sca0 );
	      glVertex3f( sha0 * cca1, cha0, sha0 * sca1 );
	    }
	  glEnd();
	}

      // Background textures are drawn on a transparent cube
      if (pixels && d_texture && ! d_wireframe)
	{
	  float v[6][4][3] = {
	    {{1,-1,1}, {-1,-1,1}, {-1,1,1}, {1,1,1}},     // Back
	    {{-1,-1,1}, {1,-1,1}, {1,-1,-1}, {-1,-1,-1}}, // Bottom
	    {{-1,-1,-1}, {1,-1,-1}, {1,1,-1}, {-1,1,-1}}, // Front
	    {{-1,-1,1}, {-1,-1,-1}, {-1,1,-1}, {-1,1,1}}, // Left
	    {{1,-1,-1}, {1,-1,1}, {1,1,1}, {1,1,-1}},     // Right
	    {{-1,1,-1}, {1,1,-1}, {1,1,1}, {-1,1,1}}};    // Top

	  glScalef( 0.5, 0.5, 0.5 );

	  glEnable( GL_TEXTURE_2D );
	  glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );

	  int t, lastT = -1;
	  for (t=0; t<6; ++t, whc+=3) {
	    // Check for non-zero width,height,coords and pixel data
	    if (whc[0] && whc[1] && whc[2] && pixels[t])
	      {
		// Optimize for the case where the same texture is used
		if (lastT == -1 || pixels[t] != pixels[lastT])
		  insertTexture(whc[0], whc[1], whc[2],
				false, false, pixels[t],
				false);  // Don't put the textures in dlists

		lastT = t;
		glBegin( GL_QUADS );

		//glTexCoord2f( 0.0, 1.0 );
		glTexCoord2f( 0.0, 0.0 );
		glVertex3fv( v[t][0] );

		//glTexCoord2f( 1.0, 1.0 );
		glTexCoord2f( 1.0, 0.0 );
		glVertex3fv( v[t][1] );

		//glTexCoord2f( 1.0, 0.0 );
		glTexCoord2f( 1.0, 1.0 );
		glVertex3fv( v[t][2] );

		//glTexCoord2f( 0.0, 0.0 );
		glTexCoord2f( 0.0, 1.0 );
		glVertex3fv( v[t][3] );

		glEnd();
	      }
	  }
	  glDisable( GL_TEXTURE_2D );
	}

      // Put everything back the way it was
      glPopMatrix();

      if (d_lit) glEnable( GL_LIGHTING );
      glEnable( GL_DEPTH_TEST );
    }

#endif // USE_STENCIL_SHAPE

  //endGeometry();
  if (glid) glEndList();

  // Save bg color so we can choose a fg color (doesn't help bg textures...)
  d_background[0] = r;
  d_background[1] = g;
  d_background[2] = b;

  return (Object) glid;
}



Viewer::Object ViewerOpenGL::insertBox(float x, float y, float z)
{
  GLuint glid = 0;

#if USE_GEOMETRY_DISPLAY_LISTS
  if (! d_selectMode)
    {
      glid = glGenLists(1);
      glNewList( glid, GL_COMPILE_AND_EXECUTE );
    }
#endif

  static GLint faces[6][4] =
  {
    {0, 1, 2, 3},
    {1, 5, 6, 2},
    {5, 4, 7, 6},
    {4, 0, 3, 7},
    {2, 6, 7, 3},
    {0, 4, 5, 1}
  };

  static GLfloat n[6][3] =	// normals
  {
    {-1.0, 0.0, 0.0},
    {0.0, 0.0, 1.0},
    {1.0, 0.0, 0.0},
    {0.0, 0.0, -1.0},
    {0.0, 1.0, 0.0},
    {0.0, -1.0, 0.0}
  };

  GLfloat v[8][3];
  GLint i;

  v[0][0] = v[1][0] = v[2][0] = v[3][0] = -x / 2;
  v[4][0] = v[5][0] = v[6][0] = v[7][0] = x / 2;
  v[0][1] = v[1][1] = v[4][1] = v[5][1] = -y / 2;
  v[2][1] = v[3][1] = v[6][1] = v[7][1] = y / 2;
  v[0][2] = v[3][2] = v[4][2] = v[7][2] = -z / 2;
  v[1][2] = v[2][2] = v[5][2] = v[6][2] = z / 2;

  beginGeometry();
  glShadeModel( GL_FLAT );

  glBegin( GL_QUADS );
  for (i = 0; i < 6; ++i)
    {
      glNormal3fv(&n[i][0]);

      //glTexCoord2f( 0.0, 1.0 );
      glTexCoord2f( 0.0, 0.0 );
      glVertex3fv(&v[faces[i][0]][0]);

      //glTexCoord2f( 1.0, 1.0 );
      glTexCoord2f( 1.0, 0.0 );
      glVertex3fv(&v[faces[i][1]][0]);

      //glTexCoord2f( 1.0, 0.0 );
      glTexCoord2f( 1.0, 1.0 );
      glVertex3fv(&v[faces[i][2]][0]);

      //glTexCoord2f( 0.0, 0.0 );
      glTexCoord2f( 0.0, 1.0 );
      glVertex3fv(&v[faces[i][3]][0]);
    }
  glEnd();

  endGeometry();
  if (glid) glEndList();

  return (Object) glid;
}


Viewer::Object ViewerOpenGL::insertCone(float h,
					float r,
					bool bottom, bool side)
{
  GLuint glid = 0;

#if USE_GEOMETRY_DISPLAY_LISTS
  if (! d_selectMode)
    {
      glid = glGenLists(1);
      glNewList( glid, GL_COMPILE_AND_EXECUTE );
    }
#endif

  beginGeometry();
  if (! bottom || ! side )
    glDisable( GL_CULL_FACE );

  if (bottom || side)
    {
      const int nfacets = 11;		// Number of polygons for sides
      const int npts = 2 * nfacets;
      const int nfaces = nfacets * 5;

      float c[ npts ][ 3 ];		// coordinates
      float tc[ npts ][ 3 ];		// texture coordinates
      int faces[ nfaces ];		// face lists

      // should only compute tc if a texture is present...
      computeCylinder( h, r, nfacets, c, tc, faces);

      for (int i=0; i<nfacets; ++i)
	c[i][0] = c[i][2] = 0.0;

      if (side)
	{
	  float Ny = r * r / h;
	  glBegin( GL_QUAD_STRIP );
	  for (int i = 0; i < nfacets; ++i)
	    {
	      glNormal3f( c[i+nfacets][0], Ny, c[i+nfacets][2] );
	      glTexCoord2fv( &tc[i+nfacets][0] );
	      glVertex3fv(   &c [i+nfacets][0] );
	      glTexCoord2fv( &tc[i][0] );
	      glVertex3fv(   &c [i][0] );
	    }

	  glNormal3f( c[nfacets][0], Ny, c[nfacets][2] );
	  glTexCoord2f( tc[nfacets][0]-1.0, tc[nfacets][1] );
	  glVertex3fv(   &c [nfacets][0] );
	  glTexCoord2f( tc[0][0]-1.0, tc[0][1] );
	  glVertex3fv(   & c[0][0] );
	  glEnd();
	}

      if (bottom)
	{
	  glBegin( GL_TRIANGLE_FAN );
	  glNormal3f( 0.0, -1.0, 0.0 );
	  glTexCoord2f( 0.5, 0.5 );
	  glVertex3f( 0.0, - 0.5 * h, 0.0 );

	  float angle = 0.5 * M_PI; // First v is at max x
	  float aincr = 2.0 * M_PI / (float) nfacets;
	  for (int i = 0; i < nfacets; ++i, angle+=aincr )
	    {
	      glTexCoord2f( 0.5*(1.0+sin( angle )),
			    1.0-0.5*(1.+cos( angle )) );
	      glVertex3fv( &c[i+nfacets][0] );
	    }
	  glTexCoord2f( 0.5*(1.0+sin( angle )),
			1.0-0.5*(1.+cos( angle )) );
	  glVertex3fv( &c[nfacets][0] );
	  glEnd();
	}
    }

  endGeometry();
  if (glid) glEndList();

  return (Object) glid;
}


Viewer::Object ViewerOpenGL::insertCylinder(float h,
					    float r,
					    bool bottom, bool side, bool top)
{
  GLuint glid = 0;

#if USE_GEOMETRY_DISPLAY_LISTS
  if (! d_selectMode)
    {
      glid = glGenLists(1);
      glNewList( glid, GL_COMPILE_AND_EXECUTE );
    }
#endif

  beginGeometry();
  if (! bottom || ! side || ! top)
    glDisable( GL_CULL_FACE );

  if (bottom || side || top)
    {
      const int nfacets = 8;		// Number of polygons for sides
      const int npts = 2 * nfacets;
      const int nfaces = nfacets * 5;

      float c[ npts ][ 3 ];		// coordinates
      float tc[ npts ][ 3 ];		// texture coordinates
      int faces[ nfaces ];		// face lists

      // should only compute tc if a texture is present...
      computeCylinder( h, r, nfacets, c, tc, faces);

      if (side)
	{
	  glBegin( GL_QUAD_STRIP );
	  for (int i = 0; i < nfacets; ++i)
	    {
	      glNormal3f( c[i+nfacets][0], 0.0, c[i+nfacets][2] );
	      glTexCoord2fv( &tc[i+nfacets][0] );
	      glVertex3fv(   &c [i+nfacets][0] );
	      glTexCoord2fv( &tc[i][0] );
	      glVertex3fv(   &c [i][0] );
	    }

	  glNormal3f( c[nfacets][0], 0.0, c[nfacets][2] );
	  glTexCoord2f( tc[nfacets][0]-1.0, tc[nfacets][1] );
	  glVertex3fv(   &c [nfacets][0] );
	  glTexCoord2f( tc[0][0]-1.0, tc[0][1] );
	  glVertex3fv(   & c[0][0] );
	  glEnd();
	}

      if (bottom)		// tex coords...
	{
	  glBegin( GL_TRIANGLE_FAN );
	  glNormal3f( 0.0, -1.0, 0.0);
	  glTexCoord2f( 0.5, 0.5 );
	  glVertex3f( 0.0, - 0.5 * h, 0.0 );

	  float angle = 0.5 * M_PI; // First v is at max x
	  float aincr = 2.0 * M_PI / (float) nfacets;
	  for (int i = 0; i < nfacets; ++i, angle+=aincr)
	    {
	      glTexCoord2f( 0.5*(1.+sin( angle )),
			    1.0 - 0.5*(1.+cos( angle )) );
	      glVertex3fv( &c[i+nfacets][0] );
	    }
	  glTexCoord2f( 0.5*(1.+sin( angle )),
			1.0 - 0.5*(1.+cos( angle )) );
	  glVertex3fv( &c[nfacets][0] );
	  glEnd();
	}

      if (top)		// tex coords...
	{
	  glBegin( GL_TRIANGLE_FAN );
	  glNormal3f( 0.0, 1.0, 0.0);
	  glTexCoord2f( 0.5, 0.5 );
	  glVertex3f( 0.0, 0.5 * h, 0.0 );

	  float angle = 0.75 * M_PI;
	  float aincr = 2.0 * M_PI / (float) nfacets;
	  for (int i = nfacets-1; i >= 0; --i, angle+=aincr)
	    {
	      glTexCoord2f( 0.5*(1.+sin( angle )),
			    1.0 - 0.5*(1.+cos( angle )) );
	      glVertex3fv( &c[i][0] );
	    }
	  glTexCoord2f( 0.5*(1.+sin( angle )),
			1.0 - 0.5*(1.+cos( angle )) );
	  glVertex3fv( &c[nfacets-1][0] );
	  glEnd();
	}
    }

  endGeometry();
  if (glid) glEndList();

  return (Object) glid;
}

// Compute a normal at vert i,j of an ElevationGrid.

static void elevationVertexNormal(int i, int j,
				  int nx, int nz,
				  float dx, float dz,
				  float *height,
				  float N[])
{
  float Vx[3], Vz[3];

  if (i > 0 && i < nx-1)
    {
      Vx[0] = 2.0 * dx;
      Vx[1] = *(height+1) - *(height-1);
    }
  else if (i == 0)
    {
      Vx[0] = dx;
      Vx[1] = *(height+1) - *(height);
    }
  else
    {
      Vx[0] = dx;
      Vx[1] = *(height) - *(height-1);
    }
  Vx[2] = 0.0;

  Vz[0] = 0.0;
  if (j > 0 && j < nz-1)
    {
      Vz[1] = *(height+nx) - *(height-nx);
      Vz[2] = 2.0 * dz;
    }
  else if (j == 0)
    {
      Vz[1] = *(height+nx) - *(height);
      Vz[2] = dz;
    }
  else
    {
      Vz[1] = *(height) - *(height-nx);
      Vz[2] = dz;
    }

  Vcross( N, Vz, Vx );
}


Viewer::Object ViewerOpenGL::insertElevationGrid(unsigned int mask,
						 int nx,
						 int nz,
						 float *height,
						 float dx,
						 float dz,
						 float *texture_coords,
						 float *normals,
						 float *colors)
{
  int i, j;
  float x, z;

  GLuint glid = 0;

#if USE_GEOMETRY_DISPLAY_LISTS
  if (! d_selectMode)
    {
      glid = glGenLists(1);
      glNewList( glid, GL_COMPILE_AND_EXECUTE );
    }
#endif

  beginGeometry();

  // Face orientation & culling
  glFrontFace( (mask & MASK_CCW) ? GL_CCW : GL_CW );
  if (! (mask & MASK_SOLID) )
    glDisable( GL_CULL_FACE );

  // x varies fastest
  for (j=0; j<nz-1; ++j)
    {
      float s0, s1, t0 = 0.0, t1 = 0.0;

      z = dz * j;
      if (! texture_coords)
	{
	  t0 = ((float) j) / (nz-1);
	  t1 = ((float) j+1) / (nz-1);
	}

      glBegin( GL_QUAD_STRIP );

      for (i=0; i<nx; ++i)
	{
	  x = dx * i;
	  
	  if (colors &&
	      ((mask & MASK_COLOR_PER_VERTEX) || (i < nx-1)))
	    {
	      glColor3fv( colors );
	    }
	      
	  if (normals &&
	      ((mask & MASK_NORMAL_PER_VERTEX) || (i < nx-1)))
	    {
	      glNormal3fv( normals );
	    }
	  else if (! normals)
	    {
	      float N[3];
	      if (mask & MASK_NORMAL_PER_VERTEX)
		{
		  elevationVertexNormal(i, j, nx, nz, dx, dz, height, N);
		  glNormal3fv( N );
		}
	      else if (i < nx-1)		// Normal per face
		{
		  float Vx[3] = { dx, *(height+1) - *height, 0.0 };
		  float Vz[3] = { 0.0, *(height+nx) - *height, dz };
		  Vcross( N, Vx, Vz );
		  glNormal3fv( N );
		}
	    }

	  if (texture_coords)
	    {
	      s0 = *(texture_coords);
	      t0 = *(texture_coords+1);
	      s1 = *(texture_coords+(nx*2));
	      t1 = *(texture_coords+(nx*2)+1);
              texture_coords+=2;
	    }
	  else
	    s0 = s1 = ((float) i) / (nx-1);

	  glTexCoord2f( s0, t0 );
          //cout << "ViewerOpenGL::insertElevationGrid():(s0,t0)=" << s0 << "," << t0 << endl;
	  glVertex3f( x, *height, z );
          //cout << "ViewerOpenGL::insertElevationGrid():(x,y,z)=" << x << "," << *height << "," << z << endl;

	  // Vertex from next row
	  if (colors && (mask & MASK_COLOR_PER_VERTEX))
	    {
	      glColor3fv( colors+3*nx );
	    }

	  if (mask & MASK_NORMAL_PER_VERTEX)
	    {
	      if (normals)
		{
		  glNormal3fv( normals+3*nx );
		}
	      else
		{
		  float N[3];
		  elevationVertexNormal(i, j+1, nx, nz, dx, dz, height+nx, N);
		  glNormal3fv( N );
		}
	    }

	  glTexCoord2f( s1, t1 );
          //cout << "ViewerOpenGL::insertElevationGrid():(s1,t1)=" << s1 << "," << t1 << endl;
	  glVertex3f( x, *(height+nx), z+dz );
          //cout << "ViewerOpenGL::insertElevationGrid():(x,y,z)=" << x << "," << *(height+nx) << "," << z+dz << endl;

	  ++height;
	  if ( colors ) colors += 3;
	  if ( normals ) normals += 3;
	}

      glEnd();
    }

  endGeometry();
  if (glid) glEndList();
  return (Object) glid;
}


#if GLU_VERSION_1_2

// Tesselator callback

#if _WIN32
# define WINAPI __stdcall
#else
# define WINAPI 
#endif
typedef GLvoid ( WINAPI *TessCB)();


// Extrusion cap tessellation for non-convex shapes

typedef struct {
  float *c;			// coordinates array [nVerts * 3]
  float *crossSection;		// crossSection coordinates [nCrossSection * 2]
  float tcDeltaU, tcDeltaV;
  float tcScaleU, tcScaleV;
  int vOffset;
  float N[3];			// Normal
} TessExtrusion;

static void WINAPI tessExtrusionBegin( GLenum type, void *pdata )
{
  TessExtrusion *p = (TessExtrusion *)pdata;
  glBegin( type );
  glNormal3fv( &p->N[0] );
}


static void WINAPI tessExtrusionVertex( void *vdata, void *pdata )
{
  int j = (int)vdata;
  TessExtrusion *p = (TessExtrusion *)pdata;
  
  glTexCoord2f( (p->crossSection[2*j] - p->tcDeltaU) * p->tcScaleU,
		(p->crossSection[2*j+1] - p->tcDeltaV) * p->tcScaleV );
  glVertex3fv( &(p->c[3 * (j + p->vOffset)]) );
}

#endif


void ViewerOpenGL::insertExtrusionCaps( unsigned int mask,
					int nSpine,
					float *c,
					int nCrossSection,
					float *cs )
{
  // Determine x,z ranges for top & bottom tex coords
  float xz[4] = { cs[0], cs[0], cs[1], cs[1] };
  float *csp = cs;

  for (int nn=1; nn<nCrossSection; ++nn, csp += 2)
    {
      if (csp[0] < xz[0])      xz[0] = csp[0];
      else if (csp[0] > xz[1]) xz[1] = csp[0];
      if (csp[1] < xz[2])      xz[2] = csp[1];
      else if (csp[1] > xz[3]) xz[3] = csp[1];
    }

  float dx = xz[1] - xz[0];
  float dz = xz[3] - xz[2];
  if (! FPZERO(dx)) dx = 1.0 / dx;
  if (! FPZERO(dz)) dz = 1.0 / dz;

  // If geometry is in dlists, should just always use the tesselator...

#if GLU_VERSION_1_2
  int last = 2*(nCrossSection-1);
  bool equalEndpts = FPEQUAL(cs[0], cs[last]) && FPEQUAL(cs[1], cs[last+1]);

  if (! (mask & MASK_CONVEX))
    {
      if (! d_tess) d_tess = gluNewTess();

      gluTessCallback( d_tess, (GLenum) GLU_TESS_BEGIN_DATA,
		       (TessCB) tessExtrusionBegin );
      gluTessCallback( d_tess, (GLenum) GLU_TESS_VERTEX_DATA,
		       (TessCB) tessExtrusionVertex );
      gluTessCallback( d_tess, (GLenum) GLU_TESS_END,
		       (TessCB) glEnd );

      if (mask & MASK_BOTTOM)
	{
	  TessExtrusion bottom = { c, cs, xz[0], xz[2], dx, dz, 0 };
	  indexFaceNormal( 0, 1, 2, c, bottom.N );

	  gluTessBeginPolygon( d_tess, &bottom );
	  gluTessBeginContour( d_tess );
	  GLdouble v[3];
	  // Mesa tesselator doesn;t like closed polys
	  int j = equalEndpts ? nCrossSection-2 : nCrossSection-1;
	  for ( ; j>=0; --j)
	    {
	      v[0] = c[3*j];
	      v[1] = c[3*j+1];
	      v[2] = c[3*j+2];
	      gluTessVertex( d_tess, v, (void*)j );
	    }
	  gluTessEndContour( d_tess );
	  gluTessEndPolygon( d_tess );
	}
      
      if (mask & MASK_TOP)
	{
	  int n = (nSpine - 1) * nCrossSection;
	  TessExtrusion top = { c, cs, xz[0], xz[2], dx, dz, n };
	  indexFaceNormal( 3*n+2, 3*n+1, 3*n, c, top.N );

	  gluTessBeginPolygon( d_tess, &top );
	  gluTessBeginContour( d_tess );

	  GLdouble v[3];
	  // Mesa tesselator doesn;t like closed polys
	  int j = equalEndpts ? 1 : 0;
	  for ( ; j < nCrossSection; ++j)
	    {
	      v[0] = c[3*(j+n)];
	      v[1] = c[3*(j+n)+1];
	      v[2] = c[3*(j+n)+2];
	      gluTessVertex( d_tess, v, (void*)j );
	    }
	  gluTessEndContour( d_tess );
	  gluTessEndPolygon( d_tess );
	}
    }

  else
#endif

    // Convex (or not GLU1.2 ...)
    {
      float N[3];			// Normal

      if (mask & MASK_BOTTOM)
	{
	  glBegin( GL_POLYGON );
	  indexFaceNormal( 0, 1, 2, c, N );
	  glNormal3fv( N );

	  for (int j = nCrossSection-1; j>=0; --j)
	    {
	      glTexCoord2f( (cs[2*j]-xz[0])*dx, (cs[2*j+1]-xz[2])*dz );
	      glVertex3fv( &c[3*j] );
	    }
	  glEnd();
	}
      
      if (mask & MASK_TOP)
	{
	  int n = (nSpine - 1) * nCrossSection;
	  glBegin( GL_POLYGON );
	  indexFaceNormal( 3*n+2, 3*n+1, 3*n, c, N );
	  glNormal3fv( N );

	  for (int j = 0; j < nCrossSection; ++j)
	    {
	      glTexCoord2f( (cs[2*j]-xz[0])*dx, (cs[2*j+1]-xz[2])*dz );
	      glVertex3fv( &c [3*(j+n)] );
	    }
	  glEnd();
	}
    }
}



Viewer::Object ViewerOpenGL::insertExtrusion(unsigned int mask,
					     int  nOrientation,
					     float *orientation,
					     int  nScale,
					     float *scale,
					     int  nCrossSection,
					     float *crossSection,
					     int nSpine,
					     float *spine )
{
  float *c  = new float[nCrossSection * nSpine * 3];
  float *tc = new float[nCrossSection * nSpine * 3];

  computeExtrusion( nOrientation, orientation,
		    nScale, scale,
		    nCrossSection, crossSection,
		    nSpine, spine,
		    c, tc, 0 );

  GLuint glid = 0;

#if USE_GEOMETRY_DISPLAY_LISTS
  if (! d_selectMode)
    {
      glid = glGenLists(1);
      glNewList( glid, GL_COMPILE_AND_EXECUTE );
    }
#endif

  beginGeometry();

  // Face orientation & culling
  glFrontFace( (mask & MASK_CCW) ? GL_CCW : GL_CW );
  if (! (mask & MASK_SOLID) )
    glDisable( GL_CULL_FACE );

  // Handle creaseAngle, correct normals, ...
  int n = 0;
  for (int i = 0; i < nSpine-1; ++i, n+=nCrossSection)
    {
      glBegin( GL_QUAD_STRIP );
      for (int j = 0; j < nCrossSection; ++j)
	{
	  // Compute normals
	  float v1[3], v2[3];
	  if (j < nCrossSection-1)
	    Vdiff( v1, &c[3*(n+j+1)], &c[3*(n+j)] );
	  else
	    Vdiff( v1, &c[3*(n+j)], &c[3*(n+j-1)] );
	  Vdiff( v2, &c[3*(n+j+nCrossSection)], &c[3*(n+j)] );
	  Vcross( v1, v1, v2 );
	  glNormal3fv( v1 );

	  glTexCoord2fv( &tc[3*(n+j+nCrossSection)] );
	  glVertex3fv(   &c [3*(n+j+nCrossSection)] );
	  glTexCoord2fv( &tc[3*(n+j)] );
	  glVertex3fv(   &c [3*(n+j)] );
	}
      glEnd();
    }

  // Draw caps. Convex can only impact the caps of an extrusion.
  if (mask & (MASK_BOTTOM | MASK_TOP))
    insertExtrusionCaps( mask, nSpine, c, nCrossSection, crossSection );

  delete [] c;
  delete [] tc;

  endGeometry();
  if (glid) glEndList();
  return (Object) glid;
}


Viewer::Object ViewerOpenGL::insertLineSet(int npoints,
					   float *points,
					   size_t nlines,
					   const long * lines,
					   bool colorPerVertex,
					   float *color,
					   size_t nci,
					   const long * ci)
{
  GLuint glid = 0;

  if (npoints < 2) return 0;

#if USE_GEOMETRY_DISPLAY_LISTS
  if (! d_selectMode)
    {
      glid = glGenLists(1);
      glNewList( glid, GL_COMPILE_AND_EXECUTE );
    }
#endif

  beginGeometry();

  // Lighting, texturing don't apply to line sets
  glDisable( GL_LIGHTING );
  glDisable( GL_TEXTURE_2D );
  if (color && ! colorPerVertex )
    glShadeModel( GL_FLAT );

  glBegin( GL_LINE_STRIP );
  if (color && ! colorPerVertex)
    glColor3fv( &color[ (nci > 0) ? 3*ci[0] : 0 ] );

  int nl = 0;
  for (size_t i = 0; i<nlines; ++i)
    {
      if (lines[i] == -1)
	{
	  glEnd();
	  glBegin( GL_LINE_STRIP );
	  ++nl;
	  if ((i < nlines-1) && color && ! colorPerVertex)
	    glColor3fv( &color[ (nci > 0) ? 3*ci[nl] : 3*nl ] );
	}
      else
	{
	  if (color && colorPerVertex)
	    glColor3fv( &color[ (nci > 0) ? 3*ci[i] : 3*lines[i] ] );
	  glVertex3fv( &points[3*lines[i]] );
	}
    }

  glEnd();

  endGeometry();
  if (glid) glEndList();
  return (Object) glid;
}

Viewer::Object ViewerOpenGL::insertPointSet(int npoints,
					    float *points,
					    float *colors)
{
  GLuint glid = 0;

#if USE_GEOMETRY_DISPLAY_LISTS
  if (! d_selectMode)
    {
      glid = glGenLists(1);
      glNewList( glid, GL_COMPILE_AND_EXECUTE );
    }
#endif

  beginGeometry();

  // Lighting, texturing don't apply to points
  glDisable( GL_LIGHTING );
  glDisable( GL_TEXTURE_2D );

  glBegin( GL_POINTS );

  for (int i = 0; i<npoints; ++i)
    {
      if (colors)
	{
	  glColor3fv( colors );
	  colors += 3;
	}
      glVertex3fv( points );
      points += 3;
    }

  glEnd();
  endGeometry();
  if (glid) glEndList();

  return (Object) glid;
}

// 

static void computeBounds( int npoints,
			   float *points,
			   float *bounds )
{
  bounds[0] = bounds[1] = points[0]; // xmin, xmax
  bounds[2] = bounds[3] = points[1]; // ymin, ymax
  bounds[4] = bounds[5] = points[2]; // zmin, zmax

  for (int i=1; i<npoints; ++i)
    {
      points += 3;
      if (points[0] < bounds[0])      bounds[0] = points[0];
      else if (points[0] > bounds[1]) bounds[1] = points[0];
      if (points[1] < bounds[2])      bounds[2] = points[1];
      else if (points[1] > bounds[3]) bounds[3] = points[1];
      if (points[2] < bounds[4])      bounds[4] = points[2];
      else if (points[2] > bounds[5]) bounds[5] = points[2];
    }
}


void
texGenParams( float bounds[],	// xmin,xmax, ymin,ymax, zmin,zmax
	      int axes[2],	// s, t
	      float params[4] ) // s0, 1/sSize, t0, 1/tSize
{
  axes[0] = 0;
  axes[1] = 1;
  params[0] = params[1] = params[2] = params[3] = 0.0;

  for (int nb=0; nb<3; ++nb)
    {
      float db = bounds[2*nb+1]-bounds[2*nb];
      if ( db > params[1] )
	{
	  axes[1] = axes[0];
	  axes[0] = nb;
	  params[2] = params[0];
	  params[3] = params[1];
	  params[0] = bounds[2*nb];
	  params[1] = db;
	}
      else if ( db > params[3] )
	{
	  axes[1] = nb;
	  params[2] = bounds[2*nb];
	  params[3] = db;
	}
    }

  // If two of the dimensions are zero, give up.
  if ( FPZERO( params[1] ) || FPZERO( params[3] )) return;
  
  params[1] = 1.0 / params[1];
  params[3] = 1.0 / params[3];
}


// Address of _ith entry of indexed triplet array _v. yummy
#define INDEX_VAL(_v,_i) \
&((_v).v[ 3*(((_v).ni > 0) ? (_v).i[_i] : (_i)) ])

#define INDEX_VTX_VAL(_v,_f,_i) \
 &((_v).v[ 3*(((_v).ni > 0) ? (_v).i[_i] : (_f)[_i]) ])


void 
ViewerOpenGL::insertShellConvex( ShellData *s )
{
  float N[3];
  size_t i, nf = 0;			// Number of faces

  for (i = 0; i<s->nfaces; ++i)
    {
      if (i == 0 || s->faces[i] == -1)
	{
	  if (i > 0) glEnd();
	  if (i == s->nfaces-1) break;

	  glBegin(GL_POLYGON);

	  // Per-face attributes
	  if (s->color.v && ! (s->mask & MASK_COLOR_PER_VERTEX))
	    glColor3fv( INDEX_VAL(s->color, nf) );

	  if (! (s->mask & MASK_NORMAL_PER_VERTEX))
	    {
	      int i1 = (i == 0) ? 0 : i+1;
	      if (s->normal.v)
		glNormal3fv( INDEX_VAL(s->normal, nf) );
	      else if (i < s->nfaces - 4 &&
		       s->faces[i1] >= 0 &&
		       s->faces[i1+1] >= 0 && s->faces[i1+2] >= 0)
		{
		  indexFaceNormal( 3*s->faces[i1], 3*s->faces[i1+1],
				   3*s->faces[i1+2], s->points, N );

		  // Lukas: flip normal if primitiv-orientation is clockwise
		  if (!(s->mask & MASK_CCW)) 
		    for (int k=0;k<3;k++) // flip Normal
		      N[k] = -N[k];
      		  glNormal3fv( N );
		}
	    }

	  ++nf;			// 
	}

      if (s->faces[i] >= 0)
	{
	  // Per-vertex attributes
	  if (s->color.v && (s->mask & MASK_COLOR_PER_VERTEX) )
	    glColor3fv( INDEX_VTX_VAL(s->color, s->faces, i) );

	  if (s->mask & MASK_NORMAL_PER_VERTEX)
	    {
	      if (s->normal.v)
		glNormal3fv( INDEX_VTX_VAL(s->normal, s->faces, i) );
	      else
		; // Generate per-vertex normal here...
	    }

	  float *v = &s->points[3*s->faces[i]];
	  if (s->texCoord.v)
	    {
	      int tcindex;
	      if (s->texCoord.ni > 0)
		tcindex = 2 * s->texCoord.i[i];
	      else
		tcindex = 2 * s->faces[i];
	      glTexCoord2f( s->texCoord.v[ tcindex ],
			    s->texCoord.v[ tcindex+1 ] );
	    }
	  else
	    {
	      float c0, c1;
	      c0 = (v[s->texAxes[0]] - s->texParams[0]) * s->texParams[1];
	      c1 = (v[s->texAxes[1]] - s->texParams[2]) * s->texParams[3];
	      glTexCoord2f( c0, c1 );
	    }

	  glVertex3fv( v );
	}
    }

  // Watch out for no terminating -1 in face list
  // two ways to break out:
  //   i>0 && i==nfaces-1 && faces[i] == -1
  //   i==nfaces
  //
  if (i>=s->nfaces) {
    if (s->faces[i-1] >= 0) glEnd();
  } else {
    if (s->faces[i] >= 0) glEnd();
  }
}


#if GLU_VERSION_1_2

static void WINAPI tessShellBegin( GLenum type, void *pdata )
{
  ViewerOpenGL::ShellData *s = (ViewerOpenGL::ShellData *)pdata;
  float N[3];

  glBegin( type );

  // Per-face attributes
  if (s->color.v && ! (s->mask & Viewer::MASK_COLOR_PER_VERTEX))
    glColor3fv( INDEX_VAL(s->color, s->nf) );

  if (! (s->mask & Viewer::MASK_NORMAL_PER_VERTEX))
    {
      int i1 = s->i == 0 ? 0 : s->i-1;
      if (s->normal.v)
	glNormal3fv( INDEX_VAL(s->normal, s->nf) );
      else if (s->i < s->nfaces - 4 &&
	       s->faces[i1] >= 0 &&
	       s->faces[i1+1] >= 0 && s->faces[i1+2] >= 0)
	{
	  indexFaceNormal( 3*s->faces[i1], 3*s->faces[i1+1],
			   3*s->faces[i1+2], s->points, N );
	  // Lukas: flip normal if primitiv-orientation is clockwise
	  if (!(s->mask & Viewer::MASK_CCW)) 
	    for (int k=0;k<3;k++) // flip Normal
	      N[k] = -N[k];
	  glNormal3fv( N );
	}
    }
}


static void WINAPI tessShellVertex( void *vdata, void *pdata )
{
  int i = (int)vdata;
  ViewerOpenGL::ShellData *s = (ViewerOpenGL::ShellData *)pdata;
  
  // Per-vertex attributes
  if (s->color.v && (s->mask & Viewer::MASK_COLOR_PER_VERTEX) )
    glColor3fv( INDEX_VTX_VAL(s->color, s->faces, i) );

  if (s->mask & Viewer::MASK_NORMAL_PER_VERTEX)
    {
      if (s->normal.v)
	glNormal3fv( INDEX_VTX_VAL(s->normal, s->faces, i) );
      else
	; // Generate per-vertex normal here...
    }

  float *v = &s->points[3*s->faces[i]];
  if (s->texCoord.v)
    {
      int tcindex;
      if (s->texCoord.ni > 0)
	tcindex = 2 * s->texCoord.i[i];
      else
	tcindex = 2 * s->faces[i];
      glTexCoord2f( s->texCoord.v[ tcindex ],
		    s->texCoord.v[ tcindex+1 ] );
    }
  else
    {
      float c0, c1;
      c0 = (v[s->texAxes[0]] - s->texParams[0]) * s->texParams[1];
      c1 = (v[s->texAxes[1]] - s->texParams[2]) * s->texParams[3];
      glTexCoord2f( c0, c1 );
    }

  glVertex3fv( v );
}


void 
ViewerOpenGL::insertShellTess(ShellData *s)
{
  if (! d_tess) d_tess = gluNewTess();

  gluTessCallback( d_tess, (GLenum) GLU_TESS_BEGIN_DATA,
		   (TessCB) tessShellBegin );
  gluTessCallback( d_tess, (GLenum) GLU_TESS_VERTEX_DATA,
		   (TessCB) tessShellVertex );
  gluTessCallback( d_tess, (GLenum) GLU_TESS_END,
		   (TessCB) glEnd );

  size_t i;
  for (i = 0; i<s->nfaces; ++i)
    {
      if (i == 0 || s->faces[i] == -1)
	{
	  if (i > 0)
	    {
	      gluTessEndContour( d_tess );
	      gluTessEndPolygon( d_tess );
	      ++ s->nf;
	    }
	  if (i == s->nfaces-1) break;
	  gluTessBeginPolygon( d_tess, s );
	  gluTessBeginContour( d_tess );
	  s->i = i;
	}

      if (s->faces[i] >= 0)
	{
	  GLdouble v[3];
	  v[0] = s->points[3*s->faces[i]+0];
	  v[1] = s->points[3*s->faces[i]+1];
	  v[2] = s->points[3*s->faces[i]+2];
	  gluTessVertex( d_tess, v, (void*)i );
	}
    }

  // Watch out for no terminating -1 in face list
  if (i > 1 && s->faces[i-1] >= 0)
    {
      gluTessEndContour( d_tess );
      gluTessEndPolygon( d_tess );
    }
}

#endif // GLU_VERSION_1_2


// There are too many arguments to this...

Viewer::Object 
ViewerOpenGL::insertShell(unsigned int mask,
			  int npoints,
			  float *points,
			  size_t nfaces,
			  const long * faces,    // face list (-1 ends each face)
			  float *tc,     // texture coordinates
			  int ntci,      // # of texture coordinate indices
			  const long * tci,      // texture coordinate indices
			  float *normal, // normals
			  int nni,       // # of normal indices
			  const long * ni,       // normal indices
			  float *color,  // colors
			  int nci,
			  const long * ci)
{
  if (nfaces < 4) return 0;	// 3 pts and a trailing -1

  // Texture coordinate generation parameters.
  int texAxes[2];			// Map s,t to x,y,z
  float texParams[4];		// s0, 1/sSize, t0, 1/tSize

  // Compute bounding box for texture coord generation and lighting.
  if ( ! tc )  // || any positional lights are active...
    {
      float bounds[6];		// xmin,xmax, ymin,ymax, zmin,zmax
      computeBounds( npoints, points, bounds );

      // do the bounds intersect the radius of any active positional lights...

      texGenParams( bounds, texAxes, texParams );
      if ( FPZERO( texParams[1] ) || FPZERO( texParams[3] )) return 0;
    }

  GLuint glid = 0;

#if USE_GEOMETRY_DISPLAY_LISTS
  if (! d_selectMode)
    {
      glid = glGenLists(1);
      glNewList( glid, GL_COMPILE_AND_EXECUTE );
    }
#endif

  beginGeometry();

  // Face orientation & culling
  glFrontFace( (mask & MASK_CCW) ? GL_CCW : GL_CW );
  if (! (mask & MASK_SOLID) )
    glDisable( GL_CULL_FACE );

  // Color per face
  if (color && ! (mask & MASK_COLOR_PER_VERTEX))
    glShadeModel(GL_FLAT);

  // -------------------------------------------------------

  // Generation of per vertex normals isn't implemented yet...
  if (! normal && (mask & MASK_NORMAL_PER_VERTEX))
    mask &= ~MASK_NORMAL_PER_VERTEX;

  // -------------------------------------------------------

  // Should build tri strips (probably at the VrmlNode level)...

  ShellData s = {
    mask, points, nfaces, faces,
    { tc, ntci, tci },
    { normal, nni, ni },
    { color, nci, ci },
    texAxes, texParams, 0
  };

#if GLU_VERSION_1_2
  // Handle non-convex polys
  if (! (mask & MASK_CONVEX))
    insertShellTess( &s );
  else
#endif
    insertShellConvex( &s );

  endGeometry();
  if (glid) glEndList();

  return (Object) glid;
}


Viewer::Object ViewerOpenGL::insertSphere(float radius)
{
  GLuint glid = 0;

#if USE_GEOMETRY_DISPLAY_LISTS
  if (! d_selectMode)
    {
      glid = glGenLists(1);
      glNewList( glid, GL_COMPILE_AND_EXECUTE );
    }
#endif

  const int numLatLong = 10;
  const int npts = numLatLong * numLatLong;

  float c[ npts ][ 3 ];
  float tc[ npts ][ 3 ];

  // should only compute tc if a texture is present...
  computeSphere(radius, numLatLong, c, tc, 0);

  beginGeometry();

  for ( int i = 0; i < numLatLong-1; ++i)
    {
      int n = i * numLatLong;

      glBegin( GL_QUAD_STRIP );

      for ( int j = 0; j < numLatLong; ++j )
	{
	  glTexCoord2f( tc[n+j+numLatLong][0], tc[n+j+numLatLong][1] );
	  glNormal3fv( &c[n+j+numLatLong][0] );
	  glVertex3fv( &c[n+j+numLatLong][0] );

	  glTexCoord2f( tc[n+j][0], tc[n+j][1] );
	  glNormal3fv( &c[n+j][0] );
	  glVertex3fv( &c[n+j][0] );
	}

      //glTexCoord2f( tc[n+numLatLong][0], tc[n+numLatLong][1] );
      //cout << "ViewerOpenGL::insertSphere():" << tc[n+numLatLong][0] << "," << tc[n+numLatLong][1] << endl;
      glTexCoord2f( 1.0, tc[n+numLatLong][1] );
      glNormal3fv( &c[n+numLatLong][0] );
      glVertex3fv( &c[n+numLatLong][0] );

      //glTexCoord2f( tc[n][0], tc[n][1] );
      //cout << "ViewerOpenGL::insertSphere():" << tc[n][0] << "," << tc[n][1] << endl;
      glTexCoord2f( 1.0, tc[n][1] );
      glNormal3fv( &c[n][0] );
      glVertex3fv( &c[n][0] );

      glEnd();
    }

  endGeometry();
  if (glid) glEndList();

  return (Object) glid;
}


// Not fully implemented... need font, extents

Viewer::Object ViewerOpenGL::insertText(int *justify,
					float size,
					int n, char const * const * s)
{
  GLuint glid = 0;

#if USE_GEOMETRY_DISPLAY_LISTS
  if (! d_selectMode)
    {
      glid = glGenLists(1);
      glNewList( glid, GL_COMPILE_AND_EXECUTE );
    }
#endif

  beginGeometry();

  this->text3(justify, size, n, s);

  endGeometry();
  if (glid) glEndList();

  return (Object) glid;
}


// Lights

Viewer::Object ViewerOpenGL::insertDirLight(float ambient,
					    float intensity,
					    const float rgb[],
					    const float direction[])
{
  float amb[4] = { ambient * rgb[0],
		   ambient * rgb[1],
		   ambient * rgb[2],
		   1.0 };
  float dif[4] = { intensity * rgb[0],
		   intensity * rgb[1],
		   intensity * rgb[2],
		   1.0 };
  float pos[4] = { direction[0], direction[1], -direction[2], 0.0 };

  // Find an unused light, give up if none left.
  int i;
  for (i=0; i<MAX_LIGHTS; ++i)
    if (d_lightInfo[i].lightType == LIGHT_UNUSED)
      break;
  if (i == MAX_LIGHTS)
    return 0;
  
  d_lightInfo[i].lightType = LIGHT_DIRECTIONAL;
  d_lightInfo[i].nestingLevel = 0;
  GLenum light = (GLenum) (GL_LIGHT0 + i);

  glEnable(light);
  glLightfv(light, GL_AMBIENT, amb);
  glLightfv(light, GL_DIFFUSE, dif);
  glLightfv(light, GL_POSITION, pos);

  // Disable any old point/spot settings
  glLightf(light, GL_CONSTANT_ATTENUATION, 1.0);
  glLightf(light, GL_LINEAR_ATTENUATION, 0.0);
  glLightf(light, GL_QUADRATIC_ATTENUATION, 0.0);

  glLightf(light, GL_SPOT_CUTOFF, 180.0);
  glLightf(light, GL_SPOT_EXPONENT, 0.0);

  return 0;
}

//
//  Only objects within radius should be lit by each PointLight.
//  Test each object drawn against each point light and enable
//  the lights accordingly? Get light and geometry into consistent
//  coordinates first...
//

Viewer::Object ViewerOpenGL::insertPointLight(float ambient,
					      const float attenuation[],
					      const float rgb[],
					      float intensity,
					      const float location[],
					      float radius)
{
  float amb[4] = { ambient * rgb[0],
		   ambient * rgb[1],
		   ambient * rgb[2],
		   1.0 };
  float dif[4] = { intensity * rgb[0],
		   intensity * rgb[1],
		   intensity * rgb[2],
		   1.0 };
  float pos[4] = { location[0], location[1], location[2], 1.0 };

  // Find an unused light, give up if none left.
  int i;
  for (i=0; i<MAX_LIGHTS; ++i)
    if (d_lightInfo[i].lightType == LIGHT_UNUSED)
      break;
  if (i == MAX_LIGHTS)
    return 0;
  
  d_lightInfo[i].lightType = LIGHT_POSITIONAL;
  d_lightInfo[i].location[0] = location[0];
  d_lightInfo[i].location[1] = location[1];
  d_lightInfo[i].location[2] = location[2];
  d_lightInfo[i].radiusSquared = radius * radius;

  GLenum light = (GLenum) (GL_LIGHT0 + i);

  // should be enabled/disabled per geometry based on distance & radius...
  glEnable(light);
  glLightfv(light, GL_AMBIENT, amb);
  glLightfv(light, GL_DIFFUSE, dif);
  glLightfv(light, GL_POSITION, pos);

  glLightf(light, GL_CONSTANT_ATTENUATION, attenuation[0]);
  glLightf(light, GL_LINEAR_ATTENUATION, attenuation[1]);
  glLightf(light, GL_QUADRATIC_ATTENUATION, attenuation[2]);

  // Disable old spot settings
  glLightf(light, GL_SPOT_CUTOFF, 180.0);
  glLightf(light, GL_SPOT_EXPONENT, 0.0);

  return 0;
}

// same comments as for PointLight apply here...
Viewer::Object ViewerOpenGL::insertSpotLight( float ambient,
					      const float attenuation[],
					      float beamWidth,
					      const float rgb[],
					      float cutOffAngle,
					      const float direction[],
					      float intensity,
					      const float location[],
					      float radius )
{
  float amb[4] = { ambient * rgb[0],
		   ambient * rgb[1],
		   ambient * rgb[2],
		   1.0 };
  float dif[4] = { intensity * rgb[0],
		   intensity * rgb[1],
		   intensity * rgb[2],
		   1.0 };
  float pos[4] = { location[0], location[1], location[2], 1.0 };


  // Find an unused light, give up if none left.
  int i;
  for (i=0; i<MAX_LIGHTS; ++i)
    if (d_lightInfo[i].lightType == LIGHT_UNUSED)
      break;
  if (i == MAX_LIGHTS)
    return 0;
  
  d_lightInfo[i].lightType = LIGHT_POSITIONAL;
  d_lightInfo[i].location[0] = location[0];
  d_lightInfo[i].location[1] = location[1];
  d_lightInfo[i].location[2] = location[2];
  d_lightInfo[i].radiusSquared = radius * radius;

  GLenum light = (GLenum) (GL_LIGHT0 + i);

  // should be enabled/disabled per geometry based on distance & radius...
  glEnable(light);
  glLightfv(light, GL_AMBIENT, amb);
  glLightfv(light, GL_DIFFUSE, dif);
  glLightfv(light, GL_POSITION, pos);

  glLightf(light, GL_CONSTANT_ATTENUATION, attenuation[0]);
  glLightf(light, GL_LINEAR_ATTENUATION, attenuation[1]);
  glLightf(light, GL_QUADRATIC_ATTENUATION, attenuation[2]);

  glLightfv(light, GL_SPOT_DIRECTION, direction);
  glLightf(light, GL_SPOT_CUTOFF, cutOffAngle * 180.0 / M_PI);
  // The exponential dropoff is not right/spec compliant...
  glLightf(light, GL_SPOT_EXPONENT, beamWidth < cutOffAngle ? 1.0 : 0.0);

  return 0;
}


// Lightweight copy

Viewer::Object ViewerOpenGL::insertReference(Object existingObject)
{
  glCallList( (GLuint) existingObject );
  return 0;
}

// Remove an object from the display list

void ViewerOpenGL::removeObject(Object key)
{
  glDeleteLists( (GLuint) key, 1 );
}


void ViewerOpenGL::enableLighting(bool lightsOn) 
{
  if (lightsOn)
    {
      if (d_lit) glEnable(GL_LIGHTING);
    }
  else
    glDisable(GL_LIGHTING);
}

// Set attributes

void ViewerOpenGL::setColor(float r, float g, float b, float a) 
{
  glColor4f(r,g,b,a);
}

void ViewerOpenGL::setFog(const float * color,
			  float   visibilityRange,
			  const char * fogType)
{
  GLfloat fogColor[4] = { color[0], color[1], color[2], 1.0 };
  GLint fogMode = (strcmp(fogType,"EXPONENTIAL") == 0) ? GL_EXP : GL_LINEAR;

  glEnable( GL_FOG );
  glFogf( GL_FOG_START, 1.5 );	// What should this be?...
  glFogf( GL_FOG_END, visibilityRange );
  glFogi( GL_FOG_MODE, fogMode );
  glFogfv( GL_FOG_COLOR, fogColor );

}

void ViewerOpenGL::setMaterial(float ambientIntensity,
			       const float diffuseColor[],
			       const float emissiveColor[],
			       float shininess,
			       const float specularColor[],
			       float transparency)
{
  float alpha = 1.0 - transparency;

  float ambient[4] = { ambientIntensity*diffuseColor[0],
		       ambientIntensity*diffuseColor[1],
		       ambientIntensity*diffuseColor[2],
		       alpha };
  float diffuse[4] = { diffuseColor[0],
		       diffuseColor[1],
		       diffuseColor[2],
		       alpha };
  float emission[4] = { emissiveColor[0],
			emissiveColor[1],
			emissiveColor[2],
			alpha };
  float specular[4] = { specularColor[0],
			specularColor[1],
			specularColor[2],
			alpha };

  // doesn't work right yet (need alpha render pass...)
  if (d_blend && ! FPZERO(transparency))
    glEnable(GL_BLEND);


  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT, ambient);
  glMaterialfv(GL_FRONT_AND_BACK, GL_DIFFUSE, diffuse);
  glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, shininess);
  glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, specular);

  if (ambientIntensity == 0. &&
      diffuse[0] == 0. && diffuse[1] == 0. && diffuse[2] == 0. &&
      specularColor[0] == 0. && specularColor[1] == 0. && specularColor[2] == 0.)
    {
      glDisable(GL_LIGHTING);
      glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, diffuse);
      glColor4fv( emission );
    }
  else
    {
      glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, emission);
      glColor4fv( diffuse );
    }
}


// This hack is necessary because setting the color mode needs to know
// about the appearance (presence & components of texture) and the geometry
// (presence of colors). Putting this stuff in either insertTexture or
// insert<geometry> causes problems when the texture or geometry node is
// USE'd with a different context.

void ViewerOpenGL::setMaterialMode (int textureComponents,
				    bool colors)
{
  if (textureComponents && d_texture && ! d_wireframe)
    {
      glEnable( GL_TEXTURE_2D );

      // This is a hack: if per-{face,vertex} colors are specified,
      // they take precedence over textures with GL_MODULATE. The
      // textures won't be lit this way but at least they show up...
      if (textureComponents > 2 && colors)
	glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE );
      else
	glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
    }
  else
    glDisable( GL_TEXTURE_2D );

  if (colors && textureComponents < 3 /* && lighting enabled... */ )
    glEnable( GL_COLOR_MATERIAL );
  else
    glDisable( GL_COLOR_MATERIAL );

}

void ViewerOpenGL::setSensitive(void *object)
{
  if (object)
    {
      // should make this dynamic...
      if (d_nSensitive == MAXSENSITIVE)
	{
	  theSystem->error("Internal Error: too many sensitive objects.\n");
	  return;
	}

      // push name, increment object count
      d_sensitiveObject[ d_nSensitive ] = object;
      glPushName( ++d_nSensitive ); // array index+1

    }

  else
    {
      glPopName( );
    }
}


// Scale an image to make sizes powers of two. This puts the data back
// into the memory pointed to by pixels, so there better be enough.

void ViewerOpenGL::scaleTexture(size_t w, size_t h,
				size_t newW, size_t newH,
				size_t nc,
				unsigned char* pixels)
{
  GLenum fmt[] = { GL_LUMINANCE,	// single component
		   GL_LUMINANCE_ALPHA,	// 2 components
		   GL_RGB,		// 3 components
		   GL_RGBA		// 4 components
  };

  unsigned char *newpix = new unsigned char[nc*newW*newH];

  glPixelStorei( GL_PACK_ALIGNMENT, 1 );
  glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
  if (0 == gluScaleImage( fmt[nc-1], w, h, GL_UNSIGNED_BYTE, pixels,
			  newW, newH, GL_UNSIGNED_BYTE, newpix))
    memcpy(pixels, newpix, nc*newW*newH);

  delete [] newpix;
}


//
// Pixels are lower left to upper right by row.
//

Viewer::TextureObject
ViewerOpenGL::insertTexture(size_t w, size_t h, size_t nc,
			    bool repeat_s,
			    bool repeat_t,
			    const unsigned char * pixels,
			    bool retainHint)
{
  GLenum fmt[] = { GL_LUMINANCE,	// single component
		   GL_LUMINANCE_ALPHA,	// 2 components
		   GL_RGB,		// 3 components
		   GL_RGBA		// 4 components
  };

  GLuint glid = 0;

  if (d_selectMode) return 0;

  // Enable blending if needed
  if (d_blend && (nc == 2 || nc == 4))
    glEnable(GL_BLEND);

#if USE_TEXTURE_DISPLAY_LISTS
  if (retainHint) glGenTextures(1, &glid);
  glBindTexture( GL_TEXTURE_2D, glid );
#endif

  // Texturing is enabled in setMaterialMode
  glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );

  glTexImage2D( GL_TEXTURE_2D, 0, nc, w, h, 0,
		fmt[nc-1], GL_UNSIGNED_BYTE, pixels);

  glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
		   repeat_s ? GL_REPEAT : GL_CLAMP );
  glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
		   repeat_t ? GL_REPEAT : GL_CLAMP );
  glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
  glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );

  return (TextureObject) glid;
}

void ViewerOpenGL::insertTextureReference(TextureObject t, int nc)
{
#if USE_TEXTURE_DISPLAY_LISTS
  // Enable blending if needed
  if (d_blend && (nc == 2 || nc == 4))
    glEnable(GL_BLEND);

  glBindTexture( GL_TEXTURE_2D, (GLuint) t );
#endif
}


void ViewerOpenGL::removeTextureObject(TextureObject t)
{
#if USE_TEXTURE_DISPLAY_LISTS
  GLuint glid = (GLuint) t;
  glDeleteTextures( 1, &glid );
#endif
}

// Texture coordinate transform
// Tc' = -C x S x R x C x T x Tc

void ViewerOpenGL::setTextureTransform(const float center[2],
				       float rotation,
				       const float scale[2],
				       const float translation[2]) {
  glMatrixMode(GL_TEXTURE);
  glLoadIdentity();

  if (center) glTranslatef(-center[0], -center[1], 0.0);
  if (scale) glScalef(scale[0], scale[1], 1.0);
  if (rotation != 0.0)
    glRotatef(rotation * 180.0 / M_PI, 0.0, 0.0, 1.0);

  if (center) glTranslatef(center[0], center[1], 0.0);
  if (translation) glTranslatef(translation[0], translation[1], 0.0);

  glMatrixMode(GL_MODELVIEW);
}

// Transforms
// P' = T x C x R x SR x S x -SR x -C x P

void ViewerOpenGL::setTransform(const float center[3],
				const float rotation[4],
				const float scale[3],
				const float scaleOrientation[4],
				const float translation[3]) 
{
  //cout << "ViewerOpenGL::setTransform()" << endl;
  glTranslatef(translation[0], translation[1], translation[2]);
  glTranslatef(center[0], center[1], center[2]);

  if (! FPZERO(rotation[3]) )
    glRotatef(rotation[3] * 180.0 / M_PI,
	      rotation[0],
	      rotation[1],
	      rotation[2]);

  if (! FPEQUAL(scale[0], 1.0) ||
      ! FPEQUAL(scale[1], 1.0) ||
      ! FPEQUAL(scale[2], 1.0) )
    {
      if (! FPZERO(scaleOrientation[3]) )
	glRotatef(scaleOrientation[3] * 180.0 / M_PI,
		  scaleOrientation[0],
		  scaleOrientation[1],
		  scaleOrientation[2]);

      glScalef(scale[0], scale[1], scale[2]);

      if (! FPZERO(scaleOrientation[3]) )
	glRotatef(-scaleOrientation[3] * 180.0 / M_PI,
		  scaleOrientation[0],
		  scaleOrientation[1],
		  scaleOrientation[2]);
    }

  glTranslatef(-center[0], -center[1], -center[2]);
}

// I used to just do a glPushMatrix()/glPopMatrix() in beginObject()/endObject().
// This is a hack to work around the glPushMatrix() limit (32 deep on Mesa).
// It has some ugly disadvantages: it is slower and the resulting transform
// after a setTransform/unsetTransform may not be identical to the original.
// It might be better to just build our own matrix stack...

void ViewerOpenGL::unsetTransform(const float center[3],
				  const float rotation[4],
				  const float scale[3],
				  const float scaleOrientation[4],
				  const float translation[3])
{
  glTranslatef(center[0], center[1], center[2]);

  if (! FPEQUAL(scale[0], 1.0) ||
      ! FPEQUAL(scale[1], 1.0) ||
      ! FPEQUAL(scale[2], 1.0) )
    {
      if (! FPZERO(scaleOrientation[3]) )
	glRotatef(scaleOrientation[3] * 180.0 / M_PI,
		  scaleOrientation[0],
		  scaleOrientation[1],
		  scaleOrientation[2]);

      glScalef(1.0/scale[0], 1.0/scale[1], 1.0/scale[2]);

      if (! FPZERO(scaleOrientation[3]) )
	glRotatef(-scaleOrientation[3] * 180.0 / M_PI,
		  scaleOrientation[0],
		  scaleOrientation[1],
		  scaleOrientation[2]);
    }

  if (! FPZERO(rotation[3]) )
    glRotatef(- rotation[3] * 180.0 / M_PI,
	      rotation[0],
	      rotation[1],
	      rotation[2]);

  glTranslatef(-center[0], -center[1], -center[2]);
  glTranslatef(-translation[0], -translation[1], -translation[2]);
}

// The matrix gets popped at endObject() - Not anymore. I added
// an explicit unsetBillboardTransform to work around the matrix
// depth limit of 32 in mesa. Now the limit only applies to 
// nested billboards.

void ViewerOpenGL::setBillboardTransform(const float axisOfRotation[3]) 
{
// Following code is modified by S. K. Bose March 02/2000 to fix the Billboardtransform
// according to the specification.

  float M[4][4];
  getBillboardTransformMatrix(M, axisOfRotation);

  glPushMatrix();
  glMultMatrixf(&M[0][0]);

}

void ViewerOpenGL::unsetBillboardTransform(const float axisOfRotation[3])
{
  glPopMatrix();
}

void ViewerOpenGL::setViewpoint(const float *position,
				float *orientation,
				float fieldOfView,
				float avatarSize,
				float visibilityLimit)
{
  //cout << "ViewerOpenGL::setViewpoint()" << endl;
  //cout << "  p:(" << position[0] << ","<< position[1] << ","<< position[2] << ")" << endl;

  glMatrixMode( GL_PROJECTION );
  if (! d_selectMode) glLoadIdentity();

  float field_of_view = fieldOfView * 180.0 / M_PI;
  float aspect = ((float) d_winWidth) / d_winHeight;
  float znear = (avatarSize > 0.0) ? (0.5 * avatarSize) : 0.01;
  float zfar = (visibilityLimit > 0.0) ? visibilityLimit : 30000.0;
  gluPerspective(field_of_view, aspect, znear, zfar);

  VrmlFrustum frust(field_of_view, aspect, znear, zfar);
  this->setFrustum(frust);
  //frust.dump(cout) << endl;

  glMatrixMode(GL_MODELVIEW);

  // Guess some distance along the sight line to use as a target...
  float d = 10.0 * avatarSize;
  if (d < znear || d > zfar) d = 0.2 * (avatarSize + zfar);

  float target[3], up[3];
  computeView(position, orientation, d, target, up);

  // Save position for use when drawing background
  d_position[0] = position[0] + d_translatex;
  d_position[1] = position[1] + d_translatey;
  d_position[2] = position[2] + d_translatez;

  //d_orientation[0] = orientation[0];
  //d_orientation[1] = orientation[1];
  //d_orientation[2] = orientation[2];
  //d_orientation[3] = orientation[3];
  
  gluLookAt(position[0], position[1], position[2],
	    target[0]+d_target[0], target[1]+d_target[1], target[2]+d_target[2],
	    up[0], up[1], up[2]);

  //cout << "ViewerOpenGL::setViewpoint():match:" << endl;
  //GLfloat actual_gl_mv[16];
  //glGetFloatv(GL_MODELVIEW_MATRIX, actual_gl_mv);
  //for(int i=0; i<4; i++) {
  //for(int j=0; j<4; j++)
  //cout << actual_gl_mv[i+j*4] << " ";
  //cout << endl;
  //}
  //cout << endl;

#if 0
  double MV[4][4];
  this->getUserNavigation(MV);
  float OGL_MV[16];
  matrix_to_glmatrix(MV, OGL_MV);
  glLoadMatrixf(OGL_MV);
#endif 

  // View modifiers are applied in first beginObject
  if (d_rotationChanged)
  {
    build_rotmatrix(d_rotationMatrix, d_curquat);
    d_rotationChanged = false;
  }
}


// The viewer knows the current viewpoint

void ViewerOpenGL::transformPoints(int np, float *p)
{
  float m[16];
  glGetFloatv (GL_MODELVIEW_MATRIX, m);
  
  float x, y, z;
  for (int i=0; i<np; ++i)
    {
      x = m[0]*p[0] + m[4]*p[1] + m[8]*p[2] + m[12];
      y = m[1]*p[0] + m[5]*p[1] + m[9]*p[2] + m[13];
      z = m[2]*p[0] + m[6]*p[1] + m[10]*p[2] + m[14];
      
      p[0] = x;
      p[1] = y;
      p[2] = z;
      p += 3;
    }

}

//
//  Viewer callbacks (called from window system specific functions)
//

// update is called from a timer callback and from checkSensitive
void ViewerOpenGL::update( double timeNow )
{

  if (d_scene->update( timeNow ))
    {
      checkErrors("update");
      wsPostRedraw();
    }

  // Set an alarm clock for the next update time.
  wsSetTimer( d_scene->getDelta() );
}


void ViewerOpenGL::redraw() 
{
  if (! d_GLinitialized) initialize();

  double start = theSystem->time();


  glDisable( GL_FOG );		// this is a global attribute
  glDisable( GL_TEXTURE_2D );

  glEnable( GL_CULL_FACE );
  glFrontFace( GL_CCW );
  glCullFace( GL_BACK );

  if (d_lit) glEnable( GL_LIGHTING );
  glDisable( GL_COLOR_MATERIAL );

  glDisable(GL_BLEND);

  glShadeModel( GL_SMOOTH );

  d_nObjects = 0;
  d_nestedObjects = 0;

  d_nSensitive = 0;

  // Clean out any defined lights
  for (int i=0; i<MAX_LIGHTS; ++i)
    {
      d_lightInfo[i].lightType = LIGHT_UNUSED;
      GLenum light = (GLenum) (GL_LIGHT0 + i);
      glDisable( light );
    }

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  d_scene->render(this);

  if (d_reportFPS)
    {
      if (d_background[0]+d_background[1]+d_background[2] > 2.0)
	glColor3f(0.0, 0.0, 0.0);
      else
	glColor3f(1.0, 1.0, 1.0);

      // Report average of last 2 frame times (can't report this
      // frame time because we want to count swapBuffers time).
      int fper = (int) (1.0 / (0.5 * (d_renderTime + d_renderTime1)));
      char buf[30];
      sprintf(buf, "%d f/s", fper);

      text2( 5, 5, 20.0, buf );
    }

  wsSwapBuffers();

  d_renderTime1 = d_renderTime;
  d_renderTime = theSystem->time() - start;
}

void ViewerOpenGL::resize(int width, int height) 
{
  if (width < 2) width = 2;
  if (height < 2) height = 2;
  glViewport(0, 0, width, height);
  d_winWidth = width;
  d_winHeight = height;
}


void ViewerOpenGL::input( EventInfo *e )
{
  switch (e->event)
    {
    case EVENT_KEY_DOWN:
      handleKey( e->what ); break;
    case EVENT_MOUSE_MOVE:
      (void) checkSensitive( e->x, e->y, EVENT_MOUSE_MOVE ); break;
    case EVENT_MOUSE_CLICK:
    case EVENT_MOUSE_RELEASE:
      handleButton( e ); break;
    case EVENT_MOUSE_DRAG:
      handleMouseDrag( e->x, e->y ); break;
    }
}

void
ViewerOpenGL::rot(float x, float y, float z, float a)
{
  double rot_mat[4][4];
  float rot_vec[4];
  rot_vec[0] = x;
  rot_vec[1] = y;
  rot_vec[2] = z;
  rot_vec[3] = a*0.01;
  Mrotation(rot_mat, rot_vec);

  double curr_rot_mat[4][4];
  int i,j;
  for(i=0; i<4; i++) // oh good grief.
    for(j=0; j<4; j++)
      curr_rot_mat[i][j] = d_rotationMatrix[j][i];

  double new_rot_mat[4][4];
  MM(new_rot_mat, curr_rot_mat, rot_mat);

  for(i=0; i<4; i++) // oh good grief.
    for(j=0; j<4; j++)
      d_rotationMatrix[i][j] = new_rot_mat[j][i];

  wsPostRedraw();
}


void ViewerOpenGL::step( float x, float y, float z )
{
  //cout << "ViewerOpenGL::step(" << x << "," << y << "," << z << ")" << endl;

#if 1

  double rot_mat[4][4];
  for(int i=0; i<4; i++) // oh good grief.
    for(int j=0; j<4; j++)
      rot_mat[i][j] = d_rotationMatrix[i][j];

  float vx[3];
  vx[0] = x;
  vx[1] = y;
  vx[2] = z;
  Vnorm(vx);
  Vscale(vx, 0.5);
  
  float d[3];
  VM(d, rot_mat, vx);

  //cout << "(" << d[0] << "," << d[1] << "," << d[2] << ")" << endl;

  this->d_translatex += d[0];
  this->d_translatey += d[1];
  this->d_translatez += d[2];
#endif

#if 0
  GLint viewport[4];
  GLdouble modelview[16], projection[16];
  glGetIntegerv (GL_VIEWPORT, viewport);
  glGetDoublev (GL_MODELVIEW_MATRIX, modelview);
  glGetDoublev (GL_PROJECTION_MATRIX, projection);

  GLdouble ox, oy, oz;
  gluUnProject( 0.0, 0.0, 0.0, modelview, projection, viewport,
		&ox, &oy, &oz);
  GLdouble dx, dy, dz;
  gluUnProject( 100.*x, 100.*y, z, modelview, projection, viewport,
		&dx, &dy, &dz);

  dx -= ox; dy -= oy; dz -= oz;
  double d = dx * dx + dy * dy + dz * dz;
  if (FPZERO(d)) return;

  d = sqrt(d);
  VrmlNodeNavigationInfo *nav = d_scene->bindableNavigationInfoTop();
  float speed = 1.0;
  if (nav) speed = nav->speed();

  d = speed / d;
  dx *= d; dy *= d; dz *= d;
  if (FPZERO(d)) return;
  
  d_translatex += dx;
  d_translatey += dy;
  d_translatez += dz;
#endif

  wsPostRedraw();
}

void ViewerOpenGL::handleKey(int key)
{
  switch (key)
    {
    case KEY_LEFT: // look left
      rot(0, 1, 0, -1);
      break;

    case KEY_RIGHT: // look right
      rot(0, 1, 0, 1);
      break;

    case KEY_UP:  // move forward along line of sight
      step(0, 0, 1);
      break;

    case KEY_DOWN: // move backwards along line of sight
      step(0, 0, -1);
      break;

    case 'a':  // look up
      rot(1, 0, 0, -1);
      break;

    case 'z':  // look down
      rot(1, 0, 0, 1);
      break;

    case 'A':  // translate up
      d_translatey-=0.5;
      wsPostRedraw();
      break;

    case 'Z':  // translate down
      d_translatey+=0.5;
      wsPostRedraw();
      break;

    case ',':			// Look left
      //trackball(d_lastquat, 0.65, 0.0, 0.45, 0.0);
      //add_quats(d_lastquat, d_curquat, d_curquat);
      //d_rotationChanged = true;
      d_target[0] -= 1.0;
      wsPostRedraw();
      break;

    case '.':			// Look right
      //trackball(d_lastquat, 0.45, 0.0, 0.65, 0.0);
      //add_quats(d_lastquat, d_curquat, d_curquat);
      //d_rotationChanged = true;
      d_target[0] += 1.0;
      wsPostRedraw();
      break;

    case KEY_PAGE_DOWN:
      if (d_scene) d_scene->nextViewpoint(); wsPostRedraw(); break;

    case KEY_PAGE_UP:
      if (d_scene) d_scene->prevViewpoint(); wsPostRedraw(); break;

    case '/':			// Frames/second
      d_reportFPS = ! d_reportFPS;
      wsPostRedraw();
      break;

    case 'b':
      d_blend = ! d_blend;
      wsPostRedraw();
      theSystem->inform(" Alpha blending %sabled.",
		     d_blend ? "en" : "dis");
      break;
	
    case 'd':
      d_drawBSpheres = ! d_drawBSpheres;
      theSystem->inform(" bspheres %sabled.", d_drawBSpheres ? "en" : "dis");
      wsPostRedraw();
      break;

    case 'c':
      d_cull = ! d_cull;
      theSystem->inform(" culling %sabled.", d_cull ? "en" : "dis");
      wsPostRedraw();
      break;
	
    case 'l':
      d_lit = ! d_lit;
      theSystem->inform(" Lighting %sabled.", d_lit ? "en" : "dis");
      wsPostRedraw();
      break;

    case KEY_HOME:
    case 'r':			// Reset view
      resetUserNavigation();
      break;
	
    case 't':
      d_texture = ! d_texture;
      wsPostRedraw();
      theSystem->inform(" Texture mapping %sabled.",
		     d_texture ? "en" : "dis");
      break;
	
    case 'w':			// Wireframe (should disable texturing)
      d_wireframe = ! d_wireframe;
      glPolygonMode(GL_FRONT_AND_BACK, d_wireframe ? GL_LINE : GL_FILL);
      wsPostRedraw();
      theSystem->inform(" Drawing polygons in %s mode.", d_wireframe ? "wireframe" : "filled");
      break;
#ifndef macintosh
    case 'q':
      d_scene->destroyWorld();	// may not return
      break;
#endif
    default:
      break;
    }
}

// Mouse button up/down

void ViewerOpenGL::handleButton( EventInfo *e)
{
  d_rotating = d_scaling = d_translating = false;

  // Check for a sensitive object first
  if (e->what == 0 &&
      checkSensitive( e->x, e->y, e->event ) )
    return;

  d_activeSensitive = 0;

  // Nothing under the mouse
  if (e->event == EVENT_MOUSE_RELEASE)
    wsSetCursor( CURSOR_INHERIT );
  else
    switch (e->what)		// button
      {
      case 0:
	wsSetCursor( CURSOR_CYCLE );
	d_rotating = true;
	d_beginx = e->x;
	d_beginy = e->y;
	break;
      
      case 1:
	wsSetCursor( CURSOR_UP_DOWN );
	d_scaling = true;
	d_beginx = e->x;
	d_beginy = e->y;
	break;
      
      case 2:
	wsSetCursor( CURSOR_CROSSHAIR );
	d_translating = true;
	d_beginx = e->x;
	d_beginy = e->y;
	break;
      }
}


// Mouse movement with button down

void ViewerOpenGL::handleMouseDrag(int x, int y)
{
  if (d_activeSensitive)
    {
      (void) checkSensitive( x, y, EVENT_MOUSE_DRAG );
    }
  else if (d_rotating)
    {
      trackball(d_lastquat,
		(2.0 * d_beginx - d_winWidth) / d_winWidth,
		(d_winHeight - 2.0 * d_beginy) / d_winHeight,
		(2.0 * x - d_winWidth) / d_winWidth,
		(d_winHeight - 2.0 * y) / d_winHeight);
      d_beginx = x;
      d_beginy = y;
      add_quats(d_lastquat, d_curquat, d_curquat);
      d_rotationChanged = true;
      wsPostRedraw();
    }
  // This is not scaling, it is now moving in screen Z coords
  else if (d_scaling)
    {
      //d_scale = d_scale * (1.0 - (((float) (d_beginy - y)) / d_winHeight));
      //d_translatez += 2.0 * ((float) (d_beginy - y)) / d_winHeight;
      step( 0.0, 0.0, 0.2 * ((float) (d_beginy - y)) / d_winHeight );
      d_beginx = x;
      d_beginy = y;
      wsPostRedraw();
    }
  else if (d_translating)
    {
      //d_translatex += 2.0 * ((float) (x - d_beginx)) / d_winWidth;
      //d_translatey += 2.0 * ((float) (d_beginy - y)) / d_winHeight;
      step( 0.2 * ((float) (x - d_beginx)) / d_winWidth,
	    0.2 * ((float) (d_beginy - y)) / d_winHeight,
	    0.0 );
      d_beginx = x;
      d_beginy = y;
      wsPostRedraw();
    }

}


// Check for pickable objects.

bool ViewerOpenGL::checkSensitive(int x, int y, EventType mouseEvent )
{
  double timeNow = theSystem->time();
  GLint viewport[4];
  glGetIntegerv( GL_VIEWPORT, viewport );

  GLuint selectBuffer[ 4*MAXSENSITIVE ];
  glSelectBuffer( 4*MAXSENSITIVE, selectBuffer );

  (void) glRenderMode( GL_SELECT );
  d_selectMode = true;

  glInitNames();
  glPushName(0);

  glMatrixMode( GL_PROJECTION );
  glLoadIdentity();
  gluPickMatrix( (GLdouble) x, (GLdouble)(viewport[3] - y),
		 2.0, 2.0, viewport );

  // Set up the global attributes
  glDisable( GL_FOG );
  glDisable( GL_TEXTURE_2D );

  glFrontFace( GL_CCW );
  glEnable( GL_CULL_FACE );
  glCullFace( GL_BACK );

  glDisable( GL_LIGHTING );
  glDisable( GL_COLOR_MATERIAL );

  glShadeModel( GL_FLAT );

  d_nObjects = 0;
  d_nestedObjects = 0;

  d_nSensitive = 0;

  // Clean out any defined lights
  for (int i=0; i<MAX_LIGHTS; ++i)
    {
      d_lightInfo[i].lightType = LIGHT_UNUSED;
      GLenum light = (GLenum) (GL_LIGHT0 + i);
      glDisable( light );
    }

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  d_scene->render( this );

  d_selectMode = false;

  // select closest hit
  GLuint closest = 0, minz = 0xffffffff;
      
  int selected = 0;		// nothing selected
  int hits = glRenderMode( GL_RENDER );
  if (hits > 0)
    {
      // selectBuffer[] = { N1, z1, z2, name1, ..., nameN1, N2, ... }
      GLuint *sel = selectBuffer;

      for (int nh=0; nh<hits; ++nh, sel+=(3+sel[0]))
	{
	  if (sel[1] <= minz)
	    {
	      minz = sel[1];		// z1
	      closest = sel[2+sel[0]];  // nameN (most deeply nested)
	    }
	}

      if (closest > 0 && closest <= (GLuint)d_nSensitive)
	{
	  selected = closest;
	}
    }

  wsSetCursor( selected ? CURSOR_INFO : CURSOR_INHERIT );

  // Compute & store the world coords of the pick if something
  // was already active or is about to become active. The window
  // Z coord is retained when a drag is started on a sensitive
  // so the drag stays in the same plane even if the mouse moves
  // off the original sensitive object.

  double selectCoord[3] = { 0.0, 0.0, 0.0 };

  if (d_activeSensitive || selected)
    {
      if (! d_activeSensitive)
	d_selectZ = minz / (double)0xffffffff;

      GLint viewport[4];
      GLdouble modelview[16], projection[16];
      glGetIntegerv (GL_VIEWPORT, viewport);
      glGetDoublev (GL_PROJECTION_MATRIX, projection);
      glGetDoublev (GL_MODELVIEW_MATRIX, modelview);

      GLdouble dx, dy, dz;
      gluUnProject( (GLdouble) x, (GLdouble) (viewport[3] - y), d_selectZ,
		    modelview, projection, viewport,
		    &dx, &dy, &dz);

      selectCoord[0] = dx;
      selectCoord[1] = dy;
      selectCoord[2] = dz;
    }

  bool wasActive = false;

  // Sanity check. This can happen when the world gets replaced
  // by clicking on an anchor - the current sensitive object goes
  // away, but these variables are not reset.
  if ( d_activeSensitive > d_nSensitive )
    d_activeSensitive = 0;
  if ( d_overSensitive > d_nSensitive )
    d_overSensitive = 0;

  // An active sensitive object "grabs" the mouse until button released
  if (d_activeSensitive)
    {
      if (mouseEvent == EVENT_MOUSE_RELEASE ||
	  mouseEvent == EVENT_MOUSE_MOVE)
	{
	  d_scene->sensitiveEvent( d_sensitiveObject[ d_activeSensitive-1 ],
				   timeNow,
				   selected == d_activeSensitive, false,
				   selectCoord );
	  d_activeSensitive = 0;
	}
      else			// _DRAG
	{
	  d_scene->sensitiveEvent( d_sensitiveObject[ d_activeSensitive-1 ],
				   timeNow,
				   selected == d_activeSensitive, true,
				   selectCoord );
	}
      wasActive = true;
    }

  // A click down over a sensitive object initiates an active grab and
  // mouse over events are no longer relevant.
  else if (mouseEvent == EVENT_MOUSE_CLICK && selected != 0)
    {
      if (d_overSensitive && d_overSensitive != selected)
	{
	  d_scene->sensitiveEvent( d_sensitiveObject[ d_overSensitive-1 ],
				   timeNow,
				   false, false, // isOver, isActive
				   selectCoord );
	  d_overSensitive = 0;
	}
      d_activeSensitive = selected;
      d_scene->sensitiveEvent( d_sensitiveObject[ d_activeSensitive-1 ],
			       timeNow,
			       true, true,  // isOver, isActive
			       selectCoord );
    }

  // Handle isOver events (coords are bogus)
  else if (mouseEvent == EVENT_MOUSE_MOVE)
    {
      if (d_overSensitive && d_overSensitive != selected)
	{
	  d_scene->sensitiveEvent( d_sensitiveObject[ d_overSensitive-1 ],
				   timeNow,
				   false, false, // isOver, isActive
				   selectCoord );
	}
      d_overSensitive = selected;
      if (d_overSensitive)
	d_scene->sensitiveEvent( d_sensitiveObject[ d_overSensitive-1 ],
				 timeNow,
				 true, false,  // isOver, isActive
				 selectCoord );
    }


  // Was event handled here?
  if (d_activeSensitive || wasActive)
    update( timeNow );

  // Everything is handled except down clicks where nothing was selected
  // and up clicks where nothing was active.
  return d_activeSensitive || wasActive;
}

// Text rendering

/* From smooth.c by Nate Robins, 1997 in the glut dist */
# include <GL/glut.h>

/* text: general purpose text routine.  draws a string according to
 * format in a stroke font at x, y after scaling it by the scale
 * specified (scale is in window-space (lower-left origin) pixels).  
 *
 * x      - position in x (in window-space)
 * y      - position in y (in window-space)
 * scale  - scale in pixels
 * text   - text string to render
 */

void ViewerOpenGL::text2( int x, int y, float scale, char* text )
{

  GLfloat font_scale = scale / (119.05 + 33.33); // ???

  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  gluOrtho2D(0, d_winWidth, 0, d_winHeight);

  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  glPushAttrib(GL_ENABLE_BIT);
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);
  glTranslatef(x, y, 0.0);

  glScalef(font_scale, font_scale, font_scale);

  for(char *p = text; *p; ++p)
    glutStrokeCharacter(GLUT_STROKE_ROMAN, *p);

  glPopAttrib();

  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
}

void ViewerOpenGL::text3(int *justify, float size, int n, const char * const *s)
{
  float font_scale = 0.005 * size;

  glPushAttrib(GL_ENABLE_BIT);
  glDisable(GL_LIGHTING);
  glDisable(GL_TEXTURE_2D);

  GLfloat x = 0.0, y = 0.0;

  if (justify[0] < 0)
    x -= strlen(s[0]);
  else if (justify[0] == 0)
    x -= 0.5 * strlen(s[0]);

  float font_height = 1.0;
  for (int i=0; i<n; ++i, y-=font_height)
    {
      const char *textLine = s[i];
      if ( textLine )
	{
	  glPushMatrix();
	  glTranslatef(x, y, 0.0);
	  glScalef(font_scale, font_scale, font_scale);
	  while (*textLine)
	    glutStrokeCharacter(GLUT_STROKE_ROMAN, *textLine++);
	  glPopMatrix();
	}
    }

  glPopAttrib();

}


static GLfloat green[] = {0.25f, 1.0f, 0.25f, 1.0f};
static GLfloat red[] = {1.0f, 0.5f, 0.5f, 1.0f};
static GLfloat grey[] = {0.5f, 0.5f, 0.5f, 1.0f};


// argh: see <URL:http://www.debian.org/Bugs/db/44/44489-b.html> for
// an explanation. quick version: mesa doesn't work right with newver
// c++ compilers (c++ has recently become more strict about
// enums). should this go into configure.in? probably, since it might
// occur outside this file. the problem should go away with the next
// version of mesa.. -cks
//
#ifdef MESA
#if (GLU_VERSION_1_1 || GLU_VERSION_1_2)
#define MESA_GLU_ENUM_FIX (GLenum)
#else
#define MESA_GLU_ENUM_FIX
#endif
#else
#define MESA_GLU_ENUM_FIX
#endif


void
ViewerOpenGL::drawBSphere(const VrmlBSphere& bs, int flag)
{
  if (!d_drawBSpheres) return;
  //cout << "ViewerOpenGL::insertBSphere(" << bs.getRadius() << ")" << endl;
  if (bs.isMAX()) return;
  if (bs.getRadius()==-1.0) return;
  //bs.dump(cout);
  glEnable(GL_CULL_FACE);
  glCullFace(GL_BACK);
  //glEnable(GL_LIGHTING);
  glShadeModel(GL_FLAT);
  GLUquadricObj* sph = (GLUquadricObj*)0;
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  const float* c = bs.getCenter();
  glTranslatef(c[0], c[1], c[2]);
  sph = gluNewQuadric();
  if (flag == VrmlBVolume::BV_OUTSIDE) {
    //cout << "out" << endl;
    //glDisable(GL_LIGHTING);
    //glEnable(GL_LIGHTING);
    gluQuadricDrawStyle(sph, MESA_GLU_ENUM_FIX GLU_LINE);
    //gluQuadricDrawStyle(sph, GLU_POINT);
    glColor3f(0.5, 0.5, 0.5);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, grey);
    gluSphere(sph, bs.getRadius(), 5, 5);
  } else if (flag == VrmlBVolume::BV_PARTIAL) {
    //cout << "par" << endl;
    //glEnable(GL_LIGHTING);
    gluQuadricNormals(sph, MESA_GLU_ENUM_FIX GLU_SMOOTH);
    //gluQuadricDrawStyle(sph, GLU_FILL);
    gluQuadricDrawStyle(sph, MESA_GLU_ENUM_FIX GLU_LINE);
    glColor3f(0.25, 1.0, 0.25);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, green);
    gluSphere(sph, bs.getRadius(), 8, 8);
  } else if (flag == VrmlBVolume::BV_INSIDE) {
    //cout << "ins" << endl;
    //glEnable(GL_LIGHTING);
    gluQuadricNormals(sph, MESA_GLU_ENUM_FIX GLU_SMOOTH);
    //gluQuadricDrawStyle(sph, GLU_FILL);
    gluQuadricDrawStyle(sph, MESA_GLU_ENUM_FIX GLU_LINE);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, red);
    glColor3f(1.0, 0.5, 0.5);
    gluSphere(sph, bs.getRadius(), 8, 8);
  } else {
    //cout << "oth" << endl;
    //glEnable(GL_LIGHTING);
    gluQuadricNormals(sph, MESA_GLU_ENUM_FIX GLU_SMOOTH);
    gluQuadricDrawStyle(sph, MESA_GLU_ENUM_FIX GLU_LINE);
    glMaterialfv(GL_FRONT, GL_DIFFUSE, grey);
    glColor3f(0.5, 0.5, 0.5);
    gluSphere(sph, bs.getRadius(), 8, 8);
  }
  gluDeleteQuadric(sph);

  glPopMatrix();
  
}
