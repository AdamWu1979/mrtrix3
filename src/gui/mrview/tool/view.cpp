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
#include "math/math.h"
#include "gui/mrview/window.h"
#include "gui/mrview/mode/base.h"
#include "gui/mrview/mode/volume.h"
#include "gui/mrview/tool/view.h"
#include "gui/mrview/adjust_button.h"

#define FOV_RATE_MULTIPLIER 0.01f
#define MRTRIX_MIN_ALPHA 1.0e-3f
#define MRTRIX_ALPHA_MULT (-MR::Math::log (MRTRIX_MIN_ALPHA)/1000.0f)


namespace {

  inline float get_alpha_from_slider (float slider_value) {
    return MRTRIX_MIN_ALPHA * MR::Math::exp (MRTRIX_ALPHA_MULT * float (slider_value));
  }

  inline float get_slider_value_from_alpha (float alpha) {
    return MR::Math::log (alpha/MRTRIX_MIN_ALPHA) / MRTRIX_ALPHA_MULT;
  }

}




namespace MR
{
  namespace GUI
  {
    namespace MRView
    {
      namespace Tool
      {


        class View::ClipPlaneModel : public QAbstractItemModel
        {
          public:

            ClipPlaneModel (QObject* parent) :
              QAbstractItemModel (parent) { }

            QVariant data (const QModelIndex& index, int role) const {
              if (!index.isValid()) return QVariant();
              if (role == Qt::CheckStateRole) {
                return planes[index.row()].active ? Qt::Checked : Qt::Unchecked;
              }
              if (role != Qt::DisplayRole) return QVariant();
              return planes[index.row()].name.c_str();
            }

            bool setData (const QModelIndex& idx, const QVariant& value, int role) {
              if (role == Qt::CheckStateRole) {
                planes[idx.row()].active = (value == Qt::Checked);
                emit dataChanged (idx, idx);
                return true;
              }
              return QAbstractItemModel::setData (idx, value, role);
            }

            Qt::ItemFlags flags (const QModelIndex& index) const {
              if (!index.isValid()) return 0;
              return Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable;
            }

            QModelIndex index (int row, int column, const QModelIndex& parent = QModelIndex()) const { return createIndex (row, column); }

            QModelIndex parent (const QModelIndex& index) const { return QModelIndex(); }

            int rowCount (const QModelIndex& parent = QModelIndex()) const { return planes.size(); }

            int columnCount (const QModelIndex& parent = QModelIndex()) const { return 1; }

            void remove (QModelIndex& index) {
              beginRemoveRows (QModelIndex(), index.row(), index.row());
              planes.erase (planes.begin() + index.row());
              endRemoveRows();
            }

            void invert (QModelIndex& index) {
              ClipPlane& p (planes[index.row()]);
              for (size_t n = 0; n < 4; ++n)
                p.plane[n] = -p.plane[n];
              if (p.name[0] == '-')
                p.name = p.name.substr (1);
              else 
                p.name = "-" + p.name;
              emit dataChanged (index, index);
            }

            void reset (QModelIndex& index, const Image::InterpVoxelType& image, int proj) {
              reset (planes[index.row()], image, proj);
            }

            void reset (ClipPlane& p, const Image::InterpVoxelType& image, int proj) {
              const Math::Matrix<float> M (image.transform());
              p.plane[0] = M (proj, 0);
              p.plane[1] = M (proj, 1);
              p.plane[2] = M (proj, 2);

              Point<> centre = image.voxel2scanner (Point<> (image.dim(0)/2.0f, image.dim(1)/2.0f, image.dim(2)/2.0f));
              p.plane[3] = centre[0]*p.plane[0] + centre[1]*p.plane[1] + centre[2]*p.plane[2];
              p.active = true;

              p.name = ( proj == 0 ? "sagittal" : ( proj == 1 ? "coronal" : "axial" ) );
            }

            void clear () {
              beginRemoveRows (QModelIndex(), 0, planes.size());
              planes.clear();
              endRemoveRows();
            }

            void add (const Image::InterpVoxelType& image, int proj) {
              ClipPlane p;
              reset (p, image, proj);
              beginInsertRows (QModelIndex(), planes.size(), planes.size() + 1);
              planes.push_back (p);
              endInsertRows();
            }

            std::vector<ClipPlane> planes;
        };




        View::View (Window& main_window, Dock* parent) : 
          Base (main_window, parent)
        {
          VBoxLayout* main_box = new VBoxLayout (this);

          QGroupBox* group_box = new QGroupBox ("FOV");
          main_box->addWidget (group_box);
          HBoxLayout* hlayout = new HBoxLayout;
          group_box->setLayout (hlayout);

          fov = new AdjustButton (this);
          connect (fov, SIGNAL (valueChanged()), this, SLOT (onSetFOV()));
          hlayout->addWidget (fov);

          plane_combobox = new QComboBox;
          plane_combobox->insertItem (0, "Sagittal");
          plane_combobox->insertItem (1, "Coronal");
          plane_combobox->insertItem (2, "Axial");
          connect (plane_combobox, SIGNAL (activated(int)), this, SLOT (onSetPlane(int)));
          hlayout->addWidget (plane_combobox);

          group_box = new QGroupBox ("Focus");
          main_box->addWidget (group_box);
          hlayout = new HBoxLayout;
          group_box->setLayout (hlayout);

          focus_x = new AdjustButton (this);
          connect (focus_x, SIGNAL (valueChanged()), this, SLOT (onSetFocus()));
          hlayout->addWidget (focus_x);

          focus_y = new AdjustButton (this);
          connect (focus_y, SIGNAL (valueChanged()), this, SLOT (onSetFocus()));
          hlayout->addWidget (focus_y);

          focus_z = new AdjustButton (this);
          connect (focus_z, SIGNAL (valueChanged()), this, SLOT (onSetFocus()));
          hlayout->addWidget (focus_z);

          group_box = new QGroupBox ("Intensity scaling");
          main_box->addWidget (group_box);
          hlayout = new HBoxLayout;
          group_box->setLayout (hlayout);

          min_entry = new AdjustButton (this);
          connect (min_entry, SIGNAL (valueChanged()), this, SLOT (onSetScaling()));
          hlayout->addWidget (min_entry);

          max_entry = new AdjustButton (this);
          connect (max_entry, SIGNAL (valueChanged()), this, SLOT (onSetScaling()));
          hlayout->addWidget (max_entry);


          GridLayout* layout;
          layout = new GridLayout;
          main_box->addLayout (layout);



          transparency_box = new QGroupBox ("Transparency");
          main_box->addWidget (transparency_box);
          VBoxLayout* vlayout = new VBoxLayout;
          transparency_box->setLayout (vlayout);

          hlayout = new HBoxLayout;
          vlayout->addLayout (hlayout);

          transparent_intensity = new AdjustButton (this);
          connect (transparent_intensity, SIGNAL (valueChanged()), this, SLOT (onSetTransparency()));
          hlayout->addWidget (transparent_intensity, 0, 0);

          opaque_intensity = new AdjustButton (this);
          connect (opaque_intensity, SIGNAL (valueChanged()), this, SLOT (onSetTransparency()));
          hlayout->addWidget (opaque_intensity);

          hlayout = new HBoxLayout;
          vlayout->addLayout (hlayout);

          hlayout->addWidget (new QLabel ("alpha"));
          opacity = new QSlider (Qt::Horizontal);
          opacity->setRange (0, 1000);
          opacity->setValue (1000);
          connect (opacity, SIGNAL (valueChanged(int)), this, SLOT (onSetTransparency()));
          hlayout->addWidget (opacity);


          threshold_box = new QGroupBox ("Thresholds");
          main_box->addWidget (threshold_box);
          hlayout = new HBoxLayout;
          threshold_box->setLayout (hlayout);

          lower_threshold_check_box = new QCheckBox (this);
          hlayout->addWidget (lower_threshold_check_box);
          lower_threshold = new AdjustButton (this);
          lower_threshold->setValue (window.image() ? window.image()->intensity_min() : 0.0);
          connect (lower_threshold_check_box, SIGNAL (clicked(bool)), this, SLOT (onCheckThreshold(bool)));
          connect (lower_threshold, SIGNAL (valueChanged()), this, SLOT (onSetTransparency()));
          hlayout->addWidget (lower_threshold);

          upper_threshold_check_box = new QCheckBox (this);
          hlayout->addWidget (upper_threshold_check_box);
          upper_threshold = new AdjustButton (this);
          upper_threshold->setValue (window.image() ? window.image()->intensity_max() : 1.0);
          connect (upper_threshold_check_box, SIGNAL (clicked(bool)), this, SLOT (onCheckThreshold(bool)));
          connect (upper_threshold, SIGNAL (valueChanged()), this, SLOT (onSetTransparency()));
          hlayout->addWidget (upper_threshold);

          // clip planes:

          clip_box = new QGroupBox ("Clip planes");
          clip_box->setCheckable (true);
          connect (clip_box, SIGNAL (toggled(bool)), this, SLOT(clip_planes_toggle_shown_slot()));
          main_box->addWidget (clip_box);
          hlayout = new HBoxLayout;
          clip_box->setLayout (hlayout);

          clip_planes_model = new ClipPlaneModel (this);
          connect (clip_planes_model, SIGNAL (dataChanged (const QModelIndex&, const QModelIndex&)),
              this, SLOT (clip_planes_selection_changed_slot()));
          connect (clip_planes_model, SIGNAL (rowsInserted (const QModelIndex&, int, int)),
              this, SLOT (clip_planes_selection_changed_slot()));
          connect (clip_planes_model, SIGNAL (rowsRemoved (const QModelIndex&, int, int)),
              this, SLOT (clip_planes_selection_changed_slot()));

          clip_planes_list_view = new QListView (this);
          clip_planes_list_view->setModel (clip_planes_model);
          clip_planes_list_view->setSelectionMode (QAbstractItemView::ExtendedSelection);
          clip_planes_list_view->setContextMenuPolicy (Qt::CustomContextMenu);
          clip_planes_list_view->setToolTip ("Right-click for more options");
          connect (clip_planes_list_view, SIGNAL (customContextMenuRequested (const QPoint&)),
              this, SLOT (clip_planes_right_click_menu_slot (const QPoint&)));
          connect (clip_planes_list_view->selectionModel(), 
              SIGNAL (selectionChanged (const QItemSelection, const QItemSelection)),
              this, SLOT (clip_planes_selection_changed_slot()));
          hlayout->addWidget (clip_planes_list_view, 1);

          QToolBar* toolbar = new QToolBar (this);
          toolbar->setOrientation (Qt::Vertical);
          toolbar->setFloatable (false);
          toolbar->setMovable (false);
          toolbar->setIconSize (QSize (16, 16));
          hlayout->addWidget (toolbar);

          // clip planes handling:

          clip_planes_option_menu = new QMenu ();
          QMenu* submenu = clip_planes_option_menu->addMenu("&New");

          QToolButton* button = new QToolButton (this);
          button->setMenu (submenu);
          button->setPopupMode (QToolButton::InstantPopup);
          button->setToolTip ("add new clip planes");
          button->setIcon (QIcon (":/new.svg"));
          toolbar->addWidget (button);

          clip_planes_new_axial_action = new QAction ("&axial", this);
          connect (clip_planes_new_axial_action, SIGNAL (triggered()), this, SLOT (clip_planes_add_axial_slot()));
          submenu->addAction (clip_planes_new_axial_action);

          clip_planes_new_sagittal_action = new QAction ("&sagittal", this);
          connect (clip_planes_new_sagittal_action, SIGNAL (triggered()), this, SLOT (clip_planes_add_sagittal_slot()));
          submenu->addAction (clip_planes_new_sagittal_action);

          clip_planes_new_coronal_action = new QAction ("&coronal", this);
          connect (clip_planes_new_coronal_action, SIGNAL (triggered()), this, SLOT (clip_planes_add_coronal_slot()));
          submenu->addAction (clip_planes_new_coronal_action);

          clip_planes_option_menu->addSeparator();

          submenu = clip_planes_reset_submenu = clip_planes_option_menu->addMenu("&Reset");

          button = new QToolButton (this);
          button->setMenu (submenu);
          button->setPopupMode (QToolButton::InstantPopup);
          button->setToolTip ("reset selected clip planes");
          button->setIcon (QIcon (":/reset.svg"));
          toolbar->addWidget (button);

          clip_planes_reset_axial_action = new QAction ("&axial", this);
          connect (clip_planes_reset_axial_action, SIGNAL (triggered()), this, SLOT (clip_planes_reset_axial_slot()));
          submenu->addAction (clip_planes_reset_axial_action);

          clip_planes_reset_sagittal_action = new QAction ("&sagittal", this);
          connect (clip_planes_reset_sagittal_action, SIGNAL (triggered()), this, SLOT (clip_planes_reset_sagittal_slot()));
          submenu->addAction (clip_planes_reset_sagittal_action);

          clip_planes_reset_coronal_action = new QAction ("&coronal", this);
          connect (clip_planes_reset_coronal_action, SIGNAL (triggered()), this, SLOT (clip_planes_reset_coronal_slot()));
          submenu->addAction (clip_planes_reset_coronal_action);


          clip_planes_invert_action = new QAction("&Invert", this);
          clip_planes_invert_action->setToolTip ("invert selected clip planes");
          clip_planes_invert_action->setIcon (QIcon (":/invert.svg"));
          connect (clip_planes_invert_action, SIGNAL (triggered()), this, SLOT (clip_planes_invert_slot()));
          clip_planes_option_menu->addAction (clip_planes_invert_action);

          button = new QToolButton (this);
          button->setDefaultAction (clip_planes_invert_action);
          toolbar->addWidget (button);

          clip_planes_remove_action = new QAction("R&emove", this);
          clip_planes_remove_action->setToolTip ("remove selected clip planes");
          clip_planes_remove_action->setIcon (QIcon (":/close.svg"));
          connect (clip_planes_remove_action, SIGNAL (triggered()), this, SLOT (clip_planes_remove_slot()));
          clip_planes_option_menu->addAction (clip_planes_remove_action);

          button = new QToolButton (this);
          button->setDefaultAction (clip_planes_remove_action);
          toolbar->addWidget (button);

          clip_planes_option_menu->addSeparator();

          clip_planes_clear_action = new QAction("&Clear", this);
          clip_planes_clear_action->setToolTip ("clear all clip planes");
          clip_planes_clear_action->setIcon (QIcon (":/clear.svg"));
          connect (clip_planes_clear_action, SIGNAL (triggered()), this, SLOT (clip_planes_clear_slot()));
          clip_planes_option_menu->addAction (clip_planes_clear_action);

          button = new QToolButton (this);
          button->setDefaultAction (clip_planes_clear_action);
          toolbar->addWidget (button);

          clip_planes_option_menu->addSeparator();

          main_box->addStretch ();
        }





        void View::showEvent (QShowEvent* event) 
        {
          connect (&window, SIGNAL (imageChanged()), this, SLOT (onImageChanged()));
          connect (&window, SIGNAL (focusChanged()), this, SLOT (onFocusChanged()));
          connect (&window, SIGNAL (planeChanged()), this, SLOT (onPlaneChanged()));
          connect (&window, SIGNAL (scalingChanged()), this, SLOT (onScalingChanged()));
          connect (&window, SIGNAL (modeChanged()), this, SLOT (onModeChanged()));
          connect (&window, SIGNAL (fieldOfViewChanged()), this, SLOT (onFOVChanged()));
          onPlaneChanged();
          onFocusChanged();
          onScalingChanged();
          onModeChanged();
          onImageChanged();
          onFOVChanged();
          clip_planes_selection_changed_slot();
        }





        void View::closeEvent (QCloseEvent* event) 
        {
          window.disconnect (this);
        }



        void View::onImageChanged () 
        {
          onScalingChanged();

          float rate = window.image()->focus_rate();
          focus_x->setRate (rate);
          focus_y->setRate (rate);
          focus_z->setRate (rate);

          set_transparency_from_image();

          lower_threshold_check_box->setChecked (window.image()->use_discard_lower());
          upper_threshold_check_box->setChecked (window.image()->use_discard_upper());
        }





        void View::onFocusChanged () 
        {
          focus_x->setValue (window.focus()[0]);
          focus_y->setValue (window.focus()[1]);
          focus_z->setValue (window.focus()[2]);
        }



        void View::onFOVChanged () 
        {
          fov->setValue (window.FOV());
          fov->setRate (FOV_RATE_MULTIPLIER * fov->value());
        }





        void View::onSetFocus () 
        {
          try {
            window.set_focus (Point<> (focus_x->value(), focus_y->value(), focus_z->value()));
            window.updateGL();
          }
          catch (Exception) { }
        }





        void View::onModeChanged () 
        {
          transparency_box->setEnabled (window.get_current_mode()->features & Mode::ShaderTransparency);
          threshold_box->setEnabled (window.get_current_mode()->features & Mode::ShaderTransparency);
          clip_box->setEnabled (window.get_current_mode()->features & Mode::ShaderClipping);
        }





        void View::onSetTransparency () 
        {
          assert (window.image()); 
          window.image()->transparent_intensity = transparent_intensity->value();
          window.image()->opaque_intensity = opaque_intensity->value();
          window.image()->alpha = get_alpha_from_slider (opacity->value());
          window.image()->lessthan = lower_threshold->value(); 
          window.image()->greaterthan = upper_threshold->value(); 
          window.updateGL();
        }





        void View::onPlaneChanged () 
        {
          plane_combobox->setCurrentIndex (window.plane());
        }





        void View::onSetPlane (int index) 
        {
          window.set_plane (index);
          window.updateGL();
        }




        void View::onCheckThreshold (bool) 
        {
          assert (window.image());
          assert (threshold_box->isEnabled());
          window.image()->set_use_discard_lower (lower_threshold_check_box->isChecked());
          window.image()->set_use_discard_upper (upper_threshold_check_box->isChecked());
          window.updateGL();
        }







        void View::set_transparency_from_image () 
        {
          if (!std::isfinite (window.image()->transparent_intensity) ||
              !std::isfinite (window.image()->opaque_intensity) ||
              !std::isfinite (window.image()->alpha) ||
              !std::isfinite (window.image()->lessthan) ||
              !std::isfinite (window.image()->greaterthan)) { // reset:
            if (!std::isfinite (window.image()->intensity_min()) || 
                !std::isfinite (window.image()->intensity_max()))
              return;

            if (!std::isfinite (window.image()->transparent_intensity))
              window.image()->transparent_intensity = window.image()->intensity_min();
            if (!std::isfinite (window.image()->opaque_intensity))
              window.image()->opaque_intensity = window.image()->intensity_max();
            if (!std::isfinite (window.image()->alpha))
              window.image()->alpha = get_alpha_from_slider (opacity->value());
            if (!std::isfinite (window.image()->lessthan))
              window.image()->lessthan = window.image()->intensity_min();
            if (!std::isfinite (window.image()->greaterthan))
              window.image()->greaterthan = window.image()->intensity_max();
          }

          assert (std::isfinite (window.image()->transparent_intensity));
          assert (std::isfinite (window.image()->opaque_intensity));
          assert (std::isfinite (window.image()->alpha));
          assert (std::isfinite (window.image()->lessthan));
          assert (std::isfinite (window.image()->greaterthan));

          transparent_intensity->setValue (window.image()->transparent_intensity);
          opaque_intensity->setValue (window.image()->opaque_intensity);
          opacity->setValue (get_slider_value_from_alpha (window.image()->alpha)); 
          lower_threshold->setValue (window.image()->lessthan);
          upper_threshold->setValue (window.image()->greaterthan);
          lower_threshold_check_box->setChecked (window.image()->use_discard_lower());
          upper_threshold_check_box->setChecked (window.image()->use_discard_upper());

          float rate = window.image() ? window.image()->scaling_rate() : 0.0;
          transparent_intensity->setRate (rate);
          opaque_intensity->setRate (rate);
          lower_threshold->setRate (rate);
          upper_threshold->setRate (rate);
        }




        void View::onSetScaling ()
        {
          if (window.image()) {
            window.image()->set_windowing (min_entry->value(), max_entry->value());
            window.updateGL();
          }
        }




        void View::onSetFOV ()
        {
          if (window.image()) {
            window.set_FOV (fov->value());
            fov->setRate (FOV_RATE_MULTIPLIER * fov->value());
            window.updateGL();
          }
        }



        void View::onScalingChanged ()
        {
          if (window.image()) {
            min_entry->setValue (window.image()->scaling_min());
            max_entry->setValue (window.image()->scaling_max());
            float rate = window.image()->scaling_rate();
            min_entry->setRate (rate);
            max_entry->setRate (rate);
          }
        }



        void View::clip_planes_right_click_menu_slot (const QPoint& pos)
        {
          QPoint globalPos = clip_planes_list_view->mapToGlobal (pos);
          QModelIndex index = clip_planes_list_view->indexAt (pos);
          clip_planes_list_view->selectionModel()->select (index, QItemSelectionModel::Select);

          clip_planes_option_menu->popup (globalPos);
        }



        void View::clip_planes_add_axial_slot () 
        {
          clip_planes_model->add (window.image()->interp, 2);
          window.updateGL();
        }

        void View::clip_planes_add_sagittal_slot () 
        {
          clip_planes_model->add (window.image()->interp, 0);
          window.updateGL();
        }

        void View::clip_planes_add_coronal_slot () 
        {
          clip_planes_model->add (window.image()->interp, 1);
          window.updateGL();
        }



        void View::clip_planes_reset_axial_slot () 
        {
          QModelIndexList indices = clip_planes_list_view->selectionModel()->selectedIndexes();
          for (int i = 0; i < indices.size(); ++i) 
            clip_planes_model->reset (indices[i], window.image()->interp, 2);
          window.updateGL();
        }


        void View::clip_planes_reset_sagittal_slot ()
        {
          QModelIndexList indices = clip_planes_list_view->selectionModel()->selectedIndexes();
          for (int i = 0; i < indices.size(); ++i) 
            clip_planes_model->reset (indices[i], window.image()->interp, 0);
          window.updateGL();
        }


        void View::clip_planes_reset_coronal_slot ()
        {
          QModelIndexList indices = clip_planes_list_view->selectionModel()->selectedIndexes();
          for (int i = 0; i < indices.size(); ++i) 
            clip_planes_model->reset (indices[i], window.image()->interp, 1);
          window.updateGL();
        }




        void View::clip_planes_invert_slot () 
        {
          QModelIndexList indices = clip_planes_list_view->selectionModel()->selectedIndexes();
          for (int i = 0; i < indices.size(); ++i) 
            clip_planes_model->invert (indices[i]);
          window.updateGL();
        }


        void View::clip_planes_remove_slot ()
        {
          QModelIndexList indices = clip_planes_list_view->selectionModel()->selectedIndexes();
          while (indices.size()) {
            clip_planes_model->remove (indices.first());
            indices = clip_planes_list_view->selectionModel()->selectedIndexes();
          }
          window.updateGL();
        }


        void View::clip_planes_clear_slot () 
        {
          clip_planes_model->clear();
          window.updateGL();
        }





        /*
           QAction* selected = clip_planes_option_menu->exec (globalPos);

           if (selected == clip_planes_clear_action) 
           clip_planes_model->clear();
           else if (selected == clip_planes_new_action[0]) 
           clip_planes_model->add (window.image()->interp, 2);
           else if (selected == clip_planes_new_action[1]) 
           clip_planes_model->add (window.image()->interp, 0);
           else if (selected == clip_planes_new_action[2]) 
           clip_planes_model->add (window.image()->interp, 1);
           else if (selected == clip_planes_help_action) {
           QMessageBox* help = new QMessageBox (QMessageBox::Information, "Clip Planes", 
           "Clip Planes are only available in Volume Render mode "
           "(along with transparency and thresholding).\n\n"
           "Adding clip planes is done through the list view "
                "context menu (right-click on the list); "
                "this provides the option of adding or removing clip "
                "planes, inverting them, or resetting them to known positions.\n\n"
                "Clip planes can be enabled via their corresponding tick-box, "
                "or more globally via the 'Clip planes' group tick box.\n\n"
                "More fine-grained manipulation is achieved by selecting the clip "
                "planes to be modified (use shift- or Ctrl- left click to select "
                "multiple clip planes), and using the same mouse gestures that would "
                "normally be used to interact with the image; if any clip planes are "
                "currently selected, they will be modified according to the actions "
                "performed, instead of the image itself. "
                "Click in a blank area of the list to return to normal operation.",
                QMessageBox::Close);
            help->setModal (false);
            help->show();
          }

          else {
            QModelIndexList indices = clip_planes_list_view->selectionModel()->selectedIndexes();
            
            if (selected == clip_planes_invert_action) {
            for (int i = 0; i < indices.size(); ++i) 
              clip_planes_model->invert (indices[i]);
            }
            else if (selected == clip_planes_remove_action) {
              while (indices.size()) {
                clip_planes_model->remove (indices.first());
                indices = clip_planes_list_view->selectionModel()->selectedIndexes();
              }
            }

            else {
              int n = (selected == clip_planes_reset_action[0]) ? 2 : ( (selected == clip_planes_reset_action[1]) ? 0 : 1 );
              QModelIndexList indices = clip_planes_list_view->selectionModel()->selectedIndexes();
              for (int i = 0; i < indices.size(); ++i) 
                clip_planes_model->reset (indices[i], window.image()->interp, n);
            }
          }

          window.updateGL();
        }
        */



        std::vector< std::pair<GL::vec4,bool> > View::get_active_clip_planes () const
        {
          std::vector< std::pair<GL::vec4,bool> > ret;
          QItemSelectionModel* selection = clip_planes_list_view->selectionModel();
          if (clip_box->isChecked()) {
            for (int i = 0; i < clip_planes_model->rowCount(); ++i) {
              if (clip_planes_model->planes[i].active) {
                std::pair<GL::vec4,bool> item;
                item.first = clip_planes_model->planes[i].plane;
                item.second = selection->isSelected (clip_planes_model->index (i,0)); 
                ret.push_back (item);
              }
            }
          }

          return ret;
        }

        std::vector<GL::vec4*> View::get_clip_planes_to_be_edited () const
        {
          std::vector<GL::vec4*> ret;
          if (clip_box->isChecked()) {
            QModelIndexList indices = clip_planes_list_view->selectionModel()->selectedIndexes();
            for (int i = 0; i < indices.size(); ++i) 
              if (clip_planes_model->planes[indices[i].row()].active)
                ret.push_back (&clip_planes_model->planes[indices[i].row()].plane);
          }
          return ret;
        }

        void View::clip_planes_selection_changed_slot () 
        {
          bool selected = clip_planes_list_view->selectionModel()->selectedIndexes().size();
          clip_planes_reset_submenu->setEnabled (selected);
          clip_planes_invert_action->setEnabled (selected);
          clip_planes_remove_action->setEnabled (selected);
          clip_planes_clear_action->setEnabled (clip_planes_model->rowCount());
          window.updateGL();
        }


        void View::clip_planes_toggle_shown_slot ()
        {
          window.updateGL();
        }

      }
    }
  }
}






