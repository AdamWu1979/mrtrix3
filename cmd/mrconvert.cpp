/*
    Copyright 2008 Brain Research Institute, Melbourne, Australia

    Written by J-Donald Tournier, 27/06/08.

    This file is part of MRtrix.

    MRtrix is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    MRtrix is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with MRtrix.  If not, see <http://www.gnu.org/licenses/>.

*/

#include "command.h"
#include "progressbar.h"
#include "image/buffer.h"
#include "image/voxel.h"
#include "image/axis.h"
#include "image/threaded_copy.h"
#include "image/adapter/extract.h"
#include "image/adapter/permute_axes.h"
#include "image/stride.h"
#include "dwi/gradient.h"


using namespace MR;
using namespace App;

void usage ()
{
  DESCRIPTION
  + "perform conversion between different file types and optionally "
  "extract a subset of the input image."

  + "If used correctly, this program can be a very useful workhorse. "
  "In addition to converting images between different formats, it can "
  "be used to extract specific studies from a data set, extract a "
  "specific region of interest, or flip the images.";

  ARGUMENTS
  + Argument ("input", "the input image.").type_image_in ()
  + Argument ("output", "the output image.").type_image_out ();

  OPTIONS
  + Option ("coord",
            "extract data from the input image only at the coordinates specified.")
  .allow_multiple()
  + Argument ("axis").type_integer (0, 0, std::numeric_limits<int>::max())
  + Argument ("coord").type_sequence_int()

  + Option ("vox",
            "change the voxel dimensions of the output image. The new sizes should "
            "be provided as a comma-separated list of values. Only those values "
            "specified will be changed. For example: 1,,3.5 will change the voxel "
            "size along the x & z axes, and leave the y-axis voxel size unchanged.")
  + Argument ("sizes").type_sequence_float()

  + Option ("axes",
            "specify the axes from the input image that will be used to form the output "
            "image. This allows the permutation, ommission, or addition of axes into the "
            "output image. The axes should be supplied as a comma-separated list of axes. "
            "Any ommitted axes must have dimension 1. Axes can be inserted by supplying "
            "-1 at the corresponding position in the list.")
  + Argument ("axes").type_sequence_int()

  + Option ("zero",
            "replace non-finite values with zeros.")

  + Option ("prs",
            "assume that the DW gradients are specified in the PRS frame (Siemens DICOM only).")

  + Image::Stride::StrideOption

  + DataType::options()

  + DWI::GradOption;
}



typedef cfloat complex_type;




template <class InfoType>
inline std::vector<int> set_header (
  Image::Header& header,
  const InfoType& input)
{
  header.info() = input.info();

  header.intensity_offset() = 0.0;
  header.intensity_scale() = 1.0;


  Options opt = get_options ("axes");
  std::vector<int> axes;
  if (opt.size()) {
    axes = opt[0][0];
    header.set_ndim (axes.size());
    for (size_t i = 0; i < axes.size(); ++i) {
      if (axes[i] >= static_cast<int> (input.ndim()))
        throw Exception ("axis supplied to option -axes is out of bounds");
      header.dim(i) = axes[i] < 0 ? 1 : input.dim (axes[i]);
    }
  }

  opt = get_options ("vox");
  if (opt.size()) {
    std::vector<float> vox = opt[0][0];
    if (vox.size() > header.ndim())
      throw Exception ("too many axes supplied to -vox option");
    for (size_t n = 0; n < vox.size(); ++n) {
      if (std::isfinite (vox[n]))
        header.vox(n) = vox[n];
    }
  }

  opt = get_options ("stride");
  if (opt.size()) {
    std::vector<int> strides = opt[0][0];
    if (strides.size() > header.ndim())
      throw Exception ("too many axes supplied to -stride option");
    for (size_t n = 0; n < strides.size(); ++n)
      header.stride(n) = strides[n];
  }


  opt = get_options ("grad");
  if (opt.size()) 
    header.DW_scheme() = DWI::get_DW_scheme<float> (header);

  opt = get_options ("prs");
  if (opt.size() &&
      header.DW_scheme().rows() &&
      header.DW_scheme().columns() == 4) {
    Math::Matrix<float>& M (header.DW_scheme());
    for (size_t row = 0; row < M.rows(); ++row) {
      float tmp = M (row, 0);
      M (row, 0) = M (row, 1);
      M (row, 1) = tmp;
      M (row, 2) = -M (row, 2);
    }
  }

  return axes;
}




inline void zero_non_finite (complex_type in, complex_type& out) 
{
  out = complex_type (
      std::isfinite (in.real()) ? in.real() : 0.0, 
      std::isfinite (in.imag()) ? in.imag() : 0.0
      );
}

template <class InputVoxelType>
inline void copy_permute (InputVoxelType& in, Image::Header& header_out, const std::string& output_filename)
{
  bool replace_nans = App::get_options ("zero").size();

  DataType datatype = header_out.datatype();
  std::vector<int> axes = set_header (header_out, in);
  header_out.datatype() = datatype;
  Image::Buffer<complex_type> buffer_out (output_filename, header_out);
  Image::Buffer<complex_type>::voxel_type out (buffer_out);

  if (axes.size()) {
    Image::Adapter::PermuteAxes<InputVoxelType> perm (in, axes);

    if (replace_nans)
      Image::ThreadedLoop ("copying from \"" + shorten (perm.name()) + "\" to \"" + shorten (out.name()) + "\"...", perm, 2)
        .run_foreach (zero_non_finite, 
            perm, Input,
            out, Output);
    else 
      Image::threaded_copy_with_progress (perm, out, 2);
  }
  else {
    if (replace_nans)
      Image::ThreadedLoop ("copying from \"" + shorten (in.name()) + "\" to \"" + shorten (out.name()) + "\"...", in, 2)
        .run_foreach (zero_non_finite, 
            in, Input,
            out, Output);
    else
      Image::threaded_copy_with_progress (in, out, 2);
  }
}










void run ()
{
  Image::Header header_in (argument[0]);

  Image::Buffer<complex_type> buffer_in (header_in);
  Image::Buffer<complex_type>::voxel_type in (buffer_in);

  Image::Header header_out (header_in);
  header_out.datatype() = DataType::from_command_line (header_out.datatype());

  if (header_in.datatype().is_complex() && !header_out.datatype().is_complex())
    WARN ("requested datatype is real but input datatype is complex - imaginary component will be ignored");


  Options opt = get_options ("coord");
  if (opt.size()) {
    std::vector<std::vector<int> > pos (buffer_in.ndim());
    for (size_t n = 0; n < opt.size(); n++) {
      int axis = opt[n][0];
      if (pos[axis].size())
        throw Exception ("\"coord\" option specified twice for axis " + str (axis));
      pos[axis] = parse_ints (opt[n][1], buffer_in.dim(axis)-1);
      if (axis == 3 && header_in.DW_scheme().is_set()) {
        Math::Matrix<float>& grad (header_in.DW_scheme());
        if ((int)grad.rows() != header_in.dim(3)) {
          WARN ("Diffusion encoding of input file does not match number of image volumes; omitting gradient information from output image");
          header_out.DW_scheme().clear();
        } else {
          Math::Matrix<float> extract_grad (pos[3].size(), 4);
          for (size_t dir = 0; dir != pos[3].size(); ++dir)
            extract_grad.row(dir) = grad.row((pos[3])[dir]);
          header_out.DW_scheme() = extract_grad;
        }
      }
    }

    for (size_t n = 0; n < buffer_in.ndim(); ++n) {
      if (pos[n].empty()) {
        pos[n].resize (buffer_in.dim (n));
        for (size_t i = 0; i < pos[n].size(); i++)
          pos[n][i] = i;
      }
    }

    Image::Adapter::Extract<Image::Buffer<complex_type>::voxel_type> extract (in, pos);
    copy_permute (extract, header_out, argument[1]);
  }
  else
    copy_permute (in, header_out, argument[1]);

}

