/*
 * Copyright 2016 Geoffrey Hunter <gbmhunter@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0

 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <chrono>
#include <cmath>
#include <iostream>
#include <stdio.h>

#include <boost/bind.hpp>
#include <rotors_gazebo_plugins/gazebo_ros_interface_plugin.h>

namespace gazebo {

GazeboRosInterfacePlugin::GazeboRosInterfacePlugin()
    : WorldPlugin(),
      gz_node_handle_(0),
      ros_node_handle_(0),
      ros_actuators_msg_(new mav_msgs::Actuators)
{
  // Nothing
}

GazeboRosInterfacePlugin::~GazeboRosInterfacePlugin() {
  event::Events::DisconnectWorldUpdateBegin(updateConnection_);
}


//void GazeboRosInterfacePlugin::Load(physics::ModelPtr _model, sdf::ElementPtr _sdf) {
void GazeboRosInterfacePlugin::Load(physics::WorldPtr _world, sdf::ElementPtr _sdf) {

  gzdbg << __FUNCTION__ << " called." << std::endl;

  // Store the pointer to the model
//  model_ = _model;
//  world_ = model_->GetWorld();
  world_ = _world;

  // default params
  namespace_.clear();

  //==============================================//
  //========== READ IN PARAMS FROM SDF ===========//
  //==============================================//

  if (_sdf->HasElement("robotNamespace"))
    namespace_ = _sdf->GetElement("robotNamespace")->Get<std::string>();
  else
    gzerr << "Please specify a robotNamespace.\n";
  gzdbg << "namespace_ = \"" << namespace_ << "\"." << std::endl;

  /// @todo Fix this hack!!! Should not need this namespace set to firefly.
  namespace_ = "firefly";

  // Get Gazebo node handle
  gz_node_handle_ = transport::NodePtr(new transport::Node());
  gz_node_handle_->Init(namespace_);
//  gz_node_handle_->Init();

  // Get ROS node handle
  ros_node_handle_ = new ros::NodeHandle(namespace_);

  // Listen to the update event. This event is broadcast every
  // simulation iteration.
  this->updateConnection_ =
      event::Events::ConnectWorldUpdateBegin(
          boost::bind(&GazeboRosInterfacePlugin::OnUpdate, this, _1));

  // ============================================ //
  // === CONNECT GAZEBO TO ROS MESSAGES SETUP === //
  // ============================================ //

  gz_connect_gazebo_to_ros_topic_sub_ = gz_node_handle_->Subscribe(
      "~/" + kConnectGazeboToRosSubtopic,
      &GazeboRosInterfacePlugin::GzConnectGazeboToRosTopicMsgCallback,
      this);

  // ============================================ //
  // === CONNECT ROS TO GAZEBO MESSAGES SETUP === //
  // ============================================ //

  gz_connect_ros_to_gazebo_topic_sub_ = gz_node_handle_->Subscribe(
      "~/" + kConnectRosToGazeboSubtopic,
      &GazeboRosInterfacePlugin::GzConnectRosToGazeboTopicMsgCallback,
      this);

}

//! @brief      A helper class that provides storage for additional parameters that are inserted
//!             into the callback.
template <typename M>
struct ConnectHelperStorage {

  //! @brief    Pointer to the ROS interface plugin class.
  GazeboRosInterfacePlugin * ptr;

  //! @brief    Function pointer to the subscriber callback with additional parameters.
  void(GazeboRosInterfacePlugin::*fp)(const boost::shared_ptr<M const> &, ros::Publisher ros_publisher);

  //! @brief    The ROS publisher that is passed into the modified callback.
  ros::Publisher ros_publisher;

  /// @brief    This is what gets passed into the Gazebo Subscribe method as a callback, and hence can only
  ///           have one parameter (note boost::bind() does not work with the current Gazebo Subscribe() definitions).
  void callback (const boost::shared_ptr<M const> & msg_ptr) {
    //gzdbg << "callback() called." << std::endl;
    (ptr->*fp)(msg_ptr, ros_publisher);
  }

};

// M is the type of the message that will be subscribed to the Gazebo framework.
// N is the type of the message published to the ROS framework
template <typename M, typename N>
void GazeboRosInterfacePlugin::ConnectHelper(
    void(GazeboRosInterfacePlugin::*fp)(const boost::shared_ptr<M const> &, ros::Publisher),
    GazeboRosInterfacePlugin * ptr,
    std::string gazeboTopicName,
    std::string rosTopicName,
    transport::NodePtr gz_node_handle) {

  // One map will be created for each type M
  static std::map< std::string, ConnectHelperStorage<M> > callback_map;

  // Create ROS publisher
//  gzdbg << "GazeboRosInterfacePlugin publishing to ROS topic \"" << rosTopicName << "\"." << std::endl;
  ros::Publisher ros_publisher = ros_node_handle_->advertise<N>(rosTopicName, 1);

  // @todo Handle collision error
  auto callback_entry = callback_map.emplace(gazeboTopicName, ConnectHelperStorage<M>{ptr, fp, ros_publisher});

//  gzdbg << "GazeboRosInterfacePlugin subscribing to Gazebo topic \"" << gazeboTopicName << "\"."<< std::endl;
  gazebo::transport::SubscriberPtr subscriberPtr;
  subscriberPtr = gz_node_handle->Subscribe(
      gazeboTopicName,
      &ConnectHelperStorage<M>::callback,
      &callback_entry.first->second);

  // Save a reference to the subscriber pointer so subsriber
  // won't be deleted.
  subscriberPtrs_.push_back(subscriberPtr);

}

void GazeboRosInterfacePlugin::GzConnectGazeboToRosTopicMsgCallback(
    GzConnectGazeboToRosTopicMsgPtr& gz_connect_gazebo_to_ros_topic_msg) {

  gzdbg << __FUNCTION__ << "() called." << std::endl;

  const std::string gazeboTopicName = gz_connect_gazebo_to_ros_topic_msg->gazebo_topic();
  const std::string rosTopicName = gz_connect_gazebo_to_ros_topic_msg->ros_topic();

  gzdbg << "Connecting Gazebo topic \"" << gazeboTopicName << "\" to ROS topic \"" << rosTopicName << "\"." << std::endl;

  switch(gz_connect_gazebo_to_ros_topic_msg->msgtype()) {
    case gz_std_msgs::ConnectGazeboToRosTopic::ACTUATORS:
      ConnectHelper<sensor_msgs::msgs::Actuators, mav_msgs::Actuators>(
          &GazeboRosInterfacePlugin::GzActuatorsMsgCallback,
          this,
          gazeboTopicName,
          rosTopicName,
          gz_node_handle_);
      break;
    case gz_std_msgs::ConnectGazeboToRosTopic::FLOAT_32:
      ConnectHelper<gz_std_msgs::Float32, std_msgs::Float32>(
          &GazeboRosInterfacePlugin::GzFloat32MsgCallback,
          this,
          gazeboTopicName,
          rosTopicName,
          gz_node_handle_);
      break;
    case gz_std_msgs::ConnectGazeboToRosTopic::IMU:
      ConnectHelper<sensor_msgs::msgs::Imu, sensor_msgs::Imu>(
          &GazeboRosInterfacePlugin::GzImuMsgCallback,
          this,
          gazeboTopicName,
          rosTopicName,
          gz_node_handle_);
      break;
    case gz_std_msgs::ConnectGazeboToRosTopic::JOINT_STATE:
      ConnectHelper<sensor_msgs::msgs::JointState, sensor_msgs::JointState>(
          &GazeboRosInterfacePlugin::GzJointStateMsgCallback,
          this,
          gazeboTopicName,
          rosTopicName,
          gz_node_handle_);
      break;
    case gz_std_msgs::ConnectGazeboToRosTopic::MAGNETIC_FIELD:
      ConnectHelper<sensor_msgs::msgs::MagneticField, sensor_msgs::MagneticField>(
          &GazeboRosInterfacePlugin::GzMagneticFieldMsgCallback,
          this,
          gazeboTopicName,
          rosTopicName,
          gz_node_handle_);
      break;
    case gz_std_msgs::ConnectGazeboToRosTopic::NAV_SAT_FIX:
      ConnectHelper<sensor_msgs::msgs::NavSatFix, sensor_msgs::NavSatFix>(
          &GazeboRosInterfacePlugin::GzNavSatFixCallback,
          this,
          gazeboTopicName,
          rosTopicName,
          gz_node_handle_);
      break;
    case gz_std_msgs::ConnectGazeboToRosTopic::POSE:
      ConnectHelper<gz_geometry_msgs::Pose, geometry_msgs::Pose>(
          &GazeboRosInterfacePlugin::GzPoseMsgCallback,
          this,
          gazeboTopicName,
          rosTopicName,
          gz_node_handle_);
      break;
    case gz_std_msgs::ConnectGazeboToRosTopic::POSE_WITH_COVARIANCE_STAMPED:
      ConnectHelper<gz_geometry_msgs::PoseWithCovarianceStamped, geometry_msgs::PoseWithCovarianceStamped>(
          &GazeboRosInterfacePlugin::GzPoseWithCovarianceStampedMsgCallback,
          this,
          gazeboTopicName,
          rosTopicName,
          gz_node_handle_);
      break;
    case gz_std_msgs::ConnectGazeboToRosTopic::POSITION_STAMPED:
      ConnectHelper<gz_geometry_msgs::PositionStamped, geometry_msgs::PointStamped>(
          &GazeboRosInterfacePlugin::GzPositionStampedMsgCallback,
          this,
          gazeboTopicName,
          rosTopicName,
          gz_node_handle_);
      break;
    case gz_std_msgs::ConnectGazeboToRosTopic::ODOMETRY:
      ConnectHelper<gz_geometry_msgs::Odometry, nav_msgs::Odometry>(
          &GazeboRosInterfacePlugin::GzOdometryMsgCallback,
          this,
          gazeboTopicName,
          rosTopicName,
          gz_node_handle_);
      break;
    case gz_std_msgs::ConnectGazeboToRosTopic::TRANSFORM_STAMPED:
        ConnectHelper<gz_geometry_msgs::TransformStamped, geometry_msgs::TransformStamped>(
            &GazeboRosInterfacePlugin::GzTransformStampedMsgCallback,
            this,
            gazeboTopicName,
            rosTopicName,
            gz_node_handle_);
        break;
    case gz_std_msgs::ConnectGazeboToRosTopic::TWIST_STAMPED:
      ConnectHelper<sensor_msgs::msgs::TwistStamped, geometry_msgs::TwistStamped>(
          &GazeboRosInterfacePlugin::GzTwistStampedMsgCallback,
          this,
          gazeboTopicName,
          rosTopicName,
          gz_node_handle_);
      break;
    case gz_std_msgs::ConnectGazeboToRosTopic::WRENCH_STAMPED:
      ConnectHelper<gz_geometry_msgs::WrenchStamped, geometry_msgs::WrenchStamped>(
          &GazeboRosInterfacePlugin::GzWrenchStampedMsgCallback,
          this,
          gazeboTopicName,
          rosTopicName,
          gz_node_handle_);
      break;
    default:
      gzthrow("ConnectGazeboToRosTopic message type with enum val = " << gz_connect_gazebo_to_ros_topic_msg->msgtype() <<
          " is not supported by GazeboRosInterfacePlugin.");
  }

  gzdbg << __FUNCTION__ << "() finished." << std::endl;

}

template<typename T>
transport::PublisherPtr GazeboRosInterfacePlugin::FindOrMakeGazeboPublisher(std::string topic) {

  gzdbg << __FUNCTION__ << " called." << std::endl;

  transport::PublisherPtr gz_publisher_ptr;

  if(gazebo::transport::TopicManager::Instance()->FindPublication(topic) == nullptr) {
    gz_publisher_ptr = gz_node_handle_->Advertise<T>(topic, 1);
  } else {
    gzdbg << "Gazebo publisher with topic = \"" << topic <<  "\" already exists, not creating another one." << std::endl;
    gzerr << "Handling an already created publisher is not supported yet!" << std::endl;
  }

  return gz_publisher_ptr;
}

void GazeboRosInterfacePlugin::GzConnectRosToGazeboTopicMsgCallback(
    GzConnectRosToGazeboTopicMsgPtr& gz_connect_ros_to_gazebo_topic_msg) {

  gzdbg << __FUNCTION__ << "() called." << std::endl;

  static std::vector<ros::Subscriber> ros_subscribers;

  switch(gz_connect_ros_to_gazebo_topic_msg->msgtype()) {
    case gz_std_msgs::ConnectRosToGazeboTopic::ACTUATORS: {

      // Create Gazebo publisher
      // (we don't need to manually save a reference for the Gazebo publisher because
      // boost::bind will do that for us)
        gazebo::transport::PublisherPtr gz_publisher_ptr =
            FindOrMakeGazeboPublisher<sensor_msgs::msgs::Actuators>(gz_connect_ros_to_gazebo_topic_msg->gazebo_topic());

      // Create ROS subscriber
      ros::Subscriber ros_subscriber = ros_node_handle_->subscribe<mav_msgs::Actuators>(
          gz_connect_ros_to_gazebo_topic_msg->ros_topic(), 1,
          boost::bind(&GazeboRosInterfacePlugin::RosActuatorsMsgCallback, this, _1, gz_publisher_ptr));

      // Save reference to the ROS subscriber so callback will continue to be called.
      ros_subscribers.push_back(ros_subscriber);

      break;
    }
    case gz_std_msgs::ConnectRosToGazeboTopic::COMMAND_MOTOR_SPEED: {

      // Create Gazebo publisher
      // (we don't need to manually save a reference for the Gazebo publisher because
      // boost::bind will do that for us)
      gazebo::transport::PublisherPtr gz_publisher_ptr = gz_node_handle_->Advertise<gz_mav_msgs::CommandMotorSpeed>(
          gz_connect_ros_to_gazebo_topic_msg->gazebo_topic(), 1);

      // Create ROS subscriber
      ros::Subscriber ros_subscriber = ros_node_handle_->subscribe<mav_msgs::Actuators>(
          gz_connect_ros_to_gazebo_topic_msg->ros_topic(), 1,
          boost::bind(&GazeboRosInterfacePlugin::RosCommandMotorSpeedMsgCallback, this, _1, gz_publisher_ptr));

      // Save reference to the ROS subscriber so callback will continue to be called.
      ros_subscribers.push_back(ros_subscriber);

      break;
    }
    case gz_std_msgs::ConnectRosToGazeboTopic::WIND_SPEED: {

      // Create Gazebo publisher
      // (we don't need to manually save a reference for the Gazebo publisher because
      // boost::bind will do that for us)
      gazebo::transport::PublisherPtr gz_publisher_ptr = gz_node_handle_->Advertise<gz_mav_msgs::WindSpeed>(
          gz_connect_ros_to_gazebo_topic_msg->gazebo_topic(), 1);

      // Create ROS subscriber
      ros::Subscriber ros_subscriber = ros_node_handle_->subscribe<rotors_comm::WindSpeed>(
          gz_connect_ros_to_gazebo_topic_msg->ros_topic(), 1,
          boost::bind(&GazeboRosInterfacePlugin::RosWindSpeedMsgCallback, this, _1, gz_publisher_ptr));

      // Save reference to the ROS subscriber so callback will continue to be called.
      ros_subscribers.push_back(ros_subscriber);

      break;

    } default: {
      gzthrow("ConnectRosToGazeboTopic message type with enum val = " << gz_connect_ros_to_gazebo_topic_msg->msgtype() <<
                " is not supported by GazeboRosInterfacePlugin.");
    }
  }

}

//===========================================================================//
//================ GAZEBO -> ROS MSG CALLBACKS/CONVERTERS ===================//
//===========================================================================//


void GazeboRosInterfacePlugin::GzActuatorsMsgCallback(GzActuatorsMsgPtr& gz_actuators_msg, ros::Publisher ros_publisher) {
//  gzdbg << __FUNCTION__ << "() called." << std::endl;

  // We need to convert the Acutuators message from a Gazebo message to a
  // ROS message and then publish it to the ROS framework

  ros_actuators_msg_->header.stamp.sec = gz_actuators_msg->header().stamp().sec();
  ros_actuators_msg_->header.stamp.nsec = gz_actuators_msg->header().stamp().nsec();
  ros_actuators_msg_->header.frame_id = gz_actuators_msg->header().frame_id();

  ros_actuators_msg_->angular_velocities.resize(gz_actuators_msg->angular_velocities_size());
  for(int i = 0; i < gz_actuators_msg->angular_velocities_size(); i++) {
    ros_actuators_msg_->angular_velocities[i] = gz_actuators_msg->angular_velocities(i);
  }

  // Publish onto ROS framework
  ros_publisher.publish(ros_actuators_msg_);

}

void GazeboRosInterfacePlugin::GzFloat32MsgCallback(GzFloat32MsgPtr& gz_float_32_msg, ros::Publisher ros_publisher) {

  // Convert Gazebo message to ROS message
  ros_float_32_msg_.data = gz_float_32_msg->data();

  // Publish to ROS
  ros_publisher.publish(ros_float_32_msg_);

}

void GazeboRosInterfacePlugin::GzImuMsgCallback(GzImuPtr& gz_imu_msg, ros::Publisher ros_publisher) {

//  gzdbg << __FUNCTION__ << " called." << std::endl;

  // We need to convert from a Gazebo message to a ROS message,
  // and then forward the IMU message onto ROS

  ros_imu_msg_.header.stamp.sec = gz_imu_msg->header().stamp().sec();
  ros_imu_msg_.header.stamp.nsec = gz_imu_msg->header().stamp().nsec();
  ros_imu_msg_.header.frame_id = gz_imu_msg->header().frame_id();

  ros_imu_msg_.orientation.x = gz_imu_msg->orientation().x();
  ros_imu_msg_.orientation.y = gz_imu_msg->orientation().y();
  ros_imu_msg_.orientation.z = gz_imu_msg->orientation().z();
  ros_imu_msg_.orientation.w = gz_imu_msg->orientation().w();

  // Orientation covariance should have 9 elements, and both the Gazebo and ROS
  // arrays should be the same size!
  GZ_ASSERT(gz_imu_msg->orientation_covariance_size() == 9, "The Gazebo IMU message does not have 9 orientation covariance elements.");
  GZ_ASSERT(ros_imu_msg_.orientation_covariance.size() == 9, "The ROS IMU message does not have 9 orientation covariance elements.");
  for(int i = 0; i < gz_imu_msg->orientation_covariance_size(); i ++) {
    ros_imu_msg_.orientation_covariance[i] = gz_imu_msg->orientation_covariance(i);
  }

  ros_imu_msg_.angular_velocity.x = gz_imu_msg->angular_velocity().x();
  ros_imu_msg_.angular_velocity.y = gz_imu_msg->angular_velocity().y();
  ros_imu_msg_.angular_velocity.z = gz_imu_msg->angular_velocity().z();

  GZ_ASSERT(gz_imu_msg->angular_velocity_covariance_size() == 9, "The Gazebo IMU message does not have 9 angular velocity covariance elements.");
  GZ_ASSERT(ros_imu_msg_.angular_velocity_covariance.size() == 9, "The ROS IMU message does not have 9 angular velocity covariance elements.");
  for(int i = 0; i < gz_imu_msg->angular_velocity_covariance_size(); i ++) {
    ros_imu_msg_.angular_velocity_covariance[i] = gz_imu_msg->angular_velocity_covariance(i);
  }

  ros_imu_msg_.linear_acceleration.x = gz_imu_msg->linear_acceleration().x();
  ros_imu_msg_.linear_acceleration.y = gz_imu_msg->linear_acceleration().y();
  ros_imu_msg_.linear_acceleration.z = gz_imu_msg->linear_acceleration().z();

  GZ_ASSERT(gz_imu_msg->linear_acceleration_covariance_size() == 9, "The Gazebo IMU message does not have 9 linear acceleration covariance elements.");
  GZ_ASSERT(ros_imu_msg_.linear_acceleration_covariance.size() == 9, "The ROS IMU message does not have 9 linear acceleration covariance elements.");
  for(int i = 0; i < gz_imu_msg->linear_acceleration_covariance_size(); i ++) {
    ros_imu_msg_.linear_acceleration_covariance[i] = gz_imu_msg->linear_acceleration_covariance(i);
  }

  // Publish onto ROS framework
  ros_publisher.publish(ros_imu_msg_);

}

void GazeboRosInterfacePlugin::GzJointStateMsgCallback(GzJointStateMsgPtr& gz_joint_state_msg, ros::Publisher ros_publisher) {
  gzdbg << __FUNCTION__ << "() called." << std::endl;

  // ============================================ //
  // =================== HEADER ================= //
  // ============================================ //
  ros_joint_state_msg_.header.stamp.sec = gz_joint_state_msg->header().stamp().sec();
  ros_joint_state_msg_.header.stamp.nsec = gz_joint_state_msg->header().stamp().nsec();
  ros_joint_state_msg_.header.frame_id = gz_joint_state_msg->header().frame_id();

  // ============================================ //
  // ==================== NAME ================== //
  // ============================================ //
  ros_joint_state_msg_.name.resize(gz_joint_state_msg->name_size());
  for(int i = 0; i < gz_joint_state_msg->name_size(); i ++) {
    ros_joint_state_msg_.name[i] = gz_joint_state_msg->name(i);
  }

  // ============================================ //
  // ================== POSITION ================ //
  // ============================================ //
  ros_joint_state_msg_.position.resize(gz_joint_state_msg->position_size());
  for(int i = 0; i < gz_joint_state_msg->position_size(); i ++) {
    ros_joint_state_msg_.position[i] = gz_joint_state_msg->position(i);
  }

  // Publish onto ROS framework
  ros_publisher.publish(ros_joint_state_msg_);

  gzdbg << __FUNCTION__ << "() finished." << std::endl;

}

void GazeboRosInterfacePlugin::GzMagneticFieldMsgCallback(GzMagneticFieldMsgPtr& gz_magnetic_field_msg, ros::Publisher ros_publisher) {
//  gzdbg << __FUNCTION__ << "() called." << std::endl;

  // We need to convert from a Gazebo message to a ROS message,
  // and then forward the MagneticField message onto ROS
  ros_magnetic_field_msg_.header.stamp.sec = gz_magnetic_field_msg->header().stamp().sec();
  ros_magnetic_field_msg_.header.stamp.nsec = gz_magnetic_field_msg->header().stamp().nsec();
  ros_magnetic_field_msg_.header.frame_id = gz_magnetic_field_msg->header().frame_id();

  ros_magnetic_field_msg_.magnetic_field.x = gz_magnetic_field_msg->magnetic_field().x();
  ros_magnetic_field_msg_.magnetic_field.y = gz_magnetic_field_msg->magnetic_field().y();
  ros_magnetic_field_msg_.magnetic_field.z = gz_magnetic_field_msg->magnetic_field().z();

  // Position covariance should have 9 elements, and both the Gazebo and ROS
  // arrays should be the same size!
  GZ_ASSERT(gz_magnetic_field_msg->magnetic_field_covariance_size() == 9, "The Gazebo MagneticField message does not have 9 magnetic field covariance elements.");
  GZ_ASSERT(ros_magnetic_field_msg_.magnetic_field_covariance.size() == 9, "The ROS MagneticField message does not have 9 magnetic field covariance elements.");
  for(int i = 0; i < gz_magnetic_field_msg->magnetic_field_covariance_size(); i ++) {
    ros_magnetic_field_msg_.magnetic_field_covariance[i] = gz_magnetic_field_msg->magnetic_field_covariance(i);
  }

  // Publish onto ROS framework
  ros_publisher.publish(ros_magnetic_field_msg_);

}

void GazeboRosInterfacePlugin::GzNavSatFixCallback(GzNavSatFixPtr& gz_nav_sat_fix_msg, ros::Publisher ros_publisher) {
//  gzdbg << __FUNCTION__ << "() called." << std::endl;

  // We need to convert from a Gazebo message to a ROS message,
  // and then forward the NavSatFix message onto ROS

  // ============================================ //
  // =================== HEADER ================= //
  // ============================================ //
  ros_nav_sat_fix_msg_.header.stamp.sec = gz_nav_sat_fix_msg->header().stamp().sec();
  ros_nav_sat_fix_msg_.header.stamp.nsec = gz_nav_sat_fix_msg->header().stamp().nsec();
  ros_nav_sat_fix_msg_.header.frame_id = gz_nav_sat_fix_msg->header().frame_id();

  ros_nav_sat_fix_msg_.status.service = gz_nav_sat_fix_msg->status().service();
  ros_nav_sat_fix_msg_.status.status = gz_nav_sat_fix_msg->status().status();

  ros_nav_sat_fix_msg_.latitude = gz_nav_sat_fix_msg->latitude();
  ros_nav_sat_fix_msg_.longitude = gz_nav_sat_fix_msg->longitude();
  ros_nav_sat_fix_msg_.altitude = gz_nav_sat_fix_msg->altitude();

  ros_nav_sat_fix_msg_.position_covariance_type = gz_nav_sat_fix_msg->position_covariance_type();

  // Position covariance should have 9 elements, and both the Gazebo and ROS
  // arrays should be the same size!
  GZ_ASSERT(gz_nav_sat_fix_msg->position_covariance_size() == 9, "The Gazebo NavSatFix message does not have 9 position covariance elements.");
  GZ_ASSERT(ros_nav_sat_fix_msg_.position_covariance.size() == 9, "The ROS NavSatFix message does not have 9 position covariance elements.");
  for(int i = 0; i < gz_nav_sat_fix_msg->position_covariance_size(); i ++) {
    ros_nav_sat_fix_msg_.position_covariance[i] = gz_nav_sat_fix_msg->position_covariance(i);
  }

  // Publish onto ROS framework
  ros_publisher.publish(ros_nav_sat_fix_msg_);

}

void GazeboRosInterfacePlugin::GzOdometryMsgCallback(GzOdometryMsgPtr& gz_odometry_msg, ros::Publisher ros_publisher) {
//  gzdbg << __FUNCTION__ << "() called." << std::endl;

  // We need to convert from a Gazebo message to a ROS message,
  // and then forward the Odometry message onto ROS

  // ============================================ //
  // =================== HEADER ================= //
  // ============================================ //
  ros_odometry_msg_.header.stamp.sec = gz_odometry_msg->header().stamp().sec();
  ros_odometry_msg_.header.stamp.nsec = gz_odometry_msg->header().stamp().nsec();
  ros_odometry_msg_.header.frame_id = gz_odometry_msg->header().frame_id();

  ros_odometry_msg_.child_frame_id = gz_odometry_msg->child_frame_id();

  // ============================================ //
  // ===================== POSE ================= //
  // ============================================ //
  ros_odometry_msg_.pose.pose.position.x = gz_odometry_msg->pose().pose().position().x();
  ros_odometry_msg_.pose.pose.position.y = gz_odometry_msg->pose().pose().position().y();
  ros_odometry_msg_.pose.pose.position.z = gz_odometry_msg->pose().pose().position().z();

  ros_odometry_msg_.pose.pose.orientation.w = gz_odometry_msg->pose().pose().orientation().w();
  ros_odometry_msg_.pose.pose.orientation.x = gz_odometry_msg->pose().pose().orientation().x();
  ros_odometry_msg_.pose.pose.orientation.y = gz_odometry_msg->pose().pose().orientation().y();
  ros_odometry_msg_.pose.pose.orientation.z = gz_odometry_msg->pose().pose().orientation().z();

  for(int i = 0; i < gz_odometry_msg->pose().covariance_size(); i ++) {
    ros_odometry_msg_.pose.covariance[i] = gz_odometry_msg->pose().covariance(i);
  }

  // ============================================ //
  // ===================== TWIST ================ //
  // ============================================ //
  ros_odometry_msg_.twist.twist.linear.x = gz_odometry_msg->twist().twist().linear().x();
  ros_odometry_msg_.twist.twist.linear.y = gz_odometry_msg->twist().twist().linear().y();
  ros_odometry_msg_.twist.twist.linear.z = gz_odometry_msg->twist().twist().linear().z();

  ros_odometry_msg_.twist.twist.angular.x = gz_odometry_msg->twist().twist().angular().x();
  ros_odometry_msg_.twist.twist.angular.y = gz_odometry_msg->twist().twist().angular().y();
  ros_odometry_msg_.twist.twist.angular.z = gz_odometry_msg->twist().twist().angular().z();

  for(int i = 0; i < gz_odometry_msg->twist().covariance_size(); i ++) {
    ros_odometry_msg_.twist.covariance[i] = gz_odometry_msg->twist().covariance(i);
  }

  // Publish onto ROS framework
//  gzdbg << "Publishing Odometry message to ROS framework..." << std::endl;
  ros_publisher.publish(ros_odometry_msg_);

}

void GazeboRosInterfacePlugin::GzPoseMsgCallback(GzPoseMsgPtr& gz_pose_msg, ros::Publisher ros_publisher) {
//  gzdbg << __FUNCTION__ << "() called." << std::endl;

  ros_pose_msg_.position.x = gz_pose_msg->position().x();
  ros_pose_msg_.position.y = gz_pose_msg->position().y();
  ros_pose_msg_.position.z = gz_pose_msg->position().z();

  ros_pose_msg_.orientation.w = gz_pose_msg->orientation().w();
  ros_pose_msg_.orientation.x = gz_pose_msg->orientation().x();
  ros_pose_msg_.orientation.y = gz_pose_msg->orientation().y();
  ros_pose_msg_.orientation.z = gz_pose_msg->orientation().z();

  ros_publisher.publish(ros_pose_msg_);
}

void GazeboRosInterfacePlugin::GzPoseWithCovarianceStampedMsgCallback(
    GzPoseWithCovarianceStampedMsgPtr& gz_pose_with_covariance_stamped_msg,
    ros::Publisher ros_publisher) {
//  gzdbg << __FUNCTION__ << "() called." << std::endl;

  // ============================================ //
  // =================== HEADER ================= //
  // ============================================ //
  ros_pose_with_covariance_stamped_msg_.header.stamp.sec = gz_pose_with_covariance_stamped_msg->header().stamp().sec();
  ros_pose_with_covariance_stamped_msg_.header.stamp.nsec = gz_pose_with_covariance_stamped_msg->header().stamp().nsec();
  ros_pose_with_covariance_stamped_msg_.header.frame_id = gz_pose_with_covariance_stamped_msg->header().frame_id();

  // ============================================ //
  // === POSE (both position and orientation) === //
  // ============================================ //
  ros_pose_with_covariance_stamped_msg_.pose.pose.position.x = gz_pose_with_covariance_stamped_msg->pose_with_covariance().pose().position().x();
  ros_pose_with_covariance_stamped_msg_.pose.pose.position.y = gz_pose_with_covariance_stamped_msg->pose_with_covariance().pose().position().y();
  ros_pose_with_covariance_stamped_msg_.pose.pose.position.z = gz_pose_with_covariance_stamped_msg->pose_with_covariance().pose().position().z();

  ros_pose_with_covariance_stamped_msg_.pose.pose.orientation.w = gz_pose_with_covariance_stamped_msg->pose_with_covariance().pose().orientation().w();
  ros_pose_with_covariance_stamped_msg_.pose.pose.orientation.x = gz_pose_with_covariance_stamped_msg->pose_with_covariance().pose().orientation().x();
  ros_pose_with_covariance_stamped_msg_.pose.pose.orientation.y = gz_pose_with_covariance_stamped_msg->pose_with_covariance().pose().orientation().y();
  ros_pose_with_covariance_stamped_msg_.pose.pose.orientation.z = gz_pose_with_covariance_stamped_msg->pose_with_covariance().pose().orientation().z();

  // Covariance should have 36 elements, and both the Gazebo and ROS
  // arrays should be the same size!
  //gzdbg << "DEBUG = " << gz_pose_with_covariance_stamped_msg->pose_with_covariance().covariance_size() << std::endl;
  GZ_ASSERT(gz_pose_with_covariance_stamped_msg->pose_with_covariance().covariance_size() == 36, "The Gazebo PoseWithCovarianceStamped message does not have 9 position covariance elements.");
  GZ_ASSERT(ros_pose_with_covariance_stamped_msg_.pose.covariance.size() == 36, "The ROS PoseWithCovarianceStamped message does not have 9 position covariance elements.");
  for(int i = 0; i < gz_pose_with_covariance_stamped_msg->pose_with_covariance().covariance_size(); i ++) {
    ros_pose_with_covariance_stamped_msg_.pose.covariance[i] = gz_pose_with_covariance_stamped_msg->pose_with_covariance().covariance(i);
  }

  ros_publisher.publish(ros_pose_with_covariance_stamped_msg_);
}

void GazeboRosInterfacePlugin::GzPositionStampedMsgCallback(
    GzPositionStampedMsgPtr& gz_position_stamped_msg,
    ros::Publisher ros_publisher) {
//  gzdbg << __FUNCTION__ << "() called." << std::endl;

  // ============================================ //
  // =================== HEADER ================= //
  // ============================================ //
  ros_position_stamped_msg_.header.stamp.sec = gz_position_stamped_msg->header().stamp().sec();
  ros_position_stamped_msg_.header.stamp.nsec = gz_position_stamped_msg->header().stamp().nsec();
  ros_position_stamped_msg_.header.frame_id = gz_position_stamped_msg->header().frame_id();

  // ============================================ //
  // ================== POSITION ================ //
  // ============================================ //

  ros_position_stamped_msg_.point.x = gz_position_stamped_msg->position().x();
  ros_position_stamped_msg_.point.y = gz_position_stamped_msg->position().y();
  ros_position_stamped_msg_.point.z = gz_position_stamped_msg->position().z();

  ros_publisher.publish(ros_position_stamped_msg_);
}

void GazeboRosInterfacePlugin::GzTransformStampedMsgCallback(
      GzTransformStampedMsgPtr& gz_transform_stamped_msg,
      ros::Publisher ros_publisher) {
//  gzdbg << __FUNCTION__ << "() called." << std::endl;
  gzthrow(__FUNCTION__ << "() is not yet implemented.");
}

void GazeboRosInterfacePlugin::GzTwistStampedMsgCallback(
    GzTwistStampedMsgPtr& gz_twist_stamped_msg,
    ros::Publisher ros_publisher) {
//  gzdbg << __FUNCTION__ << "() called." << std::endl;
  gzthrow(__FUNCTION__ << "() is not yet implemented.");
}



void GazeboRosInterfacePlugin::GzWrenchStampedMsgCallback(
    GzWrenchStampedMsgPtr& gz_wrench_stamped_msg,
    ros::Publisher ros_publisher) {

//  gzdbg << __FUNCTION__ << "() called." << std::endl;

  // ============================================ //
  // =================== HEADER ================= //
  // ============================================ //
  ros_wrench_stamped_msg_.header.stamp.sec = gz_wrench_stamped_msg->header().stamp().sec();
  ros_wrench_stamped_msg_.header.stamp.nsec = gz_wrench_stamped_msg->header().stamp().nsec();
  ros_wrench_stamped_msg_.header.frame_id = gz_wrench_stamped_msg->header().frame_id();

  // ============================================ //
  // =================== FORCE ================== //
  // ============================================ //
  ros_wrench_stamped_msg_.wrench.force.x = gz_wrench_stamped_msg->wrench().force().x();
  ros_wrench_stamped_msg_.wrench.force.y = gz_wrench_stamped_msg->wrench().force().y();
  ros_wrench_stamped_msg_.wrench.force.z = gz_wrench_stamped_msg->wrench().force().z();

  // ============================================ //
  // ==================== TORQUE ================ //
  // ============================================ //
  ros_wrench_stamped_msg_.wrench.torque.x = gz_wrench_stamped_msg->wrench().torque().x();
  ros_wrench_stamped_msg_.wrench.torque.y = gz_wrench_stamped_msg->wrench().torque().y();
  ros_wrench_stamped_msg_.wrench.torque.z = gz_wrench_stamped_msg->wrench().torque().z();

  ros_publisher.publish(ros_wrench_stamped_msg_);
}


//===========================================================================//
//================ ROS -> GAZEBO MSG CALLBACKS/CONVERTERS ===================//
//===========================================================================//

void GazeboRosInterfacePlugin::RosActuatorsMsgCallback(
    const mav_msgs::ActuatorsConstPtr& ros_actuators_msg_ptr,
    gazebo::transport::PublisherPtr gz_publisher_ptr) {

//  gzdbg << __FUNCTION__ << "() called." << std::endl;

  // Convert ROS message to Gazebo message

  sensor_msgs::msgs::Actuators gz_actuators_msg;

  gz_actuators_msg.mutable_header()->mutable_stamp()->set_sec(ros_actuators_msg_ptr->header.stamp.sec);
  gz_actuators_msg.mutable_header()->mutable_stamp()->set_nsec(ros_actuators_msg_ptr->header.stamp.nsec);
  gz_actuators_msg.mutable_header()->set_frame_id(ros_actuators_msg_ptr->header.frame_id);

  for(int i = 0; i < ros_actuators_msg_ptr->angular_velocities.size(); i++) {
    gz_actuators_msg.add_angular_velocities(ros_actuators_msg_ptr->angular_velocities[i]);
  }

  // Publish to Gazebo
//  gzdbg << "Publishing to gazebo topic \"" << gz_publisher_ptr->GetTopic() << std::endl;
  gz_publisher_ptr->Publish(gz_actuators_msg);
}

void GazeboRosInterfacePlugin::RosCommandMotorSpeedMsgCallback(
    const mav_msgs::ActuatorsConstPtr& ros_actuators_msg_ptr,
    gazebo::transport::PublisherPtr gz_publisher_ptr) {

//  gzdbg << __FUNCTION__ << "() called." << std::endl;

  // Convert ROS message to Gazebo message

  gz_mav_msgs::CommandMotorSpeed gz_command_motor_speed_msg;

//  gz_command_motor_speed_msg.mutable_header()->mutable_stamp()->set_sec(ros_actuators_msg_ptr->header.stamp.sec);
//  gz_command_motor_speed_msg.mutable_header()->mutable_stamp()->set_nsec(ros_actuators_msg_ptr->header.stamp.nsec);
//  gz_command_motor_speed_msg.mutable_header()->set_frame_id(ros_actuators_msg_ptr->header.frame_id);

  for(int i = 0; i < ros_actuators_msg_ptr->angular_velocities.size(); i++) {
    gz_command_motor_speed_msg.add_motor_speed(ros_actuators_msg_ptr->angular_velocities[i]);
  }

  // Publish to Gazebo
//  gzdbg << "Publishing to gazebo topic \"" << gz_publisher_ptr->GetTopic() << std::endl;
  gz_publisher_ptr->Publish(gz_command_motor_speed_msg);
}

void GazeboRosInterfacePlugin::RosWindSpeedMsgCallback(
      const rotors_comm::WindSpeedConstPtr& ros_wind_speed_msg_ptr,
      gazebo::transport::PublisherPtr gz_publisher_ptr) {

//  gzdbg << __FUNCTION__ << "() called." << std::endl;

  // Convert ROS message to Gazebo message
  gz_mav_msgs::WindSpeed gz_wind_speed_msg;

  gz_wind_speed_msg.mutable_header()->mutable_stamp()->set_sec(ros_wind_speed_msg_ptr->header.stamp.sec);
  gz_wind_speed_msg.mutable_header()->mutable_stamp()->set_nsec(ros_wind_speed_msg_ptr->header.stamp.nsec);
  gz_wind_speed_msg.mutable_header()->set_frame_id(ros_wind_speed_msg_ptr->header.frame_id);

  gz_wind_speed_msg.mutable_velocity()->set_x(ros_wind_speed_msg_ptr->velocity.x);
  gz_wind_speed_msg.mutable_velocity()->set_y(ros_wind_speed_msg_ptr->velocity.y);
  gz_wind_speed_msg.mutable_velocity()->set_z(ros_wind_speed_msg_ptr->velocity.z);

  // Publish to Gazebo
  gz_publisher_ptr->Publish(gz_wind_speed_msg);

}

void GazeboRosInterfacePlugin::OnUpdate(const common::UpdateInfo& _info) {
  // Do nothing
  // This plugins actions are all executed through message callbacks.
}

GZ_REGISTER_WORLD_PLUGIN(GazeboRosInterfacePlugin);

} // namespace gazebo
