//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0
//==============================================================================

///
/// A oneAPI Video Processing Library (oneVPL) wrapper for decoding/encoding or transcoding,
/// using 2.2 or newer API with internal memory management.
/// For more information see
/// https://oneapi-src.github.io/oneAPI-spec/elements/oneVPL/source/
/// @file
#define NOMINMAX
#include "vplcodec.h"
#include <iostream>
#include <vector>
#include <d3dcompiler.h>
#include "OffscreenDX11.h"

#include <assert.h>

//#define OUTPUT_FILE                "out.raw"
#define BITSTREAM_BUFFER_SIZE      10000000
#define MAJOR_API_VERSION_REQUIRED 2
#define MINOR_API_VERSION_REQUIRED 2
#define SYNC_TIMEOUT               60000

void print_surface_info(mfxFrameSurface1* surface) {
    if (surface == nullptr) {
        std::cout << "WARNING: empty surface in print info." << std::endl;
        return;
    }
    std::cout << "Surface info: W=" << surface->Info.Width << ", H=" << surface->Info.Height << std::endl;
    std::cout << "  ROI=( " << surface->Info.CropX << ", " << surface->Info.CropY<< ", "<< surface->Info.CropW << ", " << surface->Info.CropH << ")"<< std::endl;
    auto tex = VPLCodec::get_texture(surface);
    if (tex == nullptr) {
        std::cout << "  No texture attached to surface." << std::endl;
        return;
    }
    D3D11_TEXTURE2D_DESC desc = {};
    tex->GetDesc(&desc);
    std::cout << "  Texture: W=" << desc.Width << ", H=" << desc.Height << std::endl;
}

// Casting to actual ID3D...after checking that type is right
ID3D11Texture2D* VPLCodec::get_texture(mfxFrameSurface1* surface) {
    mfxHDL resource;
    mfxResourceType resource_type;
    mfxStatus s = surface->FrameInterface->GetNativeHandle(surface, &resource, &resource_type);
    if (MFX_ERR_NONE == s && resource_type == MFX_RESOURCE_DX11_TEXTURE)
        return (ID3D11Texture2D*)resource;
    std::cout << "Error: can't get DX11 texture from video surface!" << std::endl;
    return nullptr;
}


VPLCodec::VPLCodec(rational_u32 fps):fps(fps) {
}



VPLCodec::VPLCodec(std::shared_ptr<VplLoader> shared_loader, rational_u32 fps):fps(fps) {
    loader = shared_loader;
}

VPLCodec::VPLCodec(mfxU32 incodecID, mfxU32 outcodecID, bool impl_hw, rational_u32 fps):fps(fps) {
    if(outcodecID)
        loader = std::make_shared<VplLoader>(incodecID, outcodecID, impl_hw);
    else
        loader = std::make_shared<VplLoader>(incodecID, impl_hw);

    VERIFY(NULL != loader->loader, "MFX implementation configuration failed -- are parameters correct?");
    return;
end:
    loader.reset();
}

ID3D11Texture2D* VPLCodec::get_texture_rgba(mfxFrameSurface1* surfaceNV12) {
    if (offscreen == nullptr) offscreen = new RenderOffscreen();
    if (surfaceNV12 == nullptr) return nullptr;
    ID3D11Texture2D* inNV12 = get_texture(surfaceNV12);
    return offscreen->convert_NV12(inNV12);
}


ID3D11Device* VPLCodec::get_device() {
    // ATTN: what to do when device was internally created?
    return dxdev;
}

// initialize decoder, no VPP
bool VPLCodec::init_decoder(ID3D11Device* device) {
    return init_decoder({ 0,0,0,0 }, { 0,0 }, device);
}

// initialize decoder possibly with VPP
bool VPLCodec::init_decoder(const crop_roi& crop, const size2& out_size, ID3D11Device* device) {
    close();
    isDraining = false;
    isStillGoing = true;
    isFailed = false;

    // Initialize and/or check loader config
    if (loader == nullptr) {
        loader = std::make_shared<VplLoader>(MFX_CODEC_HEVC, MFX_CODEC_HEVC, true); // default configuration
        std::cout << "WARNING: MFX Loader not configured, trying to use default: HEVC->HEVC, HW codec." << std::endl;

    }
    VERIFY(nullptr != loader->loader, "MFX implementation configuration failed -- are parameters correct?");

    // MFXCreateSession has a second parameter that I'm not sure if it refers to the device order number...
    sts = MFXCreateSession(*loader, 0, &session);
    VERIFY(MFX_ERR_NONE == sts,
        "Cannot create session -- no implementations meet selection criteria");
    // Print info about implementation loaded
    ShowImplementationInfo(*loader, 0);

    // ATTN: creating dx device - using DXGI adapter index in enum order
    if (device != nullptr) {
        sts = MFXVideoCORE_SetHandle(session, MFX_HANDLE_D3D11_DEVICE, device);
        VERIFY(MFX_ERR_NONE == sts,
            "Cannot assign sessions's DX11 device");
        dxdev = device;
		dxdev->AddRef();
    }
    // save crop data
    dec_crop = crop;
    dec_size = out_size;

    // Prepare input bitstream and start decoding
    bitstream_in.resize_buf(BITSTREAM_BUFFER_SIZE);
    bitstream_in.rewind();
    bitstream_in.CodecId = loader->dec_codecID;//MFX_CODEC_HEVC;
    bitstream_in.DataFlag = 0; // reset complete frame

    decodeParams.mfx.CodecId = loader->dec_codecID;//MFX_CODEC_HEVC;
    decodeParams.IOPattern = /*MFX_IOPATTERN_IN_SYSTEM_MEMORY|*/  MFX_IOPATTERN_OUT_VIDEO_MEMORY;//MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

end:
    return !isFailed;
}


mfxStatus VPLCodec::init_decoder_deferred0() {
    // see below how to extract device created by oneVPL
    // can be extracted only alfter decoder is created

    // Pre-parse input stream
    sts = MFXVideoDECODE_DecodeHeader(session, &bitstream_in, &decodeParams);
    if (MFX_ERR_NONE != sts) return sts; // Might return MFX_ERR_MORE_DATA - incomplete header

    printf("Output colorspace: ");
    switch (decodeParams.mfx.FrameInfo.FourCC) {
    case MFX_FOURCC_I420: // CPU output
        printf("I420 (aka yuv420p)\n");
        break;
    case MFX_FOURCC_NV12: // GPU output
        printf("NV12\n");
        break;
    case MFX_FOURCC_P010: // GPU output
        printf("P010\n");
        break;
    case MFX_FOURCC_P016: // GPU output
        printf("P016\n");
        break;
    default:
        printf("Unsupported color format\n");
        isFailed = true;
        goto end;
        break;
    }

	// JPEG interlaced handling - convert to progressive by doubling height
    if (decodeParams.mfx.CodecId == MFX_CODEC_JPEG && (decodeParams.mfx.FrameInfo.PicStruct == MFX_PICSTRUCT_FIELD_TFF ||
        decodeParams.mfx.FrameInfo.PicStruct == MFX_PICSTRUCT_FIELD_BFF)) {
        // MJPEG
        decodeParams.mfx.FrameInfo.Height *= 2; // double the height
        bitstream_in.DataFlag = MFX_BITSTREAM_COMPLETE_FRAME;
        //ATTN: what to do with mfx.FrameInfo.CropH ? // double? keep?
    }

    if (0 == decodeParams.mfx.FrameInfo.FrameRateExtN &&
        0 == decodeParams.mfx.FrameInfo.FrameRateExtD) {
        decodeParams.mfx.FrameInfo.FrameRateExtN = fps.N;
        decodeParams.mfx.FrameInfo.FrameRateExtD = fps.D;
    }
    bool no_crop = (dec_crop.empty() || dec_crop.same(decodeParams.mfx.FrameInfo.Width, decodeParams.mfx.FrameInfo.Height));
    bool no_scale = (dec_size.empty() || dec_size.same(decodeParams.mfx.FrameInfo.Width, decodeParams.mfx.FrameInfo.Height));
    if (no_crop && no_scale) {
        numVPPCh = 0;
        // input parameters finished, now initialize decode
        sts = MFXVideoDECODE_Init(session, &decodeParams);
        VERIFY(MFX_ERR_NONE == sts, "Error initializing decode\n");

    }
    else { // VPP crop, scale
        numVPPCh = 1;
        mfxVPPChParams = new mfxVideoChannelParam * [numVPPCh];
        for (mfxU32 i = 0; i < numVPPCh; i++) {
            mfxVPPChParams[i] = new mfxVideoChannelParam;
            memset(mfxVPPChParams[i], 0, sizeof(mfxVideoChannelParam));
        }
        crop_roi final_crop = { dec_crop.x, dec_crop.y,
            dec_crop.w > 0 ? dec_crop.w : decodeParams.mfx.FrameInfo.CropW,
            dec_crop.h > 0 ? dec_crop.h : decodeParams.mfx.FrameInfo.CropH };
        final_crop.x = final_crop.x >= 0 ? final_crop.x : (decodeParams.mfx.FrameInfo.CropW - final_crop.w) / 2; // center crop if negative
        final_crop.y = final_crop.y >= 0 ? final_crop.y : (decodeParams.mfx.FrameInfo.CropH - final_crop.h) / 2;

        // cropped/scaled output and optional color conversion to bgra
        mfxVPPChParams[0]->VPP.FourCC = decodeParams.mfx.FrameInfo.FourCC; // out_rgba ? MFX_FOURCC_BGR4 : MFX_FOURCC_RGB4;//MFX_FOURCC_BGRA;
        mfxVPPChParams[0]->VPP.ChromaFormat = decodeParams.mfx.FrameInfo.ChromaFormat; //MFX_CHROMAFORMAT_YUV420; // Figure that out!!!!
        mfxVPPChParams[0]->VPP.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
        mfxVPPChParams[0]->VPP.FrameRateExtN = decodeParams.mfx.FrameInfo.FrameRateExtN;
        mfxVPPChParams[0]->VPP.FrameRateExtD = decodeParams.mfx.FrameInfo.FrameRateExtD;
        mfxVPPChParams[0]->VPP.CropX = 0; // output crop
        mfxVPPChParams[0]->VPP.CropY = 0;
        mfxVPPChParams[0]->VPP.CropW = dec_size.w > 0 ? dec_size.w : final_crop.w /*>0 ? crop.w : decodeParams.mfx.FrameInfo.Width*/;
        mfxVPPChParams[0]->VPP.CropH = dec_size.h > 0 ? dec_size.h : final_crop.h /*>0 ? crop.h : decodeParams.mfx.FrameInfo.Height*/;
        mfxVPPChParams[0]->VPP.Width = ALIGN16(mfxVPPChParams[0]->VPP.CropW);
        mfxVPPChParams[0]->VPP.Height = ALIGN16(mfxVPPChParams[0]->VPP.CropH);
        mfxVPPChParams[0]->VPP.ChannelId = 1;
        mfxVPPChParams[0]->Protected = 0;
        mfxVPPChParams[0]->IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY;

        sts = MFXVideoDECODE_VPP_Init(session, &decodeParams, mfxVPPChParams, numVPPCh);
        //std::cout << "Error type: " << sts << std::endl;
        VERIFY(MFX_ERR_NONE == sts, "Error initializing decodevpp\n");
    }
    if (dxdev == nullptr)
        dxdev = ::get_decoder_device(session);
    dec_initialized = true;
end:

    return sts;
}

// initialize decoder or transcoder with VPP
mfxStatus VPLCodec::init_decoder_deferred() {
    // see below how to extract device created by oneVPL
    // can be extracted only alfter decoder is created

    // Pre-parse input stream
    sts = MFXVideoDECODE_DecodeHeader(session, &bitstream_in, &decodeParams);
    if (MFX_ERR_NONE != sts) return sts; // Might return MFX_ERR_MORE_DATA - incomplete header

    printf("Output colorspace: ");
    switch (decodeParams.mfx.FrameInfo.FourCC) {
    case MFX_FOURCC_I420: // CPU output
        printf("I420 (aka yuv420p)\n");
        break;
    case MFX_FOURCC_NV12: // GPU output
        printf("NV12\n");
        break;
    case MFX_FOURCC_P010: // GPU output
        printf("P010\n");
        break;
    case MFX_FOURCC_P016: // GPU output
        printf("P016\n");
        break;
    default:
        printf("Unsupported color format\n");
        isFailed = true;
        goto end;
        break;
    }

	// JPEG interlaced handling - convert to progressive by doubling height
    if (decodeParams.mfx.CodecId==MFX_CODEC_JPEG && (decodeParams.mfx.FrameInfo.PicStruct == MFX_PICSTRUCT_FIELD_TFF ||
        decodeParams.mfx.FrameInfo.PicStruct == MFX_PICSTRUCT_FIELD_BFF)) {
        // MJPEG
        decodeParams.mfx.FrameInfo.Height *= 2; // double the height
        bitstream_in.DataFlag = MFX_BITSTREAM_COMPLETE_FRAME; // only need for MJPEG, OK for JPEG?
        //ATTN: what to do with mfx.FrameInfo.CropH ? // double? keep?
    }

    if (0 == decodeParams.mfx.FrameInfo.FrameRateExtN &&
        0 == decodeParams.mfx.FrameInfo.FrameRateExtD) {
        decodeParams.mfx.FrameInfo.FrameRateExtN = fps.N;
        decodeParams.mfx.FrameInfo.FrameRateExtD = fps.D;
    }
    //ATTN
    decodeParams.mfx.FrameInfo.Width = ALIGN16(decodeParams.mfx.FrameInfo.Width);
    decodeParams.mfx.FrameInfo.Height =
        (MFX_PICSTRUCT_PROGRESSIVE == decodeParams.mfx.FrameInfo.PicStruct) ?
        ALIGN16(decodeParams.mfx.FrameInfo.Height) :
        ALIGN32(decodeParams.mfx.FrameInfo.Height);


    // input parameters finished, now initialize decode
    sts = MFXVideoDECODE_Init(session, &decodeParams);
    VERIFY(MFX_ERR_NONE == sts, "Error initializing decode\n");

	// VPP initialization if needed
    numVPPCh = 0;
    bool no_crop = (dec_crop.empty() || dec_crop.same(decodeParams.mfx.FrameInfo.Width, decodeParams.mfx.FrameInfo.Height));
    bool no_scale = (dec_size.empty() || dec_size.same(decodeParams.mfx.FrameInfo.Width, decodeParams.mfx.FrameInfo.Height));
	if (!(no_crop && no_scale)) { // crop or scale
        numVPPCh = 1;
        vppParams.AsyncDepth = 1;
        // Input data
        vppParams.vpp.In.FourCC = decodeParams.mfx.FrameInfo.FourCC;
        vppParams.vpp.In.ChromaFormat = decodeParams.mfx.FrameInfo.ChromaFormat;
		vppParams.vpp.In.PicStruct = MFX_PICSTRUCT_PROGRESSIVE; // convert to progressive in out 
        if (decodeParams.mfx.FrameInfo.PicStruct != MFX_PICSTRUCT_PROGRESSIVE) {
			std::cout << "WARNING: interlaced input passed as progressive to VPP\n";
        }
		// ATTN: assuming FrameInfo.CropX/Y are 0, otherwise need to add them to dec_crop.x/y
        crop_roi final_crop = { dec_crop.x, dec_crop.y,
            dec_crop.w > 0 ? dec_crop.w : decodeParams.mfx.FrameInfo.CropW,
            dec_crop.h > 0 ? dec_crop.h : decodeParams.mfx.FrameInfo.CropH };
        final_crop.x = final_crop.x >= 0 ? final_crop.x : (decodeParams.mfx.FrameInfo.CropW - final_crop.w) / 2; // center crop if negative
        final_crop.y = final_crop.y >= 0 ? final_crop.y : (decodeParams.mfx.FrameInfo.CropH - final_crop.h) / 2;

        vppParams.vpp.In.CropW = mfxU16(final_crop.w);
		vppParams.vpp.In.CropH = mfxU16(final_crop.h); // there was scale factor here before
        vppParams.vpp.In.CropX = mfxU16(final_crop.x);
        vppParams.vpp.In.CropY = mfxU16(final_crop.y);
        vppParams.vpp.In.FrameRateExtN = decodeParams.mfx.FrameInfo.FrameRateExtN;
        vppParams.vpp.In.FrameRateExtD = decodeParams.mfx.FrameInfo.FrameRateExtD;
        // width must be a multiple of 16
        // height must be a multiple of 16 in case of frame picture and a        multiple of 32 in case of field picture
        vppParams.vpp.In.Width = ALIGN16(decodeParams.mfx.FrameInfo.CropW); // not copying decode width?
        vppParams.vpp.In.Height =
            (MFX_PICSTRUCT_PROGRESSIVE == vppParams.vpp.In.PicStruct) ?
            ALIGN16(decodeParams.mfx.FrameInfo.CropH) :
            ALIGN32(decodeParams.mfx.FrameInfo.CropH);
        vppParams.ExtParam = NULL;
        vppParams.NumExtParam = 0;

        // Output data
        vppParams.vpp.Out.FourCC = vppParams.vpp.In.FourCC; // ATTN: for now keep same format
        vppParams.vpp.Out.ChromaFormat = vppParams.vpp.In.ChromaFormat;
        vppParams.vpp.Out.CropX = 0;
        vppParams.vpp.Out.CropY = 0;
        vppParams.vpp.Out.CropW = mfxU16(dec_size.w>0? dec_size.w: vppParams.vpp.In.CropW);
        vppParams.vpp.Out.CropH = mfxU16(dec_size.h>0? dec_size.h: vppParams.vpp.In.CropH);
        vppParams.vpp.Out.PicStruct = vppParams.vpp.In.PicStruct;
        vppParams.vpp.Out.FrameRateExtN = vppParams.vpp.In.FrameRateExtN;
        vppParams.vpp.Out.FrameRateExtD = vppParams.vpp.In.FrameRateExtD;
        // width must be a multiple of 16
        // height must be a multiple of 16 in case of frame picture and a  multiple of 32 in case of field picture
        vppParams.vpp.Out.Width = ALIGN16(vppParams.vpp.Out.CropW);
        vppParams.vpp.Out.Height =
            (MFX_PICSTRUCT_PROGRESSIVE == vppParams.vpp.Out.PicStruct) ?
            ALIGN16(vppParams.vpp.Out.CropH) :
            ALIGN32(vppParams.vpp.Out.CropH);
        // Video memory surfaces are used to storing the raw frames. Use with HW   acceleration for better performance
        vppParams.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_VIDEO_MEMORY;


        // Initialize the VPL VPP
        sts = MFXVideoVPP_Init(session, &vppParams);
        VERIFY(MFX_ERR_NONE == sts, "Error initializing VPP\n");
    }
    if (dxdev == nullptr)
        dxdev = ::get_decoder_device(session);
    dec_initialized = true;
end:

    return sts;
}

mfxStatus VPLCodec::init_encoder_from_decoder() {
    mfxVideoParam par = {};

    mfxU16 encW = decodeParams.mfx.FrameInfo.Width;
    mfxU16 encH = decodeParams.mfx.FrameInfo.Height;
    if (numVPPCh <= 0) {
        encodeParams.mfx.FrameInfo.FourCC = decodeParams.mfx.FrameInfo.FourCC;
        encodeParams.mfx.FrameInfo.ChromaFormat = decodeParams.mfx.FrameInfo.ChromaFormat; //MFX_CHROMAFORMAT_YUV420; // ATTN: decodeParams.mfx.FrameInfo.ChromaFormat?
        encodeParams.mfx.FrameInfo.FrameRateExtN = decodeParams.mfx.FrameInfo.FrameRateExtN;
        encodeParams.mfx.FrameInfo.FrameRateExtD = decodeParams.mfx.FrameInfo.FrameRateExtD;
        encodeParams.mfx.FrameInfo.CropX = decodeParams.mfx.FrameInfo.CropX;
        encodeParams.mfx.FrameInfo.CropY = decodeParams.mfx.FrameInfo.CropY;
        encodeParams.mfx.FrameInfo.CropW = decodeParams.mfx.FrameInfo.CropW; 
        encodeParams.mfx.FrameInfo.CropH = decodeParams.mfx.FrameInfo.CropH;
    } else {
        encodeParams.mfx.FrameInfo.FourCC = vppParams.vpp.Out.FourCC;
        encodeParams.mfx.FrameInfo.ChromaFormat = vppParams.vpp.Out.ChromaFormat;
        encodeParams.mfx.FrameInfo.FrameRateExtN = vppParams.vpp.Out.FrameRateExtN;
        encodeParams.mfx.FrameInfo.FrameRateExtD = vppParams.vpp.Out.FrameRateExtD;
        encodeParams.mfx.FrameInfo.CropX = vppParams.vpp.Out.CropX;
        encodeParams.mfx.FrameInfo.CropY = vppParams.vpp.Out.CropY;
        encodeParams.mfx.FrameInfo.CropW = vppParams.vpp.Out.CropW;
        encodeParams.mfx.FrameInfo.CropH = vppParams.vpp.Out.CropH;
		encW = vppParams.vpp.Out.Width;
		encH = vppParams.vpp.Out.Height;
    }

    set_default_enc_params(loader->enc_codecID);

    encodeParams.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;   // we don't want using interleaved formats

    // width must be a multiple of 16
    // height must be a multiple of 16 in case of frame picture and a multiple of 32 in case of field picture
    encodeParams.mfx.FrameInfo.Width = ALIGN16(encW);
    encodeParams.mfx.FrameInfo.Height = (MFX_PICSTRUCT_PROGRESSIVE == encodeParams.mfx.FrameInfo.PicStruct) ? ALIGN16(encH) : ALIGN32(encH);

    float targetbr = std::min(float(MAXBITRATE), DEFAULTBITRATEPERMPIXEL * (float)encodeParams.mfx.FrameInfo.Width * (float)encodeParams.mfx.FrameInfo.Height / 1.0e6f); //ATTN: have decoder data
    encodeParams.mfx.TargetKbps = mfxU16(targetbr);

    if (enc_customize_params != nullptr) enc_customize_params(encodeParams); // adjust encoder parameters with user function

    std::cout << "TargetKbps=" << encodeParams.mfx.TargetKbps << std::endl;

    encodeParams.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;

    // Validate encode parameters
    sts = MFXVideoENCODE_Query(session, &encodeParams, &encodeParams);
    VERIFY(sts == MFX_ERR_NONE, "encode Query failed");

    // Not sure if we need this...
    encodeParams.mfx.LowPower = MFX_CODINGOPTION_ON;
    sts = MFXVideoENCODE_Query(session, &encodeParams, &encodeParams);
    VERIFY(sts == MFX_ERR_NONE, "encode Query 2 failed");
    //if (sts == MFX_ERR_NONE)
    // removing this part gives better performance when using new OneVPL 2.X API (at least on AlderLake Iris Xe iGPU)
    //{
    //    encodeParams.mfx.LowPower = MFX_CODINGOPTION_OFF;
    //    sts = MFXVideoENCODE_Query(m_session, &encodeParams, &encodeParams);
    //    VERIFY(sts == MFX_ERR_NONE, "encode Query failed");
    //}

    // Initialize the VPLencoder
    sts = MFXVideoENCODE_Init(session, &encodeParams);

    if (sts == MFX_WRN_INCOMPATIBLE_VIDEO_PARAM)
        sts = MFX_ERR_NONE;
    VERIFY(MFX_ERR_NONE == sts, "Error initializing Encode\n");


    // Retrieve video parameters selected by encoder.
    // - BufferSizeInKB parameter is required to set bit stream buffer size
    sts = MFXVideoENCODE_GetVideoParam(session, &par);
    VERIFY(MFX_ERR_NONE == sts, "GetVideoParam failed");
    //m_BufferSizeKB = m_mfxDecParams.mfx.FrameInfo.CropW * m_mfxDecParams.mfx.FrameInfo.CropH / 1024;

    // Prepare output bitstream
	// ATTN: the buffer size should be par.mfx.BufferSizeInKB but not all encoders set it!
    unsigned int BufferSize = par.mfx.BufferSizeInKB ?
        (par.mfx.BufferSizeInKB + 1) * 1024 : 
        FourCCToSize(encodeParams.mfx.FrameInfo.FourCC, encodeParams.mfx.FrameInfo.CropW, encodeParams.mfx.FrameInfo.CropH) + 1024;
        
    bitstream_out.resize_buf(BufferSize);
    bitstream_out.rewind();
    sts = MFX_ERR_MEMORY_ALLOC;
    VERIFY(bitstream_out.Data!=nullptr, "Not able to allocate output buffer");
    sts = MFX_ERR_NONE;
    bitstream_out.CodecId = loader->enc_codecID;
    enc_initialized = true;
end:
    //ATTN: if isFailed - cleanup
    //return !isFailed;
    return sts;
}

// initialize encoder only
bool VPLCodec::init_encoder(const size2& in_size, mfxU32 FourCC, ID3D11Device* device, encoderCustomizeFunc customizeParams){
    close();
    isDraining = false;
    isStillGoing = true;
    isFailed = false;

    mfxVideoParam par = {};

    // Initialize and/or check loader config
    if (loader == nullptr) {
        loader = std::make_shared<VplLoader>(0, MFX_CODEC_HEVC, true); // default configuration
        std::cout << "WARNING: MFX Loader not configured, trying to use default: HEVC->HEVC, HW codec." << std::endl;

    }
    VERIFY(NULL != loader->loader, "MFX implementation configuration failed -- are parameters correct?");

    // MFXCreateSession has a second parameter that I'm not sure if it refers to the device order number...
    sts = MFXCreateSession(*loader, 0, &session);
    VERIFY(MFX_ERR_NONE == sts,
        "Cannot create session -- no implementations meet selection criteria");
    // Print info about implementation loaded
    ShowImplementationInfo(*loader, 0);
    // ATTN: creating dx device - using DXGI adapter index in enum order
    if (device != nullptr) {
        sts = MFXVideoCORE_SetHandle(session, MFX_HANDLE_D3D11_DEVICE, device);
        VERIFY(MFX_ERR_NONE == sts,
            "Cannot assign sessions's DX11 device");
        dxdev = device;
		dxdev->AddRef();
    }
    // if session is active then dx device is already assigned

    // set encoder parameters
    set_default_enc_params(loader->enc_codecID);
    encodeParams.mfx.FrameInfo.FrameRateExtN = fps.N;
    encodeParams.mfx.FrameInfo.FrameRateExtD = fps.D;
    encodeParams.mfx.FrameInfo.FourCC = FourCC;
    encodeParams.mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420; // ATTN: decodeParams.mfx.FrameInfo.ChromaFormat?
    encodeParams.mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
    encodeParams.mfx.FrameInfo.CropX = 0;
    encodeParams.mfx.FrameInfo.CropY = 0;
    encodeParams.mfx.FrameInfo.CropW = in_size.w; //mfxVPPChParams[0]->VPP.Width;
    encodeParams.mfx.FrameInfo.CropH = in_size.h; //mfxVPPChParams[0]->VPP.Height;
    // width must be a multiple of 16
    // height must be a multiple of 16 in case of frame picture and a multiple of 32 in case of field picture
    //encodeParams.mfx.FrameInfo.Width = ALIGN16(encodeParams.mfx.FrameInfo.CropW);
    //encodeParams.mfx.FrameInfo.Height = (MFX_PICSTRUCT_PROGRESSIVE == encodeParams.mfx.FrameInfo.PicStruct) ? ALIGN16(encodeParams.mfx.FrameInfo.CropH) : ALIGN32(encodeParams.mfx.FrameInfo.CropH);
    encodeParams.mfx.FrameInfo.Width = ALIGN16(in_size.w);
    encodeParams.mfx.FrameInfo.Height = (MFX_PICSTRUCT_PROGRESSIVE == encodeParams.mfx.FrameInfo.PicStruct) ? ALIGN16(in_size.h) : ALIGN32(in_size.h);

    // mfx.TargetKbps is mfxU16 (unsigned short) so its maximum value is 65535. Casting higher values from float to mfxU16 will produce bad results
    float targetbr = std::min(float(MAXBITRATE), DEFAULTBITRATEPERMPIXEL * (float)encodeParams.mfx.FrameInfo.Width * (float)encodeParams.mfx.FrameInfo.Height / 1.0e6f); //ATTN: have decoder data
    encodeParams.mfx.TargetKbps = mfxU16(targetbr>1.0f ? targetbr: 1.0f); // ceil or round?
    std::cout << "TargetKbps=" << encodeParams.mfx.TargetKbps << std::endl;

    encodeParams.IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY;

    // Validate encode parameters
    sts = MFXVideoENCODE_Query(session, &encodeParams, &encodeParams);
    VERIFY(sts == MFX_ERR_NONE, "encode Query failed");

    // Not sure if we need this...
    encodeParams.mfx.LowPower = MFX_CODINGOPTION_ON;
    sts = MFXVideoENCODE_Query(session, &encodeParams, &encodeParams);
    VERIFY(sts == MFX_ERR_NONE, "encode Query 2 failed");
    //if (sts == MFX_ERR_NONE)
    // removing this part gives better performance when using new OneVPL 2.X API (at least on AlderLake Iris Xe iGPU)
    //{
    //    encodeParams.mfx.LowPower = MFX_CODINGOPTION_OFF;
    //    sts = MFXVideoENCODE_Query(m_session, &encodeParams, &encodeParams);
    //    VERIFY(sts == MFX_ERR_NONE, "encode Query failed");
    //}

    // Initialize the VPLencoder
    sts = MFXVideoENCODE_Init(session, &encodeParams);

    if (sts == MFX_WRN_INCOMPATIBLE_VIDEO_PARAM)
        sts = MFX_ERR_NONE;
    VERIFY(MFX_ERR_NONE == sts, "Error initializing Encode\n");


    // Retrieve video parameters selected by encoder.
    // - BufferSizeInKB parameter is required to set bit stream buffer size
    sts = MFXVideoENCODE_GetVideoParam(session, &par);
    VERIFY(MFX_ERR_NONE == sts, "GetVideoParam failed");
    //m_BufferSizeKB = par.mfx.BufferSizeInKB;
    //m_BufferSizeKB = m_mfxDecParams.mfx.FrameInfo.CropW * m_mfxDecParams.mfx.FrameInfo.CropH / 1024;

    // Prepare output bitstream
    // ATTN: the buffer size should be par.mfx.BufferSizeInKB !
    bitstream_out.resize_buf((par.mfx.BufferSizeInKB + 1) * 1024);
    bitstream_out.rewind();
    VERIFY(bitstream_out.Data != nullptr, "Not able to allocate output buffer");
    bitstream_out.CodecId = loader->enc_codecID;
    enc_initialized = true;

    if (device == nullptr)
        dxdev = ::get_encoder_device(session);
end:
    //ATTN: if isFailed - cleanup
    return !isFailed;

}

void VPLCodec::set_default_enc_params(mfxU32 codecID) {
    // for transcoder we expect cfg[2] - encoder codec ID which should be HEVC for now

    encodeParams.AsyncDepth = 1;
    encodeParams.mfx.CodecId = codecID; // HEVC; JPEG

    switch (codecID) {
    case MFX_CODEC_JPEG:
    {
        encodeParams.mfx.Quality = 90;
        encodeParams.mfx.Interleaved = MFX_SCANTYPE_INTERLEAVED;
        //encodeParams.mfx.FrameInfo.FrameRateExtN = FRAMERATE;
        //encodeParams.mfx.FrameInfo.FrameRateExtD = 1;
        //encodeParams.mfx.FrameInfo.FourCC = cliParams.encodeFourcc;
        //encodeParams.mfx.FrameInfo.ChromaFormat = FourCCToChroma(cliParams.encodeFourcc);
        //encodeParams.mfx.FrameInfo.CropW = cliParams.srcWidth;
        //encodeParams.mfx.FrameInfo.CropH = cliParams.srcHeight;
        //encodeParams.mfx.FrameInfo.Width = ALIGN16(cliParams.srcWidth);
        //encodeParams.mfx.FrameInfo.Height = ALIGN16(cliParams.srcHeight);

    }
    break;
    case MFX_CODEC_HEVC:
    default:
    {
        if (codecID != MFX_CODEC_HEVC) {
            std::cout << "WARNING: Expected HEVC encoder, different value detected. Configuration might be wrong." << std::endl;
        }

        //       encodeParams.mfx.LowPower = MFX_CODINGOPTION_UNKNOWN;
        encodeParams.mfx.MaxKbps = MAXBITRATE;
        encodeParams.mfx.GopPicSize = 15;
        encodeParams.mfx.GopRefDist = 1;
        encodeParams.mfx.IdrInterval = 1;
        encodeParams.mfx.RateControlMethod = MFX_RATECONTROL_VBR; // MFX_RATECONTROL_ICQ; // MFX_RATECONTROL_VBR;

        //encodeParams.mfx.ICQQuality = 36;
        //encodeParams.mfx.Quality = 100;
        encodeParams.mfx.TargetUsage = MFX_TARGETUSAGE_BEST_SPEED; //MFX_TARGETUSAGE_BEST_SPEED;
    }
    break;
    }
}

mfxFrameSurface1* VPLCodec::get_encode_surface() {
    if (!enc_initialized) {
        if (!dec_initialized || MFX_ERR_NONE != init_encoder_from_decoder())
            return nullptr;
    } 
    mfxFrameSurface1* surface = nullptr;
    mfxStatus sts = MFXMemory_GetSurfaceForEncode(session, &surface);
    if (MFX_ERR_NONE != sts) {
        std::cout << "Cannot retrieve sessions's surface" << std::endl;
        return nullptr;
    }
    return surface;
}

// simple synchronous implementation
// needs to be upgraded for pipelining
mfxStatus VPLCodec::decode_frame(mfxFrameSurface1** surf, int surfNum) {
    if (!dec_initialized) {
        if (session == nullptr) {
            std::cout << "ERROR: decoder session is not initialized." << std::endl;
            return MFX_ERR_NOT_INITIALIZED;
        }
        sts = init_decoder_deferred();
        if (sts != MFX_ERR_NONE) return sts;
    }
	mfxFrameSurface1* surf_dec = nullptr;

    auto sts = decode_frame_novpp((surf!=nullptr && surfNum>0)?&surf_dec:nullptr); // if output is needed
	bool need_preVPP = (surf != nullptr && surfNum >= numVPPCh + 1);
	if (need_preVPP) surf[numVPPCh] = surf_dec; // last surface is pre-VPP one
    //print_surface_info(surf_dec);

    if (numVPPCh<1 || sts != MFX_ERR_NONE || surf_dec == nullptr ) return sts;

    // VPP processing of decoded frame
    mfxFrameSurface1* surf_out = nullptr;

    mfxExtVppAuxData aux;
    mfxSyncPoint syncp;
    // cropping parameters need to be set at runtime, not at Init()
    surf_dec->Info.CropX = vppParams.vpp.In.CropX;
    surf_dec->Info.CropY = vppParams.vpp.In.CropY;
    surf_dec->Info.CropW = vppParams.vpp.In.CropW;
    surf_dec->Info.CropH = vppParams.vpp.In.CropH;;

    sts = MFXMemory_GetSurfaceForVPPOut(session, &surf_out);  //ToDo:: take care of the return value if there is any issue.
    for (;;)
    {
        // run VPP on a frame asychronously (returns immediately)
        sts = MFXVideoVPP_RunFrameVPPAsync(session, surf_dec, surf_out, &aux, &syncp);

        if (MFX_WRN_DEVICE_BUSY == sts)    // repeat the call if warning and no output
        {
            Sleep(1);  // wait if device is busy
            continue;
        } else if (MFX_ERR_NONE < sts && syncp) {
            sts = MFX_ERR_NONE;     // ignore warnings if output is available
        } else if (MFX_ERR_NOT_ENOUGH_BUFFER == sts) {
            // Allocate more bitstream buffer memory here if needed...
            std::cout << "ERROR: vpp failed with MFX_ERR_NOT_ENOUGH_BUFFER.\n";
        }
        break;
    }
    if (!need_preVPP) {
        surf_dec->FrameInterface->Release(surf_dec); // if pre-VPP surface is not needed - realease it
        surf_dec = nullptr;
    }
    if (MFX_ERR_NONE == sts) {
        sts = MFXVideoCORE_SyncOperation(session, syncp, 60000); // Synchronize. Wait until frame processing is ready
        if (sts == MFX_ERR_NONE) {
            assert(surf != nullptr && surfNum > 0);
            surf[0] = surf_out;
        } else {
            std::cout << "ERROR: vpp SyncOperation failed.\n";
        }
    }
    //print_surface_info(surf_out);

    return sts;
}

mfxStatus VPLCodec::decode_frame0(mfxFrameSurface1** surf, int surfNum) {
    if (!dec_initialized) {
        if (session == nullptr) {
            std::cout << "ERROR: decoder session is not initialized." << std::endl;
            return MFX_ERR_NOT_INITIALIZED;
        }
        sts = init_decoder_deferred();
        if (sts != MFX_ERR_NONE) return sts;
    }
    if (numVPPCh == 0) return decode_frame_novpp(surfNum > 0 ? surf : nullptr);


    mfxSurfaceArray* outSurfaces = nullptr;
    mfxFrameSurface1* aSurf = nullptr;

    if (bitstream_in.DataLength <= 0) {
        isDraining = true;
    }

    //retry_with_more_stream:
    if (isStillGoing == true) {
        // Load encoded stream if not draining
    retry_with_same_data:
        sts = MFXVideoDECODE_VPP_DecodeFrameAsync(session,
            (isDraining) ? nullptr : &bitstream_in,
            nullptr,
            0,
            &outSurfaces);
        switch (sts) {
        case MFX_ERR_NONE:
        {
            if (outSurfaces == nullptr) {
                printf("ERROR - empty array of surfaces.\n");
                isStillGoing = false;
                return MFX_ERR_NULL_PTR;
            }

            mfxFrameSurface1* outSurf = nullptr;
            for (mfxU32 i = 0; i < outSurfaces->NumSurfaces; i++) {
                aSurf = outSurfaces->Surfaces[i];
                // do {
                sts = aSurf->FrameInterface->Synchronize(aSurf, SYNC_TIMEOUT); // very long timout
                //} while (sts == MFX_WRN_IN_EXECUTION);

                if (MFX_ERR_NONE != sts) {
                    std::cout << "ERROR - FrameInterface->Synchronize failed" << std::endl;
                    break;
                }
            }
            if (MFX_ERR_NONE == sts) {
                framenum++;
                for (mfxU32 i = 0; i < outSurfaces->NumSurfaces; i++) {
                    aSurf = outSurfaces->Surfaces[i];
                    if (aSurf->Info.ChannelId == 1 && surfNum > 0) {
                        // 0 - might need return,
                        surf[0] = aSurf;
                        continue;
                    }
                    if (aSurf->Info.ChannelId == 0 && surfNum > 1) {
                        // 0 - pre-vpp
                        surf[1] = aSurf;
                        continue;
                    }
                    sts = aSurf->FrameInterface->Release(aSurf);
                }
                //ATTN!!!
                sts = outSurfaces->Release(outSurfaces);
                if (MFX_ERR_NONE != sts) std::cout << "ERROR - mfxSurfaceArray->Release failed" << std::endl;
                outSurfaces = nullptr;
                return sts; // ATTN: check that it is not dereferenced!!!
            }
        }
        break; // reach here only when error happened
        case MFX_ERR_MORE_DATA:
            // The function requires more bitstream at input before decoding can
            // proceed
            if (isDraining)
                isStillGoing = false;
            break;
        case MFX_ERR_MORE_SURFACE:
            // The function requires more frame surface at output before decoding
            // can proceed. This applies to external memory allocations and should
            // not be expected for a simple internal allocation case like this
            break;
        case MFX_ERR_DEVICE_LOST:
            // For non-CPU implementations,
            // Cleanup if device is lost
            break;
        case MFX_WRN_DEVICE_BUSY:
            // For non-CPU implementations,
            // Wait a few milliseconds then try again
            goto retry_with_same_data;
            break;
        case MFX_WRN_VIDEO_PARAM_CHANGED:
            // The decoder detected a new sequence header in the bitstream.
            // Video parameters may have changed.
            // In external memory allocation case, might need to reallocate the
            // output surface
            if (bitstream_in.DataLength > 0) goto retry_with_same_data;
            return MFX_ERR_MORE_DATA;// data fully consumed, need more goto retry_with_more_stream;
            break;
        case MFX_ERR_INCOMPATIBLE_VIDEO_PARAM:
            // The function detected that video parameters provided by the
            // application are incompatible with initialization parameters. The
            // application should close the component and then reinitialize it
            break;
        case MFX_ERR_REALLOC_SURFACE:
            // Bigger surface_work required. May be returned only if
            // mfxInfoMFX::EnableReallocRequest was set to ON during initialization.
            // This applies to external memory allocations and should not be
            // expected for a simple internal allocation case like this
            break;
        default:
            printf("unknown status %d\n", sts);
            isStillGoing = false;
            break;
        }

    }
    return sts;
}

mfxStatus VPLCodec::decode_frame_novpp(mfxFrameSurface1** surf) {
    //not checking for decode present - internal function
    mfxFrameSurface1* decSurfaceOut = nullptr;
    //if (framenum == 243) {
    //    std::cout << "ATTN: Special debug sequence header" << std::endl;
    //}
    if (bitstream_in.DataLength <= 0) {
        isDraining = true;
    }
    if (isStillGoing == true) {
    retry_with_same_data:
        sts = MFXVideoDECODE_DecodeFrameAsync(session,
            (isDraining) ? NULL : &bitstream_in,
            NULL,
            &decSurfaceOut,
            &syncp);
        if (sts != MFX_ERR_NONE && decSurfaceOut != nullptr)
            Release(decSurfaceOut);
        switch (sts) {
        case MFX_ERR_NONE:
            do {
                sts = decSurfaceOut->FrameInterface->Synchronize(decSurfaceOut,
                    WAIT_100_MILLISECONDS);
            } while (sts == MFX_WRN_IN_EXECUTION);
            if (MFX_ERR_NONE == sts) {
                framenum++;
                if (surf != nullptr) {
                    surf[0] = decSurfaceOut;
                    return sts;
                }
            }
            Release(decSurfaceOut);
            break;
        case MFX_ERR_MORE_DATA:
            // The function requires more bitstream at input before decoding can
            // proceed
            if (isDraining)
                isStillGoing = false;
            //else goto retry_with_more_stream;
            break;
        case MFX_ERR_MORE_SURFACE:
            // The function requires more frame surface at output before decoding
            // can proceed. This applies to external memory allocations and should
            // not be expected for a simple internal allocation case like this
            break;
        case MFX_ERR_DEVICE_LOST:
            // For non-CPU implementations,
            // Cleanup if device is lost
            break;
        case MFX_WRN_DEVICE_BUSY:
            // For non-CPU implementations,
            // Wait a few milliseconds then try again
            goto retry_with_same_data;
            break;
        case MFX_WRN_VIDEO_PARAM_CHANGED:
            // The decoder detected a new sequence header in the bitstream.
            // Video parameters may have changed.
            // In external memory allocation case, might need to reallocate the
            // output surface
            // part of stream is consumed, so check if refill is needed
            if (bitstream_in.DataLength > 0) goto retry_with_same_data;
            return MFX_ERR_MORE_DATA; // header consumed
            break;
        case MFX_ERR_INCOMPATIBLE_VIDEO_PARAM:
            // The function detected that video parameters provided by the
            // application are incompatible with initialization parameters. The
            // application should close the component and then reinitialize it
            break;
        case MFX_ERR_REALLOC_SURFACE:
            // Bigger surface_work required. May be returned only if
            // mfxInfoMFX::EnableReallocRequest was set to ON during initialization.
            // This applies to external memory allocations and should not be
            // expected for a simple internal allocation case like this
            break;
        default:
            printf("unknown status %d\n", sts);
            isStillGoing = false;
            break;
        }

    }
    return sts;
}


mfxStatus VPLCodec::encode_frame(mfxFrameSurface1* surface) {
    auto sts = MFX_ERR_NONE;
    if (!enc_initialized) { // attempt to init encoder from what we have
        if (!dec_initialized) return MFX_ERR_NOT_INITIALIZED;
        sts = init_encoder_from_decoder();
        if (sts != MFX_ERR_NONE) return sts;
    }
    for (;;)
    {
        mfxSyncPoint syncp{};
        // Encode a frame asychronously (returns immediately)
        sts = MFXVideoENCODE_EncodeFrameAsync(session, NULL, surface, &bitstream_out, &syncp);

        if (MFX_WRN_DEVICE_BUSY == sts)    // repeat the call if warning and no output
        {
            Sleep(1);  // wait if device is busy
            continue;
        } else if (MFX_ERR_NONE <= sts && syncp) {
            //sts = MFX_ERR_NONE;     // ignore warnings if output is available
            do{
                sts = MFXVideoCORE_SyncOperation(session, syncp, 60000);     // Synchronize. Wait until frame processing is ready
			} while (sts == MFX_WRN_IN_EXECUTION);
            if (sts != MFX_ERR_NONE) {
                std::cout << "Encoder SyncOperation failed\n";
                throw std::runtime_error("SyncOperation failed");
            }
            return sts;
        } else if (MFX_ERR_NOT_ENOUGH_BUFFER == sts) {
            // Allocate more bitstream buffer memory here if needed...
            /*mfxU32 BufferSize = getBufferSizeKB() * 1024 * 10;
            if (outBs.DataLength < (BufferSize)) {
                std::cout << "MFX_ERR_NOT_ENOUGH_BUFFER - Reallocating Buffer" << std::endl;
                outBs.Data = (mfxU8*)malloc(BufferSize);
                outBs.DataLength = BufferSize;
            }*/
            //else {
            std::cout << "ERROR: encode MFX_ERR_NOT_ENOUGH_BUFFER\n";
            //throw std::runtime_error("MFX_ERR_NOT_ENOUGH_BUFFER failed");
            //}
        } else if (surface == nullptr && MFX_ERR_MORE_DATA == sts) {
            //return false; // normal encoder flush end 
        } else {
            std::cout << "ERROR: unexpected encoder state\n";
        }
        break;
    }
    return sts;
}

// Not doing for now due to complexity of different states 
//mfxStatus VPLCodec::transcode_frame(mfxFrameSurface1** psurface, bool processed) {
//    if (source == nullptr || sink == nullptr) return MFX_ERR_NOT_INITIALIZED;
//
//    mfxFrameSurface1* surface = decode_frame(processed ? nullptr : psurface);
//    if (source->is_none() && surface == nullptr && isStillGoing) return MFX_ERR_MORE_DATA;
//
//    bool has_more_data = encode_frame(surface);
//    if (surface == nullptr) {
//        if(sink->is_none() && has_more_data) return MFX_ERR_MORE_BITSTREAM;
//        return MFX_ERR_NULL_PTR;
//    }
//    if (psurface != nullptr && processed) {
//        psurface[0] = surface;
//    }
//    else {
//        surface->FrameInterface->Release(surface);
//    }
//    return MFX_ERR_NONE;
//}

void VPLCodec::close() {
    // Clean up resources - It is recommended to close components first, before
// releasing allocated surfaces, since some surfaces may still be locked by
// internal resources.
    if (dec_initialized) {
        if (numVPPCh > 0) // using decode vpp
            //MFXVideoDECODE_VPP_Close(session);
			MFXVideoVPP_Close(session);
        //else
        MFXVideoDECODE_Close(session);
    }
    dec_initialized = false;
    if(enc_initialized)
        MFXVideoENCODE_Close(session);
    enc_initialized = false;
    if (session != nullptr) {
        MFXClose(session);
        session = nullptr;
    }

    if (mfxVPPChParams) {
        for (mfxU32 i = 0; i < numVPPCh; i++) {
            if (mfxVPPChParams[i])
                delete mfxVPPChParams[i];
        }
        delete[] mfxVPPChParams;
        mfxVPPChParams = nullptr;
    }
    numVPPCh = 0;
    dec_crop = {};
    dec_size = {};
    bitstream_in.clear();
    bitstream_out.clear();
    if (offscreen != nullptr) offscreen->reset();
    if (dxdev != nullptr) {
        dxdev->Release(); //ATTN!
        dxdev = nullptr;
    } 
}

void VPLCodec::reset() {
    close();

    if (loader) loader.reset();
    loader = nullptr;
}

bool VPLCodec::rewind() {

    if (!dec_initialized) return false;

    // close previous decoding
    isDraining = false;
    isStillGoing = true;
    isFailed = false;

    // rewind file

    bitstream_in.rewind();

    //// restart 
    //decodeParams.mfx.CodecId = bitstream_in.CodecId;//MFX_CODEC_HEVC;
    //decodeParams.IOPattern = /*MFX_IOPATTERN_IN_SYSTEM_MEMORY | */ MFX_IOPATTERN_OUT_VIDEO_MEMORY;//MFX_IOPATTERN_OUT_SYSTEM_MEMORY;

    //// Pre-parse input stream
    //do {
    //    sts = source->read(bitstream_in);
    //    VERIFY(MFX_ERR_NONE == sts, "Error reading bitstream\n");
    //    sts = MFXVideoDECODE_DecodeHeader(session, &bitstream_in, &decodeParams);
    //} while (sts == MFX_ERR_MORE_DATA);
    //VERIFY(MFX_ERR_NONE == sts, "Error decoding header\n");

    //if (numVPPCh > 0) {
    //    // input parameters finished, now reset decode
    //    sts = MFXVideoDECODE_VPP_Reset(session, &decodeParams, mfxVPPChParams, numVPPCh);
    //}
    //else {
    //    sts = MFXVideoDECODE_Reset(session, &decodeParams);
    //}
    if (numVPPCh > 0)
        sts = MFXVideoVPP_Reset(session, &vppParams);
    VERIFY(MFX_ERR_NONE == sts, "Error resetting vpp\n");
	sts = MFXVideoDECODE_Reset(session, &decodeParams);
    VERIFY(MFX_ERR_NONE == sts, "Error resetting decoder\n");

    //std::cout << "Rewind stream after "<< framenum << "frames."<< std::endl;
    framenum = 0;
end:
    return !isFailed;
}

VPLCodec::~VPLCodec() {
    reset();
    if (offscreen != nullptr) {
        delete offscreen;
        offscreen = nullptr;
    }
}
