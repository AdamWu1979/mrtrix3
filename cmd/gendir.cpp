#include <gsl/gsl_blas.h>
#include <gsl/gsl_multimin.h>

#include "command.h"
#include "progressbar.h"
#include "math/vector.h"
#include "math/matrix.h"
#include "math/rng.h"


using namespace MR;
using namespace App;

void usage () {
DESCRIPTION
  + "generate a set of directions evenly distributed over a hemisphere.";

ARGUMENTS
  + Argument ("ndir", "the number of directions to generate.").type_integer (6, 60, std::numeric_limits<int>::max())
  + Argument ("dirs", "the text file to write the directions to, as [ az el ] pairs.").type_file();

OPTIONS
  + Option ("power", "specify exponent to use for repulsion power law.")
  + Argument ("exp").type_integer (2, 128, std::numeric_limits<int>::max())

  + Option ("niter", "specify the maximum number of iterations to perform.")
  + Argument ("num").type_integer (1, 10000, 1000000)

  + Option ("cartesian", "Output the directions in Cartesian coordinates [x y z] instead of [az el].");
}





namespace
{
  double power = -1.0;
  size_t   ndirs = 0;
}




class SinCos
{
  protected:
    double cos_az, sin_az, cos_el, sin_el;
    double r2_pos, r2_neg, multiplier;

    double energy ();
    void   dist (const SinCos& B);
    void   init_deriv ();
    double daz (const SinCos& B) const;
    double del (const SinCos& B) const;
    double rdel (const SinCos& B) const;

  public:
    SinCos (const gsl_vector* v, size_t index);
    double f (const SinCos& B);
    void   df (const SinCos& B, gsl_vector* deriv, size_t i, size_t j);
    double fdf (const SinCos& B, gsl_vector* deriv, size_t i, size_t j);
};



double energy_f (const gsl_vector* x, void* params);
void   energy_df (const gsl_vector* x, void* params, gsl_vector* df);
void   energy_fdf (const gsl_vector* x, void* params, double* f, gsl_vector* df);


void range (double& azimuth, double& elevation);


void run () {
  size_t niter = 10000;
  float target_power = 128.0;

  Options opt = get_options ("power");
  if (opt.size())
    target_power = opt[0][0];

  opt = get_options ("niter");
  if (opt.size())
    niter = opt[0][0];

  ndirs = to<int> (argument[0]);


  Math::RNG    rng;
  Math::Vector<double> v (2*ndirs-3);

  v[0] = asin (2.0 * rng.uniform() - 1.0);
  for (size_t n = 1; n < 2*ndirs-3; n+=2) {
    v[n] =  M_PI * (2.0 * rng.uniform() - 1.0);
    v[n+1] = asin (2.0 * rng.uniform() - 1.0);
  }

  gsl_multimin_function_fdf fdf;

  fdf.f = energy_f;
  fdf.df = energy_df;
  fdf.fdf = &energy_fdf;
  fdf.n = 2*ndirs-3;

  gsl_multimin_fdfminimizer* minimizer =
  gsl_multimin_fdfminimizer_alloc (gsl_multimin_fdfminimizer_conjugate_fr, 2*ndirs-3);


  {
    ProgressBar progress ("Optimising directions");
    for (power = -1.0; power >= -target_power/2.0; power *= 2.0) {
      INFO ("setting power = " + str (-power*2.0));
      gsl_multimin_fdfminimizer_set (minimizer, &fdf, v.gsl(), 0.01, 1e-4);

      for (size_t iter = 0; iter < niter; iter++) {

        int status = gsl_multimin_fdfminimizer_iterate (minimizer);

        if (iter%10 == 0)
          INFO ("[ " + str (iter) + " ] (pow = " + str (-power*2.0) + ") E = " + str (minimizer->f)
          + ", grad = " + str (gsl_blas_dnrm2 (minimizer->gradient)));

        if (status) {
          INFO (std::string ("iteration stopped: ") + gsl_strerror (status));
          break;
        }

        ++progress;
      }
      gsl_vector_memcpy (v.gsl(), minimizer->x);
    }
  }


  Math::Matrix<double> directions (ndirs, 2);
  directions (0,0) = 0.0;
  directions (0,1) = 0.0;
  directions (1,0) = 0.0;
  directions (1,1) = gsl_vector_get (minimizer->x, 0);
  for (size_t n = 2; n < ndirs; n++) {
    double az = gsl_vector_get (minimizer->x, 2*n-3);
    double el = gsl_vector_get (minimizer->x, 2*n-2);
    range (az, el);
    directions (n, 0) = az;
    directions (n, 1) = el;
  }

  gsl_multimin_fdfminimizer_free (minimizer);

  opt = get_options ("cartesian");
  if (opt.size()) {
    Math::Matrix<double> cartesian (directions.rows(), 3);
    for (unsigned int i = 0; i < cartesian.rows(); i++) {
      cartesian(i,0) = sin(directions(i,1)) * cos(directions(i,0));
      cartesian(i,1) = sin(directions(i,1)) * sin(directions(i,0));
      cartesian(i,2) = cos(directions(i,1));
    }
    cartesian.save (argument[1]);
  } else {
    directions.save (argument[1]);
  }
}











inline double SinCos::energy ()
{
  return (pow (r2_pos, power) + pow (r2_neg, power));
}

inline void SinCos::dist (const SinCos& B)
{
  double a1 = cos_az*sin_el;
  double b1 = B.cos_az*B.sin_el;
  double a2 = sin_az*sin_el;
  double b2 = B.sin_az*B.sin_el;
  r2_pos = (a1+b1) * (a1+b1) + (a2+b2) * (a2+b2) + (cos_el+B.cos_el) * (cos_el+B.cos_el);
  r2_neg = (a1-b1) * (a1-b1) + (a2-b2) * (a2-b2) + (cos_el-B.cos_el) * (cos_el-B.cos_el);
}

inline void SinCos::init_deriv ()
{
  multiplier = 2.0 * power * (pow (r2_neg, power-1.0) - pow (r2_pos, power-1.0));
}

inline double SinCos::daz (const SinCos& B) const
{
  return (multiplier * (cos_az*sin_el*B.sin_az*B.sin_el - sin_az*sin_el*B.cos_az*B.sin_el));
}

inline double SinCos::del (const SinCos& B) const
{
  return (multiplier * (cos_az*cos_el*B.cos_az*B.sin_el + sin_az*cos_el*B.sin_az*B.sin_el - sin_el*B.cos_el));
}

inline double SinCos::rdel (const SinCos& B) const
{
  return (multiplier * (B.cos_az*B.cos_el*cos_az*sin_el + B.sin_az*B.cos_el*sin_az*sin_el - B.sin_el*cos_el));
}

inline SinCos::SinCos (const gsl_vector* v, size_t index)
{
  double az = index > 1 ? gsl_vector_get (v, 2*index-3) : 0.0;
  double el = index ? gsl_vector_get (v, 2*index-2) : 0.0;
  cos_az = cos (az);
  sin_az = sin (az);
  cos_el = cos (el);
  sin_el = sin (el);
}

inline double SinCos::f (const SinCos& B)
{
  dist (B);
  return (energy());
}

inline void SinCos::df (const SinCos& B, gsl_vector* deriv, size_t i, size_t j)
{
  dist (B);
  init_deriv ();
  double d = daz (B);
  if (i) {
    *gsl_vector_ptr (deriv, 2*i-2) -= del (B);
    if (i > 1) *gsl_vector_ptr (deriv, 2*i-3) -= d;
  }
  if (j) {
    *gsl_vector_ptr (deriv, 2*j-2) -= rdel (B);
    if (j > 1) *gsl_vector_ptr (deriv, 2*j-3) += d;
  }
}


inline double SinCos::fdf (const SinCos& B, gsl_vector* deriv, size_t i, size_t j)
{
  df (B, deriv, i, j);
  return (energy());
}


double energy_f (const gsl_vector* x, void* params)
{
  double  E = 0.0;
  for (size_t i = 0; i < ndirs; i++) {
    SinCos I (x, i);
    for (size_t j = i+1; j < ndirs; j++)
      E += 2.0 *I.f (SinCos (x, j));
  }
  return (E);
}



void energy_df (const gsl_vector* x, void* params, gsl_vector* df)
{
  gsl_vector_set_zero (df);
  for (size_t i = 0; i < ndirs; i++) {
    SinCos I (x, i);
    for (size_t j = i+1; j < ndirs; j++)
      I.df (SinCos (x, j), df, i, j);
  }
}



void energy_fdf (const gsl_vector* x, void* params, double* f, gsl_vector* df)
{
  *f = 0.0;
  gsl_vector_set_zero (df);
  for (size_t i = 0; i < ndirs; i++) {
    SinCos I (x, i);
    for (size_t j = i+1; j < ndirs; j++)
      *f += 2.0 * I.fdf (SinCos (x, j), df, i, j);
  }
}





inline void range (double& azimuth, double& elevation)
{
  while (elevation < 0.0) elevation += 2.0*M_PI;
  while (elevation >= 2.0*M_PI) elevation -= 2.0*M_PI;
  if (elevation >= M_PI) {
    elevation = 2.0*M_PI - elevation;
    azimuth -= M_PI;
  }
  while (azimuth < -M_PI) azimuth += 2.0*M_PI;
  while (azimuth >= M_PI) azimuth -= 2.0*M_PI;
}


