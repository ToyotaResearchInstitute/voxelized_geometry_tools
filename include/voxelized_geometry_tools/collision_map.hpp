#pragma once

#include <cmath>
#include <cstdint>
#include <functional>
#include <fstream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <Eigen/Geometry>
#include <common_robotics_utilities/maybe.hpp>
#include <common_robotics_utilities/serialization.hpp>
#include <common_robotics_utilities/utility.hpp>
#include <common_robotics_utilities/voxel_grid.hpp>
#include <voxelized_geometry_tools/signed_distance_field.hpp>
#include <voxelized_geometry_tools/signed_distance_field_generation.hpp>
#include <voxelized_geometry_tools/topology_computation.hpp>
#include <voxelized_geometry_tools/vgt_namespace.hpp>

namespace voxelized_geometry_tools
{
VGT_NAMESPACE_BEGIN
class CollisionCell
{
private:
  using DeserializedCollisionCell
      = common_robotics_utilities::serialization::Deserialized<CollisionCell>;

  common_robotics_utilities::utility
      ::CopyableMoveableAtomic<float, std::memory_order_relaxed>
          occupancy_{0.0f};
  common_robotics_utilities::utility
      ::CopyableMoveableAtomic<uint32_t, std::memory_order_relaxed>
          component_{0u};

public:
  static uint64_t Serialize(
      const CollisionCell& cell, std::vector<uint8_t>& buffer);

  static DeserializedCollisionCell Deserialize(
      const std::vector<uint8_t>& buffer, const uint64_t starting_offset);

  CollisionCell() : occupancy_(0.0f), component_(0u) {}

  CollisionCell(const float occupancy)
      : occupancy_(occupancy), component_(0u) {}

  CollisionCell(const float occupancy, const uint32_t component)
      : occupancy_(occupancy), component_(component) {}

  float Occupancy() const { return occupancy_.load(); }

  uint32_t Component() const { return component_.load(); }

  void SetOccupancy(const float occupancy) { occupancy_.store(occupancy); }

  void SetComponent(const uint32_t component) { component_.store(component); }
};

// Enforce that despite its members being std::atomics, CollisionCell has the
// same size as before.
static_assert(
    sizeof(CollisionCell) == (sizeof(float) * 2),
    "CollisionCell is larger than expected.");

using CollisionCellSerializer
    = common_robotics_utilities::serialization::Serializer<CollisionCell>;
using CollisionCellDeserializer
    = common_robotics_utilities::serialization::Deserializer<CollisionCell>;

class CollisionMap
    : public common_robotics_utilities::voxel_grid
        ::VoxelGridBase<CollisionCell, std::vector<CollisionCell>>
{
private:
  using DeserializedCollisionMap
      = common_robotics_utilities::serialization::Deserialized<CollisionMap>;

  uint32_t number_of_components_ = 0u;
  std::string frame_;
  common_robotics_utilities::utility::CopyableMoveableAtomic<bool>
      components_valid_{false};

  /// Implement the VoxelGridBase interface.

  /// We need to implement cloning.
  std::unique_ptr<common_robotics_utilities::voxel_grid
      ::VoxelGridBase<CollisionCell, std::vector<CollisionCell>>>
  DoClone() const override;

  /// We need to serialize the frame and locked flag.
  uint64_t DerivedSerializeSelf(
      std::vector<uint8_t>& buffer,
      const CollisionCellSerializer& value_serializer) const override;

  /// We need to deserialize the frame and locked flag.
  uint64_t DerivedDeserializeSelf(
      const std::vector<uint8_t>& buffer, const uint64_t starting_offset,
      const CollisionCellDeserializer& value_deserializer) override;

  /// Invalidate connected components on mutable access.
  bool OnMutableAccess(const int64_t x_index,
                       const int64_t y_index,
                       const int64_t z_index) override;

  /// Invalidate connected components on mutable raw access.
  bool OnMutableRawAccess() override;

public:
  static uint64_t Serialize(
      const CollisionMap& map, std::vector<uint8_t>& buffer);

  static DeserializedCollisionMap Deserialize(
      const std::vector<uint8_t>& buffer, const uint64_t starting_offset);

  static void SaveToFile(const CollisionMap& map,
                         const std::string& filepath,
                         const bool compress);

  static CollisionMap LoadFromFile(const std::string& filepath);

  CollisionMap(
      const Eigen::Isometry3d& origin_transform, const std::string& frame,
      const common_robotics_utilities::voxel_grid::GridSizes& sizes,
      const CollisionCell& default_value)
      : CollisionMap(
          origin_transform, frame, sizes, default_value, default_value) {}

  CollisionMap(
      const std::string& frame,
      const common_robotics_utilities::voxel_grid::GridSizes& sizes,
      const CollisionCell& default_value)
      : CollisionMap(frame, sizes, default_value, default_value) {}

  CollisionMap(
      const Eigen::Isometry3d& origin_transform, const std::string& frame,
      const common_robotics_utilities::voxel_grid::GridSizes& sizes,
      const CollisionCell& default_value, const CollisionCell& oob_value)
      : common_robotics_utilities::voxel_grid
          ::VoxelGridBase<CollisionCell, std::vector<CollisionCell>>(
              origin_transform, sizes, default_value, oob_value),
        frame_(frame)
  {
    if (!HasUniformCellSize())
    {
      throw std::invalid_argument(
          "Collision map cannot have non-uniform cell sizes");
    }
  }

  CollisionMap(
      const std::string& frame,
      const common_robotics_utilities::voxel_grid::GridSizes& sizes,
      const CollisionCell& default_value, const CollisionCell& oob_value)
      : common_robotics_utilities::voxel_grid
          ::VoxelGridBase<CollisionCell, std::vector<CollisionCell>>(
              sizes, default_value, oob_value),
        frame_(frame)
  {
    if (!HasUniformCellSize())
    {
      throw std::invalid_argument(
          "Collision map cannot have non-uniform cell sizes");
    }
  }

  CollisionMap()
      : common_robotics_utilities::voxel_grid
          ::VoxelGridBase<CollisionCell, std::vector<CollisionCell>>() {}

  bool AreComponentsValid() const { return components_valid_.load(); }

  /// Use this with great care if you know the components are still/now valid.
  void ForceComponentsToBeValid() { components_valid_.store(true); }

  /// Use this to invalidate the current components.
  void ForceComponentsToBeInvalid() { components_valid_.store(false); }

  double GetResolution() const { return GetCellSizes().x(); }

  const std::string& GetFrame() const { return frame_; }

  void SetFrame(const std::string& frame) { frame_ = frame; }

  uint32_t UpdateConnectedComponents();

  common_robotics_utilities::OwningMaybe<uint32_t>
  GetNumConnectedComponents() const
  {
    if (components_valid_.load())
    {
      return common_robotics_utilities::OwningMaybe<uint32_t>(
          number_of_components_);
    }
    else
    {
      return common_robotics_utilities::OwningMaybe<uint32_t>();
    }
  }

  common_robotics_utilities::OwningMaybe<bool> IsSurfaceIndex(
      const common_robotics_utilities::voxel_grid::GridIndex& index) const;

  common_robotics_utilities::OwningMaybe<bool> IsSurfaceIndex(
      const int64_t x_index, const int64_t y_index,
      const int64_t z_index) const;

  common_robotics_utilities::OwningMaybe<bool> IsConnectedComponentSurfaceIndex(
      const common_robotics_utilities::voxel_grid::GridIndex& index) const;

  common_robotics_utilities::OwningMaybe<bool> IsConnectedComponentSurfaceIndex(
      const int64_t x_index, const int64_t y_index,
      const int64_t z_index) const;

  common_robotics_utilities::OwningMaybe<bool> CheckIfCandidateCorner(
      const double x, const double y, const double z) const;

  common_robotics_utilities::OwningMaybe<bool> CheckIfCandidateCorner3d(
      const Eigen::Vector3d& location) const;

  common_robotics_utilities::OwningMaybe<bool> CheckIfCandidateCorner4d(
      const Eigen::Vector4d& location) const;

  common_robotics_utilities::OwningMaybe<bool> CheckIfCandidateCorner(
      const common_robotics_utilities::voxel_grid::GridIndex& index) const;

  common_robotics_utilities::OwningMaybe<bool> CheckIfCandidateCorner(
      const int64_t x_index, const int64_t y_index,
      const int64_t z_index) const;

  enum COMPONENT_TYPES : uint8_t { FILLED_COMPONENTS=0x01,
                                   EMPTY_COMPONENTS=0x02,
                                   UNKNOWN_COMPONENTS=0x04 };

  std::map<uint32_t, std::unordered_map<
      common_robotics_utilities::voxel_grid::GridIndex, uint8_t>>
  ExtractComponentSurfaces(
      const COMPONENT_TYPES component_types_to_extract) const;

  std::map<uint32_t, std::unordered_map<
      common_robotics_utilities::voxel_grid::GridIndex, uint8_t>>
  ExtractFilledComponentSurfaces() const;

  std::map<uint32_t, std::unordered_map<
      common_robotics_utilities::voxel_grid::GridIndex, uint8_t>>
  ExtractUnknownComponentSurfaces() const;

  std::map<uint32_t, std::unordered_map<
      common_robotics_utilities::voxel_grid::GridIndex, uint8_t>>
  ExtractEmptyComponentSurfaces() const;

  topology_computation::TopologicalInvariants ComputeComponentTopology(
      const COMPONENT_TYPES component_types_to_use,
      const common_robotics_utilities::utility::LoggingFunction&
          logging_fn = {});

  template<typename ScalarType>
  SignedDistanceField<ScalarType> ExtractSignedDistanceField(
      const SignedDistanceFieldGenerationParameters<ScalarType>& parameters)
      const
  {
    using common_robotics_utilities::voxel_grid::GridIndex;
    // Make the helper function
    const std::function<bool(const GridIndex&)>
        is_filled_fn = [&] (const GridIndex& index)
    {
      const auto query = GetIndexImmutable(index);
      if (query)
      {
        const float occupancy = query.Value().Occupancy();
        if (occupancy > 0.5)
        {
          // Mark as filled
          return true;
        }
        else if (parameters.UnknownIsFilled() && (occupancy == 0.5))
        {
          // Mark as filled
          return true;
        }
        // Mark as free
        return false;
      }
      else
      {
        throw std::runtime_error("index out of grid bounds");
      }
    };
    return
        signed_distance_field_generation::internal::ExtractSignedDistanceField
            <CollisionCell, std::vector<CollisionCell>, ScalarType>(
                *this, is_filled_fn, GetFrame(), parameters);
  }

  SignedDistanceField<double> ExtractSignedDistanceFieldDouble(
      const SignedDistanceFieldGenerationParameters<double>& parameters) const;

  SignedDistanceField<float> ExtractSignedDistanceFieldFloat(
      const SignedDistanceFieldGenerationParameters<float>& parameters) const;
};
VGT_NAMESPACE_END
}  // namespace voxelized_geometry_tools
