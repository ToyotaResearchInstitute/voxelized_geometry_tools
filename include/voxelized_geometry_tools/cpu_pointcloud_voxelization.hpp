#pragma once

#include <memory>
#include <vector>

#include <common_robotics_utilities/openmp_helpers.hpp>
#include <voxelized_geometry_tools/collision_map.hpp>
#include <voxelized_geometry_tools/device_voxelization_interface.hpp>
#include <voxelized_geometry_tools/pointcloud_voxelization_interface.hpp>

namespace voxelized_geometry_tools
{
namespace pointcloud_voxelization
{
/// CPU-based (OpenMP) implementation of pointcloud voxelizer.
class CpuPointCloudVoxelizer : public PointCloudVoxelizationInterface {
public:
  CpuPointCloudVoxelizer(
      const std::map<std::string, int32_t>& options,
      const LoggingFunction& logging_fn = {});

private:
  VoxelizerRuntime DoVoxelizePointClouds(
      const CollisionMap& static_environment, const double step_size_multiplier,
      const PointCloudVoxelizationFilterOptions& filter_options,
      const std::vector<PointCloudWrapperSharedPtr>& pointclouds,
      CollisionMap& output_environment) const override;

  const common_robotics_utilities::openmp_helpers::DegreeOfParallelism&
  Parallelism() const { return parallelism_; }

  common_robotics_utilities::openmp_helpers::DegreeOfParallelism parallelism_;
};
}  // namespace pointcloud_voxelization
}  // namespace voxelized_geometry_tools
