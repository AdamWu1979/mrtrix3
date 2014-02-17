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


#ifndef __image_stride_h__
#define __image_stride_h__

#include "app.h"
#include "datatype.h"
#include "math/math.h"

namespace MR
{
  namespace Image
  {

    //! Functions to handle the memory layout of DataSet classes
    /*! Strides are typically supplied as a symbolic list of increments,
     * representing the layout of the data in memory. In this symbolic
     * representation, the actual magnitude of the strides is only important
     * in that it defines the ordering of the various axes.
     *
     * For example, the vector of strides [ 3 -1 -2 ] is valid as a symbolic
     * representation of a DataSet stored as a stack of sagittal slices. Each
     * sagittal slice is stored as rows of voxels ordered from anterior to
     * posterior (i.e. negative y: -1), then stacked superior to inferior (i.e.
     * negative z: -2). These slices are then stacked from left to right (i.e.
     * positive x: 3).
     *
     * This representation is symbolic since it does not take into account the
     * size of the DataSet along each dimension. To be used in practice, these
     * strides must correspond to the number of intensity values to skip
     * between adjacent voxels along the respective axis. For the example
     * above, the DataSet might consists of 128 sagittal slices, each with
     * dimensions 256x256. The dimensions of the DataSet (as returned by dim())
     * are therefore [ 128 256 256 ]. The actual strides needed to navigate
     * through the DataSet, given the symbolic strides above, should therefore
     * be [ 65536 -256 -1 ] (since 256x256 = 65532).
     *
     * Note that a stride of zero is treated as undefined or invalid. This can
     * be used in the symbolic representation to specify that the ordering of
     * the corresponding axis is not important. A suitable stride will be
     * allocated to that axis when the DataSet is initialised (this is done
     * with a call to sanitise()).
     *
     * The functions defined in this namespace provide an interface to
     * manipulate the strides and convert symbolic into actual strides. */
    namespace Stride
    {

      typedef std::vector<ssize_t> List;

      extern const App::OptionGroup StrideOption;

      template <class InfoType> 
        void set_from_command_line (InfoType& info, const List& default_strides = List())
        {
          App::Options opt = App::get_options ("stride");
          size_t n;
          if (opt.size()) {
            std::vector<int> strides = opt[0][0];
            if (strides.size() > info.ndim())
              WARN ("too many axes supplied to -stride option - ignoring remaining strides");
            for (n = 0; n < std::min (info.ndim(), strides.size()); ++n)
              info.stride(n) = strides[n];
            for (; n < info.ndim(); ++n)
              info.stride(n) = 0;
          } 
          else if (default_strides.size()) {
            for (n = 0; n < std::min (info.ndim(), default_strides.size()); ++n) 
              info.stride(n) = default_strides[n];
            for (; n < info.ndim(); ++n)
              info.stride(n) = 0;
          }
        }





      //! \cond skip
      namespace
      {
        template <class Set> class Compare
        {
          public:
            Compare (const Set& set) : S (set) { }
            bool operator() (const size_t a, const size_t b) const {
              if (S.stride(a) == 0)
                return false;
              if (S.stride(b) == 0)
                return true;
              return abs (S.stride (a)) < abs (S.stride (b));
            }
          private:
            const Set& S;
        };

        class Wrapper
        {
          public:
            Wrapper (List& strides) : S (strides) { }
            size_t ndim () const {
              return S.size();
            }
            const ssize_t& stride (size_t axis) const {
              return S[axis];
            }
            ssize_t& stride (size_t axis) {
              return S[axis];
            }
          private:
            List& S;
        };

        template <class Set> class WrapperSet : public Wrapper
        {
          public:
            WrapperSet (List& strides, const Set& set) : Wrapper (strides), D (set) {
              assert (ndim() == D.ndim());
            }
            ssize_t dim (size_t axis) const {
              return D.dim (axis);
            }
          private:
            const Set& D;
        };
      }
      //! \endcond




      //! return the strides of \a set as a std::vector<ssize_t>
      template <class Set> List get (const Set& set)
      {
        List ret (set.ndim());
        for (size_t i = 0; i < set.ndim(); ++i)
          ret[i] = set.stride (i);
        return ret;
      }

      //! set the strides of \a set from a std::vector<ssize_t>
      template <class Set> void set (Set& ds, const List& stride)
      {
        for (size_t i = 0; i < ds.ndim(); ++i)
          ds.stride (i) = stride[i];
      }




      //! sort axes with respect to their absolute stride.
      /*! \return a vector of indices of the axes in order of increasing
       * absolute stride.
       * \note all strides should be valid (i.e. non-zero). */
      template <class Set> std::vector<size_t> order (const Set& set)
      {
        std::vector<size_t> ret (set.ndim());
        for (size_t i = 0; i < ret.size(); ++i) ret[i] = i;
        Compare<Set> compare (set);
        std::sort (ret.begin(), ret.end(), compare);
        return ret;
      }

      //! sort axes with respect to their absolute stride.
      /*! \return a vector of indices of the axes in order of increasing
       * absolute stride.
       * \note all strides should be valid (i.e. non-zero). */
      template <> inline std::vector<size_t> order<List> (const List& strides)
      {
        const Wrapper wrapper (const_cast<List&> (strides));
        return order (wrapper);
      }

      //! sort range of axes with respect to their absolute stride.
      /*! \return a vector of indices of the axes in order of increasing
       * absolute stride.
       * \note all strides should be valid (i.e. non-zero). */
      template <class Set> std::vector<size_t> order (const Set& set, size_t from_axis, size_t to_axis)
      {
        to_axis = std::min (to_axis, set.ndim());
        assert (to_axis > from_axis);
        std::vector<size_t> ret (to_axis-from_axis);
        for (size_t i = 0; i < ret.size(); ++i) ret[i] = from_axis+i;
        Compare<Set> compare (set);
        std::sort (ret.begin(), ret.end(), compare);
        return ret;
      }




      //! remove duplicate and invalid strides.
      /*! sanitise the strides of DataSet \a set by identifying invalid (i.e.
       * zero) or duplicate (absolute) strides, and assigning to each a
       * suitable value. The value chosen for each sanitised stride is the
       * lowest number greater than any of the currently valid strides. */
      template <class Set> void sanitise (Set& set)
      {
        for (size_t i = 0; i < set.ndim()-1; ++i) {
          if (!set.stride (i)) continue;
          for (size_t j = i+1; j < set.ndim(); ++j) {
            if (!set.stride (j)) continue;
            if (abs (set.stride (i)) == abs (set.stride (j))) set.stride (j) = 0;
          }
        }

        size_t max = 0;
        for (size_t i = 0; i < set.ndim(); ++i)
          if (size_t (abs (set.stride (i))) > max)
            max = abs (set.stride (i));

        for (size_t i = 0; i < set.ndim(); ++i) {
          if (set.stride (i)) continue;
          set.stride (i) = ++max;
        }
      }
      //! remove duplicate and invalid strides.
      /*! sanitise the strides of DataSet \a set by identifying invalid (i.e.
       * zero) or duplicate (absolute) strides, and assigning to each a
       * suitable value. The value chosen for each sanitised stride is the
       * lowest number greater than any of the currently valid strides. */
      template <> inline void sanitise<List> (List& strides)
      {
        Wrapper wrapper (strides);
        sanitise (wrapper);
      }




      //! convert strides from symbolic to actual strides
      template <class Set> void actualise (Set& set)
      {
        sanitise (set);
        std::vector<size_t> x (order (set));
        ssize_t skip = 1;
        for (size_t i = 0; i < set.ndim(); ++i) {
          set.stride (x[i]) = set.stride (x[i]) > 0 ? skip : -skip;
          skip *= set.dim (x[i]);
        }
      }
      //! convert strides from symbolic to actual strides
      /*! convert strides from symbolic to actual strides, assuming the strides
       * in \a strides and DataSet dimensions of \a set. */
      template <class Set> inline void actualise (List& strides, const Set& set)
      {
        WrapperSet<Set> wrapper (strides, set);
        actualise (wrapper);
      }

      //! get actual strides:
      template <class Set> inline List get_actual (Set& set)
      {
        List strides (get (set));
        actualise (strides, set);
        return strides;
      }

      //! get actual strides:
      template <class Set> inline List get_actual (const List& strides, const Set& set)
      {
        List out (strides);
        actualise (out, set);
        return out;
      }



      //! convert strides from actual to symbolic strides
      template <class Set> void symbolise (Set& set)
      {
        std::vector<size_t> p (order (set));
        for (ssize_t i = 0; i < ssize_t (p.size()); ++i)
          if (set.stride (p[i]) != 0)
            set.stride (p[i]) = set.stride (p[i]) > 0 ? i+1 : - (i+1);
      }
      //! convert strides from actual to symbolic strides
      template <> inline void symbolise<List> (List& strides)
      {
        Wrapper wrapper (strides);
        symbolise (wrapper);
      }

      //! get symbolic strides:
      template <class Set> inline List get_symbolic (const Set& set)
      {
        List strides (get (set));
        symbolise (strides);
        return strides;
      }

      //! get symbolic strides:
      template <> inline List get_symbolic (const List& list)
      {
        List strides (list);
        symbolise (strides);
        return strides;
      }


      //! calculate offset to start of data
      /*! this function caculate the offset (in number of voxels) from the start of the data region
       * to the first voxel value (i.e. at voxel [ 0 0 0 ... ]). */
      template <class Set> size_t offset (const Set& set)
      {
        size_t offset = 0;
        for (size_t i = 0; i < set.ndim(); ++i)
          if (set.stride (i) < 0)
            offset += size_t (-set.stride (i)) * (set.dim (i) - 1);
        return offset;
      }

      //! calculate offset to start of data
      /*! this function caculate the offset (in number of voxels) from the start of the data region
       * to the first voxel value (i.e. at voxel [ 0 0 0 ... ]), assuming the
       * strides in \a strides and DataSet dimensions of \a set. */
      template <class Set> size_t offset (List& strides, const Set& set)
      {
        WrapperSet<Set> wrapper (strides, set);
        return offset (wrapper);
      }



      //! produce strides from \c set that match those specified in \c desired
      /*! The strides in \c desired should be specified as symbolic strides,
       * and any zero strides will be ignored and replaced with sensible values
       * if needed.  Essentially, this function checks whether the symbolic
       * strides in \c set already match those specified in \c desired. If so,
       * these will be used as-is, otherwise a new set of strides based on \c
       * desired will be produced. */
      template <class Set> List get_nearest_match (const Set& set, const List& desired)
      {
        List in (get_symbolic (set)), out (desired);
        out.resize (in.size(), 0);

        bool strides_match = true;
        for (size_t i = 0; i < out.size(); ++i) {
          if (out[i]) {
            if (Math::abs (out[i]) != Math::abs (in[i])) {
              strides_match = false;
              break;
            }
          }
        }

        if (strides_match)
          out = in;

        sanitise (out);

        return out;
      }



      //! convenience function for use with Image::BufferPreload
      /*! when passed as the second argument to the Image::BufferPreload
       * constructor, ensures the specified axis will be contiguous in RAM. */
      inline List contiguous_along_axis (size_t axis) 
      {
        List strides (axis+1,0);
        strides[axis] = 1;
        return strides;
      }



    }
  }
}

#endif

