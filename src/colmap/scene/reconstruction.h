// Copyright (c) 2023, ETH Zurich and UNC Chapel Hill.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//
//     * Redistributions in binary form must reproduce the above copyright
//       notice, this list of conditions and the following disclaimer in the
//       documentation and/or other materials provided with the distribution.
//
//     * Neither the name of ETH Zurich and UNC Chapel Hill nor the names of
//       its contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#pragma once

#include "colmap/geometry/sim3.h"
#include "colmap/scene/camera.h"
#include "colmap/scene/database.h"
#include "colmap/scene/image.h"
#include "colmap/scene/point2d.h"
#include "colmap/scene/point3d.h"
#include "colmap/scene/track.h"
#include "colmap/util/eigen_alignment.h"
#include "colmap/util/types.h"

#include <memory>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <Eigen/Core>

namespace colmap {

struct PlyPoint;
struct RANSACOptions;
class DatabaseCache;
class CorrespondenceGraph;

// Reconstruction class holds all information about a single reconstructed
// model. It is used by the mapping and bundle adjustment classes and can be
// written to and read from disk.
class Reconstruction {
 public:
  struct ImagePairStat {
    // The number of triangulated correspondences between two images.
    size_t num_tri_corrs = 0;
    // The number of total correspondences/matches between two images.
    size_t num_total_corrs = 0;
  };

  Reconstruction();

  // Get number of objects.
  inline size_t NumCameras() const;
  inline size_t NumImages() const;
  inline size_t NumRegImages() const;
  inline size_t NumPoints3D() const;
  inline size_t NumImagePairs() const;

  // Get const objects.
  inline const struct Camera& Camera(camera_t camera_id) const;
  inline const class Image& Image(image_t image_id) const;
  inline const struct Point3D& Point3D(point3D_t point3D_id) const;
  inline const ImagePairStat& ImagePair(image_pair_t pair_id) const;
  inline ImagePairStat& ImagePair(image_t image_id1, image_t image_id2);

  // Get mutable objects.
  inline struct Camera& Camera(camera_t camera_id);
  inline class Image& Image(image_t image_id);
  inline struct Point3D& Point3D(point3D_t point3D_id);
  inline ImagePairStat& ImagePair(image_pair_t pair_id);
  inline const ImagePairStat& ImagePair(image_t image_id1,
                                        image_t image_id2) const;

  // Get reference to all objects.
  inline const std::unordered_map<camera_t, struct Camera>& Cameras() const;
  inline const std::unordered_map<image_t, class Image>& Images() const;
  inline const std::vector<image_t>& RegImageIds() const;
  inline const std::unordered_map<point3D_t, struct Point3D>& Points3D() const;
  inline const std::unordered_map<image_pair_t, ImagePairStat>& ImagePairs()
      const;

  // Identifiers of all 3D points.
  std::unordered_set<point3D_t> Point3DIds() const;

  // Check whether specific object exists.
  inline bool ExistsCamera(camera_t camera_id) const;
  inline bool ExistsImage(image_t image_id) const;
  inline bool ExistsPoint3D(point3D_t point3D_id) const;
  inline bool ExistsImagePair(image_pair_t pair_id) const;

  // Load data from given `DatabaseCache`.
  void Load(const DatabaseCache& database_cache);

  // Setup all relevant data structures before reconstruction. Note the
  // correspondence graph object must live until `TearDown` is called.
  void SetUp(std::shared_ptr<const CorrespondenceGraph> correspondence_graph);

  // Finalize the Reconstruction after the reconstruction has finished.
  //
  // Once a scene has been finalized, it cannot be used for reconstruction.
  //
  // This removes all not yet registered images and unused cameras, in order to
  // save memory.
  void TearDown();

  // Add new camera. There is only one camera per image, while multiple images
  // might be taken by the same camera.
  void AddCamera(struct Camera camera);

  // Add new image.
  void AddImage(class Image image);

  // Add new 3D point with known ID.
  void AddPoint3D(point3D_t point3D_id, struct Point3D point3D);

  // Add new 3D object, and return its unique ID.
  point3D_t AddPoint3D(
      const Eigen::Vector3d& xyz,
      Track track,
      const Eigen::Vector3ub& color = Eigen::Vector3ub::Zero());

  // Add observation to existing 3D point.
  void AddObservation(point3D_t point3D_id, const TrackElement& track_el);

  // Merge two 3D points and return new identifier of new 3D point.
  // The location of the merged 3D point is a weighted average of the two
  // original 3D point's locations according to their track lengths.
  point3D_t MergePoints3D(point3D_t point3D_id1, point3D_t point3D_id2);

  // Delete a 3D point, and all its references in the observed images.
  void DeletePoint3D(point3D_t point3D_id);

  // Delete one observation from an image and the corresponding 3D point.
  // Note that this deletes the entire 3D point, if the track has two elements
  // prior to calling this method.
  void DeleteObservation(image_t image_id, point2D_t point2D_idx);

  // Delete all 2D points of all images and all 3D points.
  void DeleteAllPoints2DAndPoints3D();

  // Register an existing image.
  void RegisterImage(image_t image_id);

  // De-register an existing image, and all its references.
  void DeRegisterImage(image_t image_id);

  // Check if image is registered.
  inline bool IsImageRegistered(image_t image_id) const;

  // Normalize scene by scaling and translation to avoid degenerate
  // visualization after bundle adjustment and to improve numerical
  // stability of algorithms.
  //
  // Translates scene such that the mean of the camera centers or point
  // locations are at the origin of the coordinate system.
  //
  // Scales scene such that the minimum and maximum camera centers are at the
  // given `extent`, whereas `p0` and `p1` determine the minimum and
  // maximum percentiles of the camera centers considered.
  void Normalize(double extent = 10.0,
                 double p0 = 0.1,
                 double p1 = 0.9,
                 bool use_images = true);

  // Compute the centroid of the 3D points
  Eigen::Vector3d ComputeCentroid(double p0 = 0.1, double p1 = 0.9) const;

  // Compute the bounding box corners of the 3D points
  std::pair<Eigen::Vector3d, Eigen::Vector3d> ComputeBoundingBox(
      double p0 = 0.0, double p1 = 1.0) const;

  // Apply the 3D similarity transformation to all images and points.
  void Transform(const Sim3d& new_from_old_world);

  // Creates a cropped reconstruction using the input bounds as corner points
  // of the bounding box containing the included 3D points of the new
  // reconstruction. Only the cameras and images of the included points are
  // registered.
  Reconstruction Crop(
      const std::pair<Eigen::Vector3d, Eigen::Vector3d>& bbox) const;

  // Find specific image by name. Note that this uses linear search.
  const class Image* FindImageWithName(const std::string& name) const;

  // Find images that are both present in this and the given reconstruction.
  // Matching of images is performed based on common image names.
  std::vector<std::pair<image_t, image_t>> FindCommonRegImageIds(
      const Reconstruction& other) const;

  // Update the image identifiers to match the ones in the database by matching
  // the names of the images.
  void TranscribeImageIdsToDatabase(const Database& database);

  // Filter 3D points with large reprojection error, negative depth, or
  // insufficient triangulation angle.
  //
  // @param max_reproj_error    The maximum reprojection error.
  // @param min_tri_angle       The minimum triangulation angle.
  // @param point3D_ids         The points to be filtered.
  //
  // @return                    The number of filtered observations.
  size_t FilterPoints3D(double max_reproj_error,
                        double min_tri_angle,
                        const std::unordered_set<point3D_t>& point3D_ids);
  size_t FilterPoints3DInImages(double max_reproj_error,
                                double min_tri_angle,
                                const std::unordered_set<image_t>& image_ids);
  size_t FilterAllPoints3D(double max_reproj_error, double min_tri_angle);

  // Filter observations that have negative depth.
  //
  // @return    The number of filtered observations.
  size_t FilterObservationsWithNegativeDepth();

  // Filter images without observations or bogus camera parameters.
  //
  // @return    The identifiers of the filtered images.
  std::vector<image_t> FilterImages(double min_focal_length_ratio,
                                    double max_focal_length_ratio,
                                    double max_extra_param);

  // Compute statistics for scene.
  size_t ComputeNumObservations() const;
  double ComputeMeanTrackLength() const;
  double ComputeMeanObservationsPerRegImage() const;
  double ComputeMeanReprojectionError() const;

  // Updates mean reprojection errors for all 3D points.
  void UpdatePoint3DErrors();

  // Read data from text or binary file. Prefer binary data if it exists.
  void Read(const std::string& path);
  void Write(const std::string& path) const;

  // Read data from binary/text file.
  void ReadText(const std::string& path);
  void ReadBinary(const std::string& path);

  // Write data from binary/text file.
  void WriteText(const std::string& path) const;
  void WriteBinary(const std::string& path) const;

  // Convert 3D points in reconstruction to PLY point cloud.
  std::vector<PlyPoint> ConvertToPLY() const;

  // Import from other data formats. Note that these import functions are
  // only intended for visualization of data and unusable for reconstruction.
  void ImportPLY(const std::string& path);
  void ImportPLY(const std::vector<PlyPoint>& ply_points);

  // Extract colors for 3D points of given image. Colors will be extracted
  // only for 3D points which are completely black.
  //
  // @param image_id      Identifier of the image for which to extract colors.
  // @param path          Absolute or relative path to root folder of image.
  //                      The image path is determined by concatenating the
  //                      root path and the name of the image.
  //
  // @return              True if image could be read at given path.
  bool ExtractColorsForImage(image_t image_id, const std::string& path);

  // Extract colors for all 3D points by computing the mean color of all images.
  //
  // @param path          Absolute or relative path to root folder of image.
  //                      The image path is determined by concatenating the
  //                      root path and the name of the image.
  void ExtractColorsForAllImages(const std::string& path);

  // Create all image sub-directories in the given path.
  void CreateImageDirs(const std::string& path) const;

 private:
  size_t FilterPoints3DWithSmallTriangulationAngle(
      double min_tri_angle, const std::unordered_set<point3D_t>& point3D_ids);
  size_t FilterPoints3DWithLargeReprojectionError(
      double max_reproj_error,
      const std::unordered_set<point3D_t>& point3D_ids);

  std::tuple<Eigen::Vector3d, Eigen::Vector3d, Eigen::Vector3d>
  ComputeBoundsAndCentroid(double p0, double p1, bool use_images) const;

  void SetObservationAsTriangulated(image_t image_id,
                                    point2D_t point2D_idx,
                                    bool is_continued_point3D);
  void ResetTriObservations(image_t image_id,
                            point2D_t point2D_idx,
                            bool is_deleted_point3D);

  std::shared_ptr<const CorrespondenceGraph> correspondence_graph_;

  std::unordered_map<camera_t, struct Camera> cameras_;
  std::unordered_map<image_t, class Image> images_;
  std::unordered_map<point3D_t, struct Point3D> points3D_;

  std::unordered_map<image_pair_t, ImagePairStat> image_pair_stats_;

  // { image_id, ... } where `images_.at(image_id).registered == true`.
  std::vector<image_t> reg_image_ids_;

  // Total number of added 3D points, used to generate unique identifiers.
  point3D_t max_point3D_id_;
};

////////////////////////////////////////////////////////////////////////////////
// Implementation
////////////////////////////////////////////////////////////////////////////////

size_t Reconstruction::NumCameras() const { return cameras_.size(); }

size_t Reconstruction::NumImages() const { return images_.size(); }

size_t Reconstruction::NumRegImages() const { return reg_image_ids_.size(); }

size_t Reconstruction::NumPoints3D() const { return points3D_.size(); }

size_t Reconstruction::NumImagePairs() const {
  return image_pair_stats_.size();
}

const struct Camera& Reconstruction::Camera(const camera_t camera_id) const {
  return cameras_.at(camera_id);
}

const class Image& Reconstruction::Image(const image_t image_id) const {
  return images_.at(image_id);
}

const struct Point3D& Reconstruction::Point3D(
    const point3D_t point3D_id) const {
  return points3D_.at(point3D_id);
}

const Reconstruction::ImagePairStat& Reconstruction::ImagePair(
    const image_pair_t pair_id) const {
  return image_pair_stats_.at(pair_id);
}

const Reconstruction::ImagePairStat& Reconstruction::ImagePair(
    const image_t image_id1, const image_t image_id2) const {
  const auto pair_id = Database::ImagePairToPairId(image_id1, image_id2);
  return image_pair_stats_.at(pair_id);
}

struct Camera& Reconstruction::Camera(const camera_t camera_id) {
  return cameras_.at(camera_id);
}

class Image& Reconstruction::Image(const image_t image_id) {
  return images_.at(image_id);
}

struct Point3D& Reconstruction::Point3D(const point3D_t point3D_id) {
  return points3D_.at(point3D_id);
}

Reconstruction::ImagePairStat& Reconstruction::ImagePair(
    const image_pair_t pair_id) {
  return image_pair_stats_.at(pair_id);
}

Reconstruction::ImagePairStat& Reconstruction::ImagePair(
    const image_t image_id1, const image_t image_id2) {
  const auto pair_id = Database::ImagePairToPairId(image_id1, image_id2);
  return image_pair_stats_.at(pair_id);
}

const std::unordered_map<camera_t, Camera>& Reconstruction::Cameras() const {
  return cameras_;
}

const std::unordered_map<image_t, class Image>& Reconstruction::Images() const {
  return images_;
}

const std::vector<image_t>& Reconstruction::RegImageIds() const {
  return reg_image_ids_;
}

const std::unordered_map<point3D_t, Point3D>& Reconstruction::Points3D() const {
  return points3D_;
}

const std::unordered_map<image_pair_t, Reconstruction::ImagePairStat>&
Reconstruction::ImagePairs() const {
  return image_pair_stats_;
}

bool Reconstruction::ExistsCamera(const camera_t camera_id) const {
  return cameras_.find(camera_id) != cameras_.end();
}

bool Reconstruction::ExistsImage(const image_t image_id) const {
  return images_.find(image_id) != images_.end();
}

bool Reconstruction::ExistsPoint3D(const point3D_t point3D_id) const {
  return points3D_.find(point3D_id) != points3D_.end();
}

bool Reconstruction::ExistsImagePair(const image_pair_t pair_id) const {
  return image_pair_stats_.find(pair_id) != image_pair_stats_.end();
}

bool Reconstruction::IsImageRegistered(const image_t image_id) const {
  return Image(image_id).IsRegistered();
}

}  // namespace colmap
