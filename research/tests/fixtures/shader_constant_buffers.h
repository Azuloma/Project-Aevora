#include <algorithm>
#include <array>
#include <cassert>
#include <climits>
#include <cstdint>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
using u32=std::uint32_t; using Id=u32;
// The production file takes static_vector from boost; capacity is irrelevant here.
namespace boost::container { template<class T,std::size_t N> using static_vector=std::vector<T>; }
namespace spv {
enum class Decoration { ArrayStride, Block, Offset, Binding, DescriptorSet };
enum class StorageClass { Uniform };
}
namespace Common { template<class T> T DivCeil(T a,T b) { return (a+b-1)/b; } }
namespace fmt { inline std::string format(std::string_view,u32) { return {}; } }
struct UniformDefinitions { Id field{}; };
struct ConstantBufferDescriptor { u32 index,count; };
struct Info {
 static constexpr size_t MAX_CBUFS{18};
 std::vector<ConstantBufferDescriptor> constant_buffer_descriptors;
 std::array<u32,18> constant_buffer_used_sizes{};
 bool uses_global_memory{};
};
struct EmitContext {
 struct { u32 supported_spirv=0x10400; } profile;
 int stage=0;
 UniformDefinitions uniform_types;
 std::array<UniformDefinitions,18> cbufs{};
 std::vector<Id> interfaces;
 std::vector<u32> lengths;
 Id next=10;
 Id Const(u32 value) { return value; }
 Id TypeArray(Id,u32 count) { lengths.push_back(count); return next++; }
 Id TypeStruct(Id) { return next++; }
 Id TypePointer(spv::StorageClass,Id) { return next++; }
 Id AddGlobalVariable(Id,spv::StorageClass) { return next++; }
 template<class... T> void Decorate(T...) {}
 template<class... T> void MemberName(T...) {}
 template<class... T> void MemberDecorate(T...) {}
 template<class... T> void Name(T...) {}
};
template<class... T> void Name(EmitContext&,T...) {}
