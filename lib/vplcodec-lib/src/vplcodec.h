//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: Apache-2.0
//==============================================================================

#ifndef DECODE_H
#define DECODE_H
#define NOMINMAX
#include "util.h"
#include <d3d11.h>
#include <memory>

class RenderOffscreen;

///
/// A minimal oneAPI Video Processing Library (oneVPL) decode application,
/// using 2.2 or newer API with internal memory management.
/// For more information see
/// https://oneapi-src.github.io/oneAPI-spec/elements/oneVPL/source/
/// @file
#define DEFAULTBITRATEPERMPIXEL 1200 // From legacy target bit rate of 5000kbps for 2440x1440: 5000 kbps / (2.880x1.440) = 1200
constexpr mfxU16 MAXBITRATE = std::min(30000, USHRT_MAX);

class VPLCodec {
public:
    struct crop_roi {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
        inline bool empty() const { return w == 0 && h == 0; }
        inline bool same(int w0, int h0) const { return w == w0 && h == h0; }
    };
    struct size2 {
        int w = 0;
        int h = 0;
        inline bool empty() const { return w == 0 && h == 0; }
        inline bool same(int w0, int h0) const { return w == w0 && h == h0; }
    };

    static ID3D11Texture2D* get_texture(mfxFrameSurface1* surface);

    VPLCodec(rational_u32 fps = 30);
    VPLCodec(mfxU32 incodecID, mfxU32 outcodecID = 0, bool impl_hw = true, rational_u32 fps = 30); // incodec  = MFX_CODEC_HEVC
    VPLCodec(std::shared_ptr<VplLoader> shared_loader, rational_u32 fps = 30);
    virtual ~VPLCodec();

    
    /**
     * @brief Initialize decoder only, no VPP.
     * @param[in] device dx11 device to use (for gpu selections or sharing device across sessions), nullptr - use default device assigned by vpl
     * @return true if succeeds
     */
    bool init_decoder(ID3D11Device* device = nullptr);
    /**
     * @brief initialize decoder with vpp for crop/scale only.
     * @param[in] crop_roi vpp cropping region, w==0&&h==0 - no cropping (+ no scale -> no VPP)
     * @param[in] out_size vpp scaling dimensions, width and height of post-crop output, w==0&&h==0 - no scale (+ no cropping -> no VPP)
     * @param[in] device dx11 device to use (for gpu selections or sharing device across sessions), nullptr - use default device assigned by vpl
     * @return true if succeeds
     */
    bool init_decoder(const crop_roi& crop, const size2& out_size, ID3D11Device* device = nullptr);

    typedef void (*encoderCustomizeFunc)(mfxVideoParam&); // function to adjust codec parameters for custom profiles

    /**
     * @brief Initialize encoder only, no VPP
     * @param[in] in_size input image resolution
     * @param[in] FourCC input image format: MFX_FOURCC_NV12, MFX_FOURCC_P010, MFX_FOURCC_P016
     * @param[in] device dx11 device to use (for gpu selections or sharing device across sessions), nullptr - use default device assigned by vpl
     * @param[in] customProfile (optional) function to patch encoder params before final inititalization
     * @return true if succeeds
     */
    bool init_encoder(const size2& in_size, mfxU32 FourCC, ID3D11Device* device = nullptr, encoderCustomizeFunc customizeParams = nullptr);

    /**
     * @brief Set/reset function patching encoder parameters before initialization
     * @param[in] customProfile (optional) function to patch encoder params before final inititalization
     * @return true if succeeds
     */
    void set_encoder_customize(encoderCustomizeFunc customizeParams = nullptr) { enc_customize_params = customizeParams;}

    /** @brief rewind the source if bitstream supports the feature
     * @return true if succeeds
    */
    bool rewind();
    /** @brief close decoding session
    */
    void close();
    /** @brief close() + release loader ref
    */
    void reset(); 

    /** 
     * @brief decode frame (if bs_source is specified) and process with vpp (if enabled)
     * @param[out] surf array for decoded and and optionally pre-vpp surfaces, caller must release
     * @param[out] surfNum number of surface pointers in the surf array: 1 - final frame (after vpp), 2 - final & pre-vpp frames
     * @return mfxStatus: MFX_ERR_NONE - if decoded, MFX_ERR_MORE_DATA - if not enough data in the input bitstream
     * ATTENTION: calling decode_frame with empty bitstream will attempt to flush decoder.
     */
    mfxStatus decode_frame(mfxFrameSurface1** surf, int surfNum = 1);
    mfxStatus decode_frame0(mfxFrameSurface1** surf, int surfNum = 1);

    /**
     * @brief encode frame 
     * @param[in] surface image to encode, nullptr to flush encoder,
     * safe to release the surface upon return
     * @return MFX_ERR_NONE,  if more output is expected, false if completely flushed
     */
    mfxStatus encode_frame(mfxFrameSurface1* surface);   // encode frame
    /**
     * @brief get surface to pass to encoder (if bs_sink was specified & decoder is initialized)
     * @return surface to pass to encoding, nullptr if error
     */
    mfxFrameSurface1* get_encode_surface();

    /**
     * @brief transcode frame, optionally returning decoded frame;
     * decoder will flush automatically when source bitstream is empty (sink->write might be called many times)
     * @param[out] psurface ptr for post-decode/post-vpp surface (e.g. for preview), nullptr - no surface is returned
     * caller has to release the surface when done.
     * @param[in] processed type of desired pre-view surface; true - post-vpp, false - post-decode.
     * @return MFX_ERR_NONE: frame was decoded and encoded, and preview is available (if requested).
     * MFX_ERR_NULL_PTR: decoder drained, encoder finished processing buffered frames, no more data is expected in the output
     * MFX_ERR_MORE_DATA (manual push only): frame can't be decoded, not enough data 
     * MFX_ERR_MORE_BITSTREAM (manual pull only): decoder drained, encoder might have more bitstream, call again
     * MFX_ERR_NOT_INITIALIZED: either encoder or decoder not initialized, can't transcode
     */
    // Not implemented for now, call decode_frame, encode_frame instead
    // mfxStatus transcode_frame(mfxFrameSurface1** psurface, bool processed=true);

    /**
     * @brief get dx device for the current active session
     * @return dx device if session is active, nullptr otherwise
     */
    ID3D11Device* get_device();

    /**
     * @brief convert NV12 surface to RGBA texture
     * new texture is created all the time, caller must release
     * @param[in] surfaceNV12 input surface in NV12 format
     * @return RGBA texture, or nullptr, if error
     */
    ID3D11Texture2D* get_texture_rgba(mfxFrameSurface1* surfaceNV12);

	mfxBitstreamEx& bs_in() { return bitstream_in; }
    const mfxBitstreamEx& bs_in() const { return bitstream_in; }
    mfxBitstreamEx& bs_out() { return bitstream_out; }
    const mfxBitstreamEx& bs_out() const { return bitstream_out; }


protected:
    // deferred decoder initialization (happens on the first decode_frame/transcode_frame)
    mfxStatus init_decoder_deferred();
    mfxStatus init_decoder_deferred0();
    // deferred encoder initialization
    mfxStatus init_encoder_from_decoder();


    // decode frame w/o vpp
    mfxStatus decode_frame_novpp(mfxFrameSurface1** surf);

    // initialize encoder with default parameters
    void set_default_enc_params(mfxU32 codecID=MFX_CODEC_HEVC);

    crop_roi    dec_crop = {};
    size2       dec_size = {};
    bool        dec_initialized = false;
    bool        enc_initialized = false;
    encoderCustomizeFunc  enc_customize_params = nullptr;

    // Variables used for legacy and 2.x
    bool isDraining = false;
    bool isStillGoing = true;
    bool isFailed = false;
    mfxBitstreamEx bitstream_in = {};
    mfxBitstreamEx bitstream_out = {};
    mfxSession session = nullptr;
    mfxSyncPoint syncp = {};
    
    // VPP parameters
    mfxU32 numVPPCh = 0;        // = 0 internally indicates that VPP is not used
    mfxVideoChannelParam** mfxVPPChParams = nullptr;

	// Output surface
    mfxFrameSurface1* decSurfaceOut = nullptr;
    mfxU32 framenum = 0;
    mfxStatus sts = MFX_ERR_NONE;

    //Params cliParams = {};
    mfxVideoParam decodeParams = {};
    mfxVideoParam encodeParams = {};
    mfxVideoParam vppParams = {};

    // default fps - if not provided in the input stream
    rational_u32 fps = 30;

    // loader with cfg variables
    std::shared_ptr<VplLoader> loader = nullptr;

    ID3D11Device* dxdev = nullptr;
    RenderOffscreen* offscreen = nullptr;
};


#endif // DECODE_H
