#include "dwi/tractography/mapping/writer.h"


namespace MR {
namespace DWI {
namespace Tractography {
namespace Mapping {


template <> float get_factor<SetVoxel>          (const SetVoxel&          set, const SetVoxel         ::const_iterator item) { return set.factor; }
template <> float get_factor<SetVoxelDEC>       (const SetVoxelDEC&       set, const SetVoxelDEC      ::const_iterator item) { return set.factor; }
template <> float get_factor<SetVoxelDir>       (const SetVoxelDir&       set, const SetVoxelDir      ::const_iterator item) { return set.factor; }
template <> float get_factor<SetVoxelFactor>    (const SetVoxelFactor&    set, const SetVoxelFactor   ::const_iterator item) { return item->get_factor(); }
template <> float get_factor<SetVoxelDECFactor> (const SetVoxelDECFactor& set, const SetVoxelDECFactor::const_iterator item) { return item->get_factor(); }




template <>
bool MapWriter<float, SetVoxelDir>::operator () (const SetVoxelDir& in)
{
  for (SetVoxelDir::const_iterator i = in.begin(); i != in.end(); ++i) {
    Image::Nav::set_pos (v_buffer, *i);
    const float factor = i->get_length();
    switch (MapWriterBase<SetVoxelDir>::voxel_statistic) {
    case V_SUM:  v_buffer.value() += factor;                       break;
    case V_MIN:  v_buffer.value() = MIN(v_buffer.value(), factor); break;
    case V_MEAN:
      // Only increment counts[] if it is necessary to do so given the chosen statistic
      v_buffer.value() += factor;
      Image::Nav::set_pos (*v_counts, *i);
      (*v_counts).value() += 1;
      break;
    case V_MAX:  v_buffer.value() = MAX(v_buffer.value(), factor); break;
    default:
      throw Exception ("Unknown / unhandled voxel statistic in MapWriter::execute()");
    }
  }
  return true;
}


template <>
bool MapWriterColour<SetVoxelDir>::operator () (const SetVoxelDir& in)
{
  for (SetVoxelDir::const_iterator i = in.begin(); i != in.end(); ++i) {
    Image::Nav::set_pos (v_buffer, *i);
    const Point<float> tangent (i->get_dir());
    const Point<float> pos_tangent (Math::abs (tangent[0]), Math::abs (tangent[1]), Math::abs (tangent[2]));
    const Point<float> current_value = get_value();
    set_value (current_value + (pos_tangent * i->get_length()));
  }
  return true;
}


}
}
}
}




