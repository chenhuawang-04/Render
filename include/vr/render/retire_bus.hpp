#pragma once

#include "Center/Memory/Container/Vector/McVector.hpp"

#include <cstdint>
#include <utility>

namespace vr::render {

template<typename T>
using RetireBusMcVector = Center::Memory::mc_vector<T, Center::Memory::Tags::Container>;

template<typename PayloadT>
class RetireBus final {
public:
    struct Node final {
        PayloadT payload{};
        std::uint64_t retire_value = 0U;
    };

    RetireBus() = default;
    ~RetireBus() = default;

    RetireBus(const RetireBus&) = delete;
    RetireBus& operator=(const RetireBus&) = delete;

    RetireBus(RetireBus&&) = delete;
    RetireBus& operator=(RetireBus&&) = delete;

    void Reserve(std::uint32_t reserve_count_) {
        nodes.reserve(reserve_count_);
    }

    void Retire(PayloadT&& payload_,
                std::uint64_t retire_value_) {
        Node node{};
        node.payload = std::move(payload_);
        node.retire_value = retire_value_;
        nodes.push_back(std::move(node));
    }

    template<typename DestroyFn>
    std::uint32_t Collect(std::uint64_t completed_submit_value_,
                          DestroyFn&& destroy_fn_) {
        if (nodes.empty()) {
            return 0U;
        }

        std::uint32_t destroyed_count = 0U;
        std::uint32_t write_index = 0U;
        for (std::uint32_t read_index = 0U; read_index < nodes.size(); ++read_index) {
            Node node = std::move(nodes[read_index]);
            if (node.retire_value <= completed_submit_value_) {
                destroy_fn_(node.payload);
                ++destroyed_count;
                continue;
            }
            if (write_index != read_index) {
                nodes[write_index] = std::move(node);
            }
            ++write_index;
        }
        nodes.resize(write_index);
        return destroyed_count;
    }

    template<typename DestroyFn>
    std::uint32_t Flush(DestroyFn&& destroy_fn_) {
        std::uint32_t destroyed_count = 0U;
        for (auto& node : nodes) {
            destroy_fn_(node.payload);
            ++destroyed_count;
        }
        nodes.clear();
        return destroyed_count;
    }

    void Clear() {
        nodes.clear();
    }

    [[nodiscard]] bool Empty() const noexcept {
        return nodes.empty();
    }

    [[nodiscard]] std::uint32_t PendingCount() const noexcept {
        return static_cast<std::uint32_t>(nodes.size());
    }

private:
    RetireBusMcVector<Node> nodes{};
};

} // namespace vr::render

