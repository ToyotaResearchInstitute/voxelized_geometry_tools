#include <voxelized_geometry_tools/opencl_pointcloud_voxelization.hpp>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

#include <Eigen/Geometry>
#include <common_robotics_utilities/print.hpp>
#include <common_robotics_utilities/voxel_grid.hpp>
#include <voxelized_geometry_tools/collision_map.hpp>
#include <voxelized_geometry_tools/opencl_voxelization_helpers.h>
#include <voxelized_geometry_tools/pointcloud_voxelization_interface.hpp>

namespace voxelized_geometry_tools
{
namespace pointcloud_voxelization
{
OpenCLPointCloudVoxelizer::OpenCLPointCloudVoxelizer()
    : interface_(opencl_helpers::MakeHelperInterface())
{
  if (!interface_->IsAvailable())
  {
    throw std::runtime_error("OpenCLPointCloudVoxelizer not available");
  }
}

CollisionMap OpenCLPointCloudVoxelizer::VoxelizePointClouds(
    const CollisionMap& static_environment, const double step_size_multiplier,
    const PointCloudVoxelizationFilterOptions& filter_options,
    const std::vector<PointCloudWrapperPtr>& pointclouds) const
{
  if (!static_environment.IsInitialized())
  {
    throw std::invalid_argument("!static_environment.IsInitialized()");
  }
  if (step_size_multiplier > 1.0 || step_size_multiplier <= 0.0)
  {
    throw std::invalid_argument("step_size_multiplier is not in (0, 1]");
  }
  if (interface_->IsAvailable())
  {
    const std::chrono::time_point<std::chrono::steady_clock> start_time =
        std::chrono::steady_clock::now();
    // Allocate device-side memory for tracking grids
    const std::vector<int64_t> device_tracking_grid_offsets =
        interface_->PrepareTrackingGrids(
            static_environment.GetTotalCells(),
            static_cast<int32_t>(pointclouds.size()));
    if (device_tracking_grid_offsets.size() != pointclouds.size())
    {
      throw std::runtime_error("Failed to allocate device tracking grid");
    }
    // Prepare grid data
    const Eigen::Isometry3f inverse_grid_origin_transform_float =
        static_environment.GetInverseOriginTransform().cast<float>();
    const float inverse_step_size =
        static_cast<float>(1.0 /
            (static_environment.GetResolution() * step_size_multiplier));
    const float inverse_cell_size =
        static_cast<float>(static_environment.GetGridSizes().InvCellXSize());
    const int32_t num_x_cells =
        static_cast<int32_t>(static_environment.GetNumXCells());
    const int32_t num_y_cells =
        static_cast<int32_t>(static_environment.GetNumYCells());
    const int32_t num_z_cells =
        static_cast<int32_t>(static_environment.GetNumZCells());
    // Do raycasting of the pointclouds
    for (size_t idx = 0; idx < pointclouds.size(); idx++)
    {
      const PointCloudWrapperPtr& pointcloud = pointclouds.at(idx);
      const Eigen::Isometry3f pointcloud_origin_transform_float =
          pointcloud->GetPointCloudOriginTransform().cast<float>();
      // Copy pointcloud
      std::vector<float> raw_points(pointcloud->Size() * 3, 0.0);
      for (int64_t point = 0; point < pointcloud->Size(); point++)
      {
        pointcloud->CopyPointLocationIntoVectorFloat(
            point, raw_points, point * 3);
      }
      // Raycast
      interface_->RaycastPoints(
          raw_points, pointcloud_origin_transform_float,
          inverse_grid_origin_transform_float, inverse_step_size,
          inverse_cell_size, num_x_cells, num_y_cells, num_z_cells,
          device_tracking_grid_offsets.at(idx));
    }
    const std::chrono::time_point<std::chrono::steady_clock> raycasted_time =
        std::chrono::steady_clock::now();
    // Filter
    const float percent_seen_free =
        static_cast<float>(filter_options.PercentSeenFree());
    const int32_t outlier_points_threshold =
        filter_options.OutlierPointsThreshold();
    const int32_t num_cameras_seen_free =
        filter_options.NumCamerasSeenFree();
    const bool ok = interface_->PrepareFilterGrid(
            static_environment.GetTotalCells(),
            static_environment.GetImmutableRawData().data());
    if (!ok)
    {
      throw std::runtime_error("Failed to allocate device filter grid");
    }
    interface_->FilterTrackingGrids(
        static_environment.GetTotalCells(),
        static_cast<int32_t>(pointclouds.size()), percent_seen_free,
        outlier_points_threshold, num_cameras_seen_free);
    // Retrieve & return
    CollisionMap filtered_grid = static_environment;
    interface_->RetrieveFilteredGrid(
        static_environment.GetTotalCells(),
        filtered_grid.GetMutableRawData().data());
    // Cleanup device memory
    interface_->CleanupAllocatedMemory();
    const std::chrono::time_point<std::chrono::steady_clock> done_time =
        std::chrono::steady_clock::now();
    std::cout
        << "Raycasting time "
        << std::chrono::duration<double>(raycasted_time - start_time).count()
        << ", filtering time "
        << std::chrono::duration<double>(done_time - raycasted_time).count()
        << std::endl;
    return filtered_grid;
  }
  else
  {
    throw std::runtime_error("OpenCLPointCloudVoxelizer not available");
  }
}
}  // namespace pointcloud_voxelization
}  // namespace voxelized_geometry_tools

