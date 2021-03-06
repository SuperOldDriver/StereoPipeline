// __BEGIN_LICENSE__
// Copyright (C) 2006-2011 United States Government as represented by
// the Administrator of the National Aeronautics and Space Administration.
// All Rights Reserved.
// __END_LICENSE__


// ===========================================================================
// The polygon rasterization code is based on s_pgdraw.c and
// s_pgspan.c from the OpenGL reference implementation
// ===========================================================================


// ===========================================================================
// NOTE: although within a pixel, we currently don't get results
// identical to NVIDIA's OpenGL implementation OpenGL version. This
// may be a difference between the definitions of the pixel center
// (NVIDIA may use the 0.5 pixel offset). We also may be using a
// subtly different transform to the final window coords. Should check
// againt __glUpdateViewport and __glUpdateViewportTransform in
// s_xform.c. Also code in __glim_Ortho. -- LJE
// ===========================================================================

// ===========================================================================
// According to Levoy:
//
//     http://graphics.stanford.edu/courses/cs248-01/scan/scan1.html
//
// SGI "uses a DDA with fixed-point arithmetic. log2N bits of
// sub-pixel precision for an N-pixel line". This seems to be similar
// to the approach taken here for polygon rasterization.
// ===========================================================================

#include <vw/Core/Exception.h>
#include <iostream>                        // debugging

#include <asp/Core/SoftwareRenderer.h>

using namespace std;
using namespace vw;
using namespace stereo;

enum { eShadeFlat = 0x0, eShadeSmooth = 0x1 };

// ===========================================================================
// Constants and enums
// ===========================================================================

// NOTE: double does *NOT* work!
// #define _VW_USE_DOUBLE_
#ifdef _VW_USE_DOUBLE_

#define __TWO_63 9223372036854775808.0
#define __FRACTION(result, f) result = (int) ((f) * __TWO_63)
#define SHIFT 63
typedef double real;

#else

#define __TWO_31 2147483648.0F
#define __FRACTION(result, f) result = (int) ((f) * __TWO_31)
#define SHIFT 31
typedef float real;

#endif

static const int kVerticesPerTriangle = 3;

// ===========================================================================
// Type declarations
// ===========================================================================

// Coordinate structure.  Coordinates contain x, y, z and w.
struct Coords
{
  Coords() {}
  Coords(float coords[2])
  {
    x = coords[0]; y = coords[1]; z = 0.0; w = 1.0;
  }
  real x, y, z, w;
};

// Color structure.  Colors are composed of red, green, blue and alpha.
struct Color
{
  Color() {}
  Color(float gray) { r = gray; g = 0.0; b = 0.0; a = 1.0; }
  Color(float color[], int numComponents)
  {
    switch (numComponents)
    {
    case 1:
      r = color[0]; g = 0.0; b = 0.0; a = 1.0;
      return;
    case 3:
      r = color[0]; g = color[1]; b = color[2]; a = 1.0;
      return;
    case 4:
      r = color[0]; g = color[1]; b = color[2]; a = color[3];
      return;
    }
  }
  real r, g, b, a;
};

// Interpolator record for color interpolators.  Used for primary and
// secondary colors;
struct ColorIterator
{
  real rLittle, gLittle, bLittle, aLittle;
  real rBig, gBig, bBig, aBig;
  real drdx, dgdx, dbdx, dadx;
  real drdy, dgdy, dbdy, dady;
};

// A fragment is a collection of all the data needed after
// rasterization of a primitive has occured, but before the data is
// entered into various framebuffers.  The data contained in the
// fragment has been normalized into a form for immediate storage into
// the framebuffer.

struct FragmentInfo
{
  FragmentInfo() {}
  int x, y;                                // Screen x, y
  // Colors of the fragment.  When in colorIndexMode only the r
  // component is valid.
  Color color;
};

// Shader record for iterated objects (lines/triangles).  This keeps
// track of all the various deltas needed to rasterize a triangle.
struct RasterInfo
{
  RasterInfo() {}
  int dxLeftLittle, dxLeftBig;
  int dxLeftFrac;
  int ixLeft, ixLeftFrac;

  int dxRightLittle, dxRightBig;
  int dxRightFrac;
  int ixRight, ixRightFrac;

  real area;
  real dxAC, dxBC, dyAC, dyBC;

  bool ccw;

  FragmentInfo frag;
  int length;

  ColorIterator  colorIter;

  unsigned int modeFlags;
};

struct GraphicsState
{
  GraphicsState() {}
  float *buffer;
  int numPixels;
  int width;
  int height;

  RasterInfo rasterInfo;
  Color currentFlatColor;
  // The smallest rectangle that is the intersection of the window clip
  // and the scissor clip.  If the scissor box is disabled then this
  // is just the window box. Note that the x0,y0 point is inside the
  // box but that the x1,y1 point is just outside the box.
  int clipX0, clipY0, clipX1, clipY1;
  // The viewport translated into offset window coordinates.  maxx and
  // maxy are one past the edge (an x coord is in if minx <= x <
  // maxx).
  int minx, miny, maxx, maxy;
};

struct Vertex
{
  Vertex(float initCoords[2], float initGray)
    : color(initGray), window(initCoords) {}
  Vertex(float initCoords[2], Color initColor)
    : color(initColor), window(initCoords) {}

  // Current face color in use.
  Color color;
  // Window coordinate. This field is filled in when the window clip
  // coordinate is converted to a drawing surface relative "window"
  // coordinate. NOTE: the window.w coordinate contains 1/clip.w.
  Coords window;
};


// ===========================================================================
// Functions
// ===========================================================================

inline real
TriangleArea(real dxAC, real dxBC, real dyAC, real dyBC)
{
  real area = dxAC * dyBC - dxBC * dyAC;
  return area;
}

static void
SortVertices(Vertex * &a, Vertex * &b, Vertex * &c)
{
  if (*(int *)&a->window.y < *(int *) &b->window.y)
  {
    if (*(int *)&b->window.y < *(int *) &c->window.y)      // Already sorted
    {
      return;
    }
    else
    {
      if (*(int *)&a->window.y < *(int *)&c->window.y)
      {
        Vertex *temp = b;
        b = c;
        c = temp;
      }
      else
      {
        Vertex *temp = a;
        a = c;
        c = b;
        b = temp;
      }
    }
  }
  else
  {
    if (*(int *)&b->window.y < *(int *)&c->window.y)
    {
      if (*(int *)&a->window.y < *(int *)&c->window.y)
      {
        Vertex *temp = a;
        a = b;
        b = temp;
      }
      else
      {
        Vertex *temp = a;
        a = b;
        b = c;
        c = temp;
      }
    }
    else
    {
      Vertex *temp = a;
      a = c;
      c = temp;
    }
   }
}

static void
SetInitialParameters(GraphicsState *gc, const Vertex *a, real dx, real dy)
{
  RasterInfo *rasterInfo = &gc->rasterInfo;
  real big = rasterInfo->dxLeftBig;
  real little = rasterInfo->dxLeftLittle;
  const Color *vertexColor;
  Color *fragColor;
  ColorIterator *iter;
  unsigned int modeFlags = rasterInfo->modeFlags;

  if (big > little)
  {
    if (modeFlags & eShadeSmooth)
    {
      vertexColor = &a->color;
      fragColor = &rasterInfo->frag.color;
      iter = &rasterInfo->colorIter;

      fragColor->r = vertexColor->r + dx*iter->drdx + dy*iter->drdy;
      iter->rLittle = iter->drdy + little * iter->drdx;
      iter->rBig = iter->rLittle + iter->drdx;
    }
  }
  else
  {
    if (modeFlags & eShadeSmooth)
    {
      vertexColor = &a->color;
      fragColor = &rasterInfo->frag.color;
      iter = &rasterInfo->colorIter;

      fragColor->r = vertexColor->r + dx*iter->drdx + dy*iter->drdy;
      iter->rLittle = iter->drdy + little * iter->drdx;
      iter->rBig = iter->rLittle - iter->drdx;
    }
  }
}


static void
DrawFlatGraySpan(GraphicsState *graphicsState)
{
  std::fill_n(&(graphicsState->buffer[graphicsState->rasterInfo.frag.y * graphicsState->width +
					graphicsState->rasterInfo.frag.x]),
	      graphicsState->rasterInfo.length,
	      float(graphicsState->rasterInfo.frag.color.r));
}

static void
DrawGraySpan(GraphicsState *graphicsState)
{
  float *span =
    &(graphicsState->buffer[graphicsState->rasterInfo.frag.y * graphicsState->width +
			    graphicsState->rasterInfo.frag.x]);

  real gray = graphicsState->rasterInfo.frag.color.r;
  real drdx = graphicsState->rasterInfo.colorIter.drdx;

  for (int i = graphicsState->rasterInfo.length; i > 0; --i) {
    *span++ = float(gray);
    gray += drdx;
  }
}

// In the SnapX* and FillSubTriangle routines, 1s31.1s31 fixed point
// arithmetic is used with the integer and fractional portions carried
// in separate ints (e.g., ixLeft and ixLeftFrac)
static void
SnapXLeft(GraphicsState *gc, real xLeft, real dxdyLeft)
{
  real little, dx;
  int ixLeft, ixLeftFrac, frac, ilittle, ibig;

  ixLeft = (int) xLeft;
  dx = xLeft - ixLeft;
  __FRACTION(ixLeftFrac, dx);

  // Is the following right? Seems like it should be ixLeft - (...) --LJE
  gc->rasterInfo.ixLeft = ixLeft + (((unsigned int) ixLeftFrac) >> SHIFT);
  gc->rasterInfo.ixLeftFrac = ixLeftFrac & ~0x80000000;

  // Compute big and little steps
  ilittle = (int) dxdyLeft;
  little = (real) ilittle;
  if (dxdyLeft < 0)
  {
    ibig = ilittle - 1;
    dx = little - dxdyLeft;
    // Here we know ilittle and ibig are not going to be zero, and
    // that frac will be negative, so we can just negate frac, to get
    // things in our standard form --LJE
    __FRACTION(frac, dx);
    gc->rasterInfo.dxLeftFrac = -frac;
  }
  else
  {
    ibig = ilittle + 1;
    dx = dxdyLeft - little;
    __FRACTION(frac, dx);
    gc->rasterInfo.dxLeftFrac = frac;
  }

  gc->rasterInfo.dxLeftLittle = ilittle;
  gc->rasterInfo.dxLeftBig = ibig;
}

static void
SnapXRight(RasterInfo *rasterInfo, real xRight, real dxdyRight)
{
  real little, big, dx;
  int ixRight, ixRightFrac, frac;

  ixRight = (int) xRight;
  dx = xRight - ixRight;
  __FRACTION(ixRightFrac, dx);

  // Is the following right? Seems like it should be ixRight - (...) -LJE
  rasterInfo->ixRight = ixRight + (((unsigned int) ixRightFrac) >> SHIFT);
  rasterInfo->ixRightFrac = ixRightFrac & ~0x80000000;

  // Compute big and little steps
  little = (real) ((int) dxdyRight);

  if (dxdyRight < 0)
  {
    big = little - 1;
    dx = little - dxdyRight;
    __FRACTION(frac,dx);
    rasterInfo->dxRightFrac = -frac;
  }
  else
  {
    big = little + 1;
    dx = dxdyRight - little;
    __FRACTION(frac,dx);
    rasterInfo->dxRightFrac = frac;
  }

  rasterInfo->dxRightLittle = (int) little;
  rasterInfo->dxRightBig = (int) big;
}

static void
FillSubTriangle(GraphicsState *gc,  int iyBottom, int iyTop)
{
  int ixLeft, ixRight;
  int ixLeftFrac, ixRightFrac;
  int dxLeftFrac, dxRightFrac;
  int dxLeftLittle, dxRightLittle;
  int dxLeftBig, dxRightBig;
  int spanWidth, clipY0, clipY1;
  unsigned int modeFlags;
  void (*processSpan)(GraphicsState *graphicsState);


  ixLeft = gc->rasterInfo.ixLeft;
  ixLeftFrac = gc->rasterInfo.ixLeftFrac;
  ixRight = gc->rasterInfo.ixRight;
  ixRightFrac = gc->rasterInfo.ixRightFrac;
  clipY0 = gc->clipY0;
  clipY1 = gc->clipY1;
  dxLeftFrac = gc->rasterInfo.dxLeftFrac;
  dxLeftBig = gc->rasterInfo.dxLeftBig;
  dxLeftLittle = gc->rasterInfo.dxLeftLittle;
  dxRightFrac = gc->rasterInfo.dxRightFrac;
  dxRightBig = gc->rasterInfo.dxRightBig;
  dxRightLittle = gc->rasterInfo.dxRightLittle;
  modeFlags = gc->rasterInfo.modeFlags;
  //  gc->rasterInfo.cfb = gc->drawBuffer;

  if (modeFlags & eShadeSmooth)
    processSpan = DrawGraySpan;
  else
    processSpan = DrawFlatGraySpan;

  while (iyBottom < iyTop)
  {
    spanWidth = ixRight - ixLeft;
    // Only render spans that have non-zero width and which are not
    // scissored out vertically.
    if ((spanWidth > 0) && (iyBottom >= clipY0) && (iyBottom < clipY1))
    {
      gc->rasterInfo.frag.x = ixLeft;
      gc->rasterInfo.frag.y = iyBottom;
      gc->rasterInfo.length = spanWidth;
      processSpan(gc);
    }

    // Advance right edge fixed point, adjusting for carry
    ixRightFrac += dxRightFrac;
    if (ixRightFrac < 0)             // Carry/Borrow'd. Use large step
    {
      ixRight += dxRightBig;
      ixRightFrac &= ~0x80000000;
    }
    else                                   // Use small step
    {
      ixRight += dxRightLittle;
    }

    iyBottom++;
    ixLeftFrac += dxLeftFrac;
    if (ixLeftFrac < 0)             // Carry/Borrow'd. Use large step
    {
      ixLeft += dxLeftBig;
      ixLeftFrac &= ~0x80000000;

      if (modeFlags & eShadeSmooth)
        gc->rasterInfo.frag.color.r += gc->rasterInfo.colorIter.rBig;
    }
    else                                   // Use small step
    {
      ixLeft += dxLeftLittle;
      if (modeFlags & eShadeSmooth)
        gc->rasterInfo.frag.color.r += gc->rasterInfo.colorIter.rLittle;
    }
  }
  gc->rasterInfo.ixLeft = ixLeft;
  gc->rasterInfo.ixLeftFrac = ixLeftFrac;
  gc->rasterInfo.ixRight = ixRight;
  gc->rasterInfo.ixRightFrac = ixRightFrac;
}

static void
FillTriangle(GraphicsState *gc, Vertex *a, Vertex *b, Vertex *c)
{
  real area, oneOverArea, t1, t2, t3, t4;
  real dxAC, dxBC, dyAC, dyBC;
  real dxAB, dyAB;
  real dx, dy, dxdyLeft, dxdyRight;
  Color *aColor, *bColor;
  int aIY, bIY, cIY;
  unsigned int modeFlags;
  bool ccw;                             // was a float for some reason

  // Sort vertices in y.
  SortVertices(a, b, c);

  // Set edge vectors
  dxAC = a->window.x - c->window.x;
  dxBC = b->window.x - c->window.x;
  dyAC = a->window.y - c->window.y;
  dyBC = b->window.y - c->window.y;

  // Compute signed area of the triangle
  area = TriangleArea(dxAC, dxBC, dyAC, dyBC);
  ccw = (area >= 0);   //  ccw = !(* (int *) & area >> 31);

  // Pre-compute one over polygon area
  oneOverArea = (area != 0) ? (1.0 / area) : 0.0;

  // Fetch some stuff we are going to reuse
  modeFlags = gc->rasterInfo.modeFlags;
  aColor = &a->color;                      // & -LJE
  bColor = &b->color;                      // & -LJE

  // Compute delta values for unit changes in x or y for each
  // parameter.
  t1 = dyAC * oneOverArea;
  t2 = dyBC * oneOverArea;
  t3 = dxAC * oneOverArea;
  t4 = dxBC * oneOverArea;

  if (modeFlags & eShadeSmooth)
  {
    real drAC, drBC;
    Color *cColor = &c->color;             // & -LJE

    // If gray scale the intensity is carried in the red component
    drAC = aColor->r - cColor->r;
    drBC = bColor->r - cColor->r;
    gc->rasterInfo.colorIter.drdx = drAC * t2 - drBC * t1;
    gc->rasterInfo.colorIter.drdy = drBC * t3 - drAC * t4;
  }
  else
  {
    Color *flatColor = &gc->currentFlatColor; // & -LJE
    gc->rasterInfo.frag.color.r = flatColor->r;
  }

  // Snap each y coordinate to its pixel center
  aIY = (int) (a->window.y);
  bIY = (int) (b->window.y);
  cIY = (int) (c->window.y);

  // This algorithim always fills from bottom to top, left to right.
  // Because of this, ccw triangles are inherently faster because the
  // parameter values need not be recomputed.
  dxAB = a->window.x - b->window.x;
  dyAB = a->window.y - b->window.y;

  if (ccw)
  {
    dxdyLeft = (dyAC != 0.0) ? dxAC / dyAC : 0.0;
    dy = (aIY + 1.0) - a->window.y;
    SnapXLeft(gc, a->window.x + dy*dxdyLeft, dxdyLeft);
    dx = (gc->rasterInfo.ixLeft + 1.0) - a->window.x;
    SetInitialParameters(gc, a, dx, dy);
    if (aIY != bIY)
    {
      dxdyRight = (dyAB != 0.0) ? dxAB / dyAB : 0.0;
      SnapXRight(&gc->rasterInfo, a->window.x + dy*dxdyRight, dxdyRight);
      FillSubTriangle(gc, aIY, bIY);
    }
    if (bIY != cIY)
    {
      dxdyRight = (dyBC != 0.0) ? dxBC / dyBC : 0.0;
      dy = (bIY + 1.0) - b->window.y;
      SnapXRight(&gc->rasterInfo, b->window.x + dy*dxdyRight, dxdyRight);
      FillSubTriangle(gc, bIY, cIY);
    }
  }
  else
  {
    dxdyRight = (dyAC != 0.0) ? dxAC / dyAC : 0.0;
    dy = (aIY + 1.0) - a->window.y;
    SnapXRight(&gc->rasterInfo, a->window.x + dy*dxdyRight, dxdyRight);
    if (aIY != bIY)
    {
      dxdyLeft = (dyAB != 0.0) ? dxAB / dyAB : 0.0;
      SnapXLeft(gc, a->window.x + dy*dxdyLeft, dxdyLeft);
      dx = (gc->rasterInfo.ixLeft + 1.0) - a->window.x;
      SetInitialParameters(gc, a, dx, dy);
      FillSubTriangle(gc, aIY, bIY);
    }
    if (bIY != cIY)
    {
      dxdyLeft = (dyBC != 0.0) ? dxBC / dyBC : 0.0;
      dy = (bIY + 1.0) - b->window.y;
      SnapXLeft(gc, b->window.x + dy*dxdyLeft, dxdyLeft);
      dx = (gc->rasterInfo.ixLeft + 1.0) - b->window.x;
      SetInitialParameters(gc, b, dx, dy);
      FillSubTriangle(gc, bIY, cIY);
    }
  }
}

inline void
MapToWindow(Coords &coords,
            const double ndcMap[3][2],
            double /*x0*/, double /*y0*/, double width, double height,
            Coords &result)
{
  // The separation of NDC and viewport mappings might seem silly
  // right now, but it allows one to modify the projection matrix
  // independently of the final map to window coords and vice
  // versa. Also if we ever do clipping it will be useful.

  // According to the glViewPort specification:
  // xw = (xNDC + 1)(width/2) + x
  // yw = (xNDC + 1)(height/2) + y
  double xNDC = ndcMap[0][0] * coords.x + ndcMap[2][0];
  double yNDC = ndcMap[1][1] * coords.y + ndcMap[2][1];

  result.x = 0.5 * (xNDC + 1.0) * width;
  result.y = 0.5 * (yNDC + 1.0) * height;
}


// ===========================================================================
// Class Member Functions
// ===========================================================================

SoftwareRenderer::SoftwareRenderer(const int width, const int height, float *buffer)
{
  m_numVertexComponents = 0;
  m_vertexPointer = 0;
  m_triangleVertexStep = 0;

  m_numColorComponents = 0;
  m_colorPointer = 0;
  m_triangleColorStep = 0;

  m_bufferWidth = width;
  m_bufferHeight = height;
  m_buffer = buffer;

  double deltaX = double(m_bufferWidth);
  double deltaY = double(m_bufferHeight);

  m_transformViewport[0][1] = m_transformViewport[1][0] = 0.0;
  // scaling
  m_transformViewport[0][0] = deltaX * 0.5;
  m_transformViewport[1][1] = deltaY * 0.5;
  // translation
  m_transformViewport[2][0] = deltaX * 0.5;
  m_transformViewport[2][1] = deltaY * 0.5;

  m_transformNDC[0][1] = m_transformNDC[1][0] = 0.0;
  // scaling
  m_transformNDC[0][0] = 2.0 / deltaX;
  m_transformNDC[1][1] = 2.0 / deltaY;
  // translation
  m_transformNDC[2][0] = -1.0;
  m_transformNDC[2][1] = -1.0;

  m_shadeMode = eShadeSmooth;
  m_currentFlatColor[0] = m_currentFlatColor[1] = m_currentFlatColor[2] = 0.0;

  // FIX ME!!! Put this state info in the software renderer class
  GraphicsState *graphicsState = new GraphicsState;
  graphicsState->buffer = m_buffer;
  graphicsState->currentFlatColor.r = m_currentFlatColor[0];
  graphicsState->width = m_bufferWidth;
  graphicsState->height = m_bufferHeight;
  graphicsState->rasterInfo.modeFlags = eShadeSmooth;
  graphicsState->minx = graphicsState->miny = 0;
  graphicsState->maxx = m_bufferWidth;
  graphicsState->maxy = m_bufferHeight;
  graphicsState->clipX0 = graphicsState->clipY0 = 0;
  graphicsState->clipX1 = m_bufferWidth;
  graphicsState->clipY1 = m_bufferHeight;
  m_graphicsState = graphicsState;
}

// Free up resources that are allocated in the constructor
SoftwareRenderer::~SoftwareRenderer() {

  if (m_graphicsState)
    delete static_cast<GraphicsState*>(m_graphicsState);

}

void
SoftwareRenderer::Ortho2D(const double left, const double right,
                          const double bottom, const double top)
{
  // This takes a 2D point to normalized coordinates (i.e, -1.0 <= x <= 1.0)
  //
  //   [2/(r-l)    0    -(r+l)/(r-l)]
  //   [   0    2/(t-b) -(t+b)/(t-b)]
  //   [   0       0         1     ]
  double deltax = right - left;
  double deltay = top - bottom;

  if ((deltax == 0.0) || (deltay == 0.0))
    vw_throw(LogicErr() << "SoftwareRenderer: Ortho2D failed.  Projection dimensions are zero.");

  m_transformNDC[0][1] = m_transformNDC[1][0] = 0.0;
  m_transformNDC[0][0] = 2.0 / deltax;
  m_transformNDC[1][1] = 2.0 / deltay;
  m_transformNDC[2][0] = -(right + left) / deltax;
  m_transformNDC[2][1] = -(top + bottom) / deltay;
}

void
SoftwareRenderer::Clear(const float value) {
  int bufferSize = m_bufferWidth * m_bufferHeight;
  for (int i = 0; i < bufferSize; ++i)
    m_buffer[i] = value;
}

void
SoftwareRenderer::SetVertexPointer(const int numComponents, float * const vertices)
{
  m_numVertexComponents = numComponents;
  m_triangleVertexStep = kVerticesPerTriangle * m_numVertexComponents;

  m_vertexPointer = vertices;
}

void
SoftwareRenderer::SetColorPointer(const int numComponents, float * const colors)
{
  m_numColorComponents = numComponents;
  m_triangleColorStep = kVerticesPerTriangle * m_numColorComponents;

  m_colorPointer = colors;
}

void
SoftwareRenderer::DrawPolygon(const int startIndex, const int numVertices)
{
  if (m_vertexPointer == 0)
    return;

  if ((m_colorPointer == 0) && (m_shadeMode != eShadeFlat))
    return;

  // NOTE: we assume polygons are convex! This allows one to easily
  // fan triangulate them.
  int numTriangles = numVertices - 2;
  float *vertices = &m_vertexPointer[startIndex * m_numVertexComponents];
  float *colors = &m_colorPointer[startIndex * m_numColorComponents];
  int vertexIndex1 = m_numVertexComponents;
  int vertexIndex2 = vertexIndex1 + m_numVertexComponents;
  int colorIndex1 = m_numColorComponents;
  int colorIndex2 = colorIndex1 + m_numColorComponents;
  ::Color color0(&colors[0], m_numColorComponents);
  Vertex vertex0(vertices, color0);

  MapToWindow(vertex0.window, m_transformNDC,
              0.0, 0.0, double(m_bufferWidth), double(m_bufferHeight),
              vertex0.window);
  for (int i = 0; i < numTriangles; i++)
  {
    ::Color color1(&colors[colorIndex1], m_numColorComponents);
    ::Color color2(&colors[colorIndex2], m_numColorComponents);

    Vertex vertex1(&vertices[vertexIndex1], color1);
    Vertex vertex2(&vertices[vertexIndex2], color2);

    MapToWindow(vertex1.window, m_transformNDC,
                0.0, 0.0, double(m_bufferWidth), double(m_bufferHeight),
                vertex1.window);
    MapToWindow(vertex2.window, m_transformNDC,
                0.0, 0.0, double(m_bufferWidth), double(m_bufferHeight),
                vertex2.window);

    FillTriangle((GraphicsState *) m_graphicsState, &vertex0, &vertex1, &vertex2);

    vertexIndex1 += m_triangleVertexStep;
    vertexIndex2 += m_triangleVertexStep;
    colorIndex1 += m_triangleColorStep;
    colorIndex2 += m_triangleColorStep;
  }
}
