/*
 * Copyright (c) 2008-2018 the MRtrix3 contributors.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, you can obtain one at http://mozilla.org/MPL/2.0/
 *
 * MRtrix3 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * For more details, see http://www.mrtrix.org/
 */


#ifndef __surface_meshfactory_h__
#define __surface_meshfactory_h__


#include "surface/mesh.h"


namespace MR
{

namespace Surface
{


template < class T > 
class Singleton
{

  public:

    static T& getInstance() 
    {    
      static T instance;
      return instance;
    }

};


class MeshFactory : public Singleton< MeshFactory >
{

  public:

    ~MeshFactory();

    Mesh box( const Eigen::Vector3d& lowerPoint,
              const Eigen::Vector3d& upperPoint ) const;

    Mesh sphere( const Eigen::Vector3d& centre, const double& radius,
                 const size_t& level = 0 ) const;

    Mesh concatenate( const std::vector< Mesh >& meshes ) const;

    void crop( Mesh& mesh, const std::vector< uint32_t >& vert_index ) const;

  protected:
  
    friend class Singleton< MeshFactory >;
    
    MeshFactory();

};


}

}

#endif

