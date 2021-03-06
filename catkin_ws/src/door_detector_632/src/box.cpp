#include <stdio.h>
#include <iostream>
#include <cstdlib>
#include <ros/ros.h>
#include <tf/tf.h>
#include <tf/transform_listener.h>
#include <tf/transform_broadcaster.h>
#include <sensor_msgs/LaserScan.h>
#include <pcl/point_types.h>
#include <pcl_ros/transforms.h>
#include <geometry_msgs/Vector3.h>
#include <gazebo_msgs/SetModelState.h>
#include <gazebo_msgs/GetModelState.h>
#include <gazebo_msgs/GetLinkState.h>
#include <std_msgs/String.h>
#include <std_msgs/Int16.h>
#include <cmath>
#include <string>

using namespace ros;
using namespace std;
using namespace pcl;

enum Room {A, B, C, D, E, N};
class DoorAndBall{
private:
  tf::TransformBroadcaster tf_br;
  tf::TransformListener tf_listener;
  tf::StampedTransform tf_pose;
  tf::Transform transform;
  tf::Quaternion q;
  Eigen::Matrix4f pose_matrix;

  float rotation_rad, rotate_x, rotate_y;
  int count_door = 0, count_ball = 0;
  Room which_room = N;
  string ball_list[4];
  geometry_msgs::PointStamped map_frame_laserpoints[241];

  Publisher pub_scan_label, pub_door_string, pub_room_info;
  Subscriber sub_scan;
  ServiceClient ser_client, link_client;

  gazebo_msgs::GetModelState getmodelstate, robotstate;
  gazebo_msgs::GetLinkState getlinkstate;
  sensor_msgs::LaserScan input_scan, output_scan;

public:
  DoorAndBall(NodeHandle &nh){
    ball_list[0]="unit_sphere";
    for(int i=0;i<3;i++) ball_list[i+1] = "unit_sphere_"+to_string(i);

    pub_scan_label = nh.advertise<sensor_msgs::LaserScan>("/RL/scan_label", 1);
    pub_door_string = nh.advertise<std_msgs::String>("/RL/door_string", 1);
    pub_room_info = nh.advertise<std_msgs::Int16>("/RL/room_info", 1);
    sub_scan = nh.subscribe("/RL/scan", 1, &DoorAndBall::scan_cb, this);
    ser_client = nh.serviceClient<gazebo_msgs::GetModelState>("/gazebo/get_model_state");
    // joint_client = nh.serviceClient<gazebo_msgs::GetJointProperties>("/gazebo/get_joint_properties");
    link_client = nh.serviceClient<gazebo_msgs::GetLinkState>("/gazebo/get_link_state");
    ROS_INFO("door detection service initialized");
  }

  void where_is_robot(double x, double y){
    which_room = N;
    if (((x>25) && (x<40)) && ((y>-35) && (y<-20))) which_room = A;
    if (((x>10) && (x<25)) && ((y>-35) && (y<-20))) which_room = B;
    if (((x>-40) && (x<-20)) && ((y>-25) && (y<-15))) which_room = C;
    if (((x>0) && (x<15)) && ((y>10) && (y<20))) which_room = D;
  }

  string RoomToString(Room c) {
    std_msgs::Int16 room;
    room.data = static_cast<int>(c);
    pub_room_info.publish(room);

    switch(c) {
      case A: return "hinge_door_0.7";
      case B: return "hinge_door_1.2_1";
      case C: return "hinge_door_1.2_0";
      case D: return "pull_box";
      default: return "NAN";
    }
  }
  void scan_process();
  void scan_cb(const sensor_msgs::LaserScan msg);
  bool get_tf(const string str_whichdoor);
  bool laserpoints_tf(const sensor_msgs::LaserScan msg, string target_frame, string source_frame);
  void check_ball();
  ~DoorAndBall(){};
};


bool DoorAndBall::get_tf(const string str_whichdoor){
  //map frame tf broadcast
  transform.setOrigin(tf::Vector3(robotstate.response.pose.position.x,
                                  robotstate.response.pose.position.y,
                                  robotstate.response.pose.position.z));
  tf::Quaternion q(robotstate.response.pose.orientation.x,
                    robotstate.response.pose.orientation.y,
                    robotstate.response.pose.orientation.z,
                    robotstate.response.pose.orientation.w);
  transform.setRotation(q);
  tf_br.sendTransform(tf::StampedTransform(transform, ros::Time::now(), "map", "base_link"));
  tf::Matrix3x3 m(q);
  double roll, pitch, yaw;
  m.getRPY(roll, pitch, yaw);

  // save map_frame_laserpoints
  if(input_scan.ranges.size()>0){
    float lidar_x = robotstate.response.pose.position.x+0.45*cos(yaw);
    float lidar_y = robotstate.response.pose.position.y+0.45*sin(yaw);
    float o_t_min = input_scan.angle_min, o_t_max = input_scan.angle_max, o_t_inc = input_scan.angle_increment;
    for(int i=0;i<input_scan.ranges.size();i++){
      float theta = o_t_min+i*o_t_inc, r = input_scan.ranges[i];
      geometry_msgs::PointStamped pt;
      pt.point.x = r*cos(theta)*cos(yaw) - r*sin(theta)*sin(yaw) + lidar_x;
      pt.point.y = r*sin(theta)*cos(yaw) + r*cos(theta)*sin(yaw) + lidar_y;
      map_frame_laserpoints[i] = pt;
    }
  }
  if (str_whichdoor=="BALL") return 0;

  // get box frame TF
  getmodelstate.request.model_name = str_whichdoor;
  if (ser_client.call(getmodelstate)) ;
  else return 0;
  string tmp = "::link_0";
//   getjointproperties.request.joint_name = str_whichdoor+tmp;
//   if (joint_client.call(getjointproperties)) ;
  getlinkstate.request.link_name = str_whichdoor+tmp;
  if(link_client.call(getlinkstate));
  else return 0;
//   rotation_rad = getjointproperties.response.position[0];
  rotate_x = getmodelstate.response.pose.position.x;
  rotate_y = getmodelstate.response.pose.position.y;
  // TF Broadcaster
  transform.setOrigin( tf::Vector3(rotate_x, rotate_y, 0) ); // z = 0

  tf::Quaternion q_box(getlinkstate.response.link_state.pose.orientation.x,
                    getlinkstate.response.link_state.pose.orientation.y,
                    getlinkstate.response.link_state.pose.orientation.z,
                    getlinkstate.response.link_state.pose.orientation.w);

  transform.setRotation(q_box);//tf::Transform transform;
  tf_br.sendTransform(tf::StampedTransform(transform, ros::Time::now(), "map", "model_box"));

  // prepare for transforming LaserScan from "front_laser" to "model_door"
  try{
      tf_listener.waitForTransform("model_box", "front_laser", ros::Time(0), ros::Duration(0.2));
      tf_listener.lookupTransform("model_box", "front_laser", ros::Time(0), tf_pose);
  }catch (tf::TransformException ex){
      ROS_ERROR("%s", ex.what());
      return 0;
  }
  return 1;
}
void DoorAndBall::scan_cb(const sensor_msgs::LaserScan msg){
  count_door = 0, count_ball = 0;
  input_scan = msg;
  output_scan.ranges.assign(msg.ranges.size(), std::numeric_limits<double>::infinity());
  output_scan.intensities.assign(msg.ranges.size(), std::numeric_limits<double>::infinity());
  for(int i=0;i<input_scan.ranges.size();i++){
    output_scan.ranges[i] = input_scan.ranges[i];
    output_scan.intensities[i] = 0;
  }

  // Which door should I detect
  getmodelstate.request.model_name = "robot";
  if (ser_client.call(getmodelstate)) ;
  else{
    ROS_ERROR("gazebo service get robot state fail");
    return ;
  }
  robotstate = getmodelstate;
  where_is_robot(robotstate.response.pose.position.x, robotstate.response.pose.position.y);
  // cout<<"RoomToString "<<RoomToString(which_room)<<" which_room "<<which_room<<endl;

  if(!(RoomToString(which_room)=="NAN")){ // check door
    std_msgs::String door_str;
    door_str.data = RoomToString(which_room);
    pub_door_string.publish(door_str);
    if(get_tf( RoomToString(which_room) )) scan_process();
  }
  check_ball();

  output_scan.header = msg.header;
  output_scan.angle_min = msg.angle_min;
  output_scan.angle_max = msg.angle_max;
  output_scan.angle_increment = msg.angle_increment;
  output_scan.time_increment = msg.time_increment;
  output_scan.scan_time = msg.scan_time;
  output_scan.range_min = msg.range_min;
  output_scan.range_max = msg.range_max;
  pub_scan_label.publish(output_scan);

  cout<<"total scanpoints:"<<output_scan.ranges.size()<<" box:"<<count_door<<" ball:"<<count_ball<<endl;
}

void DoorAndBall::check_ball(){
  get_tf("BALL");
  for(int i=0;i<4;i++){
    getmodelstate.request.model_name = ball_list[i];
    if (ser_client.call(getmodelstate)) ;
    else{
      ROS_ERROR("gazebo service get robot state fail");
      continue ;
    }
    double ball_x = getmodelstate.response.pose.position.x;
    double ball_y = getmodelstate.response.pose.position.y;
    // cout<<"check_ball()"<<ball_x<<ball_y<<endl;
    for(int j=0;j<241;j++){
      double dis = pow(map_frame_laserpoints[j].point.x - ball_x, 2)+ \
                    pow(map_frame_laserpoints[j].point.y - ball_y, 2)+ \
                    pow(0.45728 - 0.5, 2); // lidar z
      if(dis<0.5) output_scan.intensities[j] = 2, count_ball ++;
    }
  }
}

void DoorAndBall::scan_process(){
  if(input_scan.ranges.size()>0){
    float o_t_min = input_scan.angle_min, o_t_max = input_scan.angle_max, o_t_inc = input_scan.angle_increment;
    for(int i=0;i<input_scan.ranges.size();i++){
      float theta = o_t_min+i*o_t_inc, r = input_scan.ranges[i];
      geometry_msgs::PointStamped pt;
      pt.header.frame_id = "front_laser", pt.point.x = r*cos(theta), pt.point.y = r*sin(theta);
      try{tf_listener.transformPoint("model_box", pt, pt);}
      catch (tf::TransformException ex){
          ROS_ERROR("%s", ex.what());
          return;
      }
      if (which_room == A){
        if((pt.point.y<0.15) && (pt.point.y>-0.15) && (pt.point.x<(0.75-0.3)) && (pt.point.x>-0.3)){
          output_scan.intensities[i] = 1, count_door++;
        }
      }
      else{
        if((pt.point.y<0.5) && (pt.point.y>-0.5) && (pt.point.x<(0.6)) && (pt.point.x>-0.6)){
          output_scan.intensities[i] = 1, count_door++;
        }
      }
    }
  }
}


int main(int argc, char **argv){
  init(argc, argv, "box");
  NodeHandle nh;
  DoorAndBall Door_And_Ball(nh);
  spin();
  return 0;
}
