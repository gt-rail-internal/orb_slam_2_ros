#include "Node.h"
#include <opencv2/core/eigen.hpp>
#include <iostream>

Node::Node (ORB_SLAM2::System* pSLAM, ros::NodeHandle &node_handle, image_transport::ImageTransport &image_transport) {
  name_of_node_ = ros::this_node::getName();
  orb_slam_ = pSLAM;
  node_handle_ = node_handle;
  min_observations_per_point_ = 2;

  //static parameters
  node_handle_.param(name_of_node_+"/publish_pointcloud", publish_pointcloud_param_, true);
  node_handle_.param<std::string>(name_of_node_+"/pointcloud_frame_id", map_frame_id_param_, "map");
  node_handle_.param<std::string>(name_of_node_+"/camera_frame_id", camera_frame_id_param_, "odom");

  //Setup dynamic reconfigure
  dynamic_reconfigure::Server<orb_slam2_ros::dynamic_reconfigureConfig>::CallbackType dynamic_param_callback;
  dynamic_param_callback = boost::bind(&Node::ParamsChangedCallback, this, _1, _2);
  dynamic_param_server_.setCallback(dynamic_param_callback);

  rendered_image_publisher_ = image_transport.advertise (name_of_node_+"/debug_image", 1);
  if (publish_pointcloud_param_) {
    map_points_publisher_ = node_handle_.advertise<sensor_msgs::PointCloud2> (name_of_node_+"/map_points", 1);
  }
}


Node::~Node () {

}


void Node::Update () {
  cv::Mat position = orb_slam_->GetCurrentPosition();

  if (!position.empty()) {
    PublishPositionAsTransform (position);
  }

  PublishRenderedImage (orb_slam_->DrawCurrentFrame());

  if (publish_pointcloud_param_) {
    PublishMapPoints (orb_slam_->GetAllMapPoints());
  }

}


void Node::PublishMapPoints (std::vector<ORB_SLAM2::MapPoint*> map_points) {
  sensor_msgs::PointCloud2 cloud = MapPointsToPointCloud (map_points);
  map_points_publisher_.publish (cloud);
}


void Node::PublishPositionAsTransform (cv::Mat position) {
  tf::Transform transform = TransformFromMat (position);
  static tf::TransformBroadcaster tf_broadcaster;
  tf_broadcaster.sendTransform(tf::StampedTransform(transform, current_frame_time_, map_frame_id_param_, camera_frame_id_param_));
}


void Node::PublishRenderedImage (cv::Mat image) {
  std_msgs::Header header;
  header.stamp = current_frame_time_;
  header.frame_id = map_frame_id_param_;
  const sensor_msgs::ImagePtr rendered_image_msg = cv_bridge::CvImage(header, "bgr8", image).toImageMsg();
  rendered_image_publisher_.publish(rendered_image_msg);
}


tf::Transform Node::TransformFromMat (cv::Mat position_mat) {

  cv::Mat rotation(3,3,CV_32F);
  cv::Mat translation(3,1,CV_32F);
  rotation = position_mat.rowRange(0,3).colRange(0,3);
  translation = position_mat.rowRange(0,3).col(3);

  tf::Matrix3x3 tf_camera_rotation (rotation.at<float> (0,0), rotation.at<float> (0,1), rotation.at<float> (0,2),
                                    rotation.at<float> (1,0), rotation.at<float> (1,1), rotation.at<float> (1,2),
                                    rotation.at<float> (2,0), rotation.at<float> (2,1), rotation.at<float> (2,2)
                                   );

  tf::Vector3 tf_camera_translation (translation.at<float> (0), translation.at<float> (1), translation.at<float> (2));

  //Coordinate transformation matrix from orb coordinate system to ros coordinate system
  const tf::Matrix3x3 tf_orb_to_ros (0, 0, 1,
                                    -1, 0, 0,
                                     0,-1, 0);

  tf_camera_rotation = tf_orb_to_ros*tf_camera_rotation;
  tf_camera_translation = tf_orb_to_ros*tf_camera_translation;

  //Inverse matrix
  tf_camera_rotation = tf_camera_rotation.transpose();
  tf_camera_translation = -(tf_camera_rotation*tf_camera_translation);

  //Transform from orb coordinate system to ros coordinate system on map coordinates
  tf_camera_rotation = tf_orb_to_ros*tf_camera_rotation;
  tf_camera_translation = tf_orb_to_ros*tf_camera_translation;
  
  tf::Transform transform_to_ros_coordinates = tf::Transform (tf_camera_rotation, tf_camera_translation);

  // Checking
  tf::TransformListener tflistener;
  tf::StampedTransform transformStamped;

  // std::cout << "hit" << "\n" ;
  
  try {
    ros::Time now = ros::Time(0);
    tflistener.waitForTransform("head_camera_link", camera_frame_id_param_, now, ros::Duration(1.0));
    //now = ros::Time::now();
    tflistener.lookupTransform("head_camera_link", camera_frame_id_param_, now, transformStamped); // camera_frame_id_param_, ros::Time(0), transformStamped);
  }
  catch (tf::TransformException ex){
    ROS_ERROR("%s",ex.what());
    //ros::Duration(0.1).sleep();
  }
  
  tf::Transform transform_to_camera;
  //tf::Matrix3x3 rotation_matrix(transformStamped.getRotation());
  tf::Vector3 translation_vector(transformStamped.getOrigin().x(), transformStamped.getOrigin().y(), transformStamped.getOrigin().z());
  transform_to_camera.setOrigin(translation_vector);
  transform_to_camera.setRotation(transformStamped.getRotation());

  tf::Transform transformed_matrix = transform_to_ros_coordinates*transform_to_camera;

  

  /*//tf::Transform map_to_odom_transform = transform_to_ros_coordinates*


  Eigen::Matrix4d map_to_odom_transform_2;
  Eigen::Affine3d map_to_odom_transform;
  cv::cv2eigen(position_mat, map_to_odom_transform_2);
  map_to_odom_transform.matrix() = map_to_odom_transform_2;

  //std::cout << map_to_odom_transform_2 << '\n';
 
  Eigen::Affine3d final_transform;

  //Eigen::Affine3d OdomToCamera_transform_rotation;
  Eigen::Affine3d OdomToCamera_transform;
  //Eigen::Vector3d OdomToCamera_transform_translation;
  //Eigen::Matrix4d OdomToCamera_transform;
  //OdomToCamera_transform.setIdentity();
  tf::Transform transform_to_camera;
  //tf::Matrix3x3 rotation_matrix(transformStamped.getRotation());
  tf::Vector3 translation_vector(transformStamped.getOrigin().x(), transformStamped.getOrigin().y(), transformStamped.getOrigin().z());
  transform_to_camera.setOrigin(translation_vector);
  transform_to_camera.setRotation(transformStamped.getRotation());
   
  tf::transformTFToEigen(transform_to_ros_coordinates*transform_to_camera, OdomToCamera_transform);
  //tf::vectorTFToEigen(translation_vector, OdomToCamera_transform_translation);
  //OdomToCamera_transform.block<3,3>(0,0) = OdomToCamera_transform_rotation.matrix();
  //OdomToCamera_transform.rightCols<1>() = OdomToCamera_transform_translation;
 
  final_transform = map_to_odom_transform*OdomToCamera_transform;
  //Eigen::Affine3d final_transform_rotation = final_transform.block<3,3>(0,0);
  //Eigen::Vector3d final_transform_translation = final_transform.rightCols<1>();

  tf::StampedTransform transformed_matrix;
  tf::transformEigenToTF(final_transform, transformed_matrix);
  
  // Checking End*/

  /*
  cv::Mat rotation(3,3,CV_32F);
  cv::Mat translation(3,1,CV_32F);
  rotation = position_mat.rowRange(0,3).colRange(0,3);
  translation = position_mat.rowRange(0,3).col(3);

  tf::Matrix3x3 tf_camera_rotation (rotation.at<float> (0,0), rotation.at<float> (0,1), rotation.at<float> (0,2),
                                    rotation.at<float> (1,0), rotation.at<float> (1,1), rotation.at<float> (1,2),
                                    rotation.at<float> (2,0), rotation.at<float> (2,1), rotation.at<float> (2,2)
                                   );

  tf::Vector3 tf_camera_translation (translation.at<float> (0), translation.at<float> (1), translation.at<float> (2));

  //Coordinate transformation matrix from orb coordinate system to ros coordinate system
  const tf::Matrix3x3 tf_orb_to_ros (0, 0, 1,
                                    -1, 0, 0,
                                     0,-1, 0);

  //Transform from orb coordinate system to ros coordinate system on camera coordinates
  tf_camera_rotation = tf_orb_to_ros*tf_camera_rotation;
  tf_camera_translation = tf_orb_to_ros*tf_camera_translation;

  //Inverse matrix
  tf_camera_rotation = tf_camera_rotation.transpose();
  tf_camera_translation = -(tf_camera_rotation*tf_camera_translation);

  //Transform from orb coordinate system to ros coordinate system on map coordinates
  tf_camera_rotation = tf_orb_to_ros*tf_camera_rotation;
  tf_camera_translation = tf_orb_to_ros*tf_camera_translation;

  return tf::Transform (tf_camera_rotation, tf_camera_translation);*/
  
  return transformed_matrix;
}


sensor_msgs::PointCloud2 Node::MapPointsToPointCloud (std::vector<ORB_SLAM2::MapPoint*> map_points) {
  if (map_points.size() == 0) {
    std::cout << "Map point vector is empty!" << std::endl;
  }

  sensor_msgs::PointCloud2 cloud;

  const int num_channels = 3; // x y z

  cloud.header.stamp = current_frame_time_;
  cloud.header.frame_id = map_frame_id_param_;
  cloud.height = 1;
  cloud.width = map_points.size();
  cloud.is_bigendian = false;
  cloud.is_dense = true;
  cloud.point_step = num_channels * sizeof(float);
  cloud.row_step = cloud.point_step * cloud.width;
  cloud.fields.resize(num_channels);

  std::string channel_id[] = { "x", "y", "z"};
  for (int i = 0; i<num_channels; i++) {
  	cloud.fields[i].name = channel_id[i];
  	cloud.fields[i].offset = i * sizeof(float);
  	cloud.fields[i].count = 1;
  	cloud.fields[i].datatype = sensor_msgs::PointField::FLOAT32;
  }

  cloud.data.resize(cloud.row_step * cloud.height);

	unsigned char *cloud_data_ptr = &(cloud.data[0]);

  float data_array[3];
  for (unsigned int i=0; i<cloud.width; i++) {
    if (map_points.at(i)->nObs >= min_observations_per_point_) {//nObs isBad()
      data_array[0] = map_points.at(i)->GetWorldPos().at<float> (2); //x. Do the transformation by just reading at the position of z instead of x
      data_array[1] = -1.0* map_points.at(i)->GetWorldPos().at<float> (0); //y. Do the transformation by just reading at the position of x instead of y
      data_array[2] = -1.0* map_points.at(i)->GetWorldPos().at<float> (1); //z. Do the transformation by just reading at the position of y instead of z
      //TODO dont hack the transformation but have a central conversion function for MapPointsToPointCloud and TransformFromMat

      memcpy(cloud_data_ptr+(i*cloud.point_step), data_array, 3*sizeof(float));
    }
  }

  return cloud;
}


void Node::ParamsChangedCallback(orb_slam2_ros::dynamic_reconfigureConfig &config, uint32_t level) {
  orb_slam_->EnableLocalizationOnly (config.localize_only);
  min_observations_per_point_ = config.min_observations_for_ros_map;

  if (config.reset_map) {
    orb_slam_->Reset();
    config.reset_map = false;
  }

  orb_slam_->SetMinimumKeyFrames (config.min_num_kf_in_map);
}
