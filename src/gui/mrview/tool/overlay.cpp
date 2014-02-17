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
#include "gui/mrview/window.h"
#include "gui/mrview/mode/slice.h"
#include "gui/mrview/tool/overlay.h"
#include "gui/dialog/file.h"
#include "gui/mrview/tool/list_model_base.h"

namespace MR
{
  namespace GUI
  {
    namespace MRView
    {
      namespace Tool
      {

        class Item : public Image {
          public:
            Item (const MR::Image::Header& H) : Image (H) { }
            Mode::Slice::Shader slice_shader; 
        };




        class Overlay::Model : public ListModelBase
        {
          public:
            Model (QObject* parent) : 
              ListModelBase (parent) { }

            void add_items (VecPtr<MR::Image::Header>& list);

            Item* get_image (QModelIndex& index) {
              return dynamic_cast<Item*>(items[index.row()]);
            }
        };


        void Overlay::Model::add_items (VecPtr<MR::Image::Header>& list)
        {
          beginInsertRows (QModelIndex(), items.size(), items.size()+list.size());
          for (size_t i = 0; i < list.size(); ++i) {
            Item* overlay = new Item (*list[i]);
            overlay->set_allowed_features (true, true, false);
            if (!overlay->colourmap) 
              overlay->colourmap = 1;
            overlay->set_use_transparency (true);
            items.push_back (overlay);
          }
          endInsertRows();
        }




        Overlay::Overlay (Window& main_window, Dock* parent) :
          Base (main_window, parent) { 
            VBoxLayout* main_box = new VBoxLayout (this);
            HBoxLayout* layout = new HBoxLayout;
            layout->setContentsMargins (0, 0, 0, 0);
            layout->setSpacing (0);

            QPushButton* button = new QPushButton (this);
            button->setToolTip (tr ("Open Image"));
            button->setIcon (QIcon (":/open.svg"));
            connect (button, SIGNAL (clicked()), this, SLOT (image_open_slot ()));
            layout->addWidget (button, 1);

            button = new QPushButton (this);
            button->setToolTip (tr ("Close Image"));
            button->setIcon (QIcon (":/close.svg"));
            connect (button, SIGNAL (clicked()), this, SLOT (image_close_slot ()));
            layout->addWidget (button, 1);

            hide_all_button = new QPushButton (this);
            hide_all_button->setToolTip (tr ("Hide All"));
            hide_all_button->setIcon (QIcon (":/hide.svg"));
            hide_all_button->setCheckable (true);
            connect (hide_all_button, SIGNAL (clicked()), this, SLOT (hide_all_slot ()));
            layout->addWidget (hide_all_button, 1);

            main_box->addLayout (layout, 0);

            image_list_view = new QListView (this);
            image_list_view->setSelectionMode (QAbstractItemView::ExtendedSelection);
            image_list_view->setDragEnabled (true);
            image_list_view->viewport()->setAcceptDrops (true);
            image_list_view->setDropIndicatorShown (true);

            image_list_model = new Model (this);
            image_list_view->setModel (image_list_model);

            main_box->addWidget (image_list_view, 1);

            colourmap_combobox = new QComboBox;
            for (size_t n = 0; ColourMap::maps[n].name; ++n) 
              colourmap_combobox->insertItem (n, ColourMap::maps[n].name);
            main_box->addWidget (colourmap_combobox, 0);
            connect (colourmap_combobox, SIGNAL (activated(int)), this, SLOT (colourmap_changed(int)));


            QGroupBox* group_box = new QGroupBox ("Intensity scaling");
            main_box->addWidget (group_box);
            HBoxLayout* hlayout = new HBoxLayout;
            group_box->setLayout (hlayout);

            min_value = new AdjustButton (this);
            connect (min_value, SIGNAL (valueChanged()), this, SLOT (values_changed()));
            hlayout->addWidget (min_value);

            max_value = new AdjustButton (this);
            connect (max_value, SIGNAL (valueChanged()), this, SLOT (values_changed()));
            hlayout->addWidget (max_value);


            QGroupBox* threshold_box = new QGroupBox ("Thresholds");
            main_box->addWidget (threshold_box);
            hlayout = new HBoxLayout;
            threshold_box->setLayout (hlayout);

            lower_threshold_check_box = new QCheckBox (this);
            connect (lower_threshold_check_box, SIGNAL (stateChanged(int)), this, SLOT (lower_threshold_changed(int)));
            hlayout->addWidget (lower_threshold_check_box);
            lower_threshold = new AdjustButton (this, 0.1);
            lower_threshold->setEnabled (false);
            connect (lower_threshold, SIGNAL (valueChanged()), this, SLOT (lower_threshold_value_changed()));
            hlayout->addWidget (lower_threshold);

            upper_threshold_check_box = new QCheckBox (this);
            hlayout->addWidget (upper_threshold_check_box);
            upper_threshold = new AdjustButton (this, 0.1);
            upper_threshold->setEnabled (false);
            connect (upper_threshold_check_box, SIGNAL (stateChanged(int)), this, SLOT (upper_threshold_changed(int)));
            connect (upper_threshold, SIGNAL (valueChanged()), this, SLOT (upper_threshold_value_changed()));
            hlayout->addWidget (upper_threshold);


            opacity = new QSlider (Qt::Horizontal);
            opacity->setRange (1,1000);
            opacity->setSliderPosition (int (1000));
            connect (opacity, SIGNAL (valueChanged (int)), this, SLOT (update_slot (int)));
            main_box->addWidget (new QLabel ("opacity"), 0);
            main_box->addWidget (opacity, 0);

            interpolate_check_box = new QCheckBox (tr ("interpolate"));
            interpolate_check_box->setChecked (true);
            connect (interpolate_check_box, SIGNAL (clicked ()), this, SLOT (interpolate_changed ()));
            main_box->addWidget (interpolate_check_box, 0);

            connect (image_list_view->selectionModel(),
                SIGNAL(selectionChanged(const QItemSelection &, const QItemSelection &)),
                SLOT (selection_changed_slot(const QItemSelection &, const QItemSelection &)) );

            connect (image_list_model, SIGNAL (dataChanged (const QModelIndex&, const QModelIndex&)),
                     this, SLOT (toggle_shown_slot (const QModelIndex&, const QModelIndex&)));

            update_selection();
          }


        void Overlay::image_open_slot ()
        {
          std::vector<std::string> overlay_names = Dialog::File::get_images (this, "Select overlay images to open");
          if (overlay_names.empty())
            return;
          VecPtr<MR::Image::Header> list;
          for (size_t n = 0; n < overlay_names.size(); ++n)
            list.push_back (new MR::Image::Header (overlay_names[n]));

          add_images (list);
        }





        void Overlay::add_images (VecPtr<MR::Image::Header>& list) 
        {
          size_t previous_size = image_list_model->rowCount();
          image_list_model->add_items (list);

          QModelIndex first = image_list_model->index (previous_size, 0, QModelIndex());
          QModelIndex last = image_list_model->index (image_list_model->rowCount()-1, 0, QModelIndex());
          image_list_view->selectionModel()->select (QItemSelection (first, last), QItemSelectionModel::Select);
        }



        void Overlay::image_close_slot ()
        {
          QModelIndexList indexes = image_list_view->selectionModel()->selectedIndexes();
          while (indexes.size()) {
            image_list_model->remove_item (indexes.first());
            indexes = image_list_view->selectionModel()->selectedIndexes();
          }
          updateGL();
        }


        void Overlay::hide_all_slot () 
        {
          updateGL();
        }


        void Overlay::draw (const Projection& projection, bool is_3D)
        {
          float overlay_opacity = opacity->value() / 1.0e3f;

          if (!is_3D) {
            // set up OpenGL environment:
            gl::Enable (gl::BLEND);
            gl::Disable (gl::DEPTH_TEST);
            gl::DepthMask (gl::FALSE_);
            gl::ColorMask (gl::TRUE_, gl::TRUE_, gl::TRUE_, gl::TRUE_);
            gl::BlendFunc (gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
            gl::BlendEquation (gl::FUNC_ADD);
            gl::BlendColor (1.0f, 1.0f, 1.0f, overlay_opacity);
          }

          bool need_to_update = false;
          for (int i = 0; i < image_list_model->rowCount(); ++i) {
            if (image_list_model->items[i]->show && !hide_all_button->isChecked()) {
              Item* image = dynamic_cast<Item*>(image_list_model->items[i]);
              need_to_update |= !std::isfinite (image->intensity_min());
              image->set_interpolate (interpolate_check_box->isChecked());
              image->alpha = overlay_opacity;
              image->transparent_intensity = image->opaque_intensity = image->intensity_min();
              if (is_3D) 
                window.get_current_mode()->overlays_for_3D.push_back (image);
              else
                image->render3D (image->slice_shader, projection, projection.depth_of (window.focus()));
            }
          }

          if (need_to_update)
            update_selection();

          if (!is_3D) {
            // restore OpenGL environment:
            gl::Disable (gl::BLEND);
            gl::Enable (gl::DEPTH_TEST);
            gl::DepthMask (gl::TRUE_);
          }
        }





        void Overlay::toggle_shown_slot (const QModelIndex& index, const QModelIndex& index2) {
          if (index.row() == index2.row()) {
            image_list_view->setCurrentIndex(index);
          } else {
            for (size_t i = 0; i < image_list_model->items.size(); ++i) {
              if (image_list_model->items[i]->show) {
                image_list_view->setCurrentIndex (image_list_model->index (i, 0));
                break;
              }
            }
          }
          updateGL();
        }


        void Overlay::update_slot (int unused) {
          updateGL();
        }

        void Overlay::colourmap_changed (int index) 
        {
          QModelIndexList indices = image_list_view->selectionModel()->selectedIndexes();
          for (int i = 0; i < indices.size(); ++i) {
            Image* overlay = dynamic_cast<Image*> (image_list_model->get_image (indices[i]));
            overlay->set_colourmap (index);
          }
          updateGL();
        }


        void Overlay::values_changed ()
        {
          QModelIndexList indices = image_list_view->selectionModel()->selectedIndexes();
          for (int i = 0; i < indices.size(); ++i) {
            Image* overlay = dynamic_cast<Image*> (image_list_model->get_image (indices[i]));
            overlay->set_windowing (min_value->value(), max_value->value());
          }
          updateGL();
        }


        void Overlay::lower_threshold_changed (int unused)
        {
          QModelIndexList indices = image_list_view->selectionModel()->selectedIndexes();
          for (int i = 0; i < indices.size(); ++i) {
            Image* overlay = dynamic_cast<Image*> (image_list_model->get_image (indices[i]));
            overlay->lessthan = lower_threshold->value();
            overlay->set_use_discard_lower (lower_threshold_check_box->isChecked());
          }
          lower_threshold->setEnabled (indices.size() && lower_threshold_check_box->isChecked());
          updateGL();
        }


        void Overlay::upper_threshold_changed (int unused)
        {
          QModelIndexList indices = image_list_view->selectionModel()->selectedIndexes();
          for (int i = 0; i < indices.size(); ++i) {
            Image* overlay = dynamic_cast<Image*> (image_list_model->get_image (indices[i]));
            overlay->greaterthan = upper_threshold->value();
            overlay->set_use_discard_upper (upper_threshold_check_box->isChecked());
          }
          upper_threshold->setEnabled (indices.size() && upper_threshold_check_box->isChecked());
          updateGL();
        }



        void Overlay::lower_threshold_value_changed ()
        {
          if (lower_threshold_check_box->isChecked()) {
            QModelIndexList indices = image_list_view->selectionModel()->selectedIndexes();
            for (int i = 0; i < indices.size(); ++i) {
              Image* overlay = dynamic_cast<Image*> (image_list_model->get_image (indices[i]));
              overlay->lessthan = lower_threshold->value();
            }
          }
          updateGL();
        }



        void Overlay::upper_threshold_value_changed ()
        {
          if (upper_threshold_check_box->isChecked()) {
            QModelIndexList indices = image_list_view->selectionModel()->selectedIndexes();
            for (int i = 0; i < indices.size(); ++i) {
              Image* overlay = dynamic_cast<Image*> (image_list_model->get_image (indices[i]));
              overlay->greaterthan = upper_threshold->value();
            }
          }
          updateGL();
        }


        void Overlay::interpolate_changed ()
        {
          window.updateGL();
        }


        void Overlay::selection_changed_slot (const QItemSelection &, const QItemSelection &)
        {
          update_selection();
        }



        void Overlay::update_selection () 
        {
          QModelIndexList indices = image_list_view->selectionModel()->selectedIndexes();
          colourmap_combobox->setEnabled (indices.size());
          max_value->setEnabled (indices.size());
          min_value->setEnabled (indices.size());
          lower_threshold_check_box->setEnabled (indices.size());
          upper_threshold_check_box->setEnabled (indices.size());
          lower_threshold->setEnabled (indices.size());
          upper_threshold->setEnabled (indices.size());

          if (!indices.size())
            return;

          float rate = 0.0f, min_val = 0.0f, max_val = 0.0f;
          float lower_threshold_val = 0.0f, upper_threshold_val = 0.0f;
          int num_lower_threshold = 0, num_upper_threshold = 0;
          int colourmap_index = -2;
          for (int i = 0; i < indices.size(); ++i) {
            Image* overlay = dynamic_cast<Image*> (image_list_model->get_image (indices[i]));
            if (colourmap_index != int(overlay->colourmap)) {
              if (colourmap_index == -2)
                colourmap_index = overlay->colourmap;
              else 
                colourmap_index = -1;
            }
            rate += overlay->scaling_rate();
            min_val += overlay->scaling_min();
            max_val += overlay->scaling_max();
            num_lower_threshold += overlay->use_discard_lower();
            num_upper_threshold += overlay->use_discard_upper();
            if (!std::isfinite (overlay->lessthan))
              overlay->lessthan = overlay->intensity_min();
            if (!std::isfinite (overlay->greaterthan)) 
              overlay->greaterthan = overlay->intensity_max();
            lower_threshold_val += overlay->lessthan;
            upper_threshold_val += overlay->greaterthan;
          }

          rate /= indices.size();
          min_val /= indices.size();
          max_val /= indices.size();
          lower_threshold_val /= indices.size();
          upper_threshold_val /= indices.size();

          colourmap_combobox->setCurrentIndex (colourmap_index);

          min_value->setRate (rate);
          max_value->setRate (rate);
          min_value->setValue (min_val);
          max_value->setValue (max_val);

          lower_threshold->setValue (lower_threshold_val);
          lower_threshold_check_box->setCheckState (num_lower_threshold ?
              ( num_lower_threshold == indices.size() ?
                Qt::Checked :
                Qt::PartiallyChecked ) : 
              Qt::Unchecked);
          lower_threshold->setRate (rate);

          upper_threshold->setValue (upper_threshold_val);
          upper_threshold_check_box->setCheckState (num_upper_threshold ?
              ( num_upper_threshold == indices.size() ?
                Qt::Checked :
                Qt::PartiallyChecked ) : 
              Qt::Unchecked);
          upper_threshold->setRate (rate);
        }







        bool Overlay::process_batch_command (const std::string& cmd, const std::string& args)
        {

          // BATCH_COMMAND overlay.load path # Loads the specified image on the overlay tool.
          if (cmd == "overlay.load") {
            VecPtr<MR::Image::Header> list;
            try { list.push_back (new MR::Image::Header (args)); }
            catch (Exception& e) { e.display(); }
            add_images (list);
            return true;
          }

          // BATCH_COMMAND overlay.opacity value # Sets the overlay opacity to floating value [0-1].
          else if (cmd == "overlay.opacity") {
            try {
              float n = to<float> (args);
              opacity->setSliderPosition(int(1.e3f*n));
            }
            catch (Exception& e) { e.display(); }
            return true;
          }

          return false;
        }


      }
    }
  }
}





