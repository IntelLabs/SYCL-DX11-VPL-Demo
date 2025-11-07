//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0
//==============================================================================

///
/// Utility library source file for vpl codecs
///
/// @file
#define NOMINMAX
#include "util.h"
#include <assert.h>
#include <d3d11.h>
#include <vector>

#define MAJOR_API_VERSION_REQUIRED 2
#define MINOR_API_VERSION_REQUIRED 2

void mfxBitstreamEx::defragment() {
    if (DataOffset > 0) {
        mfxU8* p0 = Data;
        mfxU8* p1 = Data + DataOffset;
        for (mfxU32 i = 0; i < DataLength; i++) {
            *(p0++) = *(p1++);
        }
        DataOffset = 0;
    }
}

// bistream manipulation
mfxStatus mfxBitstreamEx::append_data(void* append_data, uint32_t append_size) {
    // if a portion of a stream was consumed, move data to 0
    defragment();

    // have data to append?
    if (append_data == nullptr || append_size == 0) return MFX_ERR_NONE;
    if (DataLength + append_size > MaxLength) {
        std::cout << "WARNING: PANIC! PANIC! PANIC! Not enough cucumber!" << std::endl;
        return MFX_ERR_NOT_ENOUGH_BUFFER;
    }
    memcpy(Data + DataLength, append_data, append_size);
    DataLength += append_size;
    return MFX_ERR_NONE;
}

uint32_t mfxBitstreamEx::get_data(void* data_dst, uint32_t data_size) {
    if (data_dst == nullptr) return 0;
    uint32_t bytes2move = std::min(DataLength, data_size);
    if (bytes2move == 0) return 0;

    memcpy(data_dst, Data + DataOffset, bytes2move);
    DataOffset += bytes2move;
    DataLength -= bytes2move;
    defragment();
    return bytes2move;
}

// fill up internal buffer space from the binary stream 
uint32_t mfxBitstreamEx::read_from(std::istream& in, uint32_t max_bytes) {
    defragment();
    uint32_t read_length = std::min(free_length(), max_bytes);
	in.read(reinterpret_cast<char*>(Data+DataLength), read_length); // DataOffset is 0 after defragment
    read_length = (uint32_t)in.gcount();
	DataLength += read_length;
    return read_length;
}

// write available data from internal buffer to the stream 
uint32_t mfxBitstreamEx::write_to(std::ostream& out, uint32_t max_bytes) {
    uint32_t write_length = std::min(buffered_length(), max_bytes);
    out.write(reinterpret_cast<const char*>(Data+DataOffset), write_length);
    if (!out) return 0; // error, don't know what to do
	DataOffset += write_length;
	DataLength -= write_length;
	defragment();
    return write_length;
}

// clean the contents and deallocate buffers
void mfxBitstreamEx::clear() {
    if (Data != nullptr) {
        free(Data);
        Data = nullptr;
    }
    init();
}

// reset pointers, keep the buffer
void mfxBitstreamEx::rewind() {
    DataLength = 0;
    DataOffset = 0;
}

// resize internal buffer if smaller than wanted_size, keep the data&pointers
void mfxBitstreamEx::resize_buf(uint32_t wanted_length) {
    if (MaxLength < wanted_length) {
        // alloc new mem
        mfxU8* new_data = (mfxU8*)calloc(wanted_length, sizeof(mfxU8));
        if (new_data == nullptr) {
            std::cout << "ERROR: Failed to allocate new bit stream buffer of size " << wanted_length << std::endl;
            exit(-1); // exit because it is fatal for now
        }
        // move data to new mem, offset 0
        if (DataLength > 0) {
            assert(Data != nullptr);
            mfxU8* p0 = new_data;
            mfxU8* p1 = Data + DataOffset;
            for (mfxU32 i = 0; i < DataLength; i++) {
                *(p0++) = *(p1++);
            }
        }
        // set the offset&ptr
        DataOffset = 0;
        if (Data != nullptr) free(Data);
        Data = new_data;
		MaxLength = wanted_length;
    }
}
mfxBitstreamEx::~mfxBitstreamEx() {
    clear();
}


mfxStatus EncodedBitstreamSource::rewind() {
    return MFX_ERR_UNSUPPORTED; // by default
}

bool EncodedBitstreamSource::is_none() {
    return false;
}

EncodedBitstreamSource::~EncodedBitstreamSource() {
}

mfxStatus PushModelNoSource::read(mfxBitstream& bs, size_t* bytes_read) {
    return MFX_ERR_MORE_DATA;
}
bool PushModelNoSource::is_open() {
    return true;
}

bool PushModelNoSource::is_none() {
    return true;
}

PushModelNoSource::~PushModelNoSource() {
}


mfxStatus EncodedFileSource::read(mfxBitstream& bs, size_t* bytes_read) {
	if (f==nullptr) return MFX_ERR_NOT_INITIALIZED;
	size_t old_len = bs.DataLength;
	auto sts = ReadEncodedStream(bs, f);
	if (bytes_read != nullptr) *bytes_read = bs.DataLength - old_len;
	return sts;
}

bool EncodedFileSource::is_open() {
	return f != nullptr;
}

mfxStatus EncodedFileSource::rewind() {
	if (f == nullptr) return MFX_ERR_NOT_INITIALIZED;
	// rewind file
	fseek(f, 0, SEEK_SET);
	return MFX_ERR_NONE;
}


EncodedFileSource::~EncodedFileSource() {
	close();
}

EncodedBitstreamSink::~EncodedBitstreamSink() {
}

bool EncodedBitstreamSink::is_none() {
    return false;
}

mfxStatus PullModelNoSink::write(mfxBitstream& bs, size_t* bytes_written) {
    return MFX_ERR_NONE;
}

bool PullModelNoSink::is_open() { return true; }

bool PullModelNoSink::is_none() {
    return true;
}

PullModelNoSink::~PullModelNoSink() {}

EncodedFileSink::~EncodedFileSink() {
    close();
}

bool EncodedFileSink::is_open() {
    return f != nullptr;
}

mfxStatus EncodedFileSink::write(mfxBitstream& bs, size_t* bytes_written) {
    if (f == nullptr) return MFX_ERR_NOT_INITIALIZED;
    size_t old_len = bs.DataLength;
    WriteEncodedStream(bs, f);
    if (bytes_written != nullptr) *bytes_written = old_len - bs.DataLength;
    return MFX_ERR_NONE;
}


ID3D11Device* create_device(unsigned int adapter_num) {
    // find suitable adapter
    IDXGIFactory1* pFactory;
    HRESULT hr;
    if (FAILED(hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)(&pFactory)))) {
        std::cout << "Creating DXGI Factory failed!" << std::endl;
        return nullptr;

    }
    UINT i = 0;
    IDXGIAdapter* pAdapter;
    std::vector <IDXGIAdapter*> vAdapters;
    DXGI_ADAPTER_DESC adapter;
    while (pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND)
    {
        vAdapters.push_back(pAdapter);
        ++i;
        // here you want to enumerate outputs and find the looking glass you want
        pAdapter->GetDesc(&adapter);
    }
    pFactory->Release();

    if (vAdapters.size() <= 0) {
        std::cout << "No adapters found!" << std::endl;
        return nullptr;
    }
    if (adapter_num >= vAdapters.size()) adapter_num = 0;

    D3D_FEATURE_LEVEL  FeatureLevelsRequested = D3D_FEATURE_LEVEL_11_0;
    UINT               numLevelsRequested = 1;
    D3D_FEATURE_LEVEL  FeatureLevelsSupported;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* dev_ctx = nullptr;

    if (FAILED(hr = D3D11CreateDevice(vAdapters[adapter_num], //NULL,
        D3D_DRIVER_TYPE_UNKNOWN, //D3D_DRIVER_TYPE_HARDWARE,
        NULL,
        0, //| D3D11_CREATE_DEVICE_DEBUG, // ATTN:  
        &FeatureLevelsRequested,
        numLevelsRequested,
        D3D11_SDK_VERSION,
        &dev,
        &FeatureLevelsSupported,
        &dev_ctx)))
    {
        std::cout << "Creating DX device failed!" << std::endl;
        return false;
    }

    // set multithreading
    ID3D10Multithread* pD10Multithread = nullptr;

    //dev->GetImmediateContext(&dev_ctx);
    dev_ctx->QueryInterface(__uuidof(ID3D10Multithread), (void**)&pD10Multithread);
    BOOL mt = pD10Multithread->SetMultithreadProtected(true);
    ////DEBUG
    //IDXGIDevice* pDXGIDevice;
    //hr = dev->QueryInterface(__uuidof(IDXGIDevice), (void**)&pDXGIDevice);
    //hr = pDXGIDevice->GetAdapter(&pAdapter);
    //DXGI_ADAPTER_DESC fadapter;
    //pAdapter->GetDesc(&fadapter);
    ////DEBUG END
    return dev;
}

// No casting to actual ID3D..., just checking that type is right
ID3D11Device* get_device(mfxFrameSurface1* surface) {
    mfxHDL  device_handle;
    mfxHandleType device_type;

    mfxStatus s = surface->FrameInterface->GetDeviceHandle(surface, &device_handle, &device_type);
    if (MFX_ERR_NONE == s && device_type == MFX_HANDLE_D3D11_DEVICE)
        return (ID3D11Device*)device_handle;

    std::cout << "Error: can't get DX11 device from video surface!" << std::endl;
    return nullptr;

}

// get surface and retrieve device
// return DX11 device or nullptr otherwise
ID3D11Device* get_decoder_device(mfxSession session) {
    ID3D11Device* dxdev = nullptr;
    mfxHDL  dev = nullptr;
    mfxHandleType dev_type;
    mfxFrameSurface1* dummys = nullptr;
    mfxStatus sts = MFXMemory_GetSurfaceForDecode(session, &dummys);
    if (MFX_ERR_NONE != sts) {
        std::cout << "Cannot retrieve sessions's surface" << std::endl;
        return nullptr;
    }
    if (dummys != nullptr) {
        dxdev = get_device(dummys);
        if (dxdev != nullptr) dxdev->AddRef();
        Release(dummys);
    }
    if (dxdev == nullptr) {
        std::cout << "Cannot retrieve session's DX11 device!" << std::endl;
    }
    return dxdev;
}

// get surface and retrieve device
// return DX11 device or nullptr otherwise
ID3D11Device* get_encoder_device(mfxSession session) {
    ID3D11Device* dxdev = nullptr;
    mfxHDL  dev = nullptr;
    mfxHandleType dev_type;
    mfxFrameSurface1* dummys = nullptr;
    mfxStatus sts = MFXMemory_GetSurfaceForEncode(session, &dummys);
    if (MFX_ERR_NONE != sts) {
        std::cout << "Cannot retrieve sessions's surface" << std::endl;
        return nullptr;
    }
    if (dummys != nullptr) {
        dxdev = get_device(dummys);
        Release(dummys);
    }
    if (dxdev == nullptr) {
        std::cout << "Cannot retrieve session's DX11 device!" << std::endl;
    }
    return dxdev;
}

// loader with request for 
// dec_codecID - decoder type
// impl_hw - HW implementation if true, SW - if false
VplLoader::VplLoader(mfxU32 dec_codecID, bool impl_hw) : dec_codecID(dec_codecID) {
    if (!impl_hw) {
        std::cout << "WARNING: SW decode pipeline requested." << std::endl;
    }
    mfxStatus sts;
    bool isFailed = false;

    loader = MFXLoad();
    VERIFY(nullptr != loader, "MFXCreateConfig failed")

    // Implementation used must be the HW
    cfg[0] = MFXCreateConfig(loader);
    VERIFY(NULL != cfg[0], "MFXCreateConfig failed")
        cfgVal[0].Type = MFX_VARIANT_TYPE_U32;
    cfgVal[0].Data.U32 = impl_hw ? MFX_IMPL_TYPE_HARDWARE : MFX_IMPL_TYPE_SOFTWARE;
    sts = MFXSetConfigFilterProperty(cfg[0], (mfxU8*)"mfxImplDescription.Impl", cfgVal[0]);
    VERIFY(MFX_ERR_NONE == sts, "MFXSetConfigFilterProperty failed for Impl");

    // Implementation must provide codecID decoder
    cfg[1] = MFXCreateConfig(loader);
    VERIFY(NULL != cfg[1], "MFXCreateConfig failed")
        cfgVal[1].Type = MFX_VARIANT_TYPE_U32;
    cfgVal[1].Data.U32 = dec_codecID;//MFX_CODEC_HEVC;
    sts = MFXSetConfigFilterProperty(
        cfg[1],
        (mfxU8*)"mfxImplDescription.mfxDecoderDescription.decoder.CodecID",
        cfgVal[1]);
    VERIFY(MFX_ERR_NONE == sts, "MFXSetConfigFilterProperty failed for decoder CodecID");

    // Implementation must provide an HEVC encoder
    //cfg[1] = MFXCreateConfig(*loader);
    //VERIFY(NULL != cfg[1], "MFXCreateConfig failed")
    //    cfgVal[1].Type = MFX_VARIANT_TYPE_U32;
    //cfgVal[1].Data.U32 = MFX_CODEC_HEVC;
    //sts = MFXSetConfigFilterProperty(cfg[1], (mfxU8*)"mfxImplDescription.mfxEncoderDescription.encoder.CodecID", cfgVal[1]);
    //VERIFY(MFX_ERR_NONE == sts, "MFXSetConfigFilterProperty failed for encoder CodecID");

    // Implementation used must provide API version 2.2 or newer
    cfg[2] = MFXCreateConfig(loader);
    VERIFY(NULL != cfg[2], "MFXCreateConfig failed")
        cfgVal[2].Type = MFX_VARIANT_TYPE_U32;
    cfgVal[2].Data.U32 = VPLVERSION(MAJOR_API_VERSION_REQUIRED, MINOR_API_VERSION_REQUIRED);
    sts = MFXSetConfigFilterProperty(cfg[2],
        (mfxU8*)"mfxImplDescription.ApiVersion.Version",
        cfgVal[2]);
    VERIFY(MFX_ERR_NONE == sts, "MFXSetConfigFilterProperty failed for API version");
    return;
end:
    if (loader) MFXUnload(loader);
    loader = nullptr;
    dec_codecID = 0;
}

// loader with request for 
// dec_codecID - decoder type
// enc_codecID - encoder type
// impl_hw - HW implementation if true, SW - if false
VplLoader::VplLoader(mfxU32 dec_codecID, mfxU32 enc_codecID, bool impl_hw) 
    : dec_codecID(dec_codecID), enc_codecID(enc_codecID){
    if (!impl_hw) {
        std::cout << "WARNING: SW decode pipeline requested." << std::endl;
    }
    mfxStatus sts;
    bool isFailed = false;

    loader = MFXLoad();
    VERIFY(nullptr != loader, "MFXCreateConfig failed")

    // Implementation used must be the HW
    cfg[0] = MFXCreateConfig(loader);
    VERIFY(NULL != cfg[0], "MFXCreateConfig failed")
    cfgVal[0].Type = MFX_VARIANT_TYPE_U32;
    cfgVal[0].Data.U32 = impl_hw ? MFX_IMPL_TYPE_HARDWARE : MFX_IMPL_TYPE_SOFTWARE;
    sts = MFXSetConfigFilterProperty(cfg[0], (mfxU8*)"mfxImplDescription.Impl", cfgVal[0]);
    VERIFY(MFX_ERR_NONE == sts, "MFXSetConfigFilterProperty failed for Impl");

    int cfgIdx = 1;
    if (dec_codecID != 0) {
        // Implementation must provide codecID decoder
        cfg[cfgIdx] = MFXCreateConfig(loader);
        VERIFY(NULL != cfg[cfgIdx], "MFXCreateConfig failed")
            cfgVal[cfgIdx].Type = MFX_VARIANT_TYPE_U32;
        cfgVal[cfgIdx].Data.U32 = dec_codecID;//MFX_CODEC_HEVC;
        sts = MFXSetConfigFilterProperty(
            cfg[cfgIdx],
            (mfxU8*)"mfxImplDescription.mfxDecoderDescription.decoder.CodecID",
            cfgVal[cfgIdx]);
        VERIFY(MFX_ERR_NONE == sts, "MFXSetConfigFilterProperty failed for decoder CodecID");
		cfgIdx++;
    }

    if (enc_codecID != 0) {
        // Implementation must provide enc_codecID encoder
        cfg[cfgIdx] = MFXCreateConfig(loader);
        VERIFY(NULL != cfg[cfgIdx], "MFXCreateConfig failed")
            cfgVal[cfgIdx].Type = MFX_VARIANT_TYPE_U32;
        cfgVal[cfgIdx].Data.U32 = enc_codecID; //MFX_CODEC_HEVC;
        sts = MFXSetConfigFilterProperty(
            cfg[cfgIdx],
            (mfxU8*)"mfxImplDescription.mfxEncoderDescription.encoder.CodecID",
            cfgVal[cfgIdx]);
        VERIFY(MFX_ERR_NONE == sts, "MFXSetConfigFilterProperty failed for encoder CodecID");
        cfgIdx++;
    }

    // Implementation used must provide API version 2.2 or newer
    cfg[cfgIdx] = MFXCreateConfig(loader);
    VERIFY(NULL != cfg[cfgIdx], "MFXCreateConfig failed")
        cfgVal[cfgIdx].Type = MFX_VARIANT_TYPE_U32;
    cfgVal[cfgIdx].Data.U32 = VPLVERSION(MAJOR_API_VERSION_REQUIRED, MINOR_API_VERSION_REQUIRED);
    sts = MFXSetConfigFilterProperty(cfg[cfgIdx],
        (mfxU8*)"mfxImplDescription.ApiVersion.Version",
        cfgVal[cfgIdx]);
    VERIFY(MFX_ERR_NONE == sts, "MFXSetConfigFilterProperty failed for API version");
    cfgIdx++;
    return;
end:
    // comes here on error
    if (loader) MFXUnload(loader);
    loader = nullptr;
    dec_codecID = 0;
    enc_codecID = 0;
}

void list_adapters(bool with_displays) {
    std::vector<unsigned int> iAdapters;
    // find suitable adapter
    IDXGIFactory1* pFactory;
    HRESULT hr;
    if (FAILED(hr = CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)(&pFactory)))) {
        std::cout << "Creating DXGI Factory failed!" << std::endl;
        return;

    }
    std::cout << "Listing Gfx adapters:" << std::endl;
    UINT i = 0;
    IDXGIAdapter* pAdapter;
    //std::vector<IDXGIAdapter*> vAdapters;
    DXGI_ADAPTER_DESC adapter;
    while (pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND)
    {
        //vAdapters.push_back(pAdapter);
        pAdapter->GetDesc(&adapter);
        std::wcout << i << ": " << adapter.Description << std::endl;
        std::cout << "  video mem: " << adapter.DedicatedVideoMemory / 1024 / 1024 << "MB; ";
        std::cout << "shared sys mem: " << adapter.SharedSystemMemory / 1024 / 1024 << "MB; ";
        std::cout << "sys mem: " << adapter.DedicatedSystemMemory / 1024 / 1024 << "MB" << std::endl;
        ++i;
        // here you want to enumerate outputs and find the looking glass you want
        UINT out = 0;
        IDXGIOutput* pOutput;
        DXGI_OUTPUT_DESC output;
        while (pAdapter->EnumOutputs(out, &pOutput) != DXGI_ERROR_NOT_FOUND)
        {
            pOutput->GetDesc(&output);
            std::wcout << "  out " << out << ": " << output.DeviceName << "; ";
            std::cout << (output.AttachedToDesktop ? "DT " : "Non-DT ") << "coord: ";
            std::cout << "(" << output.DesktopCoordinates.left << ", " << output.DesktopCoordinates.top << "); ";
            std::cout << "dim: " << "(" << output.DesktopCoordinates.right - output.DesktopCoordinates.left << ", ";
            std::cout << output.DesktopCoordinates.bottom - output.DesktopCoordinates.top << "); " << std::endl;
            ++out;
            pOutput->Release();
        }
        pAdapter->Release();
    }
    pFactory->Release();
}

