/* Copyright (c) 2015, Stefan Isler, islerstefan@bluewin.ch
*
This file is part of hand_eye_calibration, a ROS package for hand eye calibration,

hand_eye_calibration is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.
hand_eye_calibration is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU Lesser General Public License for more details.
You should have received a copy of the GNU Lesser General Public License
along with hand_eye_calibration. If not, see <http://www.gnu.org/licenses/>.
*/


#include <string>
#include <control_msgs/FollowJointTrajectoryAction.h>
#include <boost/foreach.hpp>
#include "dense_reconstruction/youbot_reconstruction_controller.h"
#include "utils/ros_eigen.h"
#include <moveit/trajectory_processing/iterative_time_parameterization.h>


YoubotReconstructionController::YoubotReconstructionController( ros::NodeHandle* _n ):
  ros_node_(_n),
  tf_listener_(*_n)
{
  planning_group_ = "arm";
  
  robot_ = boost::shared_ptr<moveit::planning_interface::MoveGroup>( new moveit::planning_interface::MoveGroup(planning_group_) );
  
  
  //setEndEffectorPlanningFrame("camera");
  base_planning_frame_="odom";
  
  scene_ = boost::shared_ptr<planning_scene_monitor::PlanningSceneMonitor>( new planning_scene_monitor::PlanningSceneMonitor("robot_description") );
  scene_->startStateMonitor();
  scene_->startSceneMonitor();
  scene_->startWorldGeometryMonitor();
  
  // block everything until complete tf tree is available: this doesn't help
  //tf_listener_.waitForTransform( "/base_footprint","/camera", ros::Time::now(), ros::Duration(0.0) );
}

bool YoubotReconstructionController::runSingleIteration()
{
  ROS_INFO("Calculate next position...");
  
  ros::spinOnce();
  
  if( !planAndMove() ) // don't have to move for first position
    return true; // no calculation for current new pos, but still new positions available
  
  
  ros::Duration(1).sleep(); // sleep one second - allow robot to move
  
  return true;
}

movements::Pose YoubotReconstructionController::getEndEffectorPoseFromTF( ros::Duration _max_wait_time )
{
  bool new_tf_available = tf_listener_.waitForTransform( "/base_footprint","/camera", ros::Time::now(), ros::Duration(3.0) );
  
  if( !new_tf_available )
    return movements::Pose();
  
  tf::StampedTransform end_effector_pose_tf;
  geometry_msgs::TransformStamped end_effector_pose_ros;
  
  try{
    tf_listener_.lookupTransform("/base_footprint", "/camera", ros::Time(0), end_effector_pose_tf);
  }
  catch (tf::TransformException ex){
    ROS_ERROR("%s",ex.what());
    return movements::Pose();
  }
  
  tf::transformStampedTFToMsg( end_effector_pose_tf, end_effector_pose_ros );
  
  Eigen::Quaterniond rotation = st_is::geometryToEigen( end_effector_pose_ros.transform.rotation );
  Eigen::Vector3d translation = st_is::geometryToEigen( end_effector_pose_ros.transform.translation );
  
  return movements::Pose( translation, rotation );
}

movements::Pose YoubotReconstructionController::getCurrentLinkPose( std::string _link )
{
  bool new_tf_available = tf_listener_.waitForTransform( base_planning_frame_,_link, ros::Time::now(), ros::Duration(3.0) );
  
  if( !new_tf_available )
    return movements::Pose();
  
  tf::StampedTransform end_effector_pose_tf;
  geometry_msgs::TransformStamped end_effector_pose_ros;
  
  try{
    tf_listener_.lookupTransform(base_planning_frame_, _link, ros::Time(0), end_effector_pose_tf);
  }
  catch (tf::TransformException ex){
    ROS_ERROR("%s",ex.what());
    return movements::Pose();
  }
  
  tf::transformStampedTFToMsg( end_effector_pose_tf, end_effector_pose_ros );
  
  Eigen::Quaterniond rotation = st_is::geometryToEigen( end_effector_pose_ros.transform.rotation );
  Eigen::Vector3d translation = st_is::geometryToEigen( end_effector_pose_ros.transform.translation );
  
  return movements::Pose( translation, rotation );
}

bool YoubotReconstructionController::makeScan()
{
  geometry_msgs::Pose current_pose = robot_->getCurrentPose().pose;
  movements::Pose base_pose = movements::fromROS(current_pose);
  
  geometry_msgs::Pose link_4_pose = robot_->getCurrentPose("arm_link_4").pose;
  movements::Pose link_4 = movements::fromROS(link_4_pose);
  // 0.05m radius, 4 turns/sec, 0.025 m/s radial speed ->2s to reach limit
  movements::KinMove scan = movements::InOutSpiral::create( link_4.orientation, 0.05, 4*6.283185307, 0.025, movements::InOutSpiral::ZXPlane );
  
  std::vector<movements::Pose> m_waypoints = scan.path( base_pose, 0.5, 3.5, 0.02 );
  
  // the camera should point into the same direction during the whole movement - seems not to have an impact
  moveit_msgs::OrientationConstraint eef_orientation_constraint = getFixedEEFLinkOrientationConstraint(base_pose);
  
  moveit_msgs::Constraints robot_constraints;
  robot_constraints.orientation_constraints.push_back(eef_orientation_constraint);
  
  bool scan_success = executeMovementsPath( m_waypoints, &robot_constraints );
  
  if( scan_success )
  {    
    geometry_msgs::Pose stop_pose = robot_->getCurrentPose().pose;
    movements::Pose stop_pose_m = movements::fromROS(stop_pose);
    std::vector<movements::Pose> go_home;
    go_home.push_back(stop_pose_m);
    go_home.push_back(base_pose);
    
    executeMovementsPath( go_home );
    
    return true;
  }
  else
    return false;
}

bool YoubotReconstructionController::moveBaseCircularlyTo( Eigen::Vector2d _target_position, Eigen::Vector2d _center )
{
  return false;
}

bool YoubotReconstructionController::executeMovementsPath( std::vector<movements::Pose>& _path, moveit_msgs::Constraints* _constraints )
{
  moveit::planning_interface::MoveGroup::Plan plan;
  bool successfully_planned = filteredPlanFromMovementsPath( _path, plan, _constraints, 2, 0.7 );
  
  if( successfully_planned )
  {
    ros::AsyncSpinner spinner(1);  
    //scene_->unlockSceneRead();   
    spinner.start();
    robot_->execute(plan);
    spinner.stop();
    //scene_->lockSceneRead();
    return true;
  }
  return false;
}

bool YoubotReconstructionController::isCollisionFree( planning_scene_monitor::LockedPlanningSceneRO& _scene, robot_state::RobotState& _robot )
{
  bool colliding = _scene->isStateColliding( _robot );
    
  return !colliding;
}

bool YoubotReconstructionController::planFromMovementsPath( std::vector<movements::Pose>& _waypoints, moveit::planning_interface::MoveGroup::Plan& _plan, moveit_msgs::Constraints* _path_constraints, int _planning_attempts )
{
        
    /*geometry_msgs::Pose current_pose = robot_->getCurrentPose().pose;
    using namespace std;
    cout<<endl<<"Current pose as given in planFromMovementsPath is:"<<endl<<current_pose<<endl<<endl;
    movements::Pose base_pose = movements::fromROS(current_pose);
    movements::RelativeMovement z_down = movements::Translation::create(0,0,-0.1);
    movements::KinMove md = movements::Linear::create(0,0,-1,1); // moving downwards with 1 m/s
            
    moveit_msgs::Constraints robot_constraints;
    robot_constraints.orientation_constraints.push_back(eef_orientation_constraint);*/
    
    if( _path_constraints!=nullptr )
    {
      robot_->setPathConstraints( *_path_constraints );
    }
    
    std::vector<geometry_msgs::Pose> waypoints = toROS(_waypoints);
    
    moveit_msgs::RobotTrajectory trajectory;
    //moveit::planning_interface::MoveGroup::Plan plan;
    
    double success_ratio = 0;
    int count=0;
    while( success_ratio!=1 && ros_node_->ok() )
    {
      count++;
      success_ratio=robot_->computeCartesianPath( waypoints, 0.1,0 /*0.2 = ~10 degree max dist in config space, 0 disables it*/, trajectory );
      ros::Duration(0.01).sleep();
      
      if( count>_planning_attempts ) return false;
    }
    
    /*using namespace std;
    cout<<endl<<"calculated path has size :"<<endl<<waypoints.size()<<".";
    cout<<endl<<"The poses in the path generated by movements are:";
    BOOST_FOREACH(auto pose, waypoints)
    {
      cout<<pose<<endl;
    }
    
    std::vector<trajectory_msgs::JointTrajectoryPoint> traj_joint_values = trajectory.joint_trajectory.points;
    std::vector<std::string> traj_joint_names = trajectory.joint_trajectory.joint_names;
    
    planning_scene_monitor::LockedPlanningSceneRO current_scene( scene_ );
    robot_state::RobotState robot_state = current_scene->getCurrentState();
    
    // print out transforms for the calculated trajectory path points
    cout<<endl<<"The moveit trajectory consists of "<<traj_joint_values.size()<<" points.";
    cout<<endl<<"The computed trajectory is:";
    BOOST_FOREACH( auto traj_joint_value, traj_joint_values )
    {
      robot_state.setVariablePositions( traj_joint_names, traj_joint_value.positions );
      Eigen::Matrix<double,4,4> state_transform = robot_state.getFrameTransform("camera").matrix();
      movements::Pose relative_pose_m( Eigen::Vector3d(state_transform.topRightCorner<3,1>()), Eigen::Quaterniond(state_transform.topLeftCorner<3,3>()) );
      geometry_msgs::Pose relative_pose = movements::toROS(relative_pose_m);
      cout<<endl<<relative_pose;
    }
    cout<<endl<<"Which is in joint space:";
    BOOST_FOREACH( auto traj_joint_value, traj_joint_values )
    {
      cout<<endl<<traj_joint_value;
    }
    cout<<endl<<endl;*/
    //cout<<endl<<"The computed trajectory is:"<<endl<<trajectory<<endl<<endl;
    
    // attempt to create velocities *************************************
    /*
    // First to create a RobotTrajectory object
    robot_state::RobotState current_robot_state = current_scene->getCurrentState();
    
    robot_trajectory::RobotTrajectory rt( current_robot_state.getRobotModel(), planning_group_);
    rt.setRobotTrajectoryMsg( current_robot_state, trajectory );
    // Thrid create a IterativeParabolicTimeParameterization object
    trajectory_processing::IterativeParabolicTimeParameterization iptp;
    bool success = iptp.computeTimeStamps(rt);
    ROS_INFO("Computed time stamp %s",success?"SUCCEDED":"FAILED");
    // Get RobotTrajectory_msg from RobotTrajectory
    rt.getRobotTrajectoryMsg(trajectory);
    // ***********************************
    */
    //cout<<endl<<"The computed trajectory is:"<<endl<<trajectory<<endl<<endl;
    
    _plan.trajectory_ = trajectory;
    //_plan.planning_time_ = 0.1;
  
    robot_->clearPathConstraints();
    
    return true;
}

bool YoubotReconstructionController::filteredPlanFromMovementsPath( std::vector<movements::Pose>& _waypoints, moveit::planning_interface::MoveGroup::Plan& _plan, moveit_msgs::Constraints* _path_constraints, int _planning_attempts, double _max_dropoff )
{
    if( _path_constraints!=nullptr )
    {
      robot_->setPathConstraints( *_path_constraints );
    }
    
    std::vector<geometry_msgs::Pose> waypoints = toROS(_waypoints);
    
    moveit_msgs::RobotTrajectory trajectory;
    //moveit::planning_interface::MoveGroup::Plan plan;
    
    double success_ratio = 0;
    int count=0;
    int dropped_points = 0;
    int passed_points = _waypoints.size();
    
    while( success_ratio!=1 && ros_node_->ok() )
    {
      count++;
      success_ratio=robot_->computeCartesianPath( waypoints, 0.1,0 /*0.2 = ~10 degree max dist in config space, 0 disables it*/, trajectory );
      
      if( success_ratio!=1 && count>_planning_attempts ) // filter stage - only if necessary: a little complicated since working with vectors (which is given)
      {
	dropped_points++;
	
	if( dropped_points==passed_points || _max_dropoff<=0 )
	{
	  using namespace std;
	  cout<<endl<<"Total number of points in trajectory passed is: p="<<_waypoints.size();
	  cout<<endl<<"Percentage of successfully computed cartesian path s is: s="<<success_ratio;
	  cout<<endl<<"Number of computed points in cartesian trajectory is: c = "<<trajectory.joint_trajectory.points.size();
	  cout<<endl<<"s*p = "<<success_ratio*_waypoints.size();
	  cout<<endl<<"Number of dropped points: "<<dropped_points;
	  cout<<endl<<"Dropped point percentage: "<<dropped_points/(double)_waypoints.size();
	  cout<<endl<<endl;
	  return false;  // too many dropped points
	}
	else if( _max_dropoff<1 )
	{
	  if( (double)dropped_points/passed_points > _max_dropoff )
	  {
	    using namespace std;
	    cout<<endl<<"Total number of points in trajectory passed is: p="<<_waypoints.size();
	    cout<<endl<<"Percentage of successfully computed cartesian path s is: s="<<success_ratio;
	    cout<<endl<<"Number of computed points in cartesian trajectory is: c = "<<trajectory.joint_trajectory.points.size();
	    cout<<endl<<"s*p = "<<success_ratio*_waypoints.size();
	    cout<<endl<<"Number of dropped points: "<<dropped_points;
	    cout<<endl<<"Dropped point percentage: "<<dropped_points/(double)_waypoints.size();
	    cout<<endl<<endl;
	    return false; // too many dropped points
	  }
	}
	else
	{
	  if( dropped_points > _max_dropoff )
	  {
	    using namespace std;
	    cout<<endl<<"Total number of points in trajectory passed is: p="<<_waypoints.size();
	    cout<<endl<<"Percentage of successfully computed cartesian path s is: s="<<success_ratio;
	    cout<<endl<<"Number of computed points in cartesian trajectory is: c = "<<trajectory.joint_trajectory.points.size();
	    cout<<endl<<"s*p = "<<success_ratio*_waypoints.size();
	    cout<<endl<<"Number of dropped points: "<<dropped_points;
	    cout<<endl<<"Dropped point percentage: "<<dropped_points/(double)_waypoints.size();
	    cout<<endl<<endl;
	    return false; // too many dropped points
	  }
	}
	
	int valid_points = trajectory.joint_trajectory.points.size(); // -> also the index of the point which will be dropped
	count = 0;
	// drop point for which calculation failed
	for( unsigned int i=valid_points; i<waypoints.size()-1; i++ )
	{
	  waypoints[i]=waypoints[i+1]; // shift all later points one position forward
	}
	waypoints.resize( waypoints.size()-1 ); // drop last
      }
      
      //ros::Duration(0.001).sleep();
    }
      
      using namespace std;
      cout<<endl<<"Total number of points in trajectory passed is: p="<<_waypoints.size();
      cout<<endl<<"Percentage of successfully computed cartesian path s is: s="<<success_ratio;
      cout<<endl<<"Number of computed points in cartesian trajectory is: c = "<<trajectory.joint_trajectory.points.size();
      cout<<endl<<"s*p = "<<success_ratio*_waypoints.size();
      cout<<endl<<"Number of dropped points: "<<dropped_points;
      cout<<endl<<"Dropped point percentage: "<<dropped_points/(double)_waypoints.size();
      cout<<endl<<endl;
      
      
    _plan.trajectory_ = trajectory;
    //_plan.planning_time_ = 0.1;
  
    robot_->clearPathConstraints();
    
    return true;
}

moveit_msgs::OrientationConstraint YoubotReconstructionController::getFixedEEFLinkOrientationConstraint( movements::Pose& _base_pose, int _weight, double _x_axis_tolerance, double _y_axis_tolerance, double _z_axis_tolerance )
{
  moveit_msgs::OrientationConstraint eef_orientation_constraint;
  eef_orientation_constraint.header.frame_id = "base_footprint";
  eef_orientation_constraint.link_name = end_effector_planning_frame_;
  eef_orientation_constraint.orientation = st_is::eigenToGeometry(_base_pose.orientation);
  eef_orientation_constraint.absolute_x_axis_tolerance = _x_axis_tolerance;
  eef_orientation_constraint.absolute_y_axis_tolerance = _y_axis_tolerance;
  eef_orientation_constraint.absolute_z_axis_tolerance = _z_axis_tolerance;
  eef_orientation_constraint.weight = _weight;
  
  return eef_orientation_constraint;
}


void YoubotReconstructionController::setEndEffectorPlanningFrame( std::string _name )
{
  if( robot_->setEndEffector(_name) )
  {
    end_effector_planning_frame_ = _name;
  }
}

bool YoubotReconstructionController::planAndMove()
{
  std::string end_effector_name;
  end_effector_name = robot_->getEndEffector();
  std::cout<<std::endl<<"end effector name: "<<end_effector_name;
  std::cout<<std::endl<<"planning frame name: "<<robot_->getPlanningFrame();
  std::cout<<std::endl<<"base position: "<<std::endl<<robot_->getCurrentPose("base_footprint")<<std::endl;
  movements::Pose basePose = getCurrentLinkPose("base_footprint");
  std::cout<<std::endl<<"base position translation from tf: "<<std::endl<<basePose.position<<std::endl;
  
  //geometry_msgs::PoseStamped current_end_effector_pose = robot_->getCurrentPose();
  //std::cout<<std::endl<<current_end_effector_pose<<std::endl;
  
  using namespace std;
  //cout<<endl<<"goal position tolerance is: "<<robot_->getGoalPositionTolerance();
  //cout<<endl<<"goal orientation tolerance is: "<<robot_->getGoalOrientationTolerance();
  //cout<<endl;
  
  
  geometry_msgs::Pose target_0, target_1;
  
  target_0.position.x = 0.126;
  target_0.position.y = -0.01;
  target_0.position.z = 0.477;
  target_0.orientation.x = 0.019;
  target_0.orientation.y = 0.109;
  target_0.orientation.z = -0.99378;
  target_0.orientation.w = 0.0109;
  
  target_1.position.x = 0.126;
  target_1.position.y = -0.01;
  target_1.position.z = 0.377;
  target_1.orientation.x = 0.019;
  target_1.orientation.y = 0.109;
  target_1.orientation.z = -0.99378;
  target_1.orientation.w = 0.0109;
  
  robot_->setGoalPositionTolerance(0.001);
  robot_->setGoalOrientationTolerance(0.001);


  // using direct pose targets
  /*
  if( test == 0 )
  {
    robot_->setPoseTarget( movements::toROS(base_pose) );
    test = 1;
  }
  else
  {
    robot_->setPoseTarget( movements::toROS( base_pose+md(0.1) ) );
    test = 0;
  }
  */
  // using cartesian paths
  cout<<endl<<"Enter time step size for the created path, 0 will move the end effector to the base pose."<<endl;
  double input;
  cin>>input;
  if( input==10 )
    return false;
  if( input == 0 )
  {
    cout<<endl<<"Moving to start position"<<endl;
    robot_->setPoseTarget( target_0 );
    
    planning_scene_monitor::LockedPlanningSceneRO current_scene( scene_ );
    robot_state::RobotState current_robot_state = current_scene->getCurrentState();
    robot_->setStartState(current_robot_state);
    
    //robot_->setStartStateToCurrentState();
    
    //planning_scene_monitor::LockedPlanningSceneRO current_scene( scene_ );
    //robot_state::RobotState current_robot_state = current_scene->getCurrentState();
    //robot_->setStartState(current_robot_state);
    
    double joint_tolerance = robot_->getGoalJointTolerance();
    double velocity_tolerance = 0.0001;
    
    
    ros::AsyncSpinner spinner(1);
    scene_->unlockSceneRead();
    
    spinner.start();
    // plan and execute a path to the target state
    bool success;
    do{ success = robot_->move(); ros::spinOnce(); }while(!success && ros_node_->ok() );
    cout<<endl<<"moveit says that the motion execution function has finished with success="<<success<<"."<<endl;
    spinner.stop();
    scene_->lockSceneRead();
    
    //if( !success ) return false;
    
    
  }
  else
  {
    bool executed_scan_successfully = makeScan();
    
    if(!executed_scan_successfully)
      cout<<endl<<"Failed to create trajectory and execute it."<<endl;
    
  }
  
  return true;
}