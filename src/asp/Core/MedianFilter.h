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

/// \file StereoSession.h
///

#ifndef __MEDIAN_FILTER_H__
#define __MEDIAN_FILTER_H__

typedef uint8 CalcPixelT;
#define CALC_PIXEL_NUM_VALS 256

CalcPixelT find_median_in_histogram(Vector<int, CALC_PIXEL_NUM_VALS> histogram, int kernSize) {
  int acc = 0;
  int acc_limit = kernSize * kernSize / 2;

  CalcPixelT i = 0;

  for (;;) {
    acc += histogram(i);

    if (acc >= acc_limit)
      break;

    i++;
  }

  return i;
}

template<class ImageT>
ImageView<typename ImageT::pixel_type> fast_median_filter(ImageViewBase<ImageT> const& img,  int kernSize) {
  typedef typename ImageT::pixel_type PixelT;

  ImageView<CalcPixelT> src = pixel_cast_rescale<CalcPixelT>(img.impl());
  ImageView<CalcPixelT> result(src.cols(), src.rows());

  Vector<int, CALC_PIXEL_NUM_VALS> histogram;

  // Seed histogram
  for (int x = 0; x < kernSize; x++) {
    for (int y = 0; y < kernSize; y++) {
      histogram(src(x, y))++;
    }
  }

  bool goingRight = true;
  int x = 0, y = 0;
  //while (y <=src.rows() - kernSize) { //this was a bug
  while (y < src.rows() - kernSize) {

    result(x + kernSize / 2, y + kernSize / 2) = find_median_in_histogram(histogram, kernSize);

    if (goingRight) {
      if (x < src.cols() - kernSize) {
        for (int i = 0; i < kernSize; i++) {
          histogram(src(x, y + i))--;
          histogram(src(x + kernSize, y + i))++;
        }
        x++;

      }
      else {
        // Reached the right edge
        for (int i = 0; i < kernSize; i++) {
          histogram(src(x + i, y))--;
          histogram(src(x + i, y + kernSize))++;
        }
        goingRight = false;
        y++;
      }
    }
    else {
      if (x > 0) {
        for (int i = 0; i < kernSize; i++) {
          histogram(src(x - 1, y + i))++;
          histogram(src(x + kernSize - 1, y + i))--;
        }
        x--;
      }
      else {
        // Reached the left edge
        for (int i = 0; i < kernSize; i++) {
          histogram(src(x + i, y))--;
          histogram(src(x + i, y + kernSize))++;
        }
        goingRight = true;
        y++;
      }
    }
  }

  ImageView<PixelT> result_scaled = pixel_cast_rescale<PixelT>(result);

  return result_scaled;

}
template<class PixelT>
class MedianFilterFunctor:public ReturnFixedType<PixelT>
{
   int m_kernel_width;
   int m_kernel_height;

public:
   MedianFilterFunctor(int kernel_width, int kernel_height):
   m_kernel_width(kernel_width),
   m_kernel_height(kernel_height){}

   BBox2i work_area() const{ return BBox2i(Vector2i(-m_kernel_width/2, -m_kernel_height/2),
                                           Vector2i(m_kernel_width/2, m_kernel_height/2));  }

   template<class PixelAccessorT>
   typename PixelAccessorT::pixel_type operator()(PixelAccessorT const& acc) const{

   Vector<int, CALC_PIXEL_NUM_VALS> histogram;

   // Seed histogram
   for (int x = 0; x < m_kernel_width; x++) {
     for (int y = 0; y < m_kernel_height; y++) {
          histogram(*acc)++;
     }
   }
   *acc = find_median_in_histogram(histogram, m_kernel_width);

   /*
   bool goingRight = true;
   int x = 0, y = 0;
   while (y < src.rows() - kernSize) {

        result(x + m_kernel_width / 2, y + m_kernel_height / 2) = find_median_in_histogram(histogram, m_kernel_width);

        if (goingRight) {
          if (x < src.cols() - kernSize) {
             for (int i = 0; i < kernSize; i++) {
                histogram(src(x, y + i))--;
                histogram(src(x + kernSize, y + i))++;
             }
             x++;
          }
          else {
             // Reached the right edge
             for (int i = 0; i < kernSize; i++) {
                 histogram(src(x + i, y))--;
                 histogram(src(x + i, y + kernSize))++;
             }
             goingRight = false;
             y++;
          }
       }
       else {
          if (x > 0) {
             for (int i = 0; i < kernSize; i++) {
                histogram(src(x - 1, y + i))++;
                histogram(src(x + kernSize - 1, y + i))--;
             }
             x--;
         }
         else {
            // Reached the left edge
            for (int i = 0; i < kernSize; i++) {
               histogram(src(x + i, y))--;
               histogram(src(x + i, y + kernSize))++;
            }
            goingRight = true;
            y++;
         }
      }

   }

   ImageView<PixelT> result_scaled = pixel_cast_rescale<PixelT>(result);

   return result_scaled;
   */
   return *acc;
   }
};

template<class ViewT>
UnaryPerPixelAccessorView<EdgeExtensionView<ViewT, ZeroEdgeExtension>, MedianFilterFunctor<typename ViewT::pixel_type> >
my_median_filter(ImageViewBase<ViewT> const& input, int kernel_width, int kernel_height)
{
 return UnaryPerPixelAccessorView<EdgeExtensionView<ViewT, ZeroEdgeExtension>, MedianFilterFunctor<typename ViewT::pixel_type> >(edge_extend(input, ZeroEdgeExtension()), MedianFilterFunctor<typename ViewT::pixel_type> (kernel_width, kernel_height));
}

#endif // __MEDIAN_FILTER_H__