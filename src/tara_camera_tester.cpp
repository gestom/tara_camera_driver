/*********************************************************************
* Software License Agreement (BSD License)
*
*  Copyright (C) 2016 Thomas Fischer
*  All rights reserved.
*
*  Redistribution and use in source and binary forms, with or without
*  modification, are permitted provided that the following conditions
*  are met:
*
*   * Redistributions of source code must retain the above copyright
*     notice, this list of conditions and the following disclaimer.
*   * Redistributions in binary form must reproduce the above
*     copyright notice, this list of conditions and the following
*     disclaimer in the documentation and/or other materials provided
*     with the distribution.
*   * Neither the name of the author nor other contributors may be
*     used to endorse or promote products derived from this software
*     without specific prior written permission.
*
*  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
*  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
*  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
*  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
*  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
*  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
*  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
*  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
*  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
*  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
*  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
*  POSSIBILITY OF SUCH DAMAGE.
*********************************************************************/

#include <ros/ros.h>
#include "tara_camera_driver.h"


#include "std_msgs/String.h"

#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>

using namespace tara_camera_driver;

CameraDriver::CameraDriver(const std::string& device, ros::NodeHandle nh, ros::NodeHandle nhp)
  : nh_( nh ), nhp_( nhp )
  , tara_cam_( device ), it_( nh )
  , cinfo_manager_left_( ros::NodeHandle(nhp, "left") )
  , cinfo_manager_right_( ros::NodeHandle(nhp, "right") )
  , next_seq_( 0 )
{
  cam_pub_left_ = it_.advertiseCamera("left/image_raw", 1, false);
  cam_pub_right_ = it_.advertiseCamera("right/image_raw", 1, false);

  // TODO make everything below a parameter.

  nhp.param<std::string>("frame_id", frame_id_, "tara_camera");

  std::string left_camera_info_url, right_camera_info_url;

  if (nhp.hasParam("left/camera_info_url"))
    nhp.param<std::string>("left/camera_info_url", left_camera_info_url, "");

  if (nhp.hasParam("right/camera_info_url"))
    nhp.param<std::string>("right/camera_info_url", right_camera_info_url, "");

  cinfo_manager_left_.setCameraName("tara_left");
  cinfo_manager_right_.setCameraName("tara_right");

  cinfo_manager_left_.loadCameraInfo( left_camera_info_url );
  cinfo_manager_right_.loadCameraInfo( right_camera_info_url );
}

void CameraDriver::exposureCallback(const std_msgs::Int32::ConstPtr& input)
{ 
  int exposure = input->data;
  bool retVal = tara_cam_.setExposure(exposure);

  if(retVal){
       ROS_INFO("done: [%i]", exposure);
   } else {
         ROS_INFO("fail: [%i]", exposure);
   }
  
}

void CameraDriver::run()
{
  cv::Mat left_image(FRAME_HEIGHT, FRAME_WIDTH, CV_8UC1);
  cv::Mat right_image(FRAME_HEIGHT, FRAME_WIDTH, CV_8UC1);

  ros::NodeHandle n;
  ros::Publisher exposure_pub = n.advertise<std_msgs::Int32>("get_exposure", 1000);
  ros::Subscriber exposure_sub = n.subscribe("set_exposure", 1000, &CameraDriver::exposureCallback, this);

  float sum = 0; 
  float lastSum = 10; 
  int exposure=10;

  int exposureArray[255];
  int minGray = 20;
  int maxGray = 255-minGray;
  for (int i = minGray;i<maxGray;i++) exposureArray[i] = 0;
  int increment = 1;
  int j = 0;
  char filename[100];
  /*initial walkthrough*/
  for (int i = 0;i<1000 && (sum < maxGray || lastSum < maxGray || exposure < 255);i++)
  {
	  tara_cam_.setExposure(exposure);
	  /*let the camera settle*/
	  for (int j=0;j<5;j++) tara_cam_.grabNextFrame(left_image, right_image);
	  /*calculate image brightness*/
	  lastSum = sum;
	  sum = cv::sum(left_image).val[0]/left_image.rows/left_image.cols;
	  sprintf(filename,"image_%03i_%02i_%06i.png",(int)(round(sum)),j,exposure);
	  cv::imwrite(filename,right_image);
	  exposureArray[(int)(sum+0.5)] = exposure;
	  if (lastSum < sum)  increment = (int)fmax(fmin((increment/(sum-lastSum)),exposure/10.0),1);   //adaptive step for exposure increment
	  exposure+=increment;
	  ROS_INFO("Exposure %i %i %f",i,exposure,sum);
	  ros::spinOnce();
  }

  /*detect missing average grays and fill them*/
  for (int i = minGray;i<maxGray;i++){
	  if (exposureArray[i] ==0)
	  {
		  ROS_INFO("Gap detected %i ",i);
		  int j = i;
		  while (j<maxGray && exposureArray[j] ==0) j++;
		  float minExposure = exposureArray[i-1];
		  float maxExposure = exposureArray[j];
		  ROS_INFO("Bounds %i:%.0f  %i:%.0f",i-1,minExposure,j,maxExposure);
		  for (int k=i;k<j;k++){
			  exposure = (k-i+1)*(maxExposure-minExposure)/(j-i+1)+minExposure;
			  tara_cam_.setExposure(exposure);
			  for (int j=0;j<8;j++)  tara_cam_.grabNextFrame(left_image, right_image);
			  sum = cv::sum(left_image).val[0]/left_image.rows/left_image.cols;
			  ROS_INFO("Tried %i, got %f ",exposure,sum);
			  int index = (int)(round(sum));
			  if (exposureArray[index]==0){
				 exposureArray[index]=exposure; 
				 ROS_INFO("Gap filled %i %i",i,exposure);
			  }
			  ros::spinOnce();
		  }

	  }
  }


{	  /*encode exposure in the image*/
	  left_image.at<char>(0,0) = exposure/256/256;	
	  left_image.at<char>(0,1) = exposure/256%256;	
	  left_image.at<char>(0,2) = exposure%256;
//	  left_image.at<char>(0,3) = brightness%256;
	
	  ros::Time timestamp = ros::Time::now();

	  //Encoding the exposure information in the image

	  std_msgs::Header header;
	  header.seq = next_seq_;
	  header.stamp = timestamp;
	  header.frame_id = frame_id_;

	  // Convert OpenCV image to ROS image msg.
	  cv_bridge::CvImage bridge_image_left(header, sensor_msgs::image_encodings::MONO8, left_image);
	  cv_bridge::CvImage bridge_image_right(header, sensor_msgs::image_encodings::MONO8, right_image);

	  sensor_msgs::CameraInfo::Ptr camera_info_left(new sensor_msgs::CameraInfo(cinfo_manager_left_.getCameraInfo()));
	  sensor_msgs::CameraInfo::Ptr camera_info_right(new sensor_msgs::CameraInfo(cinfo_manager_right_.getCameraInfo()));

	  camera_info_left->header = header;
	  camera_info_right->header = header;

	  cam_pub_left_.publish(bridge_image_left.toImageMsg(), camera_info_left);
	  cam_pub_right_.publish(bridge_image_right.toImageMsg(), camera_info_right);


	  /* // this code slow down camera
	     int exposure_value = tara_cam_.getExposure();
	     std_msgs::Int32 exposure_msg;
	     exposure_msg.data = (int) exposure_value;
	     exposure_pub.publish(exposure_msg);
	     printf("exposure msg\n");*/

	  next_seq_++;

	  ros::spinOnce();
  }
}

int main (int argc, char** argv)
{
  ros::init(argc, argv, "tara_camera");

  ros::NodeHandle nh;
  ros::NodeHandle nhp("~");

  std::string device;
  nhp.param<std::string>("device", device, "/dev/video0");

  tara_camera_driver::CameraDriver driver(device, nh, nhp);

  driver.run();

  return EXIT_SUCCESS;
}
