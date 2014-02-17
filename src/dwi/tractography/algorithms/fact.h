#ifndef __dwi_tractography_algorithms_FACT_h__
#define __dwi_tractography_algorithms_FACT_h__

#include "point.h"
#include "math/eigen.h"
#include "math/least_squares.h"
#include "dwi/gradient.h"
#include "dwi/tensor.h"
#include "dwi/tractography/tracking/method.h"
#include "dwi/tractography/tracking/shared.h"
#include "dwi/tractography/tracking/types.h"



namespace MR
{
  namespace DWI
  {
    namespace Tractography
    {
      namespace Algorithms
      {


    using namespace MR::DWI::Tractography::Tracking;


    class FACT : public MethodBase {
      public:
      class Shared : public SharedBase {
        public:
        Shared (const std::string& diff_path, DWI::Tractography::Properties& property_set) :
          SharedBase (diff_path, property_set) {

          set_step_size (0.1);
          if (rk4) {
            INFO ("minimum radius of curvature = " + str(step_size / (max_angle_rk4 / (0.5 * M_PI))) + " mm");
          } else {
            INFO ("minimum radius of curvature = " + str(step_size / ( 2.0 * sin (max_angle / 2.0))) + " mm");
          }

          properties["method"] = "FACT";

          Math::Matrix<float> grad;
          if (properties.find ("DW_scheme") != properties.end())
            grad.load (properties["DW_scheme"]);
          else
            grad = source_buffer.DW_scheme();

          if (grad.columns() != 4)
            throw Exception ("unexpected number of columns in gradient encoding (expected 4 columns)");
          if (grad.rows() < 7)
            throw Exception ("too few rows in gradient encoding (need at least 7)");

          normalise_grad (grad);

          grad2bmatrix (bmat, grad);
          Math::pinv (binv, bmat);
        }

        Math::Matrix<float> bmat, binv;
      };






      FACT (const Shared& shared) :
        MethodBase (shared),
        S (shared),
        source (S.source_voxel),
        eig (3),
        M (3,3),
        V (3,3),
        ev (3) { }




      bool init ()
      {
        if (!get_data (source))
          return false;
        return do_init();
      }



      term_t next ()
      {
        if (!get_data (source))
          return Tracking::EXIT_IMAGE;
        return do_next();
      }



      protected:
      const Shared& S;
      Tracking::Interpolator<SourceBufferType::voxel_type>::type source;
      Math::Eigen::SymmV<double> eig;
      Math::Matrix<double> M, V;
      Math::Vector<double> ev;

      void get_EV ()
      {
        M(0,0) = values[0];
        M(1,1) = values[1];
        M(2,2) = values[2];
        M(0,1) = M(1,0) = values[3];
        M(0,2) = M(2,0) = values[4];
        M(1,2) = M(2,1) = values[5];

        eig (ev, M, V);
        Math::Eigen::sort (ev, V);

        dir[0] = V(0,2);
        dir[1] = V(1,2);
        dir[2] = V(2,2);
      }


      bool do_init()
      {
        dwi2tensor (S.binv, &values[0]);

        if (tensor2FA (&values[0]) < S.init_threshold)
          return false;

        get_EV();

        return true;
      }


      term_t do_next()
      {

        dwi2tensor (S.binv, &values[0]);

        if (tensor2FA (&values[0]) < S.threshold)
          return BAD_SIGNAL;

        Point<value_type> prev_dir = dir;

        get_EV();

        value_type dot = prev_dir.dot (dir);
        if (Math::abs (dot) < S.cos_max_angle)
          return HIGH_CURVATURE;

        if (dot < 0.0)
          dir = -dir;

        pos += dir * S.step_size;

        return CONTINUE;

      }


    };

      }
    }
  }
}

#endif


