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

#ifndef __dwi_tractography_file_h__
#define __dwi_tractography_file_h__

#include <fstream>
#include <map>
#include <vector>

#include "app.h"
#include "types.h"
#include "point.h"
#include "file/key_value.h"
#include "dwi/tractography/file_base.h"
#include "dwi/tractography/properties.h"
#include "dwi/tractography/streamline.h"
#include "math/vector.h"


namespace MR
{
  namespace DWI
  {
    namespace Tractography
    {



      //! A class to read streamlines data
      template <typename T = float> 
        class Reader : public __ReaderBase__
      {
        public:
          typedef T value_type;

          //! open the \c file for reading and load header into \c properties
          Reader (const std::string& file, Properties& properties) :
            current_index (0) {
              open (file, "tracks", properties);
              App::Options opt = App::get_options ("tck_weights_in");
              if (opt.size()) {
                weights.load (opt[0][0]);
                if (weights.size() != to<size_t> (properties["count"])) // TODO: turn this into Exception once count write-out at commit-time works.
                  WARN ("number of weights does not match number of tracks in file");
                DEBUG ("loaded " + str(weights.size()) + " track weights from file \"" + std::string (opt[0][0]) + "\"");
              }
            }


          //! fetch next track from file
          bool operator() (Streamline<value_type>& tck) {
            tck.clear();

            if (!in.is_open())
              return false;

            do {
              Point<value_type> p = get_next_point();
              if (isinf (p[0])) {
                in.close();
                return false;
              }
              if (in.eof()) {
                in.close();
                return false;
              }

              if (isnan (p[0])) {
                tck.index = current_index;
                if (weights.size()) {
                  if (current_index >= weights.size())
                    return false;
                  tck.weight = weights[current_index];
                } 
                else 
                  tck.weight = 1.0;

                ++current_index;
                return true;
              }

              tck.push_back (p);
            } while (in.good());

            in.close();
            return false;
          }



        protected:
          using __ReaderBase__::in;
          using __ReaderBase__::dtype;

          size_t current_index;
          Math::Vector<value_type> weights;

          //! takes care of byte ordering issues
          Point<value_type> get_next_point ()
          { 
            using namespace ByteOrder;
            switch (dtype()) {
              case DataType::Float32LE: 
                {
                  Point<float> p;
                  in.read ((char*) &p, sizeof (p));
                  return (Point<value_type> (LE(p[0]), LE(p[1]), LE(p[2])));
                }
              case DataType::Float32BE:
                {
                  Point<float> p;
                  in.read ((char*) &p, sizeof (p));
                  return (Point<value_type> (BE(p[0]), BE(p[1]), BE(p[2])));
                }
              case DataType::Float64LE:
                {
                  Point<double> p;
                  in.read ((char*) &p, sizeof (p));
                  return (Point<value_type> (LE(p[0]), LE(p[1]), LE(p[2])));
                }
              case DataType::Float64BE:
                {
                  Point<double> p;
                  in.read ((char*) &p, sizeof (p));
                  return (Point<value_type> (BE(p[0]), BE(p[1]), BE(p[2])));
                }
              default:
                assert (0);
                break;
            }
            return (Point<value_type>());
          }

          //! copy construction explicitly disabled
          Reader (const Reader& R) : current_index (0) { assert (0); }

      };







      //! class to handle unbuffered writing of tracks to file
      /*! writes track header as specified in \a properties and individual
       * tracks to the file specified in \a file. Writing individual tracks is
       * done using the operator() method.
       *
       * This class re-opens the output file every time a new streamline is
       * written. This may result in slow operation in some circumstances, and
       * may lead to fragmentation on some file systems, but is necessary in
       * use cases where a very large number of track files are being written
       * at once. For most applications (where typically one track file is
       * written at a time), the Writer class is more appropriate.
       * */
      template <typename T = float>
        class WriterUnbuffered : public __WriterBase__ <T>
      {
        public:
          typedef T value_type;
          using __WriterBase__<T>::count;
          using __WriterBase__<T>::total_count;
          using __WriterBase__<T>::name;
          using __WriterBase__<T>::dtype;
          using __WriterBase__<T>::create;
          using __WriterBase__<T>::verify_stream;
          using __WriterBase__<T>::update_counts;

          //! create a new track file with the specified properties
          WriterUnbuffered (const std::string& file, const Properties& properties) :
            __WriterBase__<T> (file)
        {
          std::ofstream out (name.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
          if (!out)
            throw Exception ("error creating tracks file \"" + name + "\": " + strerror (errno));

          create (out, properties, "tracks");
          barrier_addr = out.tellp();

          Point<value_type> x;
          format_point (barrier(), x);
          out.write (reinterpret_cast<char*> (&x[0]), sizeof (x));
          if (!out.good())
            throw Exception ("error writing tracks file \"" + name + "\": " + strerror (errno));

          App::Options opt = App::get_options ("tck_weights_out");
          if (opt.size())
            set_weights_path (opt[0][0]);
        }

          //virtual ~WriterUnbuffered() { }

          //! append track to file
          bool operator() (const Streamline<value_type>& tck) {
            if (tck.size()) {
              // allocate buffer on the stack for performance:
              NON_POD_VLA (buffer, Point<value_type>, tck.size()+2);
              for (size_t n = 0; n < tck.size(); ++n)
                format_point (barrier(), buffer[n]); 
              format_point (delimiter(), buffer[tck.size()]);

              commit (buffer, tck.size()+1);

              if (weights_name.size()) 
                write_weights (str(tck.weight) + "\n");

              ++count;
            }
            ++total_count;
            return true;
          }


          //! set the path to the track weights
          void set_weights_path (const std::string& path) {
            if (weights_name.size())
              throw Exception ("Cannot change output streamline weights file path");
            weights_name = path;
            if (!App::overwrite_files && Path::exists (name))
              throw Exception ("error creating file \"" + weights_name + "\": file exists (use -force option to force overwrite)");
            std::ofstream out (weights_name.c_str(), std::ios::out | std::ios::binary | std::ios::trunc);
            if (!out)
              throw Exception ("error creating empty streamline weights file \"" + weights_name + "\": " + strerror (errno));
          }

        protected:
          std::string weights_name;
          int64_t barrier_addr;

          //! indicates end of track and start of new track
          Point<value_type> delimiter () const { return Point<value_type> (NAN, NAN, NAN); }
          //! indicates end of data
          Point<value_type> barrier   () const { return Point<value_type> (INFINITY, INFINITY, INFINITY); }

          //! perform per-point byte-swapping if required
          void format_point (const Point<value_type>& src, Point<value_type>& dest) {
            using namespace ByteOrder;
            if (dtype.is_little_endian()) 
              dest.set (LE(src[0]), LE(src[1]), LE(src[2]));
            else
              dest.set (BE(src[0]), BE(src[1]), BE(src[2]));
          }

          //! write track weights data to file
          void write_weights (const std::string& contents) {
            std::ofstream out (weights_name.c_str(), std::ios::in | std::ios::out | std::ios::binary | std::ios::ate);
            if (!out)
              throw Exception ("error re-opening streamline weights file \"" + weights_name + "\": " + strerror (errno));
            out << contents;
            if (!out.good())
              throw Exception ("error writing streamline weights file \"" + weights_name + "\": " + strerror (errno));
          }


          //! write track point data to file
          /*! \note \c buffer needs to be greater than \c num_points by one
           * element to add the barrier. */
          void commit (Point<value_type>* data, size_t num_points) {
            if (num_points == 0) 
              return;

            int64_t prev_barrier_addr = barrier_addr;

            format_point (barrier(), data[num_points]);
            std::ofstream out (name.c_str(), std::ios::in | std::ios::out | std::ios::binary | std::ios::ate);
            if (!out)
              throw Exception ("error re-opening tracks file \"" + name + "\": " + strerror (errno));

            out.write (reinterpret_cast<const char* const> (data+1), sizeof (Point<value_type>) * num_points);
            verify_stream (out);
            barrier_addr = int64_t (out.tellp()) - sizeof(Point<value_type>);
            out.seekp (prev_barrier_addr, out.beg);
            out.write (reinterpret_cast<const char* const> (data), sizeof(Point<value_type>));
            verify_stream (out);
            update_counts (out);
          }


          //! copy construction explicitly disabled
          WriterUnbuffered (const WriterUnbuffered& W) :
            barrier_addr (0) { 
              assert (0); 
            }
      };





      //! class to handle writing tracks to file, with RAM buffer
      /*! writes track header as specified in \a properties and individual
       * tracks to the file specified in \a file. Writing individual tracks is
       * done using the append() method.
       *
       * This class implements a large write-back RAM buffer to hold the track
       * data in RAM, and only commits to file when the buffer capacity is
       * reached. This minimises the number of write() calls, which can
       * otherwise become a bottleneck on distributed or network filesystems.
       * It also helps reduce file fragmentation when multiple processes write
       * to file concurrently. The size of the write-back buffer defaults to
       * 16MB, and can be set in the config file using the
       * TrackWriterBufferSize field (in bytes). 
       * */
      template <typename T = float> 
        class Writer : public WriterUnbuffered<T>
      {
        public:
          typedef T value_type;
          using __WriterBase__<T>::count;
          using __WriterBase__<T>::total_count;
          using WriterUnbuffered<T>::delimiter;
          using WriterUnbuffered<T>::format_point;
          using WriterUnbuffered<T>::weights_name;
          using WriterUnbuffered<T>::write_weights;

          //! create new RAM-buffered track file with specified properties
          /*! the capacity of the RAM buffer can be specified as a config file
           * option (TrackWriterBufferSize), or in the constructor by
           * specifying a value in bytes for \c default_buffer_capacity
           * (default is 16M). */
          Writer (const std::string& file, const Properties& properties, size_t default_buffer_capacity = 16777216) :
            WriterUnbuffered<T> (file, properties), 
            buffer_capacity (File::Config::get_int ("TrackWriterBufferSize", default_buffer_capacity) / sizeof (Point<value_type>)),
            buffer (new Point<value_type> [buffer_capacity+2]),
            buffer_size (0) { }

          //! commits any remaining data to file
          ~Writer() {
            commit();
          }

          //! append track to file
          bool operator() (const Streamline<value_type>& tck) {
            if (tck.size()) {
              if (buffer_size + tck.size() > buffer_capacity)
                commit ();

              for (typename std::vector<Point<value_type> >::const_iterator i = tck.begin(); i != tck.end(); ++i)
                add_point (*i);
              add_point (delimiter());

              if (weights_name.size())
                weights_buffer += str (tck.weight) + ' ';

              ++count;
            }
            ++total_count;
            return true;
          }


        protected:
          const size_t buffer_capacity;
          Ptr<Point<value_type>,true> buffer;
          size_t buffer_size;
          std::string weights_buffer;

          //! add point to buffer and increment buffer_size accordingly 
          void add_point (const Point<value_type>& p) {
            format_point (p, buffer[buffer_size++]);
          }

          void commit () {
            WriterUnbuffered<T>::commit (buffer, buffer_size);
            buffer_size = 0;

            if (weights_name.size()) {
              write_weights (weights_buffer);
              weights_buffer.clear();
            }
          }


          //! copy construction explicitly disabled
          Writer (const Writer& W) : 
            WriterUnbuffered<value_type> (W),
            buffer_capacity (W.buffer_capacity), 
            buffer_size (0) {
              assert (0); 
            }
      };



    }
  }
}


#endif

