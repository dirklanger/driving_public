/********************************************************
  Stanford Driving Software
  Copyright (c) 2011 Stanford University
  All rights reserved.

  Redistribution and use in source and binary forms, with
  or without modification, are permitted provided that the
  following conditions are met:

* Redistributions of source code must retain the above
  copyright notice, this list of conditions and the
  following disclaimer.
* Redistributions in binary form must reproduce the above
  copyright notice, this list of conditions and the
  following disclaimer in the documentation and/or other
  materials provided with the distribution.
* The names of the contributors may not be used to endorse
  or promote products derived from this software
  without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
  CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
  WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
  WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
  PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
  INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
  OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
  DAMAGE.
 ********************************************************/

#ifndef __LOG_AND_PLAYBACK__DATA_READER__H__
#define __LOG_AND_PLAYBACK__DATA_READER__H__

#include <boost/shared_ptr.hpp>
#include <boost/program_options.hpp>

#include <geometry_msgs/Point32.h>
#include <tf/transform_listener.h>
#include <tf/transform_broadcaster.h>
#include <urdf_model/model.h>
#include <kdl/tree.hpp>

#include <log_and_playback/abstract_data_reader.h>
#include <log_and_playback/bag_reader.h>
#include <log_and_playback/dgclog_reader.h>
#include <log_and_playback/kittireader.h>

#include <stdr_velodyne/message_filter.h>
#include <stdr_velodyne/pointcloud.h>
#include <localize/applanix_transformer.h>
#include <localize/fake_localizer.h>


namespace log_and_playback
{

class DataReader : public AbstractDataReader
{
public:
  /// Load the logs. Optionally skip the first @param skip seconds.
  void load(const std::vector<std::string> & logs, ros::Duration skip=ros::Duration(0));
  bool next();
  bool ok() const { return ok_; }
  ros::Time time() const { return time_; }

  stdr_msgs::ApplanixPose::ConstPtr instantiateApplanixPose() const;
  stdr_msgs::ApplanixGPS::ConstPtr instantiateApplanixGPS() const;
  stdr_msgs::ApplanixRMS::ConstPtr instantiateApplanixRMS() const;
  velodyne_msgs::VelodyneScan::ConstPtr instantiateVelodyneScans() const;
  stdr_velodyne::PointCloud::ConstPtr instantiateVelodyneSpin() const;
  stdr_msgs::LadybugImages::ConstPtr instantiateLadybugImages() const;
  stdr_msgs::LocalizePose::ConstPtr instantiateLocalizePose() const;
  stdr_msgs::EStopStatus::ConstPtr instantiateEStopStatus() const;
  stdr_msgs::PassatStatus::ConstPtr instantiatePassatStatus() const;
  stdr_msgs::Trajectory2D::ConstPtr instantiateTrajectory2D() const;

private:
  std::vector< boost::shared_ptr<AbstractDataReader> > readers_;
  bool ok_;
  ros::Time time_;
};


class RobotModel
{
private:
  std::vector< tf::StampedTransform > static_transforms_;

  /// Adds the static transforms from the robot model.
  void addModel(const urdf::ModelInterface& model);

  void addChildren(const KDL::SegmentMap::const_iterator segment);

public:
  /// Adds the static transforms from the robot model.
  /// The model is given as an XML string, retrieved from the parameter server,
  /// using the given parameter name.
  void addParam(const std::string & param);

  /// Adds the static transforms from the robot model.
  /// The model is given as an XML file.
  void addFile(const std::string & filename);

  inline const std::vector< tf::StampedTransform >& getStaticTransforms() const
  { return static_transforms_; }
};


/** A TransformListener that is convenient to use with bags.
 *
 * The TF tree can be updated directly from ApplanixPose/LocalizePose messages,
 * and static transforms can be added to it.
 *
 * Considering the global frame transforms ("utm" -> "local_utm" -> "smooth"):
 * - initially those are updated from the ApplanixPose using the fake localizer,
 *   unless useLocalizePose() was called.
 * - when a LocalizePose message is added (addLocalizePose), the localizer is
 *   reset and the global transforms are subsequently updated from the
 *   LocalizePose messages only.
 * This allows to conditionally work with LocalizePose messages, when not sure
 * whether they will be available.
 */
class BagTFListener : public tf::TransformerHelper
{
public:
  BagTFListener() : broadcast_(false), from_localize_pose_(false), initialized_(false) {}
  bool initialized() const { return initialized_; }
  void broadcast() { broadcast_ = true; }
  void useLocalizePose() { from_localize_pose_ = true; }

  void addApplanixPose(const stdr_msgs::ApplanixPose &);
  void addLocalizePose(const stdr_msgs::LocalizePose &);
  void addTFMsg(const tf::tfMessage & msg);

  /// Adds a static transform to the tree. Time stamp is irrelevant.
  /// Note: Static transforms will be added into the tree each time it is updated,
  /// which is probably too often...
  void addStaticTransform(const tf::StampedTransform &);

  /// Adds a static transforms to the tree. Time stamp is irrelevant.
  void addStaticTransforms(const std::vector< tf::StampedTransform > &);

private:
  std::vector< tf::StampedTransform > static_transforms_;
  bool broadcast_, from_localize_pose_;
  tf::TransformBroadcaster broadcaster_;
  localize::ApplanixTransformer app_trans_;
  localize::FakeLocalizer fake_localizer_;
  bool initialized_;

  void handleStaticTransforms(const ros::Time & stamp);
};


class SpinReader
{
public:

  /** Creates an empty spin reader.
   *
   * It needs to be filled with a DataReader, either by loading some data files
   * using the load function, or by hooking an external data reader with the
   * set data reader function.
   */
  SpinReader();

  ~SpinReader();

  /** Hook with an external data reader.
   *
   * Any previous data reader is discarded.
   */
  void setDataReader(AbstractDataReader &);

  /// Removes the data reader, deleting it if it's not an external one
  void unsetDataReader();

  /** Loads some data files.
   *
   * Constructs a new data reader internally.
   * If a data reader is already set it is discarded first.
   *
   * Optionally skip the first @param skip seconds.
   */
  void load(const std::vector< std::string > &logs, ros::Duration skip=ros::Duration(0));

  /// Returns the current spin
  stdr_velodyne::PointCloudConstPtr getSpin() const;

  bool prevSpin();
  bool nextSpin();

  const BagTFListener & tfListener() const { return tf_listener_; }
  BagTFListener & tfListener() { return tf_listener_; }


  static void addOptions(boost::program_options::options_description&);
  static void addOptions(boost::program_options::positional_options_description&);

  /// loads the calibration file, from the option in vm if set,
  /// otherwise from rosparam. Throws a runtime_error if none of them is set,
  /// or if it fails to load.
  virtual void loadCalibrationFromProgramOptions(const boost::program_options::variables_map & vm);

  /// loads the velodyne's TFM, from the option in vm if set,
  /// otherwise from rosparam. Throws a runtime_error if none of them is set.
  virtual void loadTFMFromProgramOptions(const boost::program_options::variables_map & vm);

protected:
  bool do_I_own_the_data_reader_;
  AbstractDataReader * data_reader_;

  stdr_velodyne::SpinCollector spin_collector_; //< buffer to get full spins
  stdr_velodyne::PointCloudPtr current_spin_;   //< current spin

  BagTFListener tf_listener_;

  // a queue to hold the spin messages until a transform is available
  std::queue< stdr_velodyne::PointCloud::ConstPtr > spinQ_;

  /// static config instance
  stdr_velodyne::Configuration::Ptr config_;

  stdr_velodyne::PacketToPcd packet2pcd_convertor_;

  /// Check whether spins in the Q can be transformed to the smooth frame.
  stdr_velodyne::PointCloudPtr processSpinQueue();

private:
  bool next();
};

} //namespace log_and_playback

#endif /* __LOG_AND_PLAYBACK__DATA_READER__H__ */
