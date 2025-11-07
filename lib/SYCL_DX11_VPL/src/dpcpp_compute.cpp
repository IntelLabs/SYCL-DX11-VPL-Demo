//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0
//==============================================================================
// exclude min and max macros from windows includes
#define NOMINMAX

#include "shared_resource.hpp"
#include <iomanip>
// this should be last to avoid issues with CYCL headers
#include "dpcpp_compute.hpp"

DPCPPCompute::~DPCPPCompute() {
	reset();
}

void DPCPPCompute::reset() {
	l0.reset();

	if (gpu_linear != nullptr) {
		sycl::free(gpu_linear, q);
		gpu_linear = nullptr;
	}
	if (gpu1_linear != nullptr) {
		sycl::free(gpu1_linear, qs[1]);
		gpu1_linear = nullptr;
	}

	if (test_kernel_dst != nullptr) {
		sycl::free(test_kernel_dst, q);
		test_kernel_dst = nullptr;
	}
	if (l0_coms.size() > 0) { // multi-GPU
		for (unsigned int d = 0; d < l0_coms.size(); d++) {
			zeCommandListDestroy(l0_coms[d]); l0_coms[d] = nullptr;
		}
	} else {
		if (l0_commands != nullptr) {
			zeCommandListDestroy(l0_commands); l0_commands = nullptr;
		}
		// transferred to sycle
		//if (l0_q) {
		//	zeCommandQueueDestroy(l0_q);
		//}
	}
	if (l0_dma_coms.size() > 0) {
		for (unsigned int d = 0; d < l0_coms.size(); d++) {
			if (l0_dma_coms[d] == nullptr) continue;
			zeCommandListDestroy(l0_dma_coms[d]); l0_dma_coms[d] = nullptr;
		}
	}
	if (l0_dma_qs.size() > 0) {
		for (unsigned int d = 0; d < l0_coms.size(); d++) {
			if (l0_dma_qs[d] == nullptr) continue;
			zeCommandQueueDestroy(l0_dma_qs[d]); l0_dma_qs[d] = nullptr;
		}
	}
	// clean all structures
	l0_coms.resize(0);
	l0_qs.resize(0);
	l0_devs.resize(0);
	l0_dma_coms.resize(0);
	l0_dma_qs.resize(0);
	l0_ctx = nullptr;
	l0_driver = nullptr;
	l0_dev = nullptr;
	l0_q = nullptr;

	qs.resize(0);
	devs.resize(0);
}

static inline bool operator==(const LUID& luid0, const LUID& luid1) {
	return (std::memcmp(&luid0, &luid1, sizeof(LUID)) == 0);
}


void get_luid(ID3D11Device* dx, LUID& luid) {
	if (dx == nullptr) return;
	IDXGIDevice* pDXGIDevice = nullptr;
	HRESULT hr = dx->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDXGIDevice);
	IDXGIAdapter* pDXGIAdapter = nullptr;
	pDXGIDevice->GetAdapter(&pDXGIAdapter);
	DXGI_ADAPTER_DESC adapter_desc;
	pDXGIAdapter->GetDesc(&adapter_desc);
	luid = adapter_desc.AdapterLuid;
	if (pDXGIAdapter != nullptr) pDXGIAdapter->Release();
	if (pDXGIDevice != nullptr) pDXGIDevice->Release();
}

void get_luid(ze_device_handle_t dev, LUID& luid) {
	ze_device_properties_t device_properties = { ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES };
	ze_device_luid_ext_properties_t device_luid_properties = { ZE_STRUCTURE_TYPE_DEVICE_LUID_EXT_PROPERTIES };
	device_properties.pNext = &device_luid_properties;
	zeDeviceGetProperties(dev, &device_properties);
	luid = *reinterpret_cast<LUID*>(&device_luid_properties.luid);
}

DPCPPCompute::DPCPPCompute(ID3D11Device* dx) {
	ze_device_luid_ext_t	luid{};
	assert(sizeof(luid) == sizeof(ze_device_luid_ext_t));
	::get_luid(dx, *reinterpret_cast<LUID*>(&luid)); // must be binary compatible
	InitGPU(luid);
	get_mem_prop();
}

DPCPPCompute::DPCPPCompute(ID3D11Device* dx, unsigned int max_peer_devices) {
	ze_device_luid_ext_t	luid{};
	assert(sizeof(luid) == sizeof(ze_device_luid_ext_t));
	::get_luid(dx, *reinterpret_cast<LUID*>(&luid)); // must be binary compatible
	InitMultiGPU(luid, max_peer_devices);
	get_mem_prop();
}

DPCPPCompute::DPCPPCompute(const std::vector<ID3D11Device*>& dx) {
	std::vector<ze_device_luid_ext_t> luids(dx.size());
	assert(sizeof(LUID) == sizeof(ze_device_luid_ext_t));
	for (int d = 0; d<dx.size(); d++)
		::get_luid(dx[d], *reinterpret_cast<LUID*>(&luids[d])); // must be binary compatible
	InitMultiGPU(luids);
	get_mem_prop();
}

static std::ostream& operator<<(std::ostream& os, const ze_device_luid_ext_t& luid)
{
	// print as 64-bit hexadecimal
	os << std::hex << std::setfill('0');
	for (int i = 0; i < ZE_MAX_DEVICE_LUID_SIZE_EXT; i++)
		os << std::setw(2) << (unsigned int)luid.id[ZE_MAX_DEVICE_LUID_SIZE_EXT-1-i];
	os << std::dec << std::setw(0);
	return os;
}

static inline bool operator==(const ze_device_luid_ext_t& luid0, const ze_device_luid_ext_t& luid1) {
	return (std::memcmp(luid0.id, luid1.id, ZE_MAX_DEVICE_LUID_SIZE_EXT) == 0);
}


int DPCPPCompute::InitGPU(const ze_device_luid_ext_t& luid) {
	reset();

	zeInit(ZE_INIT_FLAG_GPU_ONLY);

	// Discover all the driver instances
	uint32_t driverCount = 0;
	zeDriverGet(&driverCount, nullptr);
	if (driverCount == 0) std::cout << "Unable to locate driver(s)" << std::endl;

	ze_driver_handle_t* allDrivers = (ze_driver_handle_t*)malloc(driverCount * sizeof(*allDrivers));
	zeDriverGet(&driverCount, allDrivers);

	// Find a driver instance with a GPU device
	for (uint32_t i = 0; i < driverCount; ++i) {
		uint32_t deviceCount = 0;
		zeDeviceGet(allDrivers[i], &deviceCount, nullptr);
		if (deviceCount == 0) continue;
		ze_device_handle_t* allDevices =
			(ze_device_handle_t*)malloc(deviceCount * sizeof(ze_device_handle_t));
		zeDeviceGet(allDrivers[i], &deviceCount, allDevices);
		for (uint32_t d = 0; d < deviceCount; ++d) {
			ze_device_properties_t device_properties = { ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES };
			ze_device_luid_ext_properties_t device_luid_properties = { ZE_STRUCTURE_TYPE_DEVICE_LUID_EXT_PROPERTIES };
			device_properties.pNext = &device_luid_properties;
			zeDeviceGetProperties(allDevices[d], &device_properties);
			if (ZE_DEVICE_TYPE_GPU == device_properties.type) {
				std::cerr << "Level Zero: GPU device located driver=" << i << ", device=" << d <<
					", name=" << device_properties.name <<
					", luid=" << device_luid_properties.luid << std::endl;
				l0_dev = allDevices[d];
				if (device_luid_properties.luid == luid) {
					l0_driver = allDrivers[i];
					std::cerr << "Selected driver " << i << ", device " << d << " based on LUID" << std::endl;
					break;
				}
			}
		}
		if (allDevices != nullptr) { free(allDevices); allDevices = nullptr; }
		if (nullptr != l0_driver) break;
	}
	if (allDrivers != nullptr) { free(allDrivers); allDrivers = nullptr; }
	if (l0_driver == nullptr) std::cout << "unable to locate driver with GPU device" << std::endl;
	if (l0_dev == nullptr) std::cout << "unable to locate GPU device "<< luid << std::endl;

	// Create context
	ze_context_desc_t contextDesc = { ZE_STRUCTURE_TYPE_CONTEXT_DESC, nullptr, 0 };
	zeContextCreate(l0_driver, &contextDesc, &l0_ctx);

	// create a command queue
	ze_command_queue_desc_t commandQueueDesc = {
	  ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC, nullptr, 0, 0, 0,
	  ZE_COMMAND_QUEUE_MODE_SYNCHRONOUS, ZE_COMMAND_QUEUE_PRIORITY_NORMAL
	};
	zeCommandQueueCreate(l0_ctx, l0_dev, &commandQueueDesc, &l0_q);

	// sycl counterparts

	dev = make_device<sycl::backend::ext_oneapi_level_zero>(l0_dev);
	//std::vector<device> devices = {dev};
	ctx = make_context<sycl::backend::ext_oneapi_level_zero>({ l0_ctx, {dev}, ext::oneapi::level_zero::ownership::transfer });
	q = make_queue< sycl::backend::ext_oneapi_level_zero>({ l0_q, dev, ext::oneapi::level_zero::ownership::transfer }, ctx);

	// re-cast
	l0_ctx = get_native<sycl::backend::ext_oneapi_level_zero>(q.get_context());
	l0_dev = get_native<sycl::backend::ext_oneapi_level_zero>(q.get_device());

#if defined(__INTEL_LLVM_COMPILER) && (__INTEL_LLVM_COMPILER>=20230200)
	auto q_lst = get_native<sycl::backend::ext_oneapi_level_zero>(q);
	if (holds_alternative<ze_command_queue_handle_t>(q_lst)) {
		l0_q = get<ze_command_queue_handle_t>(q_lst);
		l0_commands = nullptr;
	}
	else {
		l0_q = nullptr;
		l0_commands = get<ze_command_list_handle_t>(q_lst);
	}
#else
	l0_q = std::get<ze_command_queue_handle_t>(get_native<sycl::backend::ext_oneapi_level_zero>(q));
#endif
	if (l0_commands == nullptr) {
		// create a command list
		ze_command_list_desc_t commandListDesc = {
		  ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC, nullptr, 0, 0
		};
		zeCommandListCreate(l0_ctx, l0_dev, &commandListDesc, &l0_commands);
	}
	// Try to load an extension function
	//HMODULE ze_loader = GetModuleHandle(TEXT("ze_loader.dll"));
	//CHECK2(ze_loader == nullptr, "Cannot find ze_loader.dll");
	//zeImageViewCreateExt_fnPtr = zeImageViewCreateExt_TYPE(GetProcAddress(ze_loader, "zeImageViewCreateExt"));
	//if (zeImageViewCreateExt_fnPtr == nullptr) {
	//	printf("WARNING: Function %s, is not found in %s. Some functionality may not be available on this platform. Make sure you are using latest Intel gfx driver.", "zeImageViewCreateExt", "ze_loader.dll");
	//}

	return 0;


}

int DPCPPCompute::get_luid(ze_device_luid_ext_t& luid, unsigned int dev_idx) {
	int res = 1; // error
	ze_device_handle_t dev0 = nullptr;
	if (l0_devs.size() > 0) 
		dev0 = (dev_idx < l0_devs.size())?l0_devs[dev_idx]:nullptr;
	else
		dev0 = l0_dev;
	if (dev0 != nullptr) {
		ze_device_properties_t device_properties = { ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES };
		ze_device_luid_ext_properties_t device_luid_properties = { ZE_STRUCTURE_TYPE_DEVICE_LUID_EXT_PROPERTIES };
		device_properties.pNext = &device_luid_properties;
		zeDeviceGetProperties(dev0, &device_properties);
		luid = device_luid_properties.luid;
		res = 0;
	}
	return res;
}

int DPCPPCompute::get_luid(LUID& luid, unsigned int dev_idx) {
	assert(sizeof(luid) == sizeof(ze_device_luid_ext_t));
	int res = get_luid(*reinterpret_cast<ze_device_luid_ext_t*>(&luid), dev_idx); // must be binary compatible		
	return res;
}

int DPCPPCompute::InitGPU(unsigned int GPU_ID)
{
	l0_dev = nullptr;

	zeInit(ZE_INIT_FLAG_GPU_ONLY);

	// Discover all the driver instances
	uint32_t driverCount = 0;
	zeDriverGet(&driverCount, nullptr);
	if(driverCount == 0) std::cout<<"Unable to locate driver(s)" <<std::endl;
	if(driverCount <= GPU_ID) std::cout << "0 based GPU_ID is out of range" <<std::endl;

	ze_driver_handle_t* allDrivers = (ze_driver_handle_t*)malloc(driverCount * sizeof(*allDrivers));
	zeDriverGet(&driverCount, allDrivers);

	// Find a driver instance with a GPU device
	for (uint32_t i = 0; i < driverCount; ++i) {
		uint32_t deviceCount = 0;
		zeDeviceGet(allDrivers[i], &deviceCount, nullptr);
		if (deviceCount == 0) continue;
		ze_device_handle_t* allDevices =
			(ze_device_handle_t*)malloc(deviceCount * sizeof(ze_device_handle_t));
		zeDeviceGet(allDrivers[i], &deviceCount, allDevices);
		for (uint32_t d = 0; d < deviceCount; ++d) {
			ze_device_properties_t device_properties = { ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES };
			ze_device_luid_ext_properties_t device_luid_properties = { ZE_STRUCTURE_TYPE_DEVICE_LUID_EXT_PROPERTIES };
			device_properties.pNext = &device_luid_properties;
			zeDeviceGetProperties(allDevices[d], &device_properties);
			if (ZE_DEVICE_TYPE_GPU == device_properties.type) {
				std::cerr << "Level Zero: GPU device located driver=" << i << ", device=" << d <<
					", name=" << device_properties.name << 
					", luid=" << device_luid_properties.luid << std::endl;
				l0_dev = allDevices[d];
				if (GPU_ID == i) {
					l0_driver = allDrivers[i];
					std::cerr << "Selected driver " << i << " based on GPU_ID"<<std::endl;
					break;
				}
			}
		}
		if (allDevices != nullptr) { free(allDevices); allDevices = nullptr; }
		if (nullptr != l0_driver) break;
	}
	if (allDrivers != nullptr) { free(allDrivers); allDrivers = nullptr; }
	if(l0_driver == nullptr) std::cout<< "unable to locate driver with GPU device" << std::endl;
	if(l0_dev == nullptr) std::cout<< "unable to locate GPU device"<<std::endl;

	// Create context
	ze_context_desc_t contextDesc = { ZE_STRUCTURE_TYPE_CONTEXT_DESC, nullptr, 0 };
	zeContextCreate(l0_driver, &contextDesc, &l0_ctx);

	// create a command queue
	ze_command_queue_desc_t commandQueueDesc = {
	  ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC, nullptr, 0, 0, 0,
	  ZE_COMMAND_QUEUE_MODE_SYNCHRONOUS, ZE_COMMAND_QUEUE_PRIORITY_NORMAL
	};
	zeCommandQueueCreate(l0_ctx, l0_dev, &commandQueueDesc, &l0_q);

	// sycl counterparts

	dev = make_device<sycl::backend::ext_oneapi_level_zero>(l0_dev);
	//std::vector<device> devices = {dev};
	ctx = make_context<sycl::backend::ext_oneapi_level_zero>({ l0_ctx, {dev}, ext::oneapi::level_zero::ownership::transfer });
	q = make_queue< sycl::backend::ext_oneapi_level_zero>({ l0_q, dev, ext::oneapi::level_zero::ownership::transfer }, ctx);

	// re-cast
	l0_ctx = get_native<sycl::backend::ext_oneapi_level_zero>(q.get_context());
	l0_dev = get_native<sycl::backend::ext_oneapi_level_zero>(q.get_device());
#if defined(__INTEL_LLVM_COMPILER) && (__INTEL_LLVM_COMPILER>=20230200)
	auto q_lst = get_native<sycl::backend::ext_oneapi_level_zero>(q);
	if (holds_alternative<ze_command_queue_handle_t>(q_lst)) {
		l0_q = get<ze_command_queue_handle_t>(q_lst);
		l0_commands = nullptr;
	}
	else {
		l0_q = nullptr;
		l0_commands = get<ze_command_list_handle_t>(q_lst);
	}
#else
	l0_q = std::get<ze_command_queue_handle_t>(get_native<sycl::backend::ext_oneapi_level_zero>(q));
#endif
	if (l0_commands == nullptr) {
		// create a command list
		ze_command_list_desc_t commandListDesc = {
		  ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC, nullptr, 0, 0
		};
		zeCommandListCreate(l0_ctx, l0_dev, &commandListDesc, &l0_commands);
	}

	// Try to load an extension function
	//HMODULE ze_loader = GetModuleHandle(TEXT("ze_loader.dll"));
	//CHECK2(ze_loader == nullptr, "Cannot find ze_loader.dll");
	//zeImageViewCreateExt_fnPtr = zeImageViewCreateExt_TYPE(GetProcAddress(ze_loader, "zeImageViewCreateExt"));
	//if (zeImageViewCreateExt_fnPtr == nullptr) {
	//	printf("WARNING: Function %s, is not found in %s. Some functionality may not be available on this platform. Make sure you are using latest Intel gfx driver.", "zeImageViewCreateExt", "ze_loader.dll");
	//}

	return 0;
}

static void ListCommandQs(ze_device_handle_t& device) {
	uint32_t groupCount = 0;
	zeDeviceGetCommandQueueGroupProperties(device, &groupCount, nullptr);
	std::vector< ze_command_queue_group_properties_t> properties(groupCount);
	zeDeviceGetCommandQueueGroupProperties(device, &groupCount, properties.data());
	for (int i = 0; i < properties.size(); i++) {

		std::cerr << "Q" << i << ":0x" << std::hex << properties[i].flags << std::dec;
		if (properties[i].flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COMPUTE)
			std::cerr << " COMPUTE";
		if (properties[i].flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COPY)
			std::cerr << " COPY";
		if (properties[i].flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COOPERATIVE_KERNELS)
			std::cerr << " COOP_KRNL";
		if (properties[i].flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_METRICS)
			std::cerr << " METRICS";
		std::cerr << std::endl;
	}
}

static void ListMemAccProp(ze_device_handle_t& device) {
	ze_memory_access_cap_flags_t flags[] = {
	ZE_MEMORY_ACCESS_CAP_FLAG_RW,
	ZE_MEMORY_ACCESS_CAP_FLAG_ATOMIC,
	ZE_MEMORY_ACCESS_CAP_FLAG_CONCURRENT,
	ZE_MEMORY_ACCESS_CAP_FLAG_CONCURRENT_ATOMIC };
	ze_device_memory_access_properties_t props{};
	zeDeviceGetMemoryAccessProperties(device, &props);
	std::cerr << "MEM_ACC:0x" << std::hex << props.deviceAllocCapabilities << std::dec;
	std::cerr << std::endl;
}

int DPCPPCompute::ListGPUs(bool with_connectivity)
{

	zeInit(ZE_INIT_FLAG_GPU_ONLY);

	// Discover all the driver instances
	uint32_t driverCount = 0;
	zeDriverGet(&driverCount, nullptr);
	if (driverCount == 0) std::cout << "Unable to locate driver(s)" << std::endl;
	std::vector<ze_driver_handle_t> drivers(driverCount, nullptr);
	zeDriverGet(&driverCount, drivers.data());
	std::vector < std::vector<ze_device_handle_t> > drv_devices(driverCount);

	uint32_t	totalGPUs = 0;
	// Find a driver instance with a GPU device
	for (uint32_t i = 0; i < driverCount; ++i) {
		uint32_t deviceCount = 0;
		zeDeviceGet(drivers[i], &deviceCount, nullptr);
		if (deviceCount == 0) continue;
		drv_devices[i].resize(deviceCount, nullptr);
		zeDeviceGet(drivers[i], &deviceCount, drv_devices[i].data());
		// check if this devices are GPUs, put nullptr in device handle if not GPU
		for (uint32_t d = 0; d < deviceCount; ++d) {
			ze_device_properties_t device_properties = { ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES };
			ze_device_luid_ext_properties_t device_luid_properties = { ZE_STRUCTURE_TYPE_DEVICE_LUID_EXT_PROPERTIES };
			device_properties.pNext = &device_luid_properties;
			zeDeviceGetProperties(drv_devices[i][d], &device_properties);
			if (ZE_DEVICE_TYPE_GPU == device_properties.type) {
				totalGPUs++;
				std::cerr << "Level Zero: GPU device located driver=" << i << ", device=" << d <<
					", name=" << device_properties.name <<
					", luid=" << device_luid_properties.luid << std::endl;
				ListCommandQs(drv_devices[i][d]);
				ListMemAccProp(drv_devices[i][d]);
			}
			else {
				drv_devices[i][d] = nullptr;
			}
		}
	}
	// check who can talk to who
	if (with_connectivity) {
		std::cout << "Connectivity info" << std::endl;
		std::cout << "Connectivity flags: access=0x" << ZE_DEVICE_P2P_PROPERTY_FLAG_ACCESS <<
			", atomics=0x" << ZE_DEVICE_P2P_PROPERTY_FLAG_ATOMICS <<
			", force uint32=0x" << ZE_DEVICE_P2P_PROPERTY_FLAG_FORCE_UINT32 <<
			std::dec << std::endl;
		for (uint32_t i0 = 0; i0 < drv_devices.size(); i0++) {
			for (uint32_t d0 = 0; d0 < drv_devices[i0].size(); d0++) {
				if(drv_devices[i0][d0] == nullptr) continue;
				std::cout << "(" << i0 << "," << d0 << ") Flags->\t";
				for (uint32_t i1 = 0; i1 < drv_devices.size(); i1++) {
					for (uint32_t d1 = 0; d1 < drv_devices[i1].size(); d1++) {
						if (drv_devices[i0][d0] == nullptr) continue;
						ze_device_p2p_properties_t	p2p_properties = { ZE_STRUCTURE_TYPE_DEVICE_P2P_PROPERTIES };
						ze_device_p2p_bandwidth_exp_properties_t p2p_bw_properties = { ZE_STRUCTURE_TYPE_DEVICE_P2P_BANDWIDTH_EXP_PROPERTIES };
						p2p_properties.pNext = &p2p_bw_properties;
						zeDeviceGetP2PProperties(drv_devices[i0][d0], drv_devices[i1][d1], &p2p_properties);
						std::cout<<"(" << i1 << "," << d1 << "):" << std::hex << p2p_properties.flags << std::dec << '\t';
					}
				}
				std::cout<<std::endl;
			}
		}
	}
	return totalGPUs;
}

// ask if device supports dma copy q, return ordinal in ordinal
// and number of qs supported
static uint32_t find_dma_q(ze_device_handle_t& device, uint32_t& ordinal) {
	uint32_t groupCount = 0;
	zeDeviceGetCommandQueueGroupProperties(device, &groupCount, nullptr);
	std::vector< ze_command_queue_group_properties_t> properties(groupCount);
	zeDeviceGetCommandQueueGroupProperties(device, &groupCount, properties.data());
	uint32_t qi = 0;
	for (qi = 0; qi < properties.size(); qi++) {

		if (properties[qi].flags == ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COPY)
			break;
	}

	if (qi >= properties.size()) return 0;
	ordinal = qi;
	return properties[qi].numQueues;
}

template <typename T> int find(const std::vector<T>& v, const T& val) {
	for (int i = 0; i < v.size(); i++)
		if (v[i] == val) return i;
	return -1;
}

int DPCPPCompute::InitMultiGPU(const std::vector<ze_device_luid_ext_t>& luids) {
	l0_dev = nullptr;
	l0_driver = nullptr;
	if (luids.size() == 0) return 0;

	zeInit(ZE_INIT_FLAG_GPU_ONLY);

	// Discover all the driver instances
	uint32_t driverCount = 0;
	zeDriverGet(&driverCount, nullptr);
	if (driverCount == 0) std::cout << "Unable to locate driver(s)" << std::endl;
	std::vector<ze_driver_handle_t> drivers(driverCount, nullptr);
	zeDriverGet(&driverCount, drivers.data());
	std::vector<ze_device_handle_t> devices;

	uint32_t	gpus_found = 0;	
	std::vector<ze_device_handle_t> gpus(luids.size(), nullptr);
	std::vector<ze_driver_handle_t> gpus_driver(luids.size(), nullptr);

	// Find a driver instance with a GPU device
	for (uint32_t i = 0; i < driverCount; ++i) {
		devices.resize(0);
		uint32_t deviceCount = 0;
		zeDeviceGet(drivers[i], &deviceCount, nullptr);
		if (deviceCount == 0) continue;
		devices.resize(deviceCount, nullptr);
		zeDeviceGet(drivers[i], &deviceCount, devices.data());
		// check if this devices are GPUs, put nullptr in device handle if not GPU
		for (uint32_t d = 0; d < deviceCount; ++d) {
			ze_device_properties_t device_properties = { ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES };
			ze_device_luid_ext_properties_t device_luid_properties = { ZE_STRUCTURE_TYPE_DEVICE_LUID_EXT_PROPERTIES };
			device_properties.pNext = &device_luid_properties;
			zeDeviceGetProperties(devices[d], &device_properties);
			if (ZE_DEVICE_TYPE_GPU == device_properties.type) {
				std::cerr << "Level Zero: GPU device located driver=" << i << ", device=" << d <<
					", name=" << device_properties.name <<
					", luid=" << device_luid_properties.luid << std::endl;
				int idx = find(luids, device_luid_properties.luid);
				if ( idx >= 0) {
					gpus[idx] = devices[d];
					gpus_driver[idx] = drivers[i];
					gpus_found++;
					std::cerr << "Selected driver " << i << ", device " << d << " based on LUID" << std::endl;
				}
			} else {
				devices[d] = nullptr;
			}
		}
		// finished enumerating devices for the given driver
		if (gpus_found == luids.size()) break; // found requested device
	}
	if (gpus_found < luids.size()) {
		std::cerr << "GPU devices not found. Initialization failed." << std::endl;
		return 0;
	}


	// Create context
	ze_context_desc_t contextDesc = { ZE_STRUCTURE_TYPE_CONTEXT_DESC, nullptr, 0 };
	ze_result_t res = zeContextCreateEx(gpus_driver[0], &contextDesc, gpus.size(), gpus.data(), &l0_ctx);
	if (res != ZE_RESULT_SUCCESS) {
		std::cout << "Create multi-gpu context failed!" << std::endl;
		return 0;
	}


	l0_dev = gpus[0];
	l0_driver = gpus_driver[0];

	// now create l0 objects with all devices

	// allocate 
	l0_devs.resize(gpus.size(), nullptr);
	l0_qs.resize(gpus.size(), nullptr);
	l0_coms.resize(gpus.size(), nullptr);
	// devs, qs must be empty!
	assert(devs.size() == 0);
	assert(qs.size() == 0);

	for (uint32_t d = 0; d < gpus.size(); d++) {
		// copy device
		l0_devs[d] = gpus[d];

		// create a command queue
		ze_command_queue_desc_t commandQueueDesc = {
		  ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC, nullptr, 0, 0, 0,
		  ZE_COMMAND_QUEUE_MODE_SYNCHRONOUS, ZE_COMMAND_QUEUE_PRIORITY_NORMAL
		};
		zeCommandQueueCreate(l0_ctx, l0_devs[d], &commandQueueDesc, &l0_qs[d]);

		// sickle devices, will need them for ctx creations
		devs.emplace_back(make_device<sycl::backend::ext_oneapi_level_zero>(l0_devs[d]));
	}

	// sycl counterparts
	ctx = make_context<sycl::backend::ext_oneapi_level_zero>({ l0_ctx, devs, ext::oneapi::level_zero::ownership::transfer });
	for (uint32_t d = 0; d < devs.size(); d++) {
		qs.emplace_back(make_queue< sycl::backend::ext_oneapi_level_zero>({ l0_qs[d], devs[d], ext::oneapi::level_zero::ownership::transfer }, ctx));
	}

	dev = devs[0];
	q = qs[0];

	// re-cast (just in case)
	l0_ctx = get_native<sycl::backend::ext_oneapi_level_zero>(q.get_context());
	for (uint32_t d = 0; d < devs.size(); d++) {
		l0_devs[d] = get_native<sycl::backend::ext_oneapi_level_zero>(qs[d].get_device());
		//l0_qs[d] = get_native<sycl::backend::ext_oneapi_level_zero>(qs[d]);
#if defined(__INTEL_LLVM_COMPILER) && (__INTEL_LLVM_COMPILER>=20230200)
		auto q_lst = get_native<sycl::backend::ext_oneapi_level_zero>(qs[d]);
		if (holds_alternative<ze_command_queue_handle_t>(q_lst)) {
			l0_qs[d] = get<ze_command_queue_handle_t>(q_lst);
			l0_coms[d] = nullptr;
		}
		else {
			l0_qs[d] = nullptr;
			l0_coms[d] = get<ze_command_list_handle_t>(q_lst);
		}
#else
		l0_qs[d] = std::get<ze_command_queue_handle_t>(get_native<sycl::backend::ext_oneapi_level_zero>(qs[d]));
#endif
	}
	l0_dev = l0_devs[0];
	l0_q = l0_qs[0];

	// create l0 command lists
	for (uint32_t d = 0; d < devs.size(); d++) {
		if (l0_coms[d] == nullptr) {
			ze_command_list_desc_t commandListDesc = {
			  ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC, nullptr, 0, 0
			};
			zeCommandListCreate(l0_ctx, l0_devs[d], &commandListDesc, &l0_coms[d]);
		}
	}
	l0_commands = l0_coms[0];

	// dma copy qs and command lists
	l0_dma_qs.resize(devs.size(), nullptr);
	l0_dma_coms.resize(devs.size(), nullptr);
	for (uint32_t d = 0; d < devs.size(); d++) {
		uint32_t ordinal = 0;
		if (find_dma_q(l0_devs[d], ordinal) > 0) {
			// create a command queue
			ze_command_queue_desc_t commandQueueDesc = {
			  ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC, nullptr, ordinal, 0, 0,
			  ZE_COMMAND_QUEUE_MODE_SYNCHRONOUS, ZE_COMMAND_QUEUE_PRIORITY_NORMAL
			};
			zeCommandQueueCreate(l0_ctx, l0_devs[d], &commandQueueDesc, &l0_dma_qs[d]);
			// create a command list
			ze_command_list_desc_t commandListDesc = {
				ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC, nullptr, ordinal, 0
			};
			zeCommandListCreate(l0_ctx, l0_devs[d], &commandListDesc, &l0_dma_coms[d]);
		}
	}
	return devs.size();
}

int DPCPPCompute::InitMultiGPU(const ze_device_luid_ext_t& luid, unsigned int max_devices) {
	l0_dev = nullptr;

	zeInit(ZE_INIT_FLAG_GPU_ONLY);

	// Discover all the driver instances
	uint32_t driverCount = 0;
	zeDriverGet(&driverCount, nullptr);
	if (driverCount == 0) std::cout << "Unable to locate driver(s)" << std::endl;
	std::vector<ze_driver_handle_t> drivers(driverCount, nullptr);
	zeDriverGet(&driverCount, drivers.data());
	std::vector<ze_device_handle_t> devices;

	uint32_t	totalGPUs = 0;
	l0_dev = nullptr;
	l0_driver = nullptr;
	// Find a driver instance with a GPU device
	for (uint32_t i = 0; i < driverCount; ++i) {
		devices.resize(0);
		uint32_t deviceCount = 0;
		zeDeviceGet(drivers[i], &deviceCount, nullptr);
		if (deviceCount == 0) continue;
		devices.resize(deviceCount, nullptr);
		zeDeviceGet(drivers[i], &deviceCount, devices.data());
		// check if this devices are GPUs, put nullptr in device handle if not GPU
		for (uint32_t d = 0; d < deviceCount; ++d) {
			ze_device_properties_t device_properties = { ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES };
			ze_device_luid_ext_properties_t device_luid_properties = { ZE_STRUCTURE_TYPE_DEVICE_LUID_EXT_PROPERTIES };
			device_properties.pNext = &device_luid_properties;
			zeDeviceGetProperties(devices[d], &device_properties);
			if (ZE_DEVICE_TYPE_GPU == device_properties.type) {
				std::cerr << "Level Zero: GPU device located driver=" << i << ", device=" << d <<
					", name=" << device_properties.name <<
					", luid=" << device_luid_properties.luid << std::endl;
				if (device_luid_properties.luid == luid) {
					l0_dev = devices[d];
					l0_driver = drivers[i];
					std::cerr << "Selected driver " << i << ", device " << d << " based on LUID" << std::endl;
				}
			}
			else {
				devices[d] = nullptr;
			}
		}
		// finished enumerating devices for the given driver
		if (l0_dev != nullptr) break; // found requested device
	}
	if (l0_dev == nullptr) {
		std::cerr << "GPU device not found. Initialization failed." << std::endl;
		return 0;
	}
	// find peers
	int peerGPUs = 1;
	std::vector<ze_device_handle_t> peers(devices.size(), nullptr);
	peers[0] = l0_dev;
	for (uint32_t d0 = 0; d0 < devices.size(); d0++) {
		if (max_devices > 0 && peerGPUs >= max_devices) break; //found max devices 
		if (devices[d0] == nullptr || devices[d0] == l0_dev) continue; // skip non-GPUs and self 
		ze_device_p2p_properties_t	p2p_properties = { ZE_STRUCTURE_TYPE_DEVICE_P2P_PROPERTIES };
		ze_device_p2p_bandwidth_exp_properties_t p2p_bw_properties = { ZE_STRUCTURE_TYPE_DEVICE_P2P_BANDWIDTH_EXP_PROPERTIES };
		p2p_properties.pNext = &p2p_bw_properties;
		zeDeviceGetP2PProperties(l0_dev, devices[d0], &p2p_properties);
		// TODO: do we need to check the opposite?
		//ze_device_p2p_properties_t	p2p_properties1 = { ZE_STRUCTURE_TYPE_DEVICE_P2P_PROPERTIES };
		//ze_device_p2p_bandwidth_exp_properties_t p2p_bw_properties1 = { ZE_STRUCTURE_TYPE_DEVICE_P2P_BANDWIDTH_EXP_PROPERTIES };
		//p2p_properties.pNext = &p2p_bw_properties1;
		//zeDeviceGetP2PProperties(devices[d0], l0_dev, &p2p_properties1);
		if (p2p_properties.flags != 0) { // can communicate
			peers[peerGPUs++] = devices[d0];
			ze_bool_t	access = 0;
			ze_result_t res = zeDeviceCanAccessPeer(l0_dev, devices[d0], &access);
			std::cerr << "Dev(" << d0 << ") Acc:" << (access>0) << "; ";
		}
	}
	std::cerr << "Found " << peerGPUs << " peers." << std::endl;

	// now create l0 objects with all devices
	
	// allocate 
	l0_devs.resize(peerGPUs, nullptr);
	l0_qs.resize(peerGPUs, nullptr);
	l0_coms.resize(peerGPUs, nullptr);
	// devs, qs must be empty!
	assert(devs.size() == 0);
	assert(qs.size() == 0);

	// Create context
	ze_context_desc_t contextDesc = { ZE_STRUCTURE_TYPE_CONTEXT_DESC, nullptr, 0 };
	//zeContextCreate(l0_driver, &contextDesc, &l0_ctx);
	zeContextCreateEx(l0_driver, &contextDesc, peerGPUs, peers.data(), &l0_ctx);
	for (uint32_t d = 0; d < peerGPUs; d++) {
		// copy device
		l0_devs[d] = peers[d];

		// create a command queue
		ze_command_queue_desc_t commandQueueDesc = {
		  ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC, nullptr, 0, 0, 0,
		  ZE_COMMAND_QUEUE_MODE_SYNCHRONOUS, ZE_COMMAND_QUEUE_PRIORITY_NORMAL
		};
		zeCommandQueueCreate(l0_ctx, l0_devs[d], &commandQueueDesc, &l0_qs[d]);

		// sickle devices, will need them for ctx creations
		devs.emplace_back(make_device<sycl::backend::ext_oneapi_level_zero>(l0_devs[d]));
	}

	// sycl counterparts
	ctx = make_context<sycl::backend::ext_oneapi_level_zero>({ l0_ctx, devs, ext::oneapi::level_zero::ownership::transfer });
	for (uint32_t d = 0; d < peerGPUs; d++) {
		qs.emplace_back(make_queue< sycl::backend::ext_oneapi_level_zero>({ l0_qs[d], devs[d], ext::oneapi::level_zero::ownership::transfer}, ctx));
	}

	dev = devs[0];
	q = qs[0];

	// re-cast (just in case)
	l0_ctx = get_native<sycl::backend::ext_oneapi_level_zero>(q.get_context());
	for (uint32_t d = 0; d < peerGPUs; d++) {
		l0_devs[d] = get_native<sycl::backend::ext_oneapi_level_zero>(qs[d].get_device());
		//l0_qs[d] = get_native<sycl::backend::ext_oneapi_level_zero>(qs[d]);
#if defined(__INTEL_LLVM_COMPILER) && (__INTEL_LLVM_COMPILER>=20230200)
		auto q_lst = get_native<sycl::backend::ext_oneapi_level_zero>(qs[d]);
		if (holds_alternative<ze_command_queue_handle_t>(q_lst)) {
			l0_qs[d] = get<ze_command_queue_handle_t>(q_lst);
			l0_coms[d] = nullptr;
		}
		else {
			l0_qs[d] = nullptr;
			l0_coms[d] = get<ze_command_list_handle_t>(q_lst);
		}
#else
		l0_qs[d] = std::get<ze_command_queue_handle_t>(get_native<sycl::backend::ext_oneapi_level_zero>(qs[d]));
#endif
	}
	l0_dev = l0_devs[0];
	l0_q = l0_qs[0];

	// create l0 command lists
	for (uint32_t d = 0; d < peerGPUs; d++) {
		if (l0_coms[d] == nullptr) {
			ze_command_list_desc_t commandListDesc = {
			  ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC, nullptr, 0, 0
			};
			zeCommandListCreate(l0_ctx, l0_devs[d], &commandListDesc, &l0_coms[d]);
		}
	}
	l0_commands = l0_coms[0];

	// dma copy qs and command lists
	l0_dma_qs.resize(peerGPUs, nullptr);
	l0_dma_coms.resize(peerGPUs, nullptr);
	for (uint32_t d = 0; d < peerGPUs; d++) {
		uint32_t ordinal = 0;
		if (find_dma_q(l0_devs[d], ordinal) > 0) {
			// create a command queue
			ze_command_queue_desc_t commandQueueDesc = {
			  ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC, nullptr, ordinal, 0, 0,
			  ZE_COMMAND_QUEUE_MODE_SYNCHRONOUS, ZE_COMMAND_QUEUE_PRIORITY_NORMAL
			};
			zeCommandQueueCreate(l0_ctx, l0_devs[d], &commandQueueDesc, &l0_dma_qs[d]);
			// create a command list
			ze_command_list_desc_t commandListDesc = {
				ZE_STRUCTURE_TYPE_COMMAND_LIST_DESC, nullptr, ordinal, 0
			};
			zeCommandListCreate(l0_ctx, l0_devs[d], &commandListDesc, &l0_dma_coms[d]);
		}
	}
	return peerGPUs;
}

sycl::queue l0_to_sycl(ze_command_queue_handle_t& q0) {}

sycl::queue l0_queue(sycl::queue& q0) {
	auto context = get_native<sycl::backend::ext_oneapi_level_zero>(q0.get_context());
	auto device = get_native<sycl::backend::ext_oneapi_level_zero>(q0.get_device());
	// create a command queue
	ze_command_queue_desc_t commandQueueDesc = {
	  ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC, nullptr, 0, 0, 0,
	  ZE_COMMAND_QUEUE_MODE_SYNCHRONOUS, ZE_COMMAND_QUEUE_PRIORITY_NORMAL
	};
	ze_command_queue_handle_t queue = nullptr;
	zeCommandQueueCreate(context, device, &commandQueueDesc, &queue);
	//sycl::make_buffer< sycl::backend::ext_oneapi_level_zero, T>({ l0_mem.data, ext::oneapi::level_zero::ownership::keep }, q.get_context());
	return make_queue< sycl::backend::ext_oneapi_level_zero>({ queue, q0.get_device(), ext::oneapi::level_zero::ownership::transfer },q0.get_context());
}

void DPCPPCompute::get_mem_prop() {
	uint32_t count = 0;
	zeDeviceGetMemoryProperties(l0_dev, &count, nullptr);
	dev_mem_count = count;
	if (count) {
		ze_device_memory_properties_t* mem_prop = new ze_device_memory_properties_t[count];
		memset(mem_prop, 0, sizeof(ze_device_memory_properties_t) * count);
		zeDeviceGetMemoryProperties(l0_dev, &count, mem_prop);
		for (int i = 0; i < count; i++) {
			std::cout << "Mem name: " << mem_prop[i].name << ", size: " << mem_prop[i].totalSize << ", flags: " << mem_prop[i].flags << std::endl;
		}
		delete[] mem_prop;
	}
}

// texture to a linear buffer view (no de-tiling)
l0_allocation_t DPCPPCompute::texture_to_l0(ID3D11Texture2D* src) {
	// try to cast texture memory as l0 device allocation
	HANDLE handle;
	IDXGIResource1* pResource;
	src->QueryInterface(__uuidof(IDXGIResource1), (void**)&pResource);
	pResource->CreateSharedHandle(NULL,
		DXGI_SHARED_RESOURCE_READ, //| DXGI_SHARED_RESOURCE_WRITE,
		NULL,
		&handle);
	pResource->Release();
	D3D11_TEXTURE2D_DESC tex_desc;
	src->GetDesc(&tex_desc);
	size_t tex_byte_sz = 0;
	if (tex_desc.Format == DXGI_FORMAT_R32_UINT)
		tex_byte_sz = tex_desc.Width * tex_desc.Height*4;
	else if(tex_desc.Format == DXGI_FORMAT_NV12) // different formats have different layouts and sizes
		tex_byte_sz = tex_desc.Width * tex_desc.Height * 3 / 2;

	assert(tex_byte_sz);


	void* src_l0_mem = nullptr;

	ze_external_memory_import_win32_handle_t shared_desc =
	{
	  ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMPORT_WIN32,
	  nullptr, // pNext
	  ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32,
	  handle,
	  nullptr //name
	};

	ze_device_mem_alloc_desc_t dev_desc{ };
	dev_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
	dev_desc.flags = ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_CACHED;// 0; //ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_CACHED
	dev_desc.ordinal = 0;
	dev_desc.pNext = &shared_desc;

	ze_result_t res = zeMemAllocDevice(l0_ctx, &dev_desc, tex_byte_sz, 0, l0_dev, &src_l0_mem);
	return { src_l0_mem, tex_byte_sz };
}

void DPCPPCompute::shared_to_l0(shared_resource_t& src, l0_image_t& dst) {
	dst.reset();
	D3D11_TEXTURE2D_DESC tex_desc;
	src.texture->GetDesc(&tex_desc);

	// l0 datastrucutres
	ze_image_format_layout_t format = ZE_IMAGE_FORMAT_LAYOUT_NV12;
	switch (tex_desc.Format) {
	case DXGI_FORMAT_NV12:
		format = ZE_IMAGE_FORMAT_LAYOUT_NV12;
		break;
	case DXGI_FORMAT_R8_UNORM:
		format = ZE_IMAGE_FORMAT_LAYOUT_8;
		break;
	case DXGI_FORMAT_R16_UNORM:
		format = ZE_IMAGE_FORMAT_LAYOUT_16;
		break;
	case DXGI_FORMAT_P010:
		format = ZE_IMAGE_FORMAT_LAYOUT_P010;
		break;
	case DXGI_FORMAT_P016:
		format = ZE_IMAGE_FORMAT_LAYOUT_P016;
		break;
	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM:
		format = ZE_IMAGE_FORMAT_LAYOUT_8_8_8_8;
		break;
	case DXGI_FORMAT_R8G8_UNORM:
		format = ZE_IMAGE_FORMAT_LAYOUT_8_8;
		break;
	default:
		std::cout << "ERROR: shared_to_l0: unsupported fromat." << std::endl;
		assert(0); // unsopported format
		break;
	}

	ze_external_memory_import_win32_handle_t shared_desc =
	{
	  ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMPORT_WIN32,
	  nullptr, // pNext
	  ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32,//ZE_EXTERNAL_MEMORY_TYPE_FLAG_D3D11_TEXTURE,//
	  src.handle,
	  nullptr //name
	};

	ze_image_desc_t desc = { ZE_STRUCTURE_TYPE_IMAGE_DESC,
					&shared_desc,		// pNext
					(ZE_IMAGE_FLAG_KERNEL_WRITE | ZE_IMAGE_FLAG_BIAS_UNCACHED),//0, //ZE_IMAGE_FLAG_KERNEL_WRITE,//
					ZE_IMAGE_TYPE_2D,
					{format,
					ZE_IMAGE_FORMAT_TYPE_UNORM,//ZE_IMAGE_FORMAT_TYPE_UINT,
					 ZE_IMAGE_FORMAT_SWIZZLE_R, ZE_IMAGE_FORMAT_SWIZZLE_G,
					 ZE_IMAGE_FORMAT_SWIZZLE_B, ZE_IMAGE_FORMAT_SWIZZLE_A},
					(uint64_t)tex_desc.Width,
					(uint32_t)tex_desc.Height,
					1, 1,1 };//0, 0 };
	//DstRegion = { 0, 0, 0, (uint32_t)Width, (uint32_t)Height, 1 };

	zeImageCreate(l0_ctx, l0_dev, &desc, &dst.image);
	dst.format = format;
	dst.width = (size_t)tex_desc.Width;
	dst.height = (size_t)tex_desc.Height;
	dst.shared_handle = src.handle;
}

// texture to a l0 image (no de-tiling)
l0_image_t DPCPPCompute::texture_to_image_l0(ID3D11Texture2D* src) {
	// try to cast texture memory as l0 device allocation
	HANDLE handle;
	IDXGIResource1* pResource;
	src->QueryInterface(__uuidof(IDXGIResource1), (void**)&pResource);
	pResource->CreateSharedHandle(NULL,
		DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,//DXGI_SHARED_RESOURCE_READ, //| DXGI_SHARED_RESOURCE_WRITE,
		NULL,
		&handle);
	pResource->Release();
	D3D11_TEXTURE2D_DESC tex_desc;
	src->GetDesc(&tex_desc);

	// l0 datastrucutres
	ze_image_format_layout_t format = ZE_IMAGE_FORMAT_LAYOUT_NV12;
	if (tex_desc.Format == DXGI_FORMAT_NV12)
		format = ZE_IMAGE_FORMAT_LAYOUT_NV12;
	else if (tex_desc.Format == DXGI_FORMAT_P010)
		format = ZE_IMAGE_FORMAT_LAYOUT_P010;
	else if (tex_desc.Format == DXGI_FORMAT_P016)
		format = ZE_IMAGE_FORMAT_LAYOUT_P016;
	else if (tex_desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || tex_desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM)
		format = ZE_IMAGE_FORMAT_LAYOUT_8_8_8_8;
	else {
		assert(0);
	}

	ze_external_memory_import_win32_handle_t shared_desc =
	{
	  ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMPORT_WIN32,
	  nullptr, // pNext
	  ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32,//ZE_EXTERNAL_MEMORY_TYPE_FLAG_D3D11_TEXTURE,//
	  handle,
	  nullptr //name
	};
	// TODO: currently ZE_IMAGE_FLAG_BIAS_UNCACHED and removal of ZE_IMAGE_FLAG_KERNEL_WRITE have no effect
	// tuning maybe needed in future 
	ze_image_desc_t desc = { ZE_STRUCTURE_TYPE_IMAGE_DESC,
					&shared_desc,		// pNext
					ZE_IMAGE_FLAG_KERNEL_WRITE,//ZE_IMAGE_FLAG_BIAS_UNCACHED,//(ZE_IMAGE_FLAG_KERNEL_WRITE | ZE_IMAGE_FLAG_BIAS_UNCACHED),//0, //
					ZE_IMAGE_TYPE_2D,
					{format,
					ZE_IMAGE_FORMAT_TYPE_UNORM,//ZE_IMAGE_FORMAT_TYPE_UINT,
					 ZE_IMAGE_FORMAT_SWIZZLE_R, ZE_IMAGE_FORMAT_SWIZZLE_G,
					 ZE_IMAGE_FORMAT_SWIZZLE_B, ZE_IMAGE_FORMAT_SWIZZLE_A},
					(uint64_t)tex_desc.Width,
					(uint32_t)tex_desc.Height,
					1, 1,1 };//0, 0 };
	//DstRegion = { 0, 0, 0, (uint32_t)Width, (uint32_t)Height, 1 };

	ze_image_handle_t image = nullptr;
	zeImageCreate(l0_ctx, l0_dev, &desc, &image);
	return { image, format, (size_t)tex_desc.Width, (size_t)tex_desc.Height, handle };
}

// texture to a l0 image (no de-tiling)
bool DPCPPCompute::share_image_l0(unsigned int dst_dev, l0_image_t& dst, const l0_image_t& src) {
	// try to share texture memory with l0 device and see if it crashes
	if (src.shared_handle == nullptr) return false;
	// l0 datastrucutres
	ze_image_format_layout_t format = src.format;

	ze_external_memory_import_win32_handle_t shared_desc =
	{
	  ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMPORT_WIN32,
	  nullptr, // pNext
	  ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32,//ZE_EXTERNAL_MEMORY_TYPE_FLAG_D3D11_TEXTURE,//
	  src.shared_handle,
	  nullptr //name
	};
	// TODO: currently ZE_IMAGE_FLAG_BIAS_UNCACHED and removal of ZE_IMAGE_FLAG_KERNEL_WRITE have no effect
	// tuning maybe needed in future 
	ze_image_desc_t desc = { ZE_STRUCTURE_TYPE_IMAGE_DESC,
					&shared_desc,		// pNext
					ZE_IMAGE_FLAG_KERNEL_WRITE,//ZE_IMAGE_FLAG_BIAS_UNCACHED,//(ZE_IMAGE_FLAG_KERNEL_WRITE | ZE_IMAGE_FLAG_BIAS_UNCACHED),//0, //
					ZE_IMAGE_TYPE_2D,
					{format,
					ZE_IMAGE_FORMAT_TYPE_UNORM,//ZE_IMAGE_FORMAT_TYPE_UINT,
					 ZE_IMAGE_FORMAT_SWIZZLE_R, ZE_IMAGE_FORMAT_SWIZZLE_G,
					 ZE_IMAGE_FORMAT_SWIZZLE_B, ZE_IMAGE_FORMAT_SWIZZLE_A},
					(uint64_t)src.width,
					(uint32_t)src.height,
					1, 1,1 };//0, 0 };
	//DstRegion = { 0, 0, 0, (uint32_t)Width, (uint32_t)Height, 1 };

	ze_image_handle_t image = nullptr;
	ze_result_t res = zeImageCreate(l0_ctx, l0_devs[dst_dev], &desc, &image);
	if (image == nullptr) return false;

	dst.image = image;
	dst.format = format;
	dst.width = src.width;
	dst.height = dst.height;
	dst.shared_handle = src.shared_handle;
	return true;
}

void* DPCPPCompute::malloc_dev(unsigned int dev_idx, size_t size) {
	void* ptr = nullptr;
	ze_device_mem_alloc_desc_t desc = { ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC, nullptr, ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_CACHED | ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_INITIAL_PLACEMENT , 0 };
	zeMemAllocDevice(l0_ctx, &desc, size, 1, l0_devs[dev_idx], &ptr);
	return ptr;
}

void* DPCPPCompute::malloc_dev_shared(size_t size) {
	void* ptr = nullptr;
	ze_device_mem_alloc_desc_t desc = { ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC,
		nullptr,
		ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_CACHED , 0 };
	// Set up the request for an exportable allocation
	ze_external_memory_export_desc_t export_desc = {
		ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_DESC,
		nullptr, // pNext
		ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32,                  ///< an NT handle

	};
	desc.pNext = &export_desc;

	zeMemAllocDevice(l0_ctx, &desc, size, 1, l0_dev, &ptr);
	return ptr;
}

void* DPCPPCompute::get_shared_handle(void* alloc_ptr) {
	ze_memory_allocation_properties_t	props{};
	ze_external_memory_export_win32_handle_t export_props{
		ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_WIN32,
		nullptr,	//pNext
		ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32,
		nullptr		//handle
	};

	// Link the export request into the query
	props.pNext = &export_props;
	ze_result_t res = zeMemGetAllocProperties(l0_ctx, alloc_ptr, &props, nullptr);
	if (export_props.handle == nullptr) {
		std::cerr << "Failed to export win32 allocation." << std::endl;
	}
	return export_props.handle;
}

void* DPCPPCompute::malloc_dev_shared(unsigned int dev_idx, size_t size) {
	void* ptr = nullptr;
	ze_device_mem_alloc_desc_t desc = { ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC, 
		nullptr, 
		ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_CACHED , 0 };
	// Set up the request for an exportable allocation
	ze_external_memory_export_desc_t export_desc = {
		ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_DESC,
		nullptr, // pNext
		ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32,                  ///< an NT handle

	};
	desc.pNext = &export_desc;

	zeMemAllocDevice(l0_ctx, &desc, size, 1, l0_devs[dev_idx], &ptr);
	return ptr;
}

void* DPCPPCompute::import_dev_shared(unsigned int dev_idx, void* peer_ptr, size_t size) {
	ze_memory_allocation_properties_t	props{};
	ze_external_memory_export_win32_handle_t export_props{
		ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_EXPORT_WIN32,
		nullptr,	//pNext
		ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32,
		nullptr		//handle
	};

	// Link the export request into the query
	props.pNext = &export_props;
	ze_result_t res = zeMemGetAllocProperties(l0_ctx, peer_ptr, &props, nullptr);
	if (export_props.handle == nullptr) {
		std::cerr<<"Failed to export win32 allocation."<<std::endl;
		return nullptr;
	}

	// create allocation on target device
	ze_device_mem_alloc_desc_t desc = { 
		ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC, 
		nullptr, 
		0 & ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_CACHED , 0 };
	ze_external_memory_import_win32_handle_t shared_desc =
	{
		ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMPORT_WIN32,
		nullptr, // pNext
		ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32,
		export_props.handle,
		nullptr //name
	};

	desc.pNext = &shared_desc;

	void* ptr = nullptr;
	// Crashes!!!
	res = zeMemAllocDevice(l0_ctx, &desc, size, 1, l0_devs[dev_idx], &ptr);
	return ptr;
}
void* DPCPPCompute::malloc_host(size_t size) {
	void* ptr = nullptr;
	ze_host_mem_alloc_desc_t hdesc = { ZE_STRUCTURE_TYPE_HOST_MEM_ALLOC_DESC, nullptr, ZE_HOST_MEM_ALLOC_FLAG_BIAS_CACHED };
	ze_result_t res = zeMemAllocHost(l0_ctx, &hdesc, size, 1, &ptr);
	return ptr;
}
void* DPCPPCompute::malloc_shared(size_t size) {
	void* ptr = nullptr;
	ze_device_mem_alloc_desc_t ddesc = { ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC, nullptr, ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_CACHED | ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_INITIAL_PLACEMENT , 0 };
	ze_host_mem_alloc_desc_t hdesc = { ZE_STRUCTURE_TYPE_HOST_MEM_ALLOC_DESC, nullptr, ZE_HOST_MEM_ALLOC_FLAG_BIAS_CACHED };
	ze_result_t res = zeMemAllocShared(l0_ctx, &ddesc, &hdesc, size, 1, nullptr, &ptr);
	return ptr;
}

void* DPCPPCompute::malloc_shared(unsigned int dev_idx, size_t size) {
	void* ptr = nullptr;
	ze_device_mem_alloc_desc_t ddesc = { ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC, nullptr, ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_CACHED | ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_INITIAL_PLACEMENT , 0 };
	ze_host_mem_alloc_desc_t hdesc = { ZE_STRUCTURE_TYPE_HOST_MEM_ALLOC_DESC, nullptr, ZE_HOST_MEM_ALLOC_FLAG_BIAS_CACHED };
	ze_result_t res = zeMemAllocShared(l0_ctx, &ddesc, &hdesc, size, 1, l0_devs[dev_idx], &ptr);
	return ptr;
}
void DPCPPCompute::make_resident(unsigned int dev_idx, void* ptr, size_t size) {
	ze_result_t res = zeContextMakeMemoryResident(l0_ctx, l0_devs[dev_idx], ptr, size);
}
void DPCPPCompute::evict(unsigned int dev_idx, void* ptr, size_t size) {
	ze_result_t res = zeContextEvictMemory(l0_ctx, l0_devs[dev_idx], ptr, size);
}
void DPCPPCompute::memcopy(void* dstptr, const void* srcptr, size_t size, unsigned int dev_idx) {
	ze_result_t res;
	ze_command_list_handle_t commands = l0_commands;
	ze_command_queue_handle_t queue = l0_q;
	if (l0_dma_coms.size() > 0 && dev_idx< l0_dma_coms.size()) { // multi-GPU mode
		commands = l0_dma_coms[dev_idx];
		queue = l0_dma_qs[dev_idx];
	} 
	res = zeCommandListReset(commands);
	res = zeCommandListAppendMemoryCopy(commands, dstptr, srcptr, size, nullptr, 0, nullptr);
	//res = zeCommandListAppendMemoryCopyFromContext(commands, dstptr, l0_ctx, srcptr, size, nullptr, 0, nullptr);
	res = zeCommandListAppendBarrier(commands, nullptr, 0, nullptr); // Do we need a barrier at all?

	// Close the command list, which is ready to be executed by a command queue.
	res = zeCommandListClose(commands);
	res = zeCommandQueueExecuteCommandLists(queue, 1, &commands, nullptr);
	res = zeCommandQueueSynchronize(queue, UINT64_MAX);
	//ATTN: experiment
	//res = zeContextSystemBarrier(l0_ctx, l0_devs[1]);
}

void DPCPPCompute::image_to_linear_l0(l0_image_t& src, void* dst) {
	ze_result_t res;
	res = zeCommandListReset(l0_commands);
	ze_image_region_t src_region = { 0, 0, 0, (uint32_t)src.width, (uint32_t)src.height, 1 }; // 3 origins, 3 sizes
	ze_image_handle_t planeYImageView = nullptr;
	ze_image_handle_t planeUVImageView = nullptr;

	unsigned int c = 0;
	unsigned int c_sz = 0;
	unsigned int p_cnt = src.planes(c, c_sz); // planes and ch size in bytes
	assert(c_sz <= 2); // 8-bit and 16-bit components for now
	assert(c > 0);

	if (p_cnt == 1) {
		res = zeCommandListAppendImageCopyToMemory(l0_commands, dst, src.image, &src_region, nullptr, 0, nullptr);
	} else {
		ze_image_format_layout_t planeYImageLayout = c_sz < 2 ? ZE_IMAGE_FORMAT_LAYOUT_8 : ZE_IMAGE_FORMAT_LAYOUT_16;
		ze_image_format_layout_t planeUVImageLayout = c_sz < 2 ? ZE_IMAGE_FORMAT_LAYOUT_8_8 : ZE_IMAGE_FORMAT_LAYOUT_16_16;

		ze_image_view_planar_exp_desc_t planeYdesc = {};
		planeYdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXP_DESC;
		planeYdesc.planeIndex = 0; // Y plane

		ze_image_desc_t imageViewDescPlaneY = { ZE_STRUCTURE_TYPE_IMAGE_DESC,
											   &planeYdesc,
											   ZE_IMAGE_FLAG_BIAS_UNCACHED,//(ZE_IMAGE_FLAG_KERNEL_WRITE | ZE_IMAGE_FLAG_BIAS_UNCACHED),//0, 
											   ZE_IMAGE_TYPE_2D,
											   {planeYImageLayout, ZE_IMAGE_FORMAT_TYPE_UNORM,//ZE_IMAGE_FORMAT_TYPE_UINT,
												ZE_IMAGE_FORMAT_SWIZZLE_A, ZE_IMAGE_FORMAT_SWIZZLE_B,
												ZE_IMAGE_FORMAT_SWIZZLE_G, ZE_IMAGE_FORMAT_SWIZZLE_R},
												(uint64_t)src.width,
												(uint32_t)src.height,
												1, 0, 0 };
		planeYdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXT_DESC;
		zeImageViewCreateExt(l0_ctx, l0_dev, &imageViewDescPlaneY, src.image, &planeYImageView);
		// create image_view for UV plane
		ze_image_view_planar_exp_desc_t planeUVdesc = {};
		planeUVdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXP_DESC;
		planeUVdesc.planeIndex = 1; // UV plane

		ze_image_desc_t imageViewDescPlaneUV = { ZE_STRUCTURE_TYPE_IMAGE_DESC,
												&planeUVdesc,
												ZE_IMAGE_FLAG_BIAS_UNCACHED,//(ZE_IMAGE_FLAG_KERNEL_WRITE | ZE_IMAGE_FLAG_BIAS_UNCACHED),//0, //
												ZE_IMAGE_TYPE_2D,
												{planeUVImageLayout, ZE_IMAGE_FORMAT_TYPE_UNORM,//ZE_IMAGE_FORMAT_TYPE_UINT,
												 ZE_IMAGE_FORMAT_SWIZZLE_A, ZE_IMAGE_FORMAT_SWIZZLE_B,
												 ZE_IMAGE_FORMAT_SWIZZLE_G, ZE_IMAGE_FORMAT_SWIZZLE_R},
												(uint64_t)src.width / 2,
												(uint32_t)src.height / 2,
												1, 0, 0 };
		planeUVdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXT_DESC;
		zeImageViewCreateExt(l0_ctx, l0_dev, &imageViewDescPlaneUV, src.image, &planeUVImageView);

		ze_image_region_t& srcYRegion = src_region;
		ze_image_region_t srcUVRegion = { 0, 0, 0, (uint32_t)src.width / 2, (uint32_t)src.height / 2, 1 };

		//// Append command list: copy Y from host memory to device memory.
		//zeCommandListAppendImageCopyFromMemory(commands, planeYImageView, pSrc, &srcYRegion, nullptr, 0, nullptr);
		//// Append command list: copy UV from host memory to device memory.
		//zeCommandListAppendImageCopyFromMemory(commands, planeUVImageView, pSrc + Width * Height, &srcUVRegion, nullptr, 0, nullptr);

		// Append command list: copy Y from video memory to system memory.
		res = zeCommandListAppendImageCopyToMemory(l0_commands, dst, planeYImageView, &srcYRegion, nullptr, 0, nullptr);
		// Append command list: copy UV from video memory to system memory.
		res = zeCommandListAppendImageCopyToMemory(l0_commands, reinterpret_cast<char*>(dst)+src.plane_size(0), planeUVImageView, &srcUVRegion, nullptr, 0, nullptr);
	}

	res = zeCommandListAppendBarrier(l0_commands, nullptr, 0, nullptr); // Do we need a barrier at all?

	// Close the command list, which is ready to be executed by a command queue.
	res = zeCommandListClose(l0_commands);
	res = zeCommandQueueExecuteCommandLists(l0_q, 1, &(l0_commands), nullptr);
	res = zeCommandQueueSynchronize(l0_q, UINT64_MAX);

	if (planeYImageView != nullptr) res = zeImageDestroy(planeYImageView);
	if (planeUVImageView != nullptr) res = zeImageDestroy(planeUVImageView);
	//return ZE_RESULT_SUCCESS;
}

void DPCPPCompute::image_to_linear_l0(l0_image_t* src, unsigned int src_num, void* dst) {
	tmp_views.resize(0);
	tmp_views.reserve(src_num * 2); // potentially all images can be 2-plane

	ze_result_t res;
	res = zeCommandListReset(l0_commands);
	ze_image_handle_t* tmp = nullptr;

	size_t bytes_copied = 0;
	for(unsigned int i=0; i<src_num; i++) {
		unsigned int c = 0;
		unsigned int c_sz = 0;
		unsigned int p_cnt = src[i].planes(c, c_sz); // planes and ch size in bytes
		assert(c_sz <= 2); // 8-bit and 16-bit components for now
		assert(c > 0);

		ze_image_region_t src_region = { 0, 0, 0, (uint32_t)src[i].width, (uint32_t)src[i].height, 1}; // 3 origins, 3 sizes
		if (p_cnt == 1) {
			res = zeCommandListAppendImageCopyToMemory(l0_commands, reinterpret_cast<char*>(dst)+bytes_copied, src[i].image, &src_region, nullptr, 0, nullptr);
		} else {
			ze_image_handle_t planeYImageView = nullptr;
			ze_image_handle_t planeUVImageView = nullptr;
			ze_image_format_layout_t planeYImageLayout = c_sz < 2 ? ZE_IMAGE_FORMAT_LAYOUT_8 : ZE_IMAGE_FORMAT_LAYOUT_16;
			ze_image_format_layout_t planeUVImageLayout = c_sz < 2 ? ZE_IMAGE_FORMAT_LAYOUT_8_8: ZE_IMAGE_FORMAT_LAYOUT_16_16;

			ze_image_view_planar_exp_desc_t planeYdesc = {};
			planeYdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXP_DESC;
			planeYdesc.planeIndex = 0; // Y plane

			ze_image_desc_t imageViewDescPlaneY = { ZE_STRUCTURE_TYPE_IMAGE_DESC,
												   &planeYdesc,
												   ZE_IMAGE_FLAG_BIAS_UNCACHED,//(ZE_IMAGE_FLAG_KERNEL_WRITE | ZE_IMAGE_FLAG_BIAS_UNCACHED),//0, 
												   ZE_IMAGE_TYPE_2D,
												   {planeYImageLayout, ZE_IMAGE_FORMAT_TYPE_UNORM,//ZE_IMAGE_FORMAT_TYPE_UINT,
													ZE_IMAGE_FORMAT_SWIZZLE_A, ZE_IMAGE_FORMAT_SWIZZLE_B,
													ZE_IMAGE_FORMAT_SWIZZLE_G, ZE_IMAGE_FORMAT_SWIZZLE_R},
													(uint64_t)src[i].width,
													(uint32_t)src[i].height,
													1, 0, 0 };
			planeYdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXT_DESC;
			zeImageViewCreateExt(l0_ctx, l0_dev, &imageViewDescPlaneY, src[i].image, &planeYImageView);

			// create image_view for UV plane
			ze_image_view_planar_exp_desc_t planeUVdesc = {};
			planeUVdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXP_DESC;
			planeUVdesc.planeIndex = 1; // UV plane

			ze_image_desc_t imageViewDescPlaneUV = { ZE_STRUCTURE_TYPE_IMAGE_DESC,
													&planeUVdesc,
													ZE_IMAGE_FLAG_BIAS_UNCACHED,//(ZE_IMAGE_FLAG_KERNEL_WRITE | ZE_IMAGE_FLAG_BIAS_UNCACHED),//0, //
													ZE_IMAGE_TYPE_2D,
													{planeUVImageLayout, ZE_IMAGE_FORMAT_TYPE_UNORM,//ZE_IMAGE_FORMAT_TYPE_UINT,
													 ZE_IMAGE_FORMAT_SWIZZLE_A, ZE_IMAGE_FORMAT_SWIZZLE_B,
													 ZE_IMAGE_FORMAT_SWIZZLE_G, ZE_IMAGE_FORMAT_SWIZZLE_R},
													(uint64_t)src[i].width / 2,
													(uint32_t)src[i].height / 2,
													1, 0, 0 };
			planeUVdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXT_DESC;
			zeImageViewCreateExt(l0_ctx, l0_dev, &imageViewDescPlaneUV, src[i].image, &planeUVImageView);

			ze_image_region_t& srcYRegion = src_region;
			ze_image_region_t srcUVRegion = { 0, 0, 0, (uint32_t)src[i].width / 2, (uint32_t)src[i].height / 2, 1 };

			//// Append command list: copy Y from host memory to device memory.
			//zeCommandListAppendImageCopyFromMemory(commands, planeYImageView, pSrc, &srcYRegion, nullptr, 0, nullptr);
			//// Append command list: copy UV from host memory to device memory.
			//zeCommandListAppendImageCopyFromMemory(commands, planeUVImageView, pSrc + Width * Height, &srcUVRegion, nullptr, 0, nullptr);

			// Append command list: copy Y from video memory to system memory.
			res = zeCommandListAppendImageCopyToMemory(l0_commands, reinterpret_cast<char*>(dst) + bytes_copied, planeYImageView, &srcYRegion, nullptr, 0, nullptr);
			// Append command list: copy UV from video memory to system memory.
			res = zeCommandListAppendImageCopyToMemory(l0_commands, reinterpret_cast<char*>(dst) + bytes_copied + src[i].plane_size(0), planeUVImageView, &srcUVRegion, nullptr, 0, nullptr);

			tmp_views.push_back(planeYImageView);
			tmp_views.push_back(planeUVImageView);
		}

		bytes_copied += src[i].size();

	}

	res = zeCommandListAppendBarrier(l0_commands, nullptr, 0, nullptr); // Do we need a barrier at all?

	// Close the command list, which is ready to be executed by a command queue.
	res = zeCommandListClose(l0_commands);
	res = zeCommandQueueExecuteCommandLists(l0_q, 1, &(l0_commands), nullptr);
	res = zeCommandQueueSynchronize(l0_q, UINT64_MAX);

	for (auto& tmp_view: tmp_views) {
		res = zeImageDestroy(tmp_view);
		tmp_view = nullptr;
	}
	tmp_views.resize(0);
}

void DPCPPCompute::image_Y_UV_to_linear_l0(l0_image_t* src, unsigned int src_num, void* dst) {
	ze_result_t res;
	res = zeCommandListReset(l0_commands);
	ze_image_handle_t* tmp = nullptr;
	unsigned int nv12_num = 0;

	size_t bytes_copied = 0;
	// copy Ys
	for (unsigned int i = 0; i < src_num; i++) {
		unsigned int c = 0;
		unsigned int c_sz = 0;
		unsigned int p_cnt = src[i].planes(c, c_sz); // planes and ch size in bytes
		assert(c_sz <= 2); // 8-bit and 16-bit components for now
		assert(c == 1);
		assert(p_cnt == 1);
		size_t width_Y = src[i].width;
		size_t height_Y = (src[i].height / 3) * 2;
		ze_image_region_t src_region = { 0, 0, 0, (uint32_t)width_Y, (uint32_t)height_Y, 1 }; // 3 origins, 3 sizes
		res = zeCommandListAppendImageCopyToMemory(l0_commands, reinterpret_cast<char*>(dst) + bytes_copied, src[i].image, &src_region, nullptr, 0, nullptr);

		bytes_copied += width_Y*height_Y*c_sz;

	}
	// TODO: optimize a little bit to copy U&V in one loop
	// copy Us
	for (unsigned int i = 0; i < src_num; i++) {
		unsigned int c = 0;
		unsigned int c_sz = 0;
		unsigned int p_cnt = src[i].planes(c, c_sz); // planes and ch size in bytes
		assert(c_sz <= 2); // 8-bit and 16-bit components for now
		assert(c == 1);
		assert(p_cnt == 1);
		size_t height_Y = (src[i].height / 3) * 2;
		size_t width_U = src[i].width / 2;
		size_t height_U = height_Y / 2;
		ze_image_region_t src_region = { 0, height_Y, 0, (uint32_t)width_U, (uint32_t)height_U, 1 }; // 3 origins, 3 sizes
		res = zeCommandListAppendImageCopyToMemory(l0_commands, reinterpret_cast<char*>(dst) + bytes_copied, src[i].image, &src_region, nullptr, 0, nullptr);

		bytes_copied += width_U * height_U * c_sz;

	}

	// copy Vs
	for (unsigned int i = 0; i < src_num; i++) {
		unsigned int c = 0;
		unsigned int c_sz = 0;
		unsigned int p_cnt = src[i].planes(c, c_sz); // planes and ch size in bytes
		assert(c_sz <= 2); // 8-bit and 16-bit components for now
		assert(c == 1);
		assert(p_cnt == 1);
		size_t height_Y = (src[i].height / 3) * 2;
		size_t width_V = src[i].width / 2;
		size_t height_V = height_Y / 2;
		ze_image_region_t src_region = { width_V, height_Y, 0, (uint32_t)width_V, (uint32_t)height_V, 1 }; // 3 origins, 3 sizes
		res = zeCommandListAppendImageCopyToMemory(l0_commands, reinterpret_cast<char*>(dst) + bytes_copied, src[i].image, &src_region, nullptr, 0, nullptr);

		bytes_copied += width_V * height_V * c_sz;

	}

	res = zeCommandListAppendBarrier(l0_commands, nullptr, 0, nullptr); // Do we need a barrier at all?

	// Close the command list, which is ready to be executed by a command queue.
	res = zeCommandListClose(l0_commands);
	res = zeCommandQueueExecuteCommandLists(l0_q, 1, &(l0_commands), nullptr);
	res = zeCommandQueueSynchronize(l0_q, UINT64_MAX);

	//return ZE_RESULT_SUCCESS;
}

void DPCPPCompute::linear_to_image_l0(const void* src, l0_image_t& dst) {
	ze_result_t res;
	res = zeCommandListReset(l0_commands);
	ze_image_region_t dst_region = { 0, 0, 0, (uint32_t)dst.width, (uint32_t)dst.height, 1 }; // 3 origins, 3 sizes
	ze_image_handle_t planeYImageView = nullptr;
	ze_image_handle_t planeUVImageView = nullptr;

	unsigned int c = 0;
	unsigned int c_sz = 0;
	unsigned int p_cnt = dst.planes(c, c_sz); // planes and ch size in bytes
	assert(c_sz <= 2); // 8-bit and 16-bit components for now
	assert(c > 0);

	if (p_cnt == 1) {
		res = zeCommandListAppendImageCopyFromMemory(l0_commands, dst.image, src, &dst_region, nullptr, 0, nullptr);
	} else {
		ze_image_format_layout_t planeYImageLayout = c_sz < 2 ? ZE_IMAGE_FORMAT_LAYOUT_8 : ZE_IMAGE_FORMAT_LAYOUT_16;
		ze_image_format_layout_t planeUVImageLayout = c_sz < 2 ? ZE_IMAGE_FORMAT_LAYOUT_8_8 : ZE_IMAGE_FORMAT_LAYOUT_16_16;

		ze_image_view_planar_exp_desc_t planeYdesc = {};
		planeYdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXP_DESC;
		planeYdesc.planeIndex = 0; // Y plane

		ze_image_desc_t imageViewDescPlaneY = { ZE_STRUCTURE_TYPE_IMAGE_DESC,
											   &planeYdesc,
											   ZE_IMAGE_FLAG_KERNEL_WRITE,//(ZE_IMAGE_FLAG_KERNEL_WRITE | ZE_IMAGE_FLAG_BIAS_UNCACHED),//0, 
											   ZE_IMAGE_TYPE_2D,
											   {planeYImageLayout, ZE_IMAGE_FORMAT_TYPE_UNORM,//ZE_IMAGE_FORMAT_TYPE_UINT,
												ZE_IMAGE_FORMAT_SWIZZLE_A, ZE_IMAGE_FORMAT_SWIZZLE_B,
												ZE_IMAGE_FORMAT_SWIZZLE_G, ZE_IMAGE_FORMAT_SWIZZLE_R},
												(uint64_t)dst.width,
												(uint32_t)dst.height,
												1, 0, 0 };
		planeYdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXT_DESC;
		zeImageViewCreateExt(l0_ctx, l0_dev, &imageViewDescPlaneY, dst.image, &planeYImageView);

		// create image_view for UV plane
		ze_image_view_planar_exp_desc_t planeUVdesc = {};
		planeUVdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXP_DESC;
		planeUVdesc.planeIndex = 1; // UV plane
		ze_image_desc_t imageViewDescPlaneUV = { ZE_STRUCTURE_TYPE_IMAGE_DESC,
												&planeUVdesc,
												ZE_IMAGE_FLAG_KERNEL_WRITE,//(ZE_IMAGE_FLAG_KERNEL_WRITE | ZE_IMAGE_FLAG_BIAS_UNCACHED),//0, 
												ZE_IMAGE_TYPE_2D,
												{planeUVImageLayout, ZE_IMAGE_FORMAT_TYPE_UNORM,//ZE_IMAGE_FORMAT_TYPE_UINT,
												 ZE_IMAGE_FORMAT_SWIZZLE_A, ZE_IMAGE_FORMAT_SWIZZLE_B,
												 ZE_IMAGE_FORMAT_SWIZZLE_G, ZE_IMAGE_FORMAT_SWIZZLE_R},
												(uint64_t)dst.width / 2,
												(uint32_t)dst.height / 2,
												1, 0, 0 };
		zeImageViewCreateExt(l0_ctx, l0_dev, &imageViewDescPlaneUV, dst.image, &planeUVImageView);

		ze_image_region_t& dstYRegion = dst_region;
		ze_image_region_t dstUVRegion = { 0, 0, 0, (uint32_t)dst.width / 2, (uint32_t)dst.height / 2, 1 };

		// Append command list: copy Y from linear memory to image memory.
		zeCommandListAppendImageCopyFromMemory(l0_commands, planeYImageView, src, &dstYRegion, nullptr, 0, nullptr);
		// Append command list: copy UV from linear memory to image memory.
		zeCommandListAppendImageCopyFromMemory(l0_commands, planeUVImageView, reinterpret_cast<const char*>(src) + dst.plane_size(0), &dstUVRegion, nullptr, 0, nullptr);

	}

	res = zeCommandListAppendBarrier(l0_commands, nullptr, 0, nullptr); // Do we need a barrier at all?

	// Close the command list, which is ready to be executed by a command queue.
	res = zeCommandListClose(l0_commands);
	res = zeCommandQueueExecuteCommandLists(l0_q, 1, &(l0_commands), nullptr);
	res = zeCommandQueueSynchronize(l0_q, UINT64_MAX);

	if (planeYImageView != nullptr) res = zeImageDestroy(planeYImageView);
	if (planeUVImageView != nullptr) res = zeImageDestroy(planeUVImageView);

	//return ZE_RESULT_SUCCESS;

}

void DPCPPCompute::linear_to_image_l0(const void* src, l0_image_t& dst, const shared_rect& dst_roi) {
	ze_result_t res;
	res = zeCommandListReset(l0_commands);
	ze_image_region_t dst_region = { std::max(dst_roi.x, 0), std::max(dst_roi.y, 0), 0, 
		std::min((uint32_t)dst_roi.w, (uint32_t)dst.width), std::min((uint32_t)dst_roi.h, (uint32_t)dst.height), 1}; // 3 origins, 3 sizes
	ze_image_handle_t planeYImageView = nullptr;
	ze_image_handle_t planeUVImageView = nullptr;

	unsigned int c = 0;
	unsigned int c_sz = 0;
	unsigned int p_cnt = dst.planes(c, c_sz); // planes and ch size in bytes
	assert(c_sz <= 2); // 8-bit and 16-bit components for now
	assert(c > 0);

	if (p_cnt == 1) {
		res = zeCommandListAppendImageCopyFromMemory(l0_commands, dst.image, src, &dst_region, nullptr, 0, nullptr);
	} else {
		ze_image_format_layout_t planeYImageLayout = c_sz < 2 ? ZE_IMAGE_FORMAT_LAYOUT_8 : ZE_IMAGE_FORMAT_LAYOUT_16;
		ze_image_format_layout_t planeUVImageLayout = c_sz < 2 ? ZE_IMAGE_FORMAT_LAYOUT_8_8 : ZE_IMAGE_FORMAT_LAYOUT_16_16;

		ze_image_view_planar_exp_desc_t planeYdesc = {};
		planeYdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXP_DESC;
		planeYdesc.planeIndex = 0; // Y plane

		ze_image_desc_t imageViewDescPlaneY = { ZE_STRUCTURE_TYPE_IMAGE_DESC,
											   &planeYdesc,
											   ZE_IMAGE_FLAG_KERNEL_WRITE,//(ZE_IMAGE_FLAG_KERNEL_WRITE | ZE_IMAGE_FLAG_BIAS_UNCACHED),//0, 
											   ZE_IMAGE_TYPE_2D,
											   {planeYImageLayout, ZE_IMAGE_FORMAT_TYPE_UNORM,//ZE_IMAGE_FORMAT_TYPE_UINT,
												ZE_IMAGE_FORMAT_SWIZZLE_A, ZE_IMAGE_FORMAT_SWIZZLE_B,
												ZE_IMAGE_FORMAT_SWIZZLE_G, ZE_IMAGE_FORMAT_SWIZZLE_R},
												(uint64_t)dst.width,
												(uint32_t)dst.height,
												1, 0, 0 };
		planeYdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXT_DESC;
		zeImageViewCreateExt(l0_ctx, l0_dev, &imageViewDescPlaneY, dst.image, &planeYImageView);

		// create image_view for UV plane
		ze_image_view_planar_exp_desc_t planeUVdesc = {};
		planeUVdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXP_DESC;
		planeUVdesc.planeIndex = 1; // UV plane
		ze_image_desc_t imageViewDescPlaneUV = { ZE_STRUCTURE_TYPE_IMAGE_DESC,
												&planeUVdesc,
												ZE_IMAGE_FLAG_KERNEL_WRITE,//(ZE_IMAGE_FLAG_KERNEL_WRITE | ZE_IMAGE_FLAG_BIAS_UNCACHED),//0, 
												ZE_IMAGE_TYPE_2D,
												{planeUVImageLayout, ZE_IMAGE_FORMAT_TYPE_UNORM,//ZE_IMAGE_FORMAT_TYPE_UINT,
												 ZE_IMAGE_FORMAT_SWIZZLE_A, ZE_IMAGE_FORMAT_SWIZZLE_B,
												 ZE_IMAGE_FORMAT_SWIZZLE_G, ZE_IMAGE_FORMAT_SWIZZLE_R},
												(uint64_t)dst.width / 2,
												(uint32_t)dst.height / 2,
												1, 0, 0 };
		zeImageViewCreateExt(l0_ctx, l0_dev, &imageViewDescPlaneUV, dst.image, &planeUVImageView);

		ze_image_region_t& dstYRegion = dst_region;
		ze_image_region_t dstUVRegion = { dstYRegion.originX/2, dstYRegion.originY/2, 0, 
			dstYRegion.width/2, dstYRegion.height/2, 1 };
		// Make sure the region maps to UV plane exactly
		assert(0==(dstYRegion.originX & 1)+(dstYRegion.originY & 1)+(dstYRegion.width & 1)+(dstYRegion.height & 1));
		// Append command list: copy Y from linear memory to image memory.
		zeCommandListAppendImageCopyFromMemory(l0_commands, planeYImageView, src, &dstYRegion, nullptr, 0, nullptr);
		// Append command list: copy UV from linear memory to image memory.
		zeCommandListAppendImageCopyFromMemory(l0_commands, planeUVImageView, reinterpret_cast<const char*>(src) + dst.plane_size(0, dstYRegion.width, dstYRegion.height), &dstUVRegion, nullptr, 0, nullptr);

	}

	res = zeCommandListAppendBarrier(l0_commands, nullptr, 0, nullptr); // Do we need a barrier at all?

	// Close the command list, which is ready to be executed by a command queue.
	res = zeCommandListClose(l0_commands);
	res = zeCommandQueueExecuteCommandLists(l0_q, 1, &(l0_commands), nullptr);
	res = zeCommandQueueSynchronize(l0_q, UINT64_MAX);

	if (planeYImageView != nullptr) res = zeImageDestroy(planeYImageView);
	if (planeUVImageView != nullptr) res = zeImageDestroy(planeUVImageView);

	//return ZE_RESULT_SUCCESS;

}

size_t DPCPPCompute::linear_to_image_l0(const void* src, l0_image_t* dst, int num_dst) {
	tmp_views.resize(0);
	tmp_views.reserve(num_dst * 2); // potentially all images can be 2-plane

	size_t bytes_copied = 0;
	const char* src_bytes = reinterpret_cast<const char*>(src);

	ze_result_t res;
	res = zeCommandListReset(l0_commands);

	for (int i = 0; i < num_dst; i++) {
		ze_image_region_t dst_region = { 0, 0, 0, (uint32_t)dst[i].width, (uint32_t)dst[i].height, 1 }; // 3 origins, 3 sizes
		ze_image_handle_t planeYImageView = nullptr;
		ze_image_handle_t planeUVImageView = nullptr;

		unsigned int c = 0;
		unsigned int c_sz = 0;
		unsigned int p_cnt = dst[i].planes(c, c_sz); // planes and ch size in bytes
		assert(c_sz <= 2);	// 8-bit and 16-bit components for now
		assert(c > 0);		// channels

		if (p_cnt == 1) { // single-plane image
			res = zeCommandListAppendImageCopyFromMemory(l0_commands, dst[i].image, src_bytes + bytes_copied, &dst_region, nullptr, 0, nullptr);
		} else {
			ze_image_format_layout_t planeYImageLayout = c_sz < 2 ? ZE_IMAGE_FORMAT_LAYOUT_8 : ZE_IMAGE_FORMAT_LAYOUT_16;
			ze_image_format_layout_t planeUVImageLayout = c_sz < 2 ? ZE_IMAGE_FORMAT_LAYOUT_8_8 : ZE_IMAGE_FORMAT_LAYOUT_16_16;

			ze_image_view_planar_exp_desc_t planeYdesc = {};
			planeYdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXP_DESC;
			planeYdesc.planeIndex = 0; // Y plane
			ze_image_desc_t imageViewDescPlaneY = { ZE_STRUCTURE_TYPE_IMAGE_DESC,
												   &planeYdesc,
												   ZE_IMAGE_FLAG_KERNEL_WRITE,//(ZE_IMAGE_FLAG_KERNEL_WRITE | ZE_IMAGE_FLAG_BIAS_UNCACHED),//0, 
												   ZE_IMAGE_TYPE_2D,
												   {planeYImageLayout, ZE_IMAGE_FORMAT_TYPE_UNORM,//ZE_IMAGE_FORMAT_TYPE_UINT,
													ZE_IMAGE_FORMAT_SWIZZLE_A, ZE_IMAGE_FORMAT_SWIZZLE_B,
													ZE_IMAGE_FORMAT_SWIZZLE_G, ZE_IMAGE_FORMAT_SWIZZLE_R},
													(uint64_t)dst[i].width,
													(uint32_t)dst[i].height,
													1, 0, 0 };
			zeImageViewCreateExt(l0_ctx, l0_dev, &imageViewDescPlaneY, dst[i].image, &planeYImageView);

			// create image_view for UV plane
			ze_image_view_planar_exp_desc_t planeUVdesc = {};
			planeUVdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXP_DESC;
			planeUVdesc.planeIndex = 1; // UV plane
			ze_image_desc_t imageViewDescPlaneUV = { ZE_STRUCTURE_TYPE_IMAGE_DESC,
													&planeUVdesc,
													ZE_IMAGE_FLAG_KERNEL_WRITE,//(ZE_IMAGE_FLAG_KERNEL_WRITE | ZE_IMAGE_FLAG_BIAS_UNCACHED),//0, 
													ZE_IMAGE_TYPE_2D,
													{planeUVImageLayout, ZE_IMAGE_FORMAT_TYPE_UNORM,//ZE_IMAGE_FORMAT_TYPE_UINT,
													 ZE_IMAGE_FORMAT_SWIZZLE_A, ZE_IMAGE_FORMAT_SWIZZLE_B,
													 ZE_IMAGE_FORMAT_SWIZZLE_G, ZE_IMAGE_FORMAT_SWIZZLE_R},
													(uint64_t)dst[i].width / 2,
													(uint32_t)dst[i].height / 2,
													1, 0, 0 };
			zeImageViewCreateExt(l0_ctx, l0_dev, &imageViewDescPlaneUV, dst[i].image, &planeUVImageView);

			ze_image_region_t& dstYRegion = dst_region;
			ze_image_region_t dstUVRegion = { 0, 0, 0, (uint32_t)dst[i].width / 2, (uint32_t)dst[i].height / 2, 1 };

			// Append command list: copy Y from linear memory to image memory.
			zeCommandListAppendImageCopyFromMemory(l0_commands, planeYImageView, src_bytes + bytes_copied, &dstYRegion, nullptr, 0, nullptr);
			// Append command list: copy UV from linear memory to image memory.
			zeCommandListAppendImageCopyFromMemory(l0_commands, planeUVImageView, src_bytes + bytes_copied + dst[i].plane_size(0), &dstUVRegion, nullptr, 0, nullptr);

			tmp_views.push_back(planeYImageView); // save views until copy is done
			tmp_views.push_back(planeUVImageView);
		}

		bytes_copied += dst[i].size();
	}
	res = zeCommandListAppendBarrier(l0_commands, nullptr, 0, nullptr); // Do we need a barrier at all?

	// Close the command list, which is ready to be executed by a command queue.
	res = zeCommandListClose(l0_commands);
	res = zeCommandQueueExecuteCommandLists(l0_q, 1, &(l0_commands), nullptr);
	res = zeCommandQueueSynchronize(l0_q, UINT64_MAX);

	for (auto tmp_view : tmp_views) {
		res = zeImageDestroy(tmp_view);
	}
	tmp_views.resize(0);

	return bytes_copied;

}

size_t DPCPPCompute::linear_to_image_l0(const void* src, l0_image_t* dst, int num_dst, const shared_rect& dst_roi) {
	tmp_views.resize(0);
	tmp_views.reserve(num_dst * 2); // potentially all images can be 2-plane

	size_t bytes_copied = 0;
	const char* src_bytes = reinterpret_cast<const char*>(src);

	ze_result_t res;
	res = zeCommandListReset(l0_commands);

	for (int i = 0; i < num_dst; i++) {
		ze_image_region_t dst_region = { std::max(dst_roi.x, 0), std::max(dst_roi.y, 0), 0,
		std::min((uint32_t)dst_roi.w, (uint32_t)dst[i].width), std::min((uint32_t)dst_roi.h, (uint32_t)dst[i].height), 1}; // 3 origins, 3 sizes

		ze_image_handle_t planeYImageView = nullptr;
		ze_image_handle_t planeUVImageView = nullptr;

		unsigned int c = 0;
		unsigned int c_sz = 0;
		unsigned int p_cnt = dst[i].planes(c, c_sz); // planes and ch size in bytes
		assert(c_sz <= 2);	// 8-bit and 16-bit components for now
		assert(c > 0);		// channels

		if (p_cnt == 1) { // single-plane image
			res = zeCommandListAppendImageCopyFromMemory(l0_commands, dst[i].image, src_bytes + bytes_copied, &dst_region, nullptr, 0, nullptr);
		}
		else {
			ze_image_format_layout_t planeYImageLayout = c_sz < 2 ? ZE_IMAGE_FORMAT_LAYOUT_8 : ZE_IMAGE_FORMAT_LAYOUT_16;
			ze_image_format_layout_t planeUVImageLayout = c_sz < 2 ? ZE_IMAGE_FORMAT_LAYOUT_8_8 : ZE_IMAGE_FORMAT_LAYOUT_16_16;

			ze_image_view_planar_exp_desc_t planeYdesc = {};
			planeYdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXP_DESC;
			planeYdesc.planeIndex = 0; // Y plane
			ze_image_desc_t imageViewDescPlaneY = { ZE_STRUCTURE_TYPE_IMAGE_DESC,
												   &planeYdesc,
												   ZE_IMAGE_FLAG_KERNEL_WRITE,//(ZE_IMAGE_FLAG_KERNEL_WRITE | ZE_IMAGE_FLAG_BIAS_UNCACHED),//0, 
												   ZE_IMAGE_TYPE_2D,
												   {planeYImageLayout, ZE_IMAGE_FORMAT_TYPE_UNORM,//ZE_IMAGE_FORMAT_TYPE_UINT,
													ZE_IMAGE_FORMAT_SWIZZLE_A, ZE_IMAGE_FORMAT_SWIZZLE_B,
													ZE_IMAGE_FORMAT_SWIZZLE_G, ZE_IMAGE_FORMAT_SWIZZLE_R},
													(uint64_t)dst[i].width,
													(uint32_t)dst[i].height,
													1, 0, 0 };
			zeImageViewCreateExt(l0_ctx, l0_dev, &imageViewDescPlaneY, dst[i].image, &planeYImageView);

			// create image_view for UV plane
			ze_image_view_planar_exp_desc_t planeUVdesc = {};
			planeUVdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXP_DESC;
			planeUVdesc.planeIndex = 1; // UV plane
			ze_image_desc_t imageViewDescPlaneUV = { ZE_STRUCTURE_TYPE_IMAGE_DESC,
													&planeUVdesc,
													ZE_IMAGE_FLAG_KERNEL_WRITE,//(ZE_IMAGE_FLAG_KERNEL_WRITE | ZE_IMAGE_FLAG_BIAS_UNCACHED),//0, 
													ZE_IMAGE_TYPE_2D,
													{planeUVImageLayout, ZE_IMAGE_FORMAT_TYPE_UNORM,//ZE_IMAGE_FORMAT_TYPE_UINT,
													 ZE_IMAGE_FORMAT_SWIZZLE_A, ZE_IMAGE_FORMAT_SWIZZLE_B,
													 ZE_IMAGE_FORMAT_SWIZZLE_G, ZE_IMAGE_FORMAT_SWIZZLE_R},
													(uint64_t)dst[i].width / 2,
													(uint32_t)dst[i].height / 2,
													1, 0, 0 };
			zeImageViewCreateExt(l0_ctx, l0_dev, &imageViewDescPlaneUV, dst[i].image, &planeUVImageView);

			ze_image_region_t& dstYRegion = dst_region;
			ze_image_region_t dstUVRegion = { dstYRegion.originX/2, dstYRegion.originY/2, 0, 
				dstYRegion.width/2, dstYRegion.height / 2, 1 };

			// Append command list: copy Y from linear memory to image memory.
			zeCommandListAppendImageCopyFromMemory(l0_commands, planeYImageView, src_bytes + bytes_copied, &dstYRegion, nullptr, 0, nullptr);
			// Append command list: copy UV from linear memory to image memory.
			zeCommandListAppendImageCopyFromMemory(l0_commands, planeUVImageView, src_bytes + bytes_copied + dst[i].plane_size(0, dstYRegion.width, dstYRegion.height), &dstUVRegion, nullptr, 0, nullptr);

			tmp_views.push_back(planeYImageView); // save views until copy is done
			tmp_views.push_back(planeUVImageView);
		}

		bytes_copied += dst[i].size(dst_region.width, dst_region.height);
	}
	res = zeCommandListAppendBarrier(l0_commands, nullptr, 0, nullptr); // Do we need a barrier at all?

	// Close the command list, which is ready to be executed by a command queue.
	res = zeCommandListClose(l0_commands);
	res = zeCommandQueueExecuteCommandLists(l0_q, 1, &(l0_commands), nullptr);
	res = zeCommandQueueSynchronize(l0_q, UINT64_MAX);

	for (auto tmp_view : tmp_views) {
		res = zeImageDestroy(tmp_view);
	}
	tmp_views.resize(0);

	return bytes_copied;

}

size_t DPCPPCompute::linear_to_image_l0(const void* src, l0_image_t* dst, int num_dst, const shared_rect* dst_rois) {
	tmp_views.resize(0);
	tmp_views.reserve(num_dst * 2); // potentially all images can be 2-plane

	size_t bytes_copied = 0;
	const char* src_bytes = reinterpret_cast<const char*>(src);

	ze_result_t res;
	res = zeCommandListReset(l0_commands);

	for (int i = 0; i < num_dst; i++) {
		ze_image_region_t dst_region = { std::max(dst_rois[i].x, 0), std::max(dst_rois[i].y, 0), 0,
		std::min((uint32_t)dst_rois[i].w, (uint32_t)dst[i].width), std::min((uint32_t)dst_rois[i].h, (uint32_t)dst[i].height), 1}; // 3 origins, 3 sizes

		ze_image_handle_t planeYImageView = nullptr;
		ze_image_handle_t planeUVImageView = nullptr;

		unsigned int c = 0;
		unsigned int c_sz = 0;
		unsigned int p_cnt = dst[i].planes(c, c_sz); // planes and ch size in bytes
		assert(c_sz <= 2);	// 8-bit and 16-bit components for now
		assert(c > 0);		// channels

		if (p_cnt == 1) { // single-plane image
			res = zeCommandListAppendImageCopyFromMemory(l0_commands, dst[i].image, src_bytes + bytes_copied, &dst_region, nullptr, 0, nullptr);
		}
		else {
			ze_image_format_layout_t planeYImageLayout = c_sz < 2 ? ZE_IMAGE_FORMAT_LAYOUT_8 : ZE_IMAGE_FORMAT_LAYOUT_16;
			ze_image_format_layout_t planeUVImageLayout = c_sz < 2 ? ZE_IMAGE_FORMAT_LAYOUT_8_8 : ZE_IMAGE_FORMAT_LAYOUT_16_16;

			ze_image_view_planar_exp_desc_t planeYdesc = {};
			planeYdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXP_DESC;
			planeYdesc.planeIndex = 0; // Y plane
			ze_image_desc_t imageViewDescPlaneY = { ZE_STRUCTURE_TYPE_IMAGE_DESC,
												   &planeYdesc,
												   ZE_IMAGE_FLAG_KERNEL_WRITE,//(ZE_IMAGE_FLAG_KERNEL_WRITE | ZE_IMAGE_FLAG_BIAS_UNCACHED),//0, 
												   ZE_IMAGE_TYPE_2D,
												   {planeYImageLayout, ZE_IMAGE_FORMAT_TYPE_UNORM,//ZE_IMAGE_FORMAT_TYPE_UINT,
													ZE_IMAGE_FORMAT_SWIZZLE_A, ZE_IMAGE_FORMAT_SWIZZLE_B,
													ZE_IMAGE_FORMAT_SWIZZLE_G, ZE_IMAGE_FORMAT_SWIZZLE_R},
													(uint64_t)dst[i].width,
													(uint32_t)dst[i].height,
													1, 0, 0 };
			zeImageViewCreateExt(l0_ctx, l0_dev, &imageViewDescPlaneY, dst[i].image, &planeYImageView);

			// create image_view for UV plane
			ze_image_view_planar_exp_desc_t planeUVdesc = {};
			planeUVdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXP_DESC;
			planeUVdesc.planeIndex = 1; // UV plane
			ze_image_desc_t imageViewDescPlaneUV = { ZE_STRUCTURE_TYPE_IMAGE_DESC,
													&planeUVdesc,
													ZE_IMAGE_FLAG_KERNEL_WRITE,//(ZE_IMAGE_FLAG_KERNEL_WRITE | ZE_IMAGE_FLAG_BIAS_UNCACHED),//0, 
													ZE_IMAGE_TYPE_2D,
													{planeUVImageLayout, ZE_IMAGE_FORMAT_TYPE_UNORM,//ZE_IMAGE_FORMAT_TYPE_UINT,
													 ZE_IMAGE_FORMAT_SWIZZLE_A, ZE_IMAGE_FORMAT_SWIZZLE_B,
													 ZE_IMAGE_FORMAT_SWIZZLE_G, ZE_IMAGE_FORMAT_SWIZZLE_R},
													(uint64_t)dst[i].width / 2,
													(uint32_t)dst[i].height / 2,
													1, 0, 0 };
			zeImageViewCreateExt(l0_ctx, l0_dev, &imageViewDescPlaneUV, dst[i].image, &planeUVImageView);

			ze_image_region_t& dstYRegion = dst_region;
			ze_image_region_t dstUVRegion = { dstYRegion.originX / 2, dstYRegion.originY / 2, 0,
				dstYRegion.width / 2, dstYRegion.height / 2, 1 };

			// Append command list: copy Y from linear memory to image memory.
			zeCommandListAppendImageCopyFromMemory(l0_commands, planeYImageView, src_bytes + bytes_copied, &dstYRegion, nullptr, 0, nullptr);
			// Append command list: copy UV from linear memory to image memory.
			zeCommandListAppendImageCopyFromMemory(l0_commands, planeUVImageView, src_bytes + bytes_copied + dst[i].plane_size(0, dstYRegion.width, dstYRegion.height), &dstUVRegion, nullptr, 0, nullptr);

			tmp_views.push_back(planeYImageView); // save views until copy is done
			tmp_views.push_back(planeUVImageView);
		}

		bytes_copied += dst[i].size(dst_region.width, dst_region.height);
	}
	res = zeCommandListAppendBarrier(l0_commands, nullptr, 0, nullptr); // Do we need a barrier at all?

	// Close the command list, which is ready to be executed by a command queue.
	res = zeCommandListClose(l0_commands);
	res = zeCommandQueueExecuteCommandLists(l0_q, 1, &(l0_commands), nullptr);
	res = zeCommandQueueSynchronize(l0_q, UINT64_MAX);

	for (auto tmp_view : tmp_views) {
		res = zeImageDestroy(tmp_view);
	}
	tmp_views.resize(0);

	return bytes_copied;

}

void DPCPPCompute::linear_to_texture(void* src_dpc_linear, ID3D11Texture2D* tex) {
	assert(tex != nullptr);
	shared_resource_t resource(tex);
	l0_image_t l0_image;
	shared_to_l0(resource, l0_image);
	resource.lock(0);  // lock resources if user did not do it
	linear_to_image_l0(src_dpc_linear, l0_image);
	//resource.unlock(0);
	//resource.lock(0);
}

void DPCPPCompute::linear_to_texture(void* src_dpc_linear, ID3D11Texture2D* tex, const shared_rect& dst_roi) {
	assert(tex != nullptr);
	shared_resource_t resource(tex);
	l0_image_t l0_image;
	shared_to_l0(resource, l0_image);
	resource.lock(0); // lock resources if user did not do it
	linear_to_image_l0(src_dpc_linear, l0_image, dst_roi);
	//resource.unlock(0);
}

void DPCPPCompute::linear_planes_to_textures(void* src_dpc_linear, ID3D11Texture2D* tex[2]) {
	assert(tex != nullptr);
	shared_resource_t resources[2] = {};
	l0_image_t	images[2] = {};
	for (int t = 0; t < 2; t++) {
		resources[t].init(tex[t]);
		shared_to_l0(resources[t], images[t]);
		resources[t].lock(0);
	}
	linear_to_image_l0(src_dpc_linear, images, 2);
	//for (int t = 0; t < 2; t++) {
	//	resources[t].unlock(0);
	//}
}

void DPCPPCompute::linear_planes_to_textures(void* src_dpc_linear, ID3D11Texture2D* tex[2], const shared_rect dst_rois[2]) {
	assert(tex != nullptr);
	shared_resource_t resources[2] = {};
	l0_image_t	images[2] = {};
	for (int t = 0; t < 2; t++) {
		resources[t].init(tex[t]);
		shared_to_l0(resources[t], images[t]);
		resources[t].lock(0); // lock resources if user did not do it
	}
	linear_to_image_l0(src_dpc_linear, images, 2, dst_rois);
	//for (int t = 0; t < 2; t++) {
	//	resources[t].unlock(0);
	//}
}

sycl::image<2> DPCPPCompute::image_to_compute(l0_image_t& src, unsigned int pln) {
	ze_result_t res;
	ze_image_region_t src_region = { 0, 0, 0, (uint32_t)src.width, (uint32_t)src.height, 1 }; // 3 origins, 3 sizes

	unsigned int c = 0;
	unsigned int c_sz = 0;
	unsigned int p_cnt = src.planes(c, c_sz); // planes and ch size in bytes
	assert(c_sz <= 2); // 8-bit and 16-bit components for now
	assert(c > 0 );

	const sycl::image_channel_order channel_order[4] = {
		sycl::image_channel_order::r, sycl::image_channel_order::rg, sycl::image_channel_order::rgb, sycl::image_channel_order::rgba
	};
	if (p_cnt == 1) { // ignore pln and create 8-bit 4ch sycl::image from descriptor
		sycl::image_channel_order ch_order = channel_order[c-1];
		sycl::image_channel_type ch_type
			= sycl::image_channel_type::unsigned_int8;
		size_t width = src.width;
		size_t height = src.height;
		sycl::range<2> img_range_2d(width, height);

		constexpr sycl::backend BE
			= sycl::backend::ext_oneapi_level_zero;
		sycl::backend_input_t<BE, sycl::image<2>> image_interop_input{
			src.image,
			ch_order,
			ch_type,
			img_range_2d,
			sycl::ext::oneapi::level_zero::ownership::keep };

		sycl::image<2> image_2d
			= sycl::make_image<BE, 2>(image_interop_input, ctx);
		return image_2d;
	}
	assert(p_cnt == 2); // we don't support other image types
	if (pln == 0) { // Y plane of a planar image
		ze_image_handle_t planeYImageView = nullptr;

		ze_image_format_layout_t planeYImageLayout = c_sz < 2 ? ZE_IMAGE_FORMAT_LAYOUT_8 : ZE_IMAGE_FORMAT_LAYOUT_16;
		ze_image_view_planar_exp_desc_t planeYdesc = {};
		planeYdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXP_DESC;
		planeYdesc.planeIndex = 0; // Y plane

		ze_image_desc_t imageViewDescPlaneY = { ZE_STRUCTURE_TYPE_IMAGE_DESC,
											   &planeYdesc,
											   ZE_IMAGE_FLAG_BIAS_UNCACHED,//(ZE_IMAGE_FLAG_KERNEL_WRITE | ZE_IMAGE_FLAG_BIAS_UNCACHED),//0, 
											   ZE_IMAGE_TYPE_2D,
											   {planeYImageLayout, ZE_IMAGE_FORMAT_TYPE_UNORM,//ZE_IMAGE_FORMAT_TYPE_UINT,
												ZE_IMAGE_FORMAT_SWIZZLE_A, ZE_IMAGE_FORMAT_SWIZZLE_B,
												ZE_IMAGE_FORMAT_SWIZZLE_G, ZE_IMAGE_FORMAT_SWIZZLE_R},
												(uint64_t)src.width,
												(uint32_t)src.height,
												1, 0, 0 };
		planeYdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXT_DESC;
		zeImageViewCreateExt(l0_ctx, l0_dev, &imageViewDescPlaneY, src.image, &planeYImageView);

		sycl::image_channel_order ch_order
			= sycl::image_channel_order::r;
		sycl::image_channel_type ch_type
			= c_sz < 2 ? sycl::image_channel_type::unsigned_int8 : sycl::image_channel_type::unsigned_int16;
		size_t width = src.width;
		size_t height = src.height;
		sycl::range<2> img_range_2d(width, height);

		constexpr sycl::backend BE
			= sycl::backend::ext_oneapi_level_zero;
		sycl::backend_input_t<BE, sycl::image<2>> image_interop_input{
			src.image,
			ch_order,
			ch_type,
			img_range_2d,
			sycl::ext::oneapi::level_zero::ownership::keep };

		sycl::image<2> image_2d
			= sycl::make_image<BE, 2>(image_interop_input, ctx);
		return image_2d;
	}

	assert(pln == 1);

	ze_image_handle_t planeUVImageView = nullptr;

	ze_image_format_layout_t planeUVImageLayout = c_sz < 2 ? ZE_IMAGE_FORMAT_LAYOUT_8_8 : ZE_IMAGE_FORMAT_LAYOUT_16_16;

	// create image_view for UV plane
	ze_image_view_planar_exp_desc_t planeUVdesc = {};
	planeUVdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXP_DESC;
	planeUVdesc.planeIndex = 1; // UV plane

	ze_image_desc_t imageViewDescPlaneUV = { ZE_STRUCTURE_TYPE_IMAGE_DESC,
											&planeUVdesc,
											ZE_IMAGE_FLAG_BIAS_UNCACHED,//(ZE_IMAGE_FLAG_KERNEL_WRITE | ZE_IMAGE_FLAG_BIAS_UNCACHED),//0, //
											ZE_IMAGE_TYPE_2D,
											{planeUVImageLayout, ZE_IMAGE_FORMAT_TYPE_UNORM,//ZE_IMAGE_FORMAT_TYPE_UINT,
												ZE_IMAGE_FORMAT_SWIZZLE_A, ZE_IMAGE_FORMAT_SWIZZLE_B,
												ZE_IMAGE_FORMAT_SWIZZLE_G, ZE_IMAGE_FORMAT_SWIZZLE_R},
											(uint64_t)src.width / 2,
											(uint32_t)src.height / 2,
											1, 0, 0 };
	planeUVdesc.stype = ZE_STRUCTURE_TYPE_IMAGE_VIEW_PLANAR_EXT_DESC;
	zeImageViewCreateExt(l0_ctx, l0_dev, &imageViewDescPlaneUV, src.image, &planeUVImageView);

	sycl::image_channel_order ch_order
		= sycl::image_channel_order::rg;
	sycl::image_channel_type ch_type
		= c_sz < 2 ? sycl::image_channel_type::unsigned_int8 : sycl::image_channel_type::unsigned_int16;
	size_t width = src.width / 2;
	size_t height = src.height / 2;
	sycl::range<2> img_range_2d(width, height);

	constexpr sycl::backend BE
		= sycl::backend::ext_oneapi_level_zero;
	sycl::backend_input_t<BE, sycl::image<2>> image_interop_input{
		src.image,
		ch_order,
		ch_type,
		img_range_2d,
		sycl::ext::oneapi::level_zero::ownership::keep };

	sycl::image<2> image_2d
		= sycl::make_image<BE, 2>(image_interop_input, ctx);
	return image_2d;

	//if (planeYImageView != nullptr) res = zeImageDestroy(planeYImageView);
	//if (planeUVImageView != nullptr) res = zeImageDestroy(planeUVImageView);
	//return ZE_RESULT_SUCCESS;
}


l0_allocation_t DPCPPCompute::buffer_to_l0(ID3D11Buffer* src) {
	// try to cast texture memory as l0 device allocation
	HANDLE handle;
	IDXGIResource1* pResource;
	src->QueryInterface(__uuidof(IDXGIResource1), (void**)&pResource);
	pResource->CreateSharedHandle(NULL,
		DXGI_SHARED_RESOURCE_READ, //| DXGI_SHARED_RESOURCE_WRITE,
		NULL,
		&handle);
	pResource->Release();

	D3D11_BUFFER_DESC desc = {};
	src->GetDesc(&desc);
	size_t tex_byte_sz = desc.ByteWidth;

	void* src_l0_mem = nullptr;

	ze_external_memory_import_win32_handle_t shared_desc =
	{
	  ZE_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMPORT_WIN32,
	  nullptr, // pNext
	  ZE_EXTERNAL_MEMORY_TYPE_FLAG_OPAQUE_WIN32,
	  handle,
	  nullptr //name
	};

	ze_device_mem_alloc_desc_t dev_desc{ };
	dev_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
	dev_desc.flags = 0; //ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_CACHED
	dev_desc.ordinal = 0;
	dev_desc.pNext = &shared_desc;

	ze_result_t res = zeMemAllocDevice(l0_ctx, &dev_desc, tex_byte_sz, 0, l0_dev, &src_l0_mem);
	return { src_l0_mem, tex_byte_sz };
}
#define align_to(addr, a) ((addr +((unsigned int)a-1))&~((unsigned int)a-1))

// all pointers are USM
void DPCPPCompute::test_copy_usm(const void* src_gpu, void* dst_gpu, size_t szbytes, unsigned int dev_idx) {
	// make sure de-tiled version is accessible on gpu
	typedef int T;
	if (dst_gpu != nullptr && src_gpu != nullptr) { // run toy kernel
		sycl::range<1> num_items{ szbytes / sizeof(T) };
		{
			T* d = reinterpret_cast<T*>(dst_gpu);
			const T* s = reinterpret_cast<const T*>(src_gpu);

			if (dev_idx == 0 || dev_idx >= qs.size()) {
				// Submit a command group to the queue by a lambda function that contains the
				// data access permission and device computation (kernel).
				q.submit([&](handler& h) {
					h.parallel_for(num_items, [=](auto i0) {
						//auto i = i0.get_id(0);
						//auto t = s[i];
						//d[i] = t;
						d[i0] = s[i0];
						});
					});
				// Wait until compute tasks on GPU done
				q.wait();
			}
			else {
				// Submit a command group to the queue by a lambda function that contains the
				// data access permission and device computation (kernel).
				qs[dev_idx].submit([&](handler& h) {
					h.parallel_for(num_items, [=](auto i0) {
						//auto i = i0.get_id(0);
						//auto t = s[i];
						//d[i] = t;
						d[i0] = s[i0];
						});
					});
				// Wait until compute tasks on GPU done
				qs[dev_idx].wait();

			}
		}
	}

}

void DPCPPCompute::test_copy(const void* src_gpu, void* dst, size_t szbytes, unsigned int dev_idx) {
	// make sure de-tiled version is accessible on gpu
	typedef int T;
	if (dst != nullptr && src_gpu !=nullptr) { // run toy kernel
		sycl::range<1> num_items{ szbytes / sizeof(T) };
		{
			sycl::buffer<T, 1> dst_buf((T*)(dst), num_items);
			const T* s = reinterpret_cast<const T*>(src_gpu);

			//for (size_t i = 0; i < num_repetitions; i++) {
			if (dev_idx == 0 || dev_idx >= qs.size()) {
				// Submit a command group to the queue by a lambda function that contains the
				// data access permission and device computation (kernel).
				q.submit([&](handler& h) {
					// Create an accessor for each buffer with access permission: read, write or
					// read/write. The accessor is a mean to access the memory in the buffer.
					accessor d(dst_buf, h, write_only, no_init);
					// convert Y plane
					h.parallel_for(num_items, [=](auto i0) {
						//auto i = i0.get_id(0);
						//auto t = s[i];
						//d[i] = t;
						d[i0] = s[i0];
						});
					});
				// Wait until compute tasks on GPU done
				q.wait();
			}
			else {
				// Submit a command group to the queue by a lambda function that contains the
				// data access permission and device computation (kernel).
				qs[dev_idx].submit([&](handler& h) {
					// Create an accessor for each buffer with access permission: read, write or
					// read/write. The accessor is a mean to access the memory in the buffer.
					accessor d(dst_buf, h, write_only, no_init);
					// convert Y plane
					h.parallel_for(num_items, [=](auto i0) {
						//auto i = i0.get_id(0);
						//auto t = s[i];
						//d[i] = t;
						d[i0] = s[i0];
						});
					});
				// Wait until compute tasks on GPU done
				qs[dev_idx].wait();

			}
		}
	}

}
void DPCPPCompute::test_copy(l0_image_t& src, void* dst) {
	// make sure de-tiled version is accessible on gpu
	typedef int T;
	if (gpu_linear == nullptr) gpu_linear = sycl::malloc_device(src.size(), q);
	// de-tile to gpu memory
	image_to_linear_l0(src, gpu_linear);
	if(dst!=nullptr) { // run toy kernel
		sycl::range<1> num_items{src.size() / sizeof(T)};
		{
			sycl::buffer<T, 1> dst_buf((T*)(dst), num_items);
			T* s = reinterpret_cast<T*>(gpu_linear);

			//for (size_t i = 0; i < num_repetitions; i++) {

				// Submit a command group to the queue by a lambda function that contains the
				// data access permission and device computation (kernel).
			q.submit([&](handler& h) {
				// Create an accessor for each buffer with access permission: read, write or
				// read/write. The accessor is a mean to access the memory in the buffer.
				accessor d(dst_buf, h, write_only , no_init);
				// convert Y plane
				h.parallel_for(num_items, [=](auto i0) {
					//auto i = i0.get_id(0);
					//auto t = s[i];
					//d[i] = t;
					d[i0] = s[i0];
					});
				});
			// Wait until compute tasks on GPU done
			q.wait();
		}
	}
}

void DPCPPCompute::test_copy(l0_image_t& src, std::vector<char>& dst, bool via_gpu) {
	// make sure de-tiled version is accessible on gpu
	typedef int T;
	if (dst.size() < src.size()) {
		dst.resize(src.size());
		//memset(dst.data(), 0, dst.size() * sizeof(dst[0]));
	}
	if (via_gpu) {
		if(gpu_linear == nullptr) gpu_linear = sycl::malloc_device(src.size(), q);
		// de-tile to gpu memory
		image_to_linear_l0(src, gpu_linear);

		sycl::range<1> num_items{src.size() / sizeof(T)};
		{
			sycl::buffer<T, 1> dst_buf((T*)(dst.data()), num_items);
			T* s = reinterpret_cast<T*>(gpu_linear);

			//for (size_t i = 0; i < num_repetitions; i++) {

				// Submit a command group to the queue by a lambda function that contains the
				// data access permission and device computation (kernel).
			q.submit([&](handler& h) {
				// Create an accessor for each buffer with access permission: read, write or
				// read/write. The accessor is a mean to access the memory in the buffer.
				accessor d(dst_buf, h, write_only, no_init);
				// convert Y plane
				h.parallel_for(num_items, [=](auto i0) {
					//auto i = i0.get_id(0);
					//auto t = s[i];
					//d[i] = t;
					d[i0] = s[i0];
					});
				});
			// Wait until compute tasks on GPU done
			q.wait();
		}
	} else {
		image_to_linear_l0(src, dst.data());
	}
}

void DPCPPCompute::test_copy(l0_allocation_t& src, std::vector<char>& dst) {
	typedef int T;
	if (dst.size() < src.byte_size) {
		dst.resize(src.byte_size);
		//memset(dst.data(), 0, dst.size() * sizeof( dst[0]));
	}
	sycl::range<1> num_items{src.byte_size/sizeof(T)};
	{
		// Create buffers that hold the data shared between the host and the devices.
		// The buffer destructor is responsible to copy the data back to host when it
		// goes out of scope.
		sycl::buffer<T, 1> src_buf = import_buffer<T>(src);
		sycl::buffer<T, 1> dst_buf((T*)(dst.data()), num_items);

		//for (size_t i = 0; i < num_repetitions; i++) {

			// Submit a command group to the queue by a lambda function that contains the
			// data access permission and device computation (kernel).
		q.submit([&](handler& h) {
			// Create an accessor for each buffer with access permission: read, write or
			// read/write. The accessor is a mean to access the memory in the buffer.
			accessor s(src_buf, h, read_only);
			accessor d(dst_buf, h, write_only, no_init);
			// convert Y plane
			h.parallel_for(num_items, [=](auto i) {
				d[i] = s[i];
				});
			});
		// Wait until compute tasks on GPU done
		q.wait();
	}

}

void DPCPPCompute::test_convert_dpcpp(l0_allocation_t& src, std::vector<char>& dst) {
	if (dst.size() < src.byte_size) {
		dst.resize(src.byte_size);
		memset(dst.data(), 0, dst.size() * sizeof(dst[0]));
	}
	unsigned int texw{ 8192 }, texh{ 8192 };
	const unsigned int pixel_size_Y = 1;
	const unsigned int pixel_size_UV = 2;
	// Create the range object for the vectors managed by the buffer.
	sycl::range<2> tex_dim_Y{texw, texh};
	sycl::range<2> tex_dim_UV{texw / 2, texh / 2};
	sycl::range<1> num_items{src.byte_size};

	const unsigned int tile_pitch = align_to(texw * pixel_size_Y, 128) * 32; //128byte x 32rows, always padded
	const unsigned int image_pitch = texw * pixel_size_Y;
	const unsigned int tex_offset_UV = ((texh + (32 - 1)) >> 5) * tile_pitch; //height padded to 32 rows
	const unsigned int image_offset_UV = texh * texw * pixel_size_Y; //height padded to 32 rows
	// Create buffers that hold the data shared between the host and the devices.
	// The buffer destructor is responsible to copy the data back to host when it
	// goes out of scope.
	sycl::buffer<char, 1> src_buf = import_buffer<char>(src);
	sycl::buffer<char, 1> dst_buf((char*)(dst.data()), num_items);

	//for (size_t i = 0; i < num_repetitions; i++) {

		// Submit a command group to the queue by a lambda function that contains the
		// data access permission and device computation (kernel).
	q.submit([&](handler& h) {
		// Create an accessor for each buffer with access permission: read, write or
		// read/write. The accessor is a mean to access the memory in the buffer.
		accessor s(src_buf, h, read_only);
		accessor d(dst_buf, h, write_only, no_init);
		// convert Y plane
		h.parallel_for(tex_dim_Y, [=](id<2> idx) {
			unsigned int x = idx[0];
			unsigned int y = idx[1];
			unsigned int tilex = (x * pixel_size_Y) >> 7; // /128
			unsigned int tiley = y >> 5; // /32
			unsigned int tilex_offs = (x * pixel_size_Y) & 0x7f; // mod 128
			unsigned int tiley_offs = y & 0x1f; // mod 32

			// siwzzle pattern x6x5x4y4y3y2y1y0x3x2x1x0
			int x_offs = (tilex_offs & 0xF) | ((tilex_offs & 0x07) << 5);
			int y_offs = (tiley_offs & 0x1f) << 4;
			int offset = x_offs | y_offs; // + would work too
			unsigned int addr = (tiley * tile_pitch) + (tilex * (128 * 32)) + offset;
			addr = addr ^ ((addr & (1 << 9)) >> 3); //CSX swizzle
			d[y * image_pitch + x * pixel_size_Y] = s[addr];
			});
		/*
		// convert UV plane
		h.parallel_for(tex_dim_UV, [=](id<2> idx) {
			unsigned int x = idx[0];
			unsigned int y = idx[1];
			unsigned int tilex = (x * pixel_size_UV) >> 7; // /128
			unsigned int tiley = y >> 5; // /32
			unsigned int tilex_offs = (x * pixel_size_UV) & 0x7f; // mod 128
			unsigned int tiley_offs = y & 0x1f; // mod 32

			// siwzzle pattern x6x5x4y4y3y2y1y0x3x2x1x0
			int x_offs = (tilex_offs & 0xF) | ((tilex_offs & 0x07) << 5);
			int y_offs = (tiley_offs & 0x1f) << 4;
			int offset = x_offs | y_offs; // + would work too
			unsigned int addr = (tiley * tile_pitch) + (tilex * (128 * 32));
			// addr = addr ^ ((addr & (1 << 9)) >> 3); //CSX swizzle
			d[image_offset_UV + y * image_pitch + x * pixel_size_UV] = s[tex_offset_UV+addr];
			d[image_offset_UV + y * image_pitch + x * pixel_size_UV+1] = s[tex_offset_UV + addr+1];
		});*/
		});
	// Wait until compute tasks on GPU done
	q.wait();

}

void DPCPPCompute::test_convert_cpp(const std::vector<char>& src, std::vector<char>& dst) {
	if (dst.size() < src.size()) {
		dst.resize(src.size());
		memset(dst.data(), 0, dst.size() * sizeof(dst[0]));
	}
	unsigned int texw{ 8192 }, texh{ 8192 };
	const unsigned int pixel_size_Y = 1;
	const unsigned int pixel_size_UV = 2;
	// Create the range object for the vectors managed by the buffer.
	//sycl::range<2> tex_dim_Y{texw, texh};
	//sycl::range<2> tex_dim_UV{texw / 2, texh / 2};
	//sycl::range<1> num_items{src.byte_size};

	const unsigned int tile_pitch = align_to(texw * pixel_size_Y, 128) * 32; //128byte x 32rows, always padded
	const unsigned int image_pitch = texw * pixel_size_Y;
	const unsigned int tex_offset_UV = ((texh + (32 - 1)) >> 5) * tile_pitch; //height padded to 32 rows
	const unsigned int image_offset_UV = texh * texw * pixel_size_Y; //height padded to 32 rows
	// Create buffers that hold the data shared between the host and the devices.
	// The buffer destructor is responsible to copy the data back to host when it
	// goes out of scope.
	//sycl::buffer<char, 1> src_buf = import_buffer<char>(src);
	//sycl::buffer<char, 1> dst_buf((char*)(dst.data()), num_items);

	//for (size_t i = 0; i < num_repetitions; i++) {

		// Submit a command group to the queue by a lambda function that contains the
		// data access permission and device computation (kernel).
	char* d = dst.data();
	const char* s = src.data();
	for(unsigned int y = 0; y<texh; y++)
		for(unsigned int x=0; x<texw; x++) {
			unsigned int tilex = (x * pixel_size_Y) >> 7; // /128
			unsigned int tiley = y >> 5; // /32
			unsigned int tilex_offs = (x * pixel_size_Y) & 0x7f; // mod 128
			unsigned int tiley_offs = y & 0x1f; // mod 32

			// siwzzle pattern x6x5x4y4y3y2y1y0x3x2x1x0
			unsigned int x_offs = (tilex_offs & 0xf) | ((tilex_offs & 0x70) << 5);
			unsigned int y_offs = (tiley_offs & 0x1f) << 4;
			unsigned int offset = x_offs | y_offs; // + would work too
			unsigned int addr = (tiley * tile_pitch) + (tilex * (128 * 32)) + offset;
			//addr = addr ^ ((addr & (1 << 9)) >> 3); //CSX swizzle
			d[y * image_pitch + x * pixel_size_Y] = s[addr];
		}
	for (unsigned int y = 0; y < texh/2; y++)
		for (unsigned int x = 0; x < texw/2; x++) {
			unsigned int tilex = (x * pixel_size_UV) >> 7; // /128
			unsigned int tiley = y >> 5; // /32
			unsigned int tilex_offs = (x * pixel_size_UV) & 0x7f; // mod 128
			unsigned int tiley_offs = y & 0x1f; // mod 32

			// siwzzle pattern x6x5x4y4y3y2y1y0x3x2x1x0
			unsigned int x_offs = (tilex_offs & 0xf) | ((tilex_offs & 0x70) << 5);
			unsigned int y_offs = (tiley_offs & 0x1f) << 4;
			unsigned int offset = x_offs | y_offs; // + would work too
			unsigned int addr = (tiley * tile_pitch) + (tilex * (128 * 32))+offset;
			//addr = addr ^ ((addr & (1 << 9)) >> 3); //CSX swizzle
			d[image_offset_UV + y * image_pitch + x * pixel_size_UV] = s[tex_offset_UV+addr];
			d[image_offset_UV + y * image_pitch + x * pixel_size_UV+1] = s[tex_offset_UV + addr+1];
		}

}
