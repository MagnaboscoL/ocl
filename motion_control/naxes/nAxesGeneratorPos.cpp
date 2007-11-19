// $Id: nAxisGeneratorCartesianPos.hpp,v 1.1.1.1 2003/12/02 20:32:06 kgadeyne Exp $
// Copyright (C) 2003 Klaas Gadeyne <klaas.gadeyne@mech.kuleuven.ac.be>
//                    Wim Meeussen  <wim.meeussen@mech.kuleuven.ac.be>
// Copyright (C) 2006 Ruben Smits <ruben.smits@mech.kuleuven.ac.be>
//  
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//  
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//  
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
//  

#include "nAxesGeneratorPos.hpp"
#include <assert.h>

namespace OCL
{
    using namespace RTT;
    using namespace KDL;
    using namespace std;
    typedef nAxesGeneratorPos MyType;

    nAxesGeneratorPos::nAxesGeneratorPos(string name,unsigned int num_axes,
                                         string propertyfile)
        : TaskContext(name),
          _num_axes(num_axes), 
          _propertyfile(propertyfile),
          _position_meas_local(num_axes),
          _position_desi_local(num_axes),
          _velocity_desi_local(num_axes),
          _moveTo( "moveTo",&MyType::moveTo,&MyType::moveFinished, this),
          _reset_position( "resetPosition", &MyType::resetPosition, this),
          _position_meas("nAxesSensorPosition"),
          _position_desi("nAxesDesiredPosition"),
          _velocity_desi("nAxesDesiredVelocity"),
          _motion_profile(num_axes),
          _maximum_velocity("max_vel", "Maximum Velocity in Trajectory",vector<double>(num_axes,0)),
          _maximum_acceleration("max_acc", "Maximum Acceleration in Trajectory",vector<double>(num_axes,0))
    {
        //Creating TaskContext
        
        //Adding properties
        this->properties()->addProperty(&_maximum_velocity);
        this->properties()->addProperty(&_maximum_acceleration);
        
        //Adding ports
        this->ports()->addPort(&_position_meas);
        this->ports()->addPort(&_position_desi);
        this->ports()->addPort(&_velocity_desi);
    
        //Adding Commands
        this->commands()->addCommand( &_moveTo,"Set the position setpoint",
                                      "setpoint", "joint setpoint for all axes",
                                      "time", "minimum time to complete trajectory" );

        //Adding Methods
        this->methods()->addMethod( &_reset_position, "Reset generator position" );  

    if(!marshalling()->readProperties(_propertyfile))
      log(Error) <<"(nAxesGeneratorPos) Reading Properties from "<<_propertyfile<<" failed!!"<<endlog();

    // Instantiate Motion Profiles
    for( unsigned int i=0; i<_num_axes; i++)
      _motion_profile[i] = new VelocityProfile_Trap( 0, 0 );
  }
    
  nAxesGeneratorPos::~nAxesGeneratorPos()
  {
    for( unsigned int i=0; i<_num_axes; i++)
      if( _motion_profile[i]) delete _motion_profile[i];
  }
  
  bool nAxesGeneratorPos::startup()
  {

    //Check if readPort is connected
    if (!_position_meas.connected())
      log(Warning) <<"(nAxesGeneratorPos) Port "<<_position_meas.getName()<<" not connected"<<endlog();
    
    
    // check size of properties
    if(_maximum_velocity.value().size() != _num_axes || _maximum_acceleration.value().size() != _num_axes){
      log(Error) <<"Sizes of properies not equal to num_axes"<<endlog();
      return false;
    }
    
    for( unsigned int i=0; i<_num_axes; i++)
      _motion_profile[i]->SetMax(_maximum_velocity.value()[i], _maximum_acceleration.value()[i]);
    
    // initialize
    _position_desi_local = _position_meas.Get();
    for(unsigned int i = 0; i < _num_axes; i++){
      _velocity_desi_local[i] = 0;
    }
    _position_desi.Set(_position_desi_local);
    _velocity_desi.Set(_velocity_desi_local);
    
    _is_moving = false;
    
    return true;
  }
  
  void nAxesGeneratorPos::update()
  {
    if (_is_moving)
      {
	_time_passed = TimeService::Instance()->secondsSince(_time_begin);
	if ( _time_passed > _max_duration )// Profile is ended
	  {
	    // set end position
	    for (unsigned int i=0; i<_num_axes; i++){
	      _position_desi_local[i] = _motion_profile[i]->Pos( _max_duration );
	      _velocity_desi_local[i] = 0;//_motion_profile[i]->Vel( _max_duration );
	      _is_moving = false;
	    }
	  }
	else
	  {
	    for(unsigned int i=0; i<_num_axes; i++){
	      _position_desi_local[i] = _motion_profile[i]->Pos( _time_passed );
	      _velocity_desi_local[i] = _motion_profile[i]->Vel( _time_passed );
	    }
	  }
	_position_desi.Set(_position_desi_local);
	_velocity_desi.Set(_velocity_desi_local);
      }
  }
  
  
  void nAxesGeneratorPos::shutdown()
  {
  
  }
  
  bool nAxesGeneratorPos::moveTo(const vector<double>& position, double time)
  {
      // if previous movement is finished
      if (!_is_moving){
          assert(position.size() == _num_axes);
          _max_duration = 0;
          // get current position/
          _position_meas_local = _position_meas.Get();
          for (unsigned int i=0; i<_num_axes; i++){
              // Set motion profiles
              _motion_profile[i]->SetProfileDuration( _position_meas_local[i], position[i], time );
              // Find lengthiest trajectory
              _max_duration = max( _max_duration, _motion_profile[i]->Duration() );
          }
          // Rescale trajectories to maximal duration
          for(unsigned int i = 0; i < _num_axes; i++)
              _motion_profile[i]->SetProfileDuration( _position_meas_local[i], position[i], _max_duration );
          
          _time_begin = TimeService::Instance()->getTicks();
          _time_passed = 0;
          
          _is_moving = true;
          return true;
      }
      // still moving
      else
          return false;
  }
  
  bool nAxesGeneratorPos::moveFinished() const
  {
    return (!_is_moving);
  }
  
  void nAxesGeneratorPos::resetPosition()
  {
    _position_desi_local = _position_meas.Get();
    for(unsigned int i = 0; i < _num_axes; i++)
      _velocity_desi_local[i] = 0;
    _position_desi.Set(_position_desi_local);
    _velocity_desi.Set(_velocity_desi_local);
    _is_moving = false;
  }
}//namespace




