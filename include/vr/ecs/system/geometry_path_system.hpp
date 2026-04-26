#pragma once

#include "vr/ecs/system/geometry_system.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace vr::ecs {

enum class GeometryPathCommandType : std::uint8_t {
    move_to = 0U,
    line_to = 1U,
    quad_to = 2U,
    cubic_to = 3U,
    close = 4U,
};

struct GeometryPathCommandHeader final {
    GeometryPathCommandType type;
    std::uint8_t command_size_bytes;
    std::uint16_t reserved0;
};

struct GeometryPathMoveToCommand final {
    GeometryPathCommandHeader header;
    Float2 to;
};

struct GeometryPathLineToCommand final {
    GeometryPathCommandHeader header;
    Float2 to;
};

struct GeometryPathQuadToCommand final {
    GeometryPathCommandHeader header;
    Float2 control;
    Float2 to;
};

struct GeometryPathCubicToCommand final {
    GeometryPathCommandHeader header;
    Float2 control0;
    Float2 control1;
    Float2 to;
};

struct GeometryPathCloseCommand final {
    GeometryPathCommandHeader header;
};

struct GeometryPathCommandView final {
    GeometryPathCommandType type = GeometryPathCommandType::move_to;
    const std::uint8_t* bytes = nullptr;
    std::uint32_t size_bytes = 0U;
};

static_assert(std::is_standard_layout_v<GeometryPathCommandHeader> &&
              std::is_trivial_v<GeometryPathCommandHeader>);
static_assert(std::is_standard_layout_v<GeometryPathMoveToCommand> &&
              std::is_trivial_v<GeometryPathMoveToCommand>);
static_assert(std::is_standard_layout_v<GeometryPathLineToCommand> &&
              std::is_trivial_v<GeometryPathLineToCommand>);
static_assert(std::is_standard_layout_v<GeometryPathQuadToCommand> &&
              std::is_trivial_v<GeometryPathQuadToCommand>);
static_assert(std::is_standard_layout_v<GeometryPathCubicToCommand> &&
              std::is_trivial_v<GeometryPathCubicToCommand>);
static_assert(std::is_standard_layout_v<GeometryPathCloseCommand> &&
              std::is_trivial_v<GeometryPathCloseCommand>);

class GeometryPathSystem final {
public:
    using Geometry2D = Geometry<Dim2>;

    static void Initialize(Geometry2D& component_) noexcept {
        GeometrySystem<Dim2>::Initialize(component_);
    }

    static void ClearPath(Geometry2D& component_) noexcept {
        if (!EnsurePathStorageInitialized(component_)) {
            return;
        }
        if (component_.path.size_bytes == 0U &&
            component_.runtime.path_command_count == 0U) {
            return;
        }

        component_.path.size_bytes = 0U;
        ++component_.path.revision;
        component_.runtime.path_command_count = 0U;
        component_.runtime.path_data_hash = 0U;
        ++component_.runtime.tessellation_revision;
        component_.runtime.bounds_min = Float2{.x = 0.0F, .y = 0.0F};
        component_.runtime.bounds_max = Float2{.x = 0.0F, .y = 0.0F};
        GeometrySystem<Dim2>::MarkDirty(component_,
                                        geometry_dirty_data_flag |
                                            geometry_dirty_runtime_flag |
                                            geometry_dirty_bounds_flag);
    }

    [[nodiscard]] static bool SetPathData(Geometry2D& component_,
                                          const void* bytes_,
                                          std::uint32_t size_bytes_) noexcept {
        if (bytes_ == nullptr && size_bytes_ != 0U) {
            return false;
        }
        if (!EnsurePathStorageInitialized(component_)) {
            return false;
        }
        if (size_bytes_ > component_.path.capacity_bytes) {
            return false;
        }

        const bool same_size = component_.path.size_bytes == size_bytes_;
        if (same_size && size_bytes_ > 0U) {
            if (std::memcmp(component_.path.bytes, bytes_, size_bytes_) == 0) {
                return true;
            }
        } else if (same_size && size_bytes_ == 0U) {
            return true;
        }

        if (size_bytes_ > 0U) {
            std::memcpy(component_.path.bytes, bytes_, size_bytes_);
        }
        component_.path.size_bytes = size_bytes_;
        ++component_.path.revision;
        UpdateRuntimeAfterPathMutation(component_);
        return true;
    }

    [[nodiscard]] static bool AppendMoveTo(Geometry2D& component_,
                                           float x_,
                                           float y_) noexcept {
        GeometryPathMoveToCommand command{};
        command.header.type = GeometryPathCommandType::move_to;
        command.header.command_size_bytes = static_cast<std::uint8_t>(sizeof(command));
        command.header.reserved0 = 0U;
        command.to = Float2{.x = x_, .y = y_};
        return AppendCommand(component_, command);
    }

    [[nodiscard]] static bool AppendLineTo(Geometry2D& component_,
                                           float x_,
                                           float y_) noexcept {
        GeometryPathLineToCommand command{};
        command.header.type = GeometryPathCommandType::line_to;
        command.header.command_size_bytes = static_cast<std::uint8_t>(sizeof(command));
        command.header.reserved0 = 0U;
        command.to = Float2{.x = x_, .y = y_};
        return AppendCommand(component_, command);
    }

    [[nodiscard]] static bool AppendQuadTo(Geometry2D& component_,
                                           float control_x_,
                                           float control_y_,
                                           float x_,
                                           float y_) noexcept {
        GeometryPathQuadToCommand command{};
        command.header.type = GeometryPathCommandType::quad_to;
        command.header.command_size_bytes = static_cast<std::uint8_t>(sizeof(command));
        command.header.reserved0 = 0U;
        command.control = Float2{.x = control_x_, .y = control_y_};
        command.to = Float2{.x = x_, .y = y_};
        return AppendCommand(component_, command);
    }

    [[nodiscard]] static bool AppendCubicTo(Geometry2D& component_,
                                            float control0_x_,
                                            float control0_y_,
                                            float control1_x_,
                                            float control1_y_,
                                            float x_,
                                            float y_) noexcept {
        GeometryPathCubicToCommand command{};
        command.header.type = GeometryPathCommandType::cubic_to;
        command.header.command_size_bytes = static_cast<std::uint8_t>(sizeof(command));
        command.header.reserved0 = 0U;
        command.control0 = Float2{.x = control0_x_, .y = control0_y_};
        command.control1 = Float2{.x = control1_x_, .y = control1_y_};
        command.to = Float2{.x = x_, .y = y_};
        return AppendCommand(component_, command);
    }

    [[nodiscard]] static bool AppendClose(Geometry2D& component_) noexcept {
        GeometryPathCloseCommand command{};
        command.header.type = GeometryPathCommandType::close;
        command.header.command_size_bytes = static_cast<std::uint8_t>(sizeof(command));
        command.header.reserved0 = 0U;
        return AppendCommand(component_, command);
    }

    [[nodiscard]] static const std::uint8_t* Data(const Geometry2D& component_) noexcept {
        return component_.path.bytes;
    }

    [[nodiscard]] static std::uint32_t DataSizeBytes(const Geometry2D& component_) noexcept {
        return component_.path.size_bytes;
    }

    [[nodiscard]] static std::uint32_t CommandCount(const Geometry2D& component_) noexcept {
        return component_.runtime.path_command_count;
    }

    template<typename VisitorT>
    requires std::invocable<VisitorT, const GeometryPathCommandView&>
    static void ForEachCommandRaw(const Geometry2D& component_,
                                  VisitorT&& visitor_) noexcept {
        const std::uint8_t* data = component_.path.bytes;
        const std::uint32_t size_bytes = component_.path.size_bytes;

        std::uint32_t offset = 0U;
        while (offset + sizeof(GeometryPathCommandHeader) <= size_bytes) {
            const auto* header = reinterpret_cast<const GeometryPathCommandHeader*>(data + offset);
            const std::uint32_t command_size_bytes = header->command_size_bytes;
            if (command_size_bytes < sizeof(GeometryPathCommandHeader) ||
                offset > size_bytes - command_size_bytes) {
                break;
            }

            GeometryPathCommandView view{};
            view.type = header->type;
            view.bytes = data + offset;
            view.size_bytes = command_size_bytes;
            visitor_(view);
            offset += command_size_bytes;
        }
    }

private:
    [[nodiscard]] static bool EnsurePathStorageInitialized(Geometry2D& component_) noexcept {
        if (component_.path.capacity_bytes == GeometryPathInlineData::inline_capacity_bytes) {
            return true;
        }
        if (component_.path.capacity_bytes != 0U) {
            return false;
        }

        component_.path.capacity_bytes = GeometryPathInlineData::inline_capacity_bytes;
        component_.path.size_bytes = std::min(component_.path.size_bytes,
                                              GeometryPathInlineData::inline_capacity_bytes);
        component_.path.reserved = 0U;
        return true;
    }

    template<typename CommandT>
    [[nodiscard]] static bool AppendCommand(Geometry2D& component_,
                                            const CommandT& command_) noexcept {
        static_assert(std::is_standard_layout_v<CommandT> && std::is_trivial_v<CommandT>);

        if (!EnsurePathStorageInitialized(component_)) {
            return false;
        }

        constexpr std::uint32_t command_size_bytes = static_cast<std::uint32_t>(sizeof(CommandT));
        if (command_size_bytes > std::numeric_limits<std::uint8_t>::max()) {
            return false;
        }

        if (component_.path.size_bytes > component_.path.capacity_bytes - command_size_bytes) {
            return false;
        }

        std::memcpy(component_.path.bytes + component_.path.size_bytes, &command_, command_size_bytes);
        component_.path.size_bytes += command_size_bytes;
        ++component_.path.revision;

        UpdateRuntimeAfterPathMutation(component_);
        return true;
    }

    static void UpdateRuntimeAfterPathMutation(Geometry2D& component_) noexcept {
        component_.runtime.path_command_count = CountCommands(component_);
        component_.runtime.path_data_hash = ComputePathHash(component_);
        ++component_.runtime.tessellation_revision;
        UpdatePathBounds(component_);
        GeometrySystem<Dim2>::MarkDirty(component_,
                                        geometry_dirty_data_flag |
                                            geometry_dirty_runtime_flag |
                                            geometry_dirty_bounds_flag);
    }

    [[nodiscard]] static std::uint32_t CountCommands(const Geometry2D& component_) noexcept {
        std::uint32_t command_count = 0U;
        ForEachCommandRaw(component_,
                          [&](const GeometryPathCommandView&) noexcept {
                              ++command_count;
                          });
        return command_count;
    }

    [[nodiscard]] static std::uint32_t ComputePathHash(const Geometry2D& component_) noexcept {
        constexpr std::uint32_t fnv_offset = 2166136261U;
        constexpr std::uint32_t fnv_prime = 16777619U;

        std::uint32_t hash = fnv_offset;
        const std::uint8_t* bytes = component_.path.bytes;
        for (std::uint32_t i = 0U; i < component_.path.size_bytes; ++i) {
            hash ^= static_cast<std::uint32_t>(bytes[i]);
            hash *= fnv_prime;
        }
        return hash;
    }

    static void UpdatePathBounds(Geometry2D& component_) noexcept {
        Float2 bounds_min{.x = 0.0F, .y = 0.0F};
        Float2 bounds_max{.x = 0.0F, .y = 0.0F};
        bool has_point = false;

        ForEachCommandRaw(component_,
                          [&](const GeometryPathCommandView& view_) noexcept {
                              switch (view_.type) {
                              case GeometryPathCommandType::move_to:
                                  if (view_.size_bytes == sizeof(GeometryPathMoveToCommand)) {
                                      const auto* command =
                                          reinterpret_cast<const GeometryPathMoveToCommand*>(view_.bytes);
                                      ExpandBounds(command->to, has_point, bounds_min, bounds_max);
                                  }
                                  break;
                              case GeometryPathCommandType::line_to:
                                  if (view_.size_bytes == sizeof(GeometryPathLineToCommand)) {
                                      const auto* command =
                                          reinterpret_cast<const GeometryPathLineToCommand*>(view_.bytes);
                                      ExpandBounds(command->to, has_point, bounds_min, bounds_max);
                                  }
                                  break;
                              case GeometryPathCommandType::quad_to:
                                  if (view_.size_bytes == sizeof(GeometryPathQuadToCommand)) {
                                      const auto* command =
                                          reinterpret_cast<const GeometryPathQuadToCommand*>(view_.bytes);
                                      ExpandBounds(command->control, has_point, bounds_min, bounds_max);
                                      ExpandBounds(command->to, has_point, bounds_min, bounds_max);
                                  }
                                  break;
                              case GeometryPathCommandType::cubic_to:
                                  if (view_.size_bytes == sizeof(GeometryPathCubicToCommand)) {
                                      const auto* command =
                                          reinterpret_cast<const GeometryPathCubicToCommand*>(view_.bytes);
                                      ExpandBounds(command->control0, has_point, bounds_min, bounds_max);
                                      ExpandBounds(command->control1, has_point, bounds_min, bounds_max);
                                      ExpandBounds(command->to, has_point, bounds_min, bounds_max);
                                  }
                                  break;
                              case GeometryPathCommandType::close:
                                  break;
                              default:
                                  break;
                              }
                          });

        if (!has_point) {
            bounds_min = Float2{.x = 0.0F, .y = 0.0F};
            bounds_max = Float2{.x = 0.0F, .y = 0.0F};
        }

        component_.runtime.bounds_min = bounds_min;
        component_.runtime.bounds_max = bounds_max;
    }

    static void ExpandBounds(const Float2& point_,
                             bool& has_point_,
                             Float2& bounds_min_,
                             Float2& bounds_max_) noexcept {
        if (!has_point_) {
            bounds_min_ = point_;
            bounds_max_ = point_;
            has_point_ = true;
            return;
        }

        bounds_min_.x = std::min(bounds_min_.x, point_.x);
        bounds_min_.y = std::min(bounds_min_.y, point_.y);
        bounds_max_.x = std::max(bounds_max_.x, point_.x);
        bounds_max_.y = std::max(bounds_max_.y, point_.y);
    }
};

} // namespace vr::ecs
