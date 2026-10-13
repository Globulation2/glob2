// SPDX-License-Identifier: GPL-3.0-or-later
#include "GradientAccelerators.h"
#include "OpenCLGradient.h"
#include "GradientComputeShaders.h"
#include "GradientGpuValidation.h"
#include "GradientDiagnostics.h"
#include <SDL3/SDL_loadso.h>
#include <array>
#include <mutex>
#include <exception>
#include <stdexcept>
#include <vector>
#include <type_traits>
#if defined(__x86_64__) || defined(_M_X64)
#include <emmintrin.h>
#endif
#if !defined(__EMSCRIPTEN__)
#define VK_NO_PROTOTYPES
#include "../../third_party/vulkan/vulkan_core.h"
#endif
namespace gradient_kernel
{
#if !defined(__EMSCRIPTEN__)
namespace
{
struct VulkanError : std::runtime_error
{
	VkResult result;
	explicit VulkanError(VkResult result)
		: std::runtime_error("Vulkan error " + std::to_string(result)), result(result)
	{
	}
};
struct MemoryDecline : std::runtime_error
{
	MemoryDecline() : std::runtime_error("Vulkan shared memory budget exceeded") {}
};
void check(VkResult result)
{
	if (result != VK_SUCCESS)
		throw VulkanError(result);
}
void orderMappedWrites() noexcept
{
	// Coherent Vulkan allocations may be mapped write-combined on discrete
	// GPUs. Finish streaming/write-combined stores before queue submission.
#if defined(__x86_64__) || defined(_M_X64)
	_mm_sfence();
#else
	std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
}
#define VULKAN_FUNCTIONS(X)                                                                        \
	X(CreateInstance)                                                                              \
	X(DestroyInstance)                                                                             \
	X(EnumerateInstanceExtensionProperties)                                                        \
	X(EnumeratePhysicalDevices) X(GetPhysicalDeviceProperties)                                     \
		X(GetPhysicalDeviceQueueFamilyProperties) X(GetPhysicalDeviceMemoryProperties) X(          \
			EnumerateDeviceExtensionProperties) X(CreateDevice) X(DestroyDevice) X(GetDeviceQueue) \
			X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(   \
				FreeMemory) X(BindBufferMemory) X(MapMemory) X(UnmapMemory)                        \
				X(CreateDescriptorSetLayout) X(DestroyDescriptorSetLayout) X(CreatePipelineLayout) \
					X(DestroyPipelineLayout) X(CreateShaderModule) X(DestroyShaderModule) X(       \
						CreateComputePipelines) X(DestroyPipeline) X(CreateDescriptorPool)         \
						X(DestroyDescriptorPool) X(AllocateDescriptorSets) X(UpdateDescriptorSets) \
							X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers)   \
								X(ResetCommandBuffer) X(BeginCommandBuffer) X(EndCommandBuffer)    \
									X(CmdBindPipeline) X(CmdBindDescriptorSets) X(CmdDispatch)     \
										X(CmdPipelineBarrier) X(CmdCopyBuffer) X(CmdFillBuffer)    \
											X(CreateFence) X(DestroyFence) X(ResetFences)          \
												X(QueueSubmit) X(WaitForFences) X(GetFenceStatus)
enum BufferSlot : unsigned
{
	InputA,
	InputB,
	Costs,
	Changed,
	Params,
	ActiveA,
	ActiveB,
	Transfer,
	BufferCount
};
constexpr unsigned otherActive(unsigned active)
{
	return active == ActiveA ? ActiveB : ActiveA;
}
struct Runtime;
struct Buffer
{
	Runtime *runtime = nullptr;
	VkBuffer buffer = VK_NULL_HANDLE;
	VkDeviceMemory memory = VK_NULL_HANDLE;
	void *mapped = nullptr;
	std::size_t charged = 0, hostCharged = 0;
	Buffer() = default;
	Buffer(const Buffer &) = delete;
	Buffer &operator=(const Buffer &) = delete;
	Buffer(Buffer &&) = delete;
	Buffer &operator=(Buffer &&) = delete;
	~Buffer();
};
struct Work
{
	Runtime *runtime;
	std::array<Buffer, BufferCount> buffers;
	VkDescriptorPool descriptors = VK_NULL_HANDLE;
	VkCommandPool commands = VK_NULL_HANDLE;
	VkCommandBuffer command = VK_NULL_HANDLE;
	VkFence fence = VK_NULL_HANDLE;
	bool submitted = false, reusable = false;
	std::size_t cellsCapacity = 0, tilesCapacity = 0, hostCharged = 0;
	std::array<VkDescriptorSet, 2> sets{};
	explicit Work(Runtime *r) : runtime(r)
	{
		constexpr auto bytes = sizeof(Work) + 128;
		if (!reserveOpenCLHostBytes(bytes))
			throw MemoryDecline();
		hostCharged = bytes;
	}
	Work(const Work &) = delete;
	Work &operator=(const Work &) = delete;
	Work(Work &&) = delete;
	Work &operator=(Work &&) = delete;
	~Work();
};
static_assert(!std::is_copy_constructible_v<Buffer> && !std::is_move_constructible_v<Buffer>);
static_assert(!std::is_copy_constructible_v<Work> && !std::is_move_constructible_v<Work>);
struct Runtime
{
	SDL_SharedObject *library = nullptr;
	PFN_vkGetInstanceProcAddr getProc = nullptr;
#define DECLARE(name) PFN_vk##name name = nullptr;
	VULKAN_FUNCTIONS(DECLARE)
#undef DECLARE
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physical = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkQueue queue = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties properties{};
	VkPhysicalDeviceMemoryProperties memory{};
	VkDescriptorSetLayout descriptors = VK_NULL_HANDLE;
	VkPipelineLayout layout = VK_NULL_HANDLE;
	std::array<VkPipeline, 2> pipelines{};
	unsigned queueFamily = 0;
	std::vector<std::unique_ptr<Work>> quarantine;
	// One serial adapter work set. Only successful fence retirement admits reuse.
	std::unique_ptr<Work> idle;
	std::mutex mutex, statusMutex;
	AcceleratorStatus status{Backend::Vulkan}, published{Backend::Vulkan};
	void publish()
	{
		if (status.availability == Availability::Failed)
			readyPlans.fetch_and(
				~((1u << unsigned(Plan::VulkanFrozen8)) | (1u << unsigned(Plan::VulkanJacobi))));
		std::lock_guard lock(statusMutex);
		published = status;
	}
	bool attempted = false;
	void loadFunctions()
	{
#define LOAD(name)                                                                                 \
	if (!name)                                                                                     \
		name = reinterpret_cast<PFN_vk##name>(getProc(instance, "vk" #name));
		VULKAN_FUNCTIONS(LOAD)
#undef LOAD
	}
	void allocate(Buffer &out, std::size_t bytes, bool hostAccess)
	{
		GradientPhaseScope phase(GradientPhase::Allocation);
		out.runtime = this;
		VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		info.size = bytes;
		info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
					 VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		check(CreateBuffer(device, &info, nullptr, &out.buffer));
		countGradientWork(GradientCounter::BufferAllocations);
		VkMemoryRequirements requirements{};
		GetBufferMemoryRequirements(device, out.buffer, &requirements);
		if (requirements.size > SIZE_MAX ||
			!reserveGradientDeviceBytes(std::size_t(requirements.size)))
			throw MemoryDecline();
		out.charged = std::size_t(requirements.size);
		unsigned type = memory.memoryTypeCount;
		int best = -1;
		for (unsigned i = 0; i < memory.memoryTypeCount; ++i)
		{
			const auto flags = memory.memoryTypes[i].propertyFlags;
			const auto required =
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			if (!(requirements.memoryTypeBits & (1u << i)) ||
				(hostAccess && (flags & required) != required))
				continue;
			// Compute storage stays on the device. CPU transfer/control
			// storage prefers cached coherent memory and is separately charged.
			const int score = hostAccess ? bool(flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
										 : bool(flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
			if (score > best)
			{
				type = i;
				best = score;
			}
		}
		if (type == memory.memoryTypeCount)
			throw std::runtime_error("No compatible Vulkan storage memory");
		if (hostAccess)
		{
			if (!reserveOpenCLHostBytes(std::size_t(requirements.size)))
				throw MemoryDecline();
			out.hostCharged = std::size_t(requirements.size);
		}
		VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
		allocation.allocationSize = requirements.size;
		allocation.memoryTypeIndex = type;
		check(AllocateMemory(device, &allocation, nullptr, &out.memory));
		check(BindBufferMemory(device, out.buffer, out.memory, 0));
		if (hostAccess)
			check(MapMemory(device, out.memory, 0, bytes, 0, &out.mapped));
	}
	bool initialize()
	{
		if (attempted)
			return status.availability == Availability::Ready;
		attempted = true;
		status.availability = Availability::Initializing;
		publish();
		quarantine.reserve(1);
		try
		{
#ifdef _WIN32
			library = SDL_LoadObject("vulkan-1.dll");
#elif defined(__APPLE__)
			// Packaged beside the application, with loader-relative rpaths.
			library = SDL_LoadObject("@rpath/MoltenVK.framework/MoltenVK");
			if (!library)
				library =
					SDL_LoadObject("@executable_path/../Frameworks/MoltenVK.framework/MoltenVK");
			if (!library)
				library = SDL_LoadObject("@executable_path/Frameworks/MoltenVK.framework/MoltenVK");
			if (!library)
				library = SDL_LoadObject("@rpath/libMoltenVK.dylib");
			if (!library)
				library = SDL_LoadObject("libMoltenVK.dylib");
#else
			library = SDL_LoadObject("libvulkan.so.1");
#if defined(__ANDROID__)
			if (!library)
				library = SDL_LoadObject("libvulkan.so");
#endif
#endif
			if (!library)
				throw std::runtime_error("Vulkan driver library unavailable");
			getProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
				SDL_LoadFunction(library, "vkGetInstanceProcAddr"));
			if (!getProc)
				throw std::runtime_error("Missing vkGetInstanceProcAddr");
			loadFunctions();
			if (!CreateInstance || !EnumerateInstanceExtensionProperties)
				throw std::runtime_error("Missing Vulkan entrypoints");
			std::uint32_t count = 0;
			check(EnumerateInstanceExtensionProperties(nullptr, &count, nullptr));
			std::vector<VkExtensionProperties> extensions(count);
			check(EnumerateInstanceExtensionProperties(nullptr, &count, extensions.data()));
			const bool portability = std::any_of(
				extensions.begin(), extensions.end(), [](const auto &e)
				{ return std::strcmp(e.extensionName, "VK_KHR_portability_enumeration") == 0; });
			const char *extension = "VK_KHR_portability_enumeration";
			VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
			application.pApplicationName = "Globulation 2 gradient compute";
			application.apiVersion = VK_API_VERSION_1_0;
			VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
			create.pApplicationInfo = &application;
			if (portability)
			{
				create.flags = 1;
				create.enabledExtensionCount = 1;
				create.ppEnabledExtensionNames = &extension;
			}
			check(CreateInstance(&create, nullptr, &instance));
			loadFunctions();
			check(EnumeratePhysicalDevices(instance, &count, nullptr));
			std::vector<VkPhysicalDevice> devices(count);
			check(EnumeratePhysicalDevices(instance, &count, devices.data()));
			for (auto candidate : devices)
			{
				std::uint32_t queues = 0;
				GetPhysicalDeviceQueueFamilyProperties(candidate, &queues, nullptr);
				std::vector<VkQueueFamilyProperties> families(queues);
				GetPhysicalDeviceQueueFamilyProperties(candidate, &queues, families.data());
				VkPhysicalDeviceProperties p{};
				GetPhysicalDeviceProperties(candidate, &p);
				if (p.limits.maxComputeWorkGroupInvocations < 128 ||
					p.limits.maxComputeWorkGroupSize[0] < 128 ||
					p.limits.maxComputeSharedMemorySize < 2600 ||
					p.limits.maxPerStageDescriptorStorageBuffers < 7 ||
					p.limits.maxDescriptorSetStorageBuffers < 7)
					continue;
				for (unsigned i = 0; i < queues; ++i)
					if (families[i].queueCount && (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT))
					{
						physical = candidate;
						queueFamily = i;
						properties = p;
						break;
					}
				if (physical)
					break;
			}
			if (!physical)
				throw std::runtime_error("No Vulkan device supports gradient kernels");
			GetPhysicalDeviceMemoryProperties(physical, &memory);
			check(EnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr));
			extensions.resize(count);
			check(EnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data()));
			const bool subset = std::any_of(
				extensions.begin(), extensions.end(), [](const auto &e)
				{ return std::strcmp(e.extensionName, "VK_KHR_portability_subset") == 0; });
			extension = "VK_KHR_portability_subset";
			const float priority = 1;
			VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
			q.queueFamilyIndex = queueFamily;
			q.queueCount = 1;
			q.pQueuePriorities = &priority;
			VkDeviceCreateInfo d{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
			d.queueCreateInfoCount = 1;
			d.pQueueCreateInfos = &q;
			if (subset)
			{
				d.enabledExtensionCount = 1;
				d.ppEnabledExtensionNames = &extension;
			}
			check(CreateDevice(physical, &d, nullptr, &device));
			GetDeviceQueue(device, queueFamily, 0, &queue);
#define REQUIRE(name)                                                                              \
	if (!name)                                                                                     \
		throw std::runtime_error("Missing Vulkan function: " #name);
			VULKAN_FUNCTIONS(REQUIRE)
#undef REQUIRE
			std::array<VkDescriptorSetLayoutBinding, 7> bindings{};
			for (unsigned i = 0; i < 7; ++i)
				bindings[i] = {i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT,
							   nullptr};
			VkDescriptorSetLayoutCreateInfo ds{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
			ds.bindingCount = bindings.size();
			ds.pBindings = bindings.data();
			check(CreateDescriptorSetLayout(device, &ds, nullptr, &descriptors));
			VkPipelineLayoutCreateInfo pl{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
			pl.setLayoutCount = 1;
			pl.pSetLayouts = &descriptors;
			check(CreatePipelineLayout(device, &pl, nullptr, &layout));
			const std::array<std::span<const std::uint32_t>, 2> binaries{gradient_shaders::frozen8,
																		 gradient_shaders::jacobi};
			for (unsigned i = 0; i < 2; ++i)
			{
				VkShaderModule module = VK_NULL_HANDLE;
				VkShaderModuleCreateInfo sm{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
				sm.codeSize = binaries[i].size_bytes();
				sm.pCode = binaries[i].data();
				check(CreateShaderModule(device, &sm, nullptr, &module));
				VkComputePipelineCreateInfo pipe{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
				pipe.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
				pipe.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
				pipe.stage.module = module;
				pipe.stage.pName = "main";
				pipe.layout = layout;
				const auto result = CreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipe,
														   nullptr, &pipelines[i]);
				DestroyShaderModule(device, module, nullptr);
				check(result);
			}
			status.device = properties.deviceName;
			status.driver = std::to_string(properties.driverVersion);
			status.shaderIdentity = gradient_shaders::identity;
			status.availability = Availability::Ready;
			return true;
		}
		catch (const std::exception &error)
		{
			status.error = error.what();
			status.availability = Availability::Unavailable;
			return false;
		}
	}
	void beginCommands(Work &work)
	{
		GradientPhaseScope phase(GradientPhase::Recording);
		// Every reuse follows a successful fence wait. A quarantined Work is
		// never recorded again and continues owning all referenced storage.
		check(ResetCommandBuffer(work.command, 0));
		VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		check(BeginCommandBuffer(work.command, &begin));
	}
	void submitCommands(Work &work)
	{
		GradientPhaseScope phase(GradientPhase::Submission);
		check(EndCommandBuffer(work.command));
		check(ResetFences(device, 1, &work.fence));
		VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
		submit.commandBufferCount = 1;
		submit.pCommandBuffers = &work.command;
		orderMappedWrites();
		check(QueueSubmit(queue, 1, &submit, work.fence));
		// Only successful submission transfers storage ownership to the queue.
		work.submitted = true;
		countGradientWork(GradientCounter::Submissions);
	}
	bool waitSubmission(std::unique_ptr<Work> &work, const BackendRequest &request)
	{
		GradientPhaseScope phase(GradientPhase::Waiting);
		for (;;)
		{
			const auto result = WaitForFences(device, 1, &work->fence, VK_TRUE, 1000000);
			if (result == VK_SUCCESS)
			{
				work->submitted = false;
				return true;
			}
			if (result != VK_TIMEOUT ||
				(request.requiredControl && request.requiredControl->shouldCancel(monotonicNs())))
			{
				status.error = result == VK_TIMEOUT
								   ? "Vulkan canceled while dispatch still owns storage"
								   : "Vulkan dispatch failed";
				if (result != VK_TIMEOUT)
					status.availability = Availability::Failed;
				quarantine.push_back(std::move(work));
				return false;
			}
		}
	}
	bool execute(const BackendRequest &request, Plan plan)
	{
		GradientPhaseScope phase(GradientPhase::Bookkeeping);
		const bool frozen = plan == Plan::VulkanFrozen8;
		const auto cells = request.grid.cells();
		const auto w = unsigned(request.grid.width()), h = unsigned(request.grid.height()),
				   pitch = (w + 15) / 16, rows = (h + 15) / 16;
		const auto tiles = std::size_t(pitch) * rows;
		if (!cells || cells > properties.limits.maxStorageBufferRange / 4 || cells > UINT32_MAX ||
			request.limit < 0)
			return false;
		if (cells > (SIZE_MAX - 16) / 8 || tiles > (SIZE_MAX - cells * 8 - 16) / 4)
			return false;
		if ((frozen && (pitch > properties.limits.maxComputeWorkGroupCount[0] ||
						rows > properties.limits.maxComputeWorkGroupCount[1])) ||
			(!frozen && (cells + 127) / 128 > properties.limits.maxComputeWorkGroupCount[0]))
			return false;
		if (request.requiredControl && request.requiredControl->shouldCancel(monotonicNs()))
			return false;
		phase.setPhase(GradientPhase::Allocation);
		std::unique_ptr<Work> work;
		if (idle && idle->cellsCapacity >= cells && idle->tilesCapacity >= tiles)
		{
			work = std::move(idle);
			countGradientWork(GradientCounter::ResourceReuses);
		}
		else
		{
			idle.reset();
			work = std::make_unique<Work>(this);
		}
		struct ReturnRetired
		{
			Runtime &runtime;
			std::unique_ptr<Work> &work;
			int exceptions = std::uncaught_exceptions();
			~ReturnRetired()
			{
				if (std::uncaught_exceptions() == exceptions && work && work->reusable &&
					!work->submitted && runtime.status.availability == Availability::Ready)
					runtime.idle = std::move(work);
			}
		} returnRetired{*this, work};
		const std::array<std::size_t, BufferCount> sizes{
			cells * 4, cells * 4, cells * 4, 4,
			16,        tiles * 4, tiles * 4, cells * 8 + tiles * 4 + 16};
		if (!work->fence)
		{
			for (unsigned i = 0; i < BufferCount; ++i)
				allocate(work->buffers[i], sizes[i], i == Changed || i == Transfer);
			VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
			cp.queueFamilyIndex = queueFamily;
			cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
			check(CreateCommandPool(device, &cp, nullptr, &work->commands));
			countGradientWork(GradientCounter::CommandAllocations);
			VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
			ca.commandPool = work->commands;
			ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			ca.commandBufferCount = 1;
			check(AllocateCommandBuffers(device, &ca, &work->command));
			countGradientWork(GradientCounter::CommandAllocations);
			VkFenceCreateInfo fc{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
			check(CreateFence(device, &fc, nullptr, &work->fence));
			countGradientWork(GradientCounter::CommandAllocations);
			work->cellsCapacity = cells;
			work->tilesCapacity = tiles;
		}
		auto &sets = work->sets;
		if (!work->descriptors)
		{
			phase.setPhase(GradientPhase::Allocation);
			VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 14};
			VkDescriptorPoolCreateInfo dp{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
			dp.maxSets = 2;
			dp.poolSizeCount = 1;
			dp.pPoolSizes = &poolSize;
			check(CreateDescriptorPool(device, &dp, nullptr, &work->descriptors));
			countGradientWork(GradientCounter::BindingAllocations);
			const std::array layouts{descriptors, descriptors};
			VkDescriptorSetAllocateInfo da{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
			da.descriptorPool = work->descriptors;
			da.descriptorSetCount = 2;
			da.pSetLayouts = layouts.data();
			check(AllocateDescriptorSets(device, &da, sets.data()));
			countGradientWork(GradientCounter::BindingAllocations, sets.size());
			phase.setPhase(GradientPhase::Recording);
			// Each ping-pong orientation owns an immutable descriptor set. No set is
			// rewritten while retained command buffers reference its bindings.
			std::array<VkDescriptorBufferInfo, 14> bi{};
			std::array<VkWriteDescriptorSet, 14> writes{};
			for (unsigned direction = 0; direction < 2; ++direction)
			{
				const std::array<unsigned, 7> indices{
					direction,           1 - direction,      Costs, Changed, Params,
					ActiveA + direction, ActiveB - direction};
				for (unsigned binding = 0; binding < 7; ++binding)
				{
					const auto i = direction * 7 + binding;
					bi[i] = {work->buffers[indices[binding]].buffer, 0, sizes[indices[binding]]};
					writes[i] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
					writes[i].dstSet = sets[direction];
					writes[i].dstBinding = binding;
					writes[i].descriptorCount = 1;
					writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					writes[i].pBufferInfo = &bi[i];
				}
			}
			UpdateDescriptorSets(device, writes.size(), writes.data(), 0, nullptr);
		}
		work->reusable = true;
		phase.setPhase(GradientPhase::InputConversion);
		const auto upload = monotonicNs();
		// Keep every input in a separate staging range until the shared upload
		// fence retires. No host write can replace a queued transfer's source.
		auto *staging = static_cast<std::uint32_t *>(work->buffers[Transfer].mapped);
		const std::array<unsigned, 4> destinations{InputA, Costs, ActiveA, Params};
		const std::array<std::size_t, 4> offsets{0, cells, cells * 2, cells * 2 + tiles};
		const std::array<std::size_t, 4> words{cells, cells, tiles, 4};
		const std::array<std::uint32_t, 4> params{
			w, h, unsigned(std::min(request.limit, COST_LIMIT)), pitch};
		for (std::size_t i = 0; i < cells; ++i)
			staging[offsets[0] + i] = request.gradient[i];
		for (std::size_t i = 0; i < cells; ++i)
		{
			const auto cost = request.gradient[i] || request.identity.allCells
								  ? request.costAt(request.context, i)
								  : LAND_STEPS;
			if (!cost.cardinal || !cost.diagonal || cost.cardinal > 65535 || cost.diagonal > 65535)
				return false;
			staging[offsets[1] + i] = cost.cardinal | (cost.diagonal << 16);
		}
		std::fill_n(staging + offsets[2], tiles, 1u);
		std::memcpy(staging + offsets[3], params.data(), sizeof(params));
		const auto *transferVerification = std::getenv("GLOB2_VULKAN_VERIFY_TRANSFERS");
		const bool verifyTransfers =
			transferVerification && std::strcmp(transferVerification, "1") == 0;
		{
			GradientPhaseScope phase(GradientPhase::Recording);
			beginCommands(*work);
			VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
			barrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
			CmdPipelineBarrier(
				work->command, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
			for (unsigned input = 0; input < destinations.size(); ++input)
			{
				VkBufferCopy copy{offsets[input] * 4, 0, words[input] * 4};
				CmdCopyBuffer(work->command, work->buffers[Transfer].buffer,
							  work->buffers[destinations[input]].buffer, 1, &copy);
			}
			if (verifyTransfers)
			{
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
				CmdPipelineBarrier(work->command, VK_PIPELINE_STAGE_TRANSFER_BIT,
								   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0,
								   nullptr);
				for (unsigned input = 0; input < destinations.size(); ++input)
				{
					VkBufferCopy copy{0, offsets[input] * 4, words[input] * 4};
					CmdCopyBuffer(work->command, work->buffers[destinations[input]].buffer,
								  work->buffers[Transfer].buffer, 1, &copy);
				}
			}
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_HOST_READ_BIT;
			CmdPipelineBarrier(work->command, VK_PIPELINE_STAGE_TRANSFER_BIT,
							   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_HOST_BIT, 0,
							   1, &barrier, 0, nullptr, 0, nullptr);
			phase.setPhase(GradientPhase::Upload);
			submitCommands(*work);
			const auto bytes = (cells * 2 + tiles + params.size()) * 4;
			countGradientWork(GradientCounter::UploadBytes, bytes);
			if (verifyTransfers)
				countGradientWork(GradientCounter::ReadbackBytes, bytes);
			if (!waitSubmission(work, request))
				return false;
			if (verifyTransfers)
			{
				phase.setPhase(GradientPhase::Validation);
				std::atomic_thread_fence(std::memory_order_acquire);
				for (unsigned input = 0; input < destinations.size(); ++input)
				{
					const auto destination = destinations[input];
					const auto expectedAt = [&](std::size_t i) -> std::uint32_t
					{
						if (destination == InputA)
							return request.gradient[i];
						if (destination == Costs)
						{
							const auto cost = request.gradient[i] || request.identity.allCells
												  ? request.costAt(request.context, i)
												  : LAND_STEPS;
							return cost.cardinal | (cost.diagonal << 16);
						}
						return destination == Params ? params[i] : 1u;
					};
					const std::span<const std::uint32_t> actual(staging + offsets[input],
																words[input]);
					for (std::size_t i = 0; i < actual.size(); ++i)
						if (actual[i] != expectedAt(i))
							failGradientTransfer(
								destination,
								std::span<const std::uint16_t>(request.gradient, cells), actual,
								request.grid, request.limit, expectedAt,
								{status.device, status.driver, status.shaderIdentity,
								 request.capturedGeneration.value_or(
									 request.session.currentGeneration())});
				}
			}
		}
		status.uploadNs += monotonicNs() - upload;
		phase.setPhase(GradientPhase::Convergence);
		unsigned current = InputA, active = ActiveA;
		const auto computeStart = monotonicNs();
		// Positive integer edges bound any useful simple path by the field range.
		// A final no-change round proves convergence, independently of hardware speed.
		for (unsigned round = 0; round <= unsigned(COST_LIMIT) + 1; ++round)
		{
			if (request.requiredControl && request.requiredControl->shouldCancel(monotonicNs()))
				return false;
			phase.setPhase(GradientPhase::Recording);
			beginCommands(*work);
			VkMemoryBarrier beforeFill{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
			beforeFill.srcAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT |
									   VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
			beforeFill.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			CmdPipelineBarrier(
				work->command,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
				VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &beforeFill, 0, nullptr, 0, nullptr);
			CmdFillBuffer(work->command, work->buffers[Changed].buffer, 0, 4, 0);
			CmdFillBuffer(work->command, work->buffers[otherActive(active)].buffer, 0, tiles * 4,
						  0);
			VkMemoryBarrier barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
			barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
			CmdPipelineBarrier(
				work->command,
				VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
				VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
			CmdBindPipeline(work->command, VK_PIPELINE_BIND_POINT_COMPUTE,
							pipelines[frozen ? 0 : 1]);
			CmdBindDescriptorSets(work->command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1,
								  &sets[current], 0, nullptr);
			CmdDispatch(work->command, frozen ? pitch : unsigned((cells + 127) / 128),
						frozen ? rows : 1, 1);
			barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
			barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
			CmdPipelineBarrier(work->command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
							   VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0, nullptr);
			submitCommands(*work);
			++status.dispatches;
			countGradientWork(GradientCounter::Dispatches);
			phase.setPhase(GradientPhase::Convergence);
			if (!waitSubmission(work, request))
				return false;
			countGradientWork(GradientCounter::ConvergenceChecks);
			countGradientWork(GradientCounter::MappedControlBytesRead, sizeof(std::uint32_t));
			if (round == 0)
				countGradientWork(GradientCounter::ExecutedFields);
			if (request.deviceExecutionObserved)
				*request.deviceExecutionObserved = true;
			current = 1 - current;
			active = otherActive(active);
			if (!*static_cast<std::uint32_t *>(work->buffers[Changed].mapped))
			{
				status.computeNs += monotonicNs() - computeStart;
				const auto readback = monotonicNs();
				phase.setPhase(GradientPhase::Recording);
				beginCommands(*work);
				barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
				CmdPipelineBarrier(work->command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
								   VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &barrier, 0, nullptr, 0,
								   nullptr);
				VkBufferCopy copy{0, 0, cells * 4};
				CmdCopyBuffer(work->command, work->buffers[current].buffer,
							  work->buffers[Transfer].buffer, 1, &copy);
				barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
				barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
				CmdPipelineBarrier(work->command, VK_PIPELINE_STAGE_TRANSFER_BIT,
								   VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr, 0,
								   nullptr);
				phase.setPhase(GradientPhase::Readback);
				submitCommands(*work);
				countGradientWork(GradientCounter::ReadbackBytes, cells * 4);
				if (!waitSubmission(work, request))
					return false;
				std::atomic_thread_fence(std::memory_order_acquire);
				const auto *out =
					static_cast<const std::uint32_t *>(work->buffers[Transfer].mapped);
				countGradientWork(GradientCounter::ConvergedFields);
				phase.setPhase(GradientPhase::Validation);
				if (!exactGradientFixedPoint(
						std::span<const std::uint16_t>(request.gradient, cells),
						std::span<const std::uint32_t>(out, cells), request.grid, request.limit,
						[&](std::size_t i)
						{
							return request.gradient[i] || request.identity.allCells
									   ? request.costAt(request.context, i)
									   : LAND_STEPS;
						}))
				{
					failGradientProof(
						frozen ? "vulkan-frozen8" : "vulkan-jacobi",
						std::span<const std::uint16_t>(request.gradient, cells),
						std::span<const std::uint32_t>(out, cells), request.grid, request.limit,
						[&](std::size_t i)
						{
							return request.gradient[i] || request.identity.allCells
									   ? request.costAt(request.context, i)
									   : LAND_STEPS;
						},
						{status.device, status.driver, status.shaderIdentity,
						 request.capturedGeneration.value_or(request.session.currentGeneration())});
				}
				countGradientWork(GradientCounter::ValidatedFields);
				phase.setPhase(GradientPhase::OutputConversion);
				for (std::size_t i = 0; i < cells; ++i)
					request.gradient[i] = std::uint16_t(out[i]);
				status.readbackNs += monotonicNs() - readback;
				if (request.executedOnDevice)
					*request.executedOnDevice = true;
				// The readback fence has retired every command and transfer using this set.
				idle = std::move(work);
				return true;
			}
		}
		throw std::runtime_error("Vulkan gradient failed to converge within integer field range");
	}
	void maintain()
	{
		GradientPhaseScope phase(GradientPhase::Retirement);
		for (auto at = quarantine.begin(); at != quarantine.end();)
		{
			const auto result = GetFenceStatus(device, (*at)->fence);
			if (result == VK_SUCCESS || result == VK_ERROR_DEVICE_LOST)
			{
				(*at)->submitted = false;
				if (result == VK_SUCCESS && (*at)->reusable && !idle &&
					status.availability == Availability::Ready)
					idle = std::move(*at);
				at = quarantine.erase(at);
			}
			else
				++at;
		}
		if (quarantine.empty() && status.availability == Availability::Ready &&
			status.error == "Vulkan canceled while dispatch still owns storage")
			status.error.clear();
	}
	void shutdown()
	{
		idle.reset();
		// Unknown in-flight storage pins both its device and loader until process exit.
		if (!quarantine.empty())
			return;
		if (device)
		{
			for (auto &pipe : pipelines)
			{
				if (pipe)
					DestroyPipeline(device, pipe, nullptr);
				pipe = VK_NULL_HANDLE;
			}
			if (layout)
				DestroyPipelineLayout(device, layout, nullptr);
			if (descriptors)
				DestroyDescriptorSetLayout(device, descriptors, nullptr);
			DestroyDevice(device, nullptr);
			device = VK_NULL_HANDLE;
			layout = VK_NULL_HANDLE;
			descriptors = VK_NULL_HANDLE;
		}
		if (instance)
		{
			DestroyInstance(instance, nullptr);
			instance = VK_NULL_HANDLE;
		}
		if (library)
		{
			SDL_UnloadObject(library);
			library = nullptr;
		}
		getProc = nullptr;
#define CLEAR(name) name = nullptr;
		VULKAN_FUNCTIONS(CLEAR)
#undef CLEAR
		attempted = false;
		physical = VK_NULL_HANDLE;
		queue = VK_NULL_HANDLE;
		status.availability = Availability::Uninitialized;
		status.device.clear();
		status.error.clear();
		publish();
		readyPlans.fetch_and(
			~((1u << unsigned(Plan::VulkanFrozen8)) | (1u << unsigned(Plan::VulkanJacobi))));
	}
	~Runtime()
	{
		idle.reset();
		if (!quarantine.empty())
		{
			for (auto &work : quarantine)
				work.release();
			return;
		}
		shutdown();
	}
};
Buffer::~Buffer()
{
	GradientPhaseScope phase(GradientPhase::Retirement);
	if (!runtime)
		return;
	if (mapped)
		runtime->UnmapMemory(runtime->device, memory);
	if (buffer)
		runtime->DestroyBuffer(runtime->device, buffer, nullptr);
	if (memory)
		runtime->FreeMemory(runtime->device, memory, nullptr);
	if (charged)
		releaseGradientDeviceBytes(charged);
	if (hostCharged)
		releaseOpenCLHostBytes(hostCharged);
}
Work::~Work()
{
	GradientPhaseScope phase(GradientPhase::Retirement);
	if (submitted)
		std::terminate();
	if (fence)
		runtime->DestroyFence(runtime->device, fence, nullptr);
	if (commands)
		runtime->DestroyCommandPool(runtime->device, commands, nullptr);
	if (descriptors)
		runtime->DestroyDescriptorPool(runtime->device, descriptors, nullptr);
	if (hostCharged)
		releaseOpenCLHostBytes(hostCharged);
}
Runtime &runtime()
{
	static Runtime value;
	return value;
}
} // namespace
bool initializeVulkanGradient()
{
	GradientProfileSession profile(Backend::Vulkan, gradientProfilingRequested());
	GradientPhaseScope phase(GradientPhase::Initialization);
	auto &r = runtime();
	std::lock_guard lock(r.mutex);
	const bool ready = r.initialize();
	r.publish();
	if (ready)
		readyPlans.fetch_or((1u << unsigned(Plan::VulkanFrozen8)) |
								(1u << unsigned(Plan::VulkanJacobi)),
							std::memory_order_release);
	return ready;
}
bool executeVulkanGradient(std::span<const BackendRequest> requests, Plan plan)
{
	const bool diagnostics = requests.empty()               ? gradientProfilingRequested()
							 : requests.front().diagnostics ? *requests.front().diagnostics
															: gradientProfilingRequested();
	GradientProfileSession profile(Backend::Vulkan, diagnostics);
	GradientPhaseScope phase(GradientPhase::Bookkeeping);
	profile.count(GradientCounter::AttemptedFields, requests.size());
	for (const auto &request : requests)
	{
		if (request.executedOnDevice)
			*request.executedOnDevice = false;
		if (request.deviceExecutionObserved)
			*request.deviceExecutionObserved = false;
	}
	auto &r = runtime();
	std::lock_guard lock(r.mutex);
	if (r.device)
		r.maintain();
	if (r.status.availability != Availability::Ready || !r.quarantine.empty())
		return false;
	if (plan != Plan::VulkanFrozen8 && plan != Plan::VulkanJacobi)
		return false;
	if (requests.size() > BATCH_CATEGORIES)
		return false;
	for (const auto &request : requests)
		if (!request.gradient || !request.costAt || request.limit < 0 ||
			request.operation != Operation::CompleteField)
			return false;
	// Batch results commit together: restore original fields if any member declines.
	std::size_t bytes = 0;
	for (const auto &q : requests)
	{
		if (q.grid.cells() > (OpenCLHostBudget - bytes) / 2)
			return false;
		bytes += q.grid.cells() * 2;
	}
	if (!reserveOpenCLHostBytes(bytes))
	{
		r.idle.reset();
		if (!reserveOpenCLHostBytes(bytes))
			return false;
	}
	struct Lease
	{
		std::size_t bytes;
		~Lease() { releaseOpenCLHostBytes(bytes); }
	} lease{bytes};
	phase.setPhase(GradientPhase::Allocation);
	std::vector<std::vector<std::uint16_t>> seeds;
	for (const auto &q : requests)
		seeds.emplace_back(q.gradient, q.gradient + q.grid.cells());
	phase.setPhase(GradientPhase::Bookkeeping);
	try
	{
		bool success = true;
		for (std::size_t i = 0; i < requests.size(); ++i)
		{
			auto request = requests[i];
			request.gradient = seeds[i].data();
			// A completed private field is execution evidence, but publication
			// belongs to the whole batch. A later decline must not report a commit.
			request.executedOnDevice = nullptr;
			if (!r.execute(request, plan))
			{
				success = false;
				break;
			}
		}
		if (success)
		{
			phase.setPhase(GradientPhase::OutputConversion);
			for (std::size_t i = 0; i < requests.size(); ++i)
			{
				std::copy(seeds[i].begin(), seeds[i].end(), requests[i].gradient);
				if (requests[i].executedOnDevice)
					*requests[i].executedOnDevice = true;
			}
			r.status.fields += requests.size();
			profile.count(GradientCounter::CommittedFields, requests.size());
			r.publish();
			return true;
		}
	}
	catch (const GradientProofFailure &e)
	{
		r.status.error = e.what();
		r.status.availability = Availability::Failed;
		r.publish();
		throw;
	}
	catch (const MemoryDecline &e)
	{
		r.status.error = e.what();
	}
	catch (const VulkanError &e)
	{
		r.status.error = e.what();
		if (e.result != VK_ERROR_OUT_OF_HOST_MEMORY && e.result != VK_ERROR_OUT_OF_DEVICE_MEMORY)
			r.status.availability = Availability::Failed;
	}
	catch (const std::bad_alloc &)
	{
		r.status.error = "Vulkan host allocation declined";
	}
	catch (const std::exception &e)
	{
		r.status.error = e.what();
		r.status.availability = Availability::Failed;
	}
	for (std::size_t i = 0; i < requests.size(); ++i)
	{
		if (requests[i].executedOnDevice)
			*requests[i].executedOnDevice = false;
	}
	r.publish();
	return false;
}
AcceleratorStatus vulkanGradientStatus()
{
	auto &r = runtime();
	std::lock_guard lock(r.statusMutex);
	return r.published;
}
void maintainVulkanGradient() noexcept
{
	GradientProfileSession profile(Backend::Vulkan, gradientProfilingRequested());
	GradientPhaseScope phase(GradientPhase::Retirement);
	auto &r = runtime();
	std::lock_guard lock(r.mutex);
	if (r.device)
		r.maintain();
}
void shutdownVulkanGradient() noexcept
{
	GradientProfileSession profile(Backend::Vulkan, gradientProfilingRequested());
	GradientPhaseScope phase(GradientPhase::Shutdown);
	auto &r = runtime();
	std::lock_guard lock(r.mutex);
	if (r.device)
		r.maintain();
	r.shutdown();
}
#else
bool initializeVulkanGradient()
{
	return false;
}
bool executeVulkanGradient(std::span<const BackendRequest>, Plan)
{
	return false;
}
AcceleratorStatus vulkanGradientStatus()
{
	return {Backend::Vulkan, Availability::Unavailable, "",
			"Vulkan is unavailable in browser builds"};
}
void maintainVulkanGradient() noexcept {}
void shutdownVulkanGradient() noexcept {}
#endif
} // namespace gradient_kernel
