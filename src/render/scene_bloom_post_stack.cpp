#include "vr/render/scene_bloom_post_stack.hpp"

#include <stdexcept>
#include <string>

namespace vr::render {

void SceneBloomPostStack::Initialize(
    const SceneRenderTargetSetCreateInfo& scene_create_info_,
    const RenderTargetBloomRendererCreateInfo& bloom_create_info_) noexcept {
    scene_targets.Initialize(scene_create_info_);
    bloom_renderer.Initialize(bloom_create_info_);
    initialized = true;
}

void SceneBloomPostStack::Shutdown(VulkanContext& context_) noexcept {
    if (!initialized) {
        return;
    }
    bloom_renderer.Shutdown(context_);
    scene_targets.Reset();
    initialized = false;
}

void SceneBloomPostStack::Record(const FrameRecordContext& record_context_) {
    EnsureInitialized("Record");
    bloom_renderer.Record(record_context_);
}

bool SceneBloomPostStack::IsInitialized() const noexcept {
    return initialized;
}

SceneRenderTargetSet& SceneBloomPostStack::Targets() noexcept {
    return scene_targets;
}

const SceneRenderTargetSet& SceneBloomPostStack::Targets() const noexcept {
    return scene_targets;
}

RenderTargetBloomRenderer& SceneBloomPostStack::Bloom() noexcept {
    return bloom_renderer;
}

const RenderTargetBloomRenderer& SceneBloomPostStack::Bloom() const noexcept {
    return bloom_renderer;
}

const RenderTargetBloomRendererStats& SceneBloomPostStack::Stats() const noexcept {
    return bloom_renderer.Stats();
}

void SceneBloomPostStack::EnsureInitialized(const char* operation_) const {
    if (initialized) {
        return;
    }
    throw std::runtime_error(std::string("SceneBloomPostStack::") + operation_ +
                             " called before Initialize");
}

} // namespace vr::render
