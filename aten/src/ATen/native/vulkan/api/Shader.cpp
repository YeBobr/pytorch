#include <ATen/native/vulkan/api/Shader.h>

#ifdef USE_VULKAN_SHADERC_RUNTIME
#include <shaderc/shaderc.hpp>
#endif /* USE_VULKAN_SHADERC_RUNTIME */

namespace at {
namespace native {
namespace vulkan {
namespace api {

Shader::Descriptor::Descriptor(const char* const glsl)
 : type(Type::Source) {
  shader.source = {
    glsl,
    0u,
  };
}

Shader::Descriptor::Descriptor(const uint32_t* const code, const uint32_t size)
 : type(Type::Binary) {
  shader.binary = {
    code,
    size,
  };
}

#ifdef USE_VULKAN_SHADERC_RUNTIME

struct Shader::Factory::Compiler final {
  shaderc::Compiler context;
  shaderc::CompileOptions options;

  Compiler() {
    options.SetSourceLanguage(shaderc_source_language_glsl);
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_0);
    options.SetWarningsAsErrors();
  #ifdef DEBUG
    options.SetGenerateDebugInfo();
    options.SetOptimizationLevel(shaderc_optimization_level_zero);
  #else
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
  #endif /* DEBUG */
  }

  std::vector<const uint32_t> compile(const char* const source) const {
    const shaderc::SpvCompilationResult result = context.CompileGlslToSpv(
        source,
        ::strlen(source),
        shaderc_compute_shader,
        "vulkan_shader.comp",
        options);

    const shaderc_compilation_status status = result.GetCompilationStatus();
    TORCH_INTERNAL_ASSERT(
        shaderc_compilation_status_success == status,
        "Shader compilation error: ",
        result.GetErrorMessage());

    return std::vector<const uint32_t>(result.cbegin(), result.cend());
  }
};

#else

struct Shader::Factory::Compiler final {
  std::vector<const uint32_t> compile(const char* const /* source */) const {
    return std::vector<const uint32_t>{};
  }
};

#endif /* USE_VULKAN_SHADERC_RUNTIME */

Shader::Factory::Factory(const VkDevice device)
 : device_(device),
   compiler_(new Compiler) {
}

// std::unique_ptr requires its template parameter to be fully defined.
// For that reason pimpl through unique_ptr requires the definition of
// the [default] constructor and move assignment operator to appear after
// impl is fully defined.

Shader::Factory::Factory(Factory&&) = default;
Shader::Factory& Shader::Factory::Factory::operator=(Factory&&) = default;
Shader::Factory::~Factory() = default;

typename Shader::Factory::Handle Shader::Factory::operator()(
    const Descriptor& descriptor) const {
  std::vector<const uint32_t> binary;

  const uint32_t* code = nullptr;
  uint32_t size = 0u;

  if (Descriptor::Type::Source == descriptor.type) {
    binary = compiler_->compile(descriptor.shader.source.glsl);
    code = binary.data();
    size = sizeof(uint32_t) * binary.size();
  }
  else if (Descriptor::Type::Binary == descriptor.type) {
    code = descriptor.shader.binary.spirv;
    size = descriptor.shader.binary.size;
  }
  else {
    TORCH_INTERNAL_ASSERT(false, "Invalid descriptor type!");
  }

  const VkShaderModuleCreateInfo shader_module_create_info{
    VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
    nullptr,
    0u,
    size,
    code,
  };

  VkShaderModule shader_module{};
  VK_CHECK(vkCreateShaderModule(
      device_, &shader_module_create_info, nullptr, &shader_module));

  return Handle{
    shader_module,
    Deleter(device_),
  };
}

Shader::Layout::Factory::Factory(const VkDevice device)
  : device_(device) {
}

Shader::Layout::Factory::Handle Shader::Layout::Factory::operator()(
    const Descriptor& descriptor) const {
  static_assert(
      sizeof(Descriptor::Slot) == sizeof(VkDescriptorSetLayoutBinding),
      "This implementation assumes a Descriptor::Slot's memory layout is the same"
      "as VkDescriptorSetLayoutBinding.  A copy needs to be performed otherwise.");

  const VkDescriptorSetLayoutCreateInfo descriptor_set_layout_create_info{
    VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
    nullptr,
    0u,
    descriptor.slots.size(),
    reinterpret_cast<const VkDescriptorSetLayoutBinding*>(descriptor.slots.data()),
  };

  VkDescriptorSetLayout descriptor_set_layout{};
  VK_CHECK(vkCreateDescriptorSetLayout(
      device_, &descriptor_set_layout_create_info, nullptr, &descriptor_set_layout));

  return Handle{
    descriptor_set_layout,
    Deleter(device_),
  };
}

} // namespace api
} // namespace vulkan
} // namespace native
} // namespace at
