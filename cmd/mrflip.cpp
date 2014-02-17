#include "command.h"
#include "dwi/gradient.h"
#include "image/buffer.h"
#include "image/voxel.h"
#include "image/loop.h"
#include "image/transform.h"



using namespace MR;
using namespace App;


void usage ()
{
  AUTHOR = "David Raffelt (d.raffelt@brain.org.au)";

  DESCRIPTION
  + "Flip an image across a given axis. If the input image is a DWI, then the gradient "
    "directions (defined wrt scanner coordinates) are also adjusted (wrt the chosen image axis)";

  ARGUMENTS
  + Argument ("input", "the input image").type_image_in()
  + Argument ("axis", "the axis to be flipped")
  + Argument ("output", "the output image").type_image_out();

  OPTIONS
  + DWI::GradOption;
}


void run ()
{
  Image::Header input_header (argument[0]);

  Math::Matrix<float> grad = DWI::get_DW_scheme<float> (input_header);
  int axis = argument[1];
  if (axis < 0 || axis > 2)
    throw Exception ("the image axis must be between 0 and 2 inclusive");

  if (grad.is_set()) {
    Image::Transform transform (input_header);
    for (size_t dir = 0; dir < grad.rows(); dir++) {
      Math::Vector<float> grad_flipped (3);
      transform.scanner2image_dir(grad.row(dir).sub(0,3), grad_flipped);
      grad_flipped[axis] = -grad_flipped[axis];
      Math::Vector<float> grad_flipped_scanner (3);
      transform.image2scanner_dir(grad_flipped, grad_flipped_scanner);
      grad.row(dir).sub(0,3) = grad_flipped_scanner;
    }
  }
  Image::Header output_header (input_header);
  if (grad.is_set()) {
    output_header.DW_scheme() = grad;
  }

 Image::Buffer<float> input_data (input_header);
 Image::Buffer<float>::voxel_type input_voxel(input_data);
 Image::Buffer<float> output_data (argument[2], output_header);
 Image::Buffer<float>::voxel_type output_voxel (output_data);

 Image::LoopInOrder loop (input_voxel, "flipping image...");
 for (loop.start (input_voxel); loop.ok(); loop.next(input_voxel)) {
   Image::voxel_assign(output_voxel, input_voxel);
   output_voxel[axis] = input_voxel.dim(axis) - input_voxel[axis] - 1;
   output_voxel.value() = input_voxel.value();
 }

}

