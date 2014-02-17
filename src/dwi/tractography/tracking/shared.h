/*******************************************************************************
    Copyright (C) 2014 Brain Research Institute, Melbourne, Australia
    
    Permission is hereby granted under the Patent Licence Agreement between
    the BRI and Siemens AG from July 3rd, 2012, to Siemens AG obtaining a
    copy of this software and associated documentation files (the
    "Software"), to deal in the Software without restriction, including
    without limitation the rights to possess, use, develop, manufacture,
    import, offer for sale, market, sell, lease or otherwise distribute
    Products, and to permit persons to whom the Software is furnished to do
    so, subject to the following conditions:
    
    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.
    
    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
    OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
    CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
    TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
    SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*******************************************************************************/


#ifndef __dwi_tractography_shared_h__
#define __dwi_tractography_shared_h__

#include <vector>

#include "image/nav.h"

#include "point.h"
#include "image/buffer_preload.h"
#include "image/header.h"
#include "image/transform.h"
#include "dwi/tractography/properties.h"
#include "dwi/tractography/roi.h"
#include "dwi/tractography/resample.h"
#include "dwi/tractography/tracking/types.h"

#define MAX_TRIALS 1000


// If this is enabled, images will be output in the current directory showing the density of streamline terminations due to different termination mechanisms throughout the brain
//#define DEBUG_TERMINATIONS


namespace MR
{
  namespace DWI
  {
    namespace Tractography
    {
      namespace Tracking
      {



        namespace {
          std::vector<ssize_t> strides_by_volume () {
            std::vector<ssize_t> S (4, 0);
            S[3] = 1;
            return S;
          }
        }





        class SharedBase {

          public:

            SharedBase (const std::string& diff_path, DWI::Tractography::Properties& property_set) :

              source_buffer (diff_path, strides_by_volume()),
              source_voxel (source_buffer),
              properties (property_set),
              max_num_tracks (0),
              max_angle (NAN),
              max_angle_rk4 (NAN),
              cos_max_angle (NAN),
              cos_max_angle_rk4 (NAN),
              step_size (NAN),
              threshold (0.1),
              unidirectional (false),
              rk4 (false)
#ifdef DEBUG_TERMINATIONS
                , debug_header       (diff_path),
              transform          (debug_header)
#endif
              {

                properties.set (threshold, "threshold");
                properties.set (unidirectional, "unidirectional");
                properties.set (max_num_tracks, "max_num_tracks");
                properties.set (rk4, "rk4");

                properties["source"] = source_buffer.name();

                init_threshold = 2.0*threshold;
                properties.set (init_threshold, "init_threshold");

                max_num_attempts = 100 * max_num_tracks;
                properties.set (max_num_attempts, "max_num_attempts");

                if (properties.find ("init_direction") != properties.end()) {
                  std::vector<float> V = parse_floats (properties["init_direction"]);
                  if (V.size() != 3) throw Exception (std::string ("invalid initial direction \"") + properties["init_direction"] + "\"");
                  init_dir[0] = V[0];
                  init_dir[1] = V[1];
                  init_dir[2] = V[2];
                  init_dir.normalise();
                }

                if (properties.find ("downsample_factor") != properties.end())
                  downsampler.set_ratio (to<int> (properties["downsample_factor"]));

                for (size_t i = 0; i != TERMINATION_REASON_COUNT; ++i)
                  terminations[i] = 0;
                for (size_t i = 0; i != REJECTION_REASON_COUNT; ++i)
                  rejections[i] = 0;

#ifdef DEBUG_TERMINATIONS
                debug_header.set_ndim (3);
                debug_header.datatype() = DataType::UInt32;
                for (size_t i = 0; i != TERMINATION_REASON_COUNT; ++i) {
                  std::string name;
                  switch (i) {
                    case 0:  name = "undefined";      break;
                    case 1:  name = "calibrate_fail"; break;
                    case 2:  name = "exit_image";     break;
                    case 3:  name = "bad_signal";     break;
                    case 4:  name = "curvature";      break;
                    case 5:  name = "max_length";     break;
                    case 6:  name = "exit_mask";      break;
                    case 7:  name = "enter_exclude";  break;
                  }
                  debug_images[i] = new Image::Buffer<uint32_t>("terms_" + name + ".mif", debug_header);
                }
#endif

              }


            virtual ~SharedBase()
            {

              size_t sum_terminations = 0;
              for (size_t i = 0; i != TERMINATION_REASON_COUNT; ++i)
                sum_terminations += terminations[i];
              INFO ("Total number of track terminations: " + str (sum_terminations));
              INFO ("Termination reason probabilities:");
              for (size_t i = 0; i != TERMINATION_REASON_COUNT; ++i) {
                std::string term_type;
                bool to_print = false;
                switch (i) {
                  case 0: term_type = "Unknown";                  to_print = false;                     break;
                  case 1: term_type = "Calibrator failed";        to_print = true;                      break;
                  case 2: term_type = "Exited image";             to_print = true;                      break;
                  case 3: term_type = "Bad diffusion signal";     to_print = true;                      break;
                  case 4: term_type = "Excessive curvature";      to_print = true;                      break;
                  case 5: term_type = "Max length exceeded";      to_print = true;                      break;
                  case 6: term_type = "Exited mask";              to_print = properties.mask.size();    break;
                  case 7: term_type = "Entered exclusion region"; to_print = properties.exclude.size(); break;
                }
                if (to_print)
                  INFO ("  " + term_type + ": " + str (100.0 * terminations[i] / (double)sum_terminations) + "\%");
              }

              INFO ("Track rejection counts:");
              for (size_t i = 0; i != REJECTION_REASON_COUNT; ++i) {
                std::string reject_type;
                bool to_print = false;
                switch (i) {
                  case 0: reject_type = "Shorter than minimum length";     to_print = true;                      break;
                  case 1: reject_type = "Entered exclusion region";        to_print = properties.exclude.size(); break;
                  case 2: reject_type = "Missed inclusion region";         to_print = properties.include.size(); break;
                }
                if (to_print)
                  INFO ("  " + reject_type + ": " + str (rejections[i]));
              }

#ifdef DEBUG_TERMINATIONS
              for (size_t i = 0; i != TERMINATION_REASON_COUNT; ++i) {
                delete debug_images[i];
                debug_images[i] = NULL;
              }
#endif

            }


            SourceBufferType source_buffer;
            SourceBufferType::voxel_type source_voxel;
            DWI::Tractography::Properties& properties;
            Point<value_type> init_dir;
            size_t max_num_tracks, max_num_attempts, min_num_points, max_num_points;
            value_type max_angle, max_angle_rk4, cos_max_angle, cos_max_angle_rk4;
            value_type step_size, threshold, init_threshold;
            bool unidirectional;
            bool rk4;
            Downsampler downsampler;



            value_type vox () const
            {
              return Math::pow (source_buffer.vox(0)*source_buffer.vox(1)*source_buffer.vox(2), value_type (1.0/3.0));
            }

            void set_step_size (value_type stepsize)
            {
              step_size = stepsize * vox();
              properties.set (step_size, "step_size");
              INFO ("step size = " + str (step_size) + " mm");

              if (downsampler.get_ratio() > 1)
                properties["output_step_size"] = str (step_size * downsampler.get_ratio());

              value_type max_dist = 100.0 * vox();
              properties.set (max_dist, "max_dist");
              max_num_points = round (max_dist/step_size) + 1;

              value_type min_dist = 5.0 * vox();
              properties.set (min_dist, "min_dist");
              min_num_points = std::max (2, round (min_dist/step_size) + 1);

              max_angle = 90.0 * step_size / vox();
              properties.set (max_angle, "max_angle");
              INFO ("maximum deviation angle = " + str (max_angle) + " deg");
              max_angle *= M_PI / 180.0;
              cos_max_angle = Math::cos (max_angle);

              if (rk4) {
                max_angle_rk4 = max_angle;
                cos_max_angle_rk4 = cos_max_angle;
                max_angle = M_PI;
                cos_max_angle = 0.0;
              }
            }


            // This gets overloaded for iFOD2, as each sample is output rather than just each step, and there are
            //   multiple samples per step
            virtual float internal_step_size() const { return step_size; }


            void add_termination (const term_t i)   const { ++terminations[i]; }
            void add_rejection   (const reject_t i) const { ++rejections[i]; }


#ifdef DEBUG_TERMINATIONS
            void add_termination (const term_t i, const Point<value_type>& p) const
            {
              ++terminations[i];
              Image::Buffer<uint32_t>::voxel_type voxel (*debug_images[i]);
              const Point<value_type> pv = transform.scanner2voxel (p);
              const Point<int> v (Math::round (pv[0]), Math::round (pv[1]), Math::round (pv[2]));
              if (Image::Nav::within_bounds (voxel, v)) {
                Image::Nav::set_pos (voxel, v);
                voxel.value() += 1;
              }
            }
#endif


          private:
            mutable size_t terminations[TERMINATION_REASON_COUNT];
            mutable size_t rejections  [REJECTION_REASON_COUNT];

#ifdef DEBUG_TERMINATIONS
            Image::Header debug_header;
            Image::Buffer<uint32_t>* debug_images[TERMINATION_REASON_COUNT];
            const Image::Transform transform;
#endif


        };

      }
    }
  }
}

#endif

