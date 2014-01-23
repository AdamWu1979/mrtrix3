/*
   Copyright 2009 Brain Research Institute, Melbourne, Australia

   Written by J-Donald Tournier, 13/11/09.

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

#include "mrtrix.h"
#include "math/vector.h"
#include "gui/mrview/mode/ortho.h"
#include "gui/cursor.h"

namespace MR
{
  namespace GUI
  {
    namespace MRView
    {
      namespace Mode
      {




        void Ortho::paint (Projection& projection)
        {
          // set up OpenGL environment:
          gl::Disable (gl::BLEND);
          gl::Disable (gl::DEPTH_TEST);
          gl::DepthMask (gl::FALSE_);
          gl::ColorMask (gl::TRUE_, gl::TRUE_, gl::TRUE_, gl::TRUE_);

          GLint w = glarea()->width()/2;
          GLint h = glarea()->height()/2;

          projections[0].set_viewport (w, h, w, h); 
          draw_plane (0, slice_shader, projections[0]);
          projections[1].set_viewport (0, h, w, h); 
          draw_plane (1, slice_shader, projections[1]);
          projections[2].set_viewport (0, 0, w, h); 
          draw_plane (2, slice_shader, projections[2]);

          projection.set_viewport ();

          GL::mat4 MV = GL::identity();
          GL::mat4 P = GL::ortho (0, glarea()->width(), 0, glarea()->height(), -1.0, 1.0);
          projection.set (MV, P);

          gl::Disable (gl::DEPTH_TEST);
          gl::LineWidth (2.0);

          if (!frame_VB || !frame_VAO) {
            frame_VB.gen();
            frame_VAO.gen();

            frame_VB.bind (gl::ARRAY_BUFFER);
            frame_VAO.bind();

            gl::EnableVertexAttribArray (0);
            gl::VertexAttribPointer (0, 2, gl::FLOAT, gl::FALSE_, 0, (void*)0);

            GLfloat data [] = {
              -1.0f, 0.0f,
              1.0f, 0.0f,
              0.0f, -1.0f,
              0.0f, 1.0f
            };
            gl::BufferData (gl::ARRAY_BUFFER, sizeof(data), data, gl::STATIC_DRAW);
          }
          else 
            frame_VAO.bind();

          if (!frame_program) {
            GL::Shader::Vertex vertex_shader (
                "layout(location=0) in vec2 pos;\n"
                "void main () {\n"
                "  gl_Position = vec4 (pos, 0.0, 1.0);\n"
                "}\n");
            GL::Shader::Fragment fragment_shader (
                "out vec3 color;\n"
                "void main () {\n"
                "  color = vec3 (0.1);\n"
                "}\n");
            frame_program.attach (vertex_shader);
            frame_program.attach (fragment_shader);
            frame_program.link();
          }

          frame_program.start();
          gl::DrawArrays (gl::LINES, 0, 4);
          frame_program.stop();

          gl::Enable (gl::DEPTH_TEST);
        }





        const Projection* Ortho::get_current_projection () const  
        {
          if (current_plane < 0 || current_plane > 2)
            return NULL;
          return &projections[current_plane];
        }



        void Ortho::mouse_press_event ()
        {
          if (window.mouse_position().x() < glarea()->width()/2) 
            if (window.mouse_position().y() >= glarea()->height()/2) 
              current_plane = 1;
            else 
              current_plane = 2;
          else 
            if (window.mouse_position().y() >= glarea()->height()/2)
              current_plane = 0;
            else 
              current_plane = -1;
        }




        void Ortho::slice_move_event (int x) 
        {
          const Projection* proj = get_current_projection();
          if (!proj) return;
          move_in_out (x * std::min (std::min (image()->header().vox(0), image()->header().vox(1)), image()->header().vox(2)), *proj);
          updateGL();
        }


        void Ortho::panthrough_event ()
        {
          const Projection* proj = get_current_projection();
          if (!proj) return;
          move_in_out_FOV (window.mouse_displacement().y(), *proj);
          updateGL();
        }


      }
    }
  }
}



