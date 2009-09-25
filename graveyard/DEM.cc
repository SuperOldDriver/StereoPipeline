// __BEGIN_LICENSE__
// 
// Copyright (C) 2008 United States Government as represented by the
// Administrator of the National Aeronautics and Space Administration
// (NASA).  All Rights Reserved.
// 
// Copyright 2008 Carnegie Mellon University. All rights reserved.
// 
// This software is distributed under the NASA Open Source Agreement
// (NOSA), version 1.3.  The NOSA has been approved by the Open Source
// Initiative.  See the file COPYING at the top of the distribution
// directory tree for the complete NOSA document.
// 
// THE SUBJECT SOFTWARE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY OF ANY
// KIND, EITHER EXPRESSED, IMPLIED, OR STATUTORY, INCLUDING, BUT NOT
// LIMITED TO, ANY WARRANTY THAT THE SUBJECT SOFTWARE WILL CONFORM TO
// SPECIFICATIONS, ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR
// A PARTICULAR PURPOSE, OR FREEDOM FROM INFRINGEMENT, ANY WARRANTY THAT
// THE SUBJECT SOFTWARE WILL BE ERROR FREE, OR ANY WARRANTY THAT
// DOCUMENTATION, IF PROVIDED, WILL CONFORM TO THE SUBJECT SOFTWARE.
//
// __END_LICENSE__

/// \file DEM.cc
///

#include "DEM.h"

#include <stdlib.h>

void write_GMT_script(std::string const prefix,
                      int width, int height, double min_value, double max_value,
                      double scale_factor,
                      vw::cartography::GeoReference const& georef) {
  std::string filename = prefix + "-gmt.sh";
  std::string dem_filename = prefix + "-DEM.tif";

  std::cout << "Width: " << width << "  Height: " << height << "\n";

  std::cout << "\tWriting GMT script: " << filename << "\n";

//   double left = georef.transform()(0,2);
//   double top = georef.transform()(1,2);
//   double right = georef.transform()(0,2) + (width-1) * georef.transform()(0,0);
//   double bottom = georef.transform()(1,2) + (height-1) * georef.transform()(1,1);

  FILE *f = fopen(filename.c_str(), "w");

  fprintf(f, "# Generic Mapping Tools Contour Mapping Script\n");
  fprintf(f, "# \n");
  fprintf(f, "# Automatically generated by the Ames Stereo Pipeline\n");
  fprintf(f, "# You must have the GMT packages and GDAL installed and in your search path,\n");
  fprintf(f, "# and GDAL must be built with NetCDF (GMT) support.\n");
  fprintf(f, "# \n");
  fprintf(f, "# Usage: <scriptname>-gmt.sh <dem filename>\n");
  fprintf(f, "# \n");
  fprintf(f, "gdal_translate %s -of GMT temporary-dem.grd\n", dem_filename.c_str());
  fprintf(f, "grdinfo temporary-dem.grd \n");

  std::string texture_contour_name = prefix + "-DEM-contour.ps";
  fprintf(f, "rm -f %s\n", texture_contour_name.c_str());
  fprintf(f, "gmtset PAPER_MEDIA Custom_%0.0fx%0.0f DOTS_PR_INCH 300 X_ORIGIN 0 Y_ORIGIN 0 GRID_CROSS_SIZE 0 ANNOT_FONT_SIZE_PRIMARY 10\n\n", float(height)/300.0*72, float(width)/300.0*72);

  float contour_interval = round((max_value - min_value) / 100)*10;
  fprintf(f, "grdcontour temporary-dem.grd -JX%0.2fi/%0.2fi -C%0.0f -A%0.0f+s7+g -Gd4i -Wcthinnest,- -Wathin -T0.1i/0.02i -L%0.0f/%0.0f > %s\n",
          float(width)/300, float(height)/300, contour_interval, contour_interval*4, (min_value-contour_interval), (max_value+contour_interval), texture_contour_name.c_str());

  fprintf(f, "rm -f .gmtcommands4 .gmtdefaults4 g.cpt temporary-dem.grd\n");

  fclose(f);

}
