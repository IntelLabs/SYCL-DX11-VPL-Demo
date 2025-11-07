//==============================================================================
// Copyright Intel Corporation
//
// SPDX-License-Identifier: MIT
//==============================================================================

#include <string>
#include "vplcodec.h"

#include "tqueue.h"
#include "tfqueue.h"
#include <opencv2/opencv.hpp>
#include "vpl/mfxadapter.h"

#include "dpcpp_compute.hpp"
#include "shared_resource.hpp"
#include "shared_vpl_l0.hpp"

#include <barrier>
#include <fstream>
#include <filesystem>

#define NUM_DEVICES 1 //2 //3 //3
#define NUM_CAMS 7 //5 //5 //5
#define TEST_DPCPP_INTEROP

const bool OUTPUTRAW = false;



// width is the width of an individual U or V channels (i.e., the width of the original image/2)
// in NV12 each line will have U0V0U1V1U2V2..., and potentially some extra bytes for alignment, so we input Pitch as
// the number of bytes required to go to the next line
void WhiteBalanceGWNV12(unsigned char* uv, unsigned int width, unsigned int height, unsigned int Pitch) {

	// avoid values too close to saturation 
	const unsigned char max_val = 254;
	const unsigned char min_val = 1;
	// U and V are positive and negative values, when represented as unsigned char they will typically have a value of 128 representing 0
	const unsigned char zero_value = 128;

	float meanU = 0.0f;
	float meanV = 0.0f;
	unsigned int nU = 0;
	unsigned int nV = 0;

	// compute mean of U and V excluding near saturation values
	for (int y = 0; y < height; y++) {
		unsigned char* line = uv + y * Pitch;
		for (int x = 0; x < width; x++) {
			unsigned char u = line[2 * x];
			unsigned char v = line[2 * x + 1];
			if (u<max_val && u>min_val) {
				meanU += u;
				nU++;
			}
			if (v<max_val && v>min_val) {
				meanV += v;
				nV++;
			}
		}
	}

	unsigned char meanUc = 0;
	unsigned char meanVc = 0;
	// compute mean only if we have values to avoid division by 0
	if (nU > 0)
		meanUc = (unsigned char)(meanU / nU) - zero_value; // adjust the mean by the zero_value
	if (nV > 0)
		meanVc = (unsigned char)(meanV / nV) - zero_value; // adjust the mean by the zero_value

	// adjust U and V to have zero_value mean
	for (int y = 0; y < height; y++) {
		unsigned char* line = uv + y * Pitch;
		for (int x = 0; x < width; x++) {
			line[2 * x] -= meanUc;
			line[2 * x + 1] -= meanVc;
		}
	}
}

cv::Mat YUV420_NV12toRGB(unsigned char* Ybuff, unsigned char* UVbuff, unsigned int width, unsigned int height)
{
	cv::Mat yuvnv12(height * 1.5, width, CV_8UC1);
	cv::Mat rgb;
	memcpy(yuvnv12.data, Ybuff, width * height);
	memcpy(yuvnv12.data + width * height, UVbuff, width * height / 2);
	cvtColor(yuvnv12, rgb, cv::COLOR_YUV2BGR_NV12);
	return rgb;
}
cv::Mat YUV420_YV12toRGB(unsigned char* Ybuff, unsigned char* Ubuff, unsigned char* Vbuff, unsigned int width, unsigned int height)
{
	cv::Mat yuvyv12(height * 1.5, width, CV_8UC1);
	cv::Mat rgb;
	memcpy(yuvyv12.data, Ybuff, width * height);
	memcpy(yuvyv12.data + width * height, Ubuff, width * height / 4);
	memcpy(yuvyv12.data + width * height + width * height / 4, Vbuff, width * height / 4);
	cvtColor(yuvyv12, rgb, cv::COLOR_YUV2BGR_YV12);
	return rgb;
}


//struct Pipeline
//{
//	// For some reason, when passing device here it is slower ... need to recheck if this happens also in transcoder/transcoderscaler
//	Pipeline(ID3D11Device* dxdev, unsigned short cropW, unsigned short cropH, unsigned short finalW, unsigned short finalH)
//	{
//		codec.reset(new VPLCodec(loader));
//		codec->init_decoder({ cropW>0?-1:0, cropH>0?-1:0, cropW, cropH }, { finalW, finalH }, dxdev);
//		//mfxU32 BufferSizeKB = 200000;
//		//outBS.Extend(BufferSizeKB * 1024); // 1024 * 1024 * 10);
//	}
//	std::unique_ptr<VPLCodec> codec = nullptr;
////	mfxBitstreamWrapper inBS;
////	threadsafe_queue<mfxBitstreamWrapper> inBS_queue;
////	threadsafe_queue<mfxBitstream> outBS_queue;
////	mfxBitstreamWrapper outBS;
//};

template<typename T> 
void write_vector(std::vector<T>& v, const std::string name = "test.yuv") {
	FILE* f1 = fopen_sec(name.c_str(), "wb");
	assert(f1);
	size_t r1 = fwrite(v.data(), sizeof(T), v.size(), f1);
	if (r1 != v.size()) std::cout << "ERROR: can write vector to file." << std::endl;
	assert(r1 == v.size());
	fclose(f1);
}

// naive example of sharing and processing
void process_frame(DPCPPCompute& dpcpp, mfxFrameSurface1* surface) {
	ID3D11Texture2D* texture = get_texture(surface);
	//D3D11_TEXTURE2D_DESC desc = {};
	//texture->GetDesc(&desc);
	shared_resource_t shared_texture(texture);	//DX&Win structures enabling sharing
	l0_image_t	shared_image;					// extra bookiping on L0 until image is fully implemented


	dpcpp.shared_to_l0(shared_texture, shared_image);
	// use the image for something...
	size_t img_size = shared_image.size(); // size of the actual footprint
	void* linear = malloc_device(img_size, dpcpp.q);
	dpcpp.image_to_linear_l0(shared_image, linear);

	std::vector<uint8_t>	test_buffer(img_size);
	// run toy kernel
	dpcpp.test_copy(linear, test_buffer.data(), img_size);

	if (false) write_vector(test_buffer); //debug dump to read in matlab
}
// actual size of all pixels of linearized images
// tightly packed together with no image or pitch 
// alignment
size_t frame_memory_size(mfxFrameSurface1* surf) {
	ID3D11Texture2D* texture = get_texture(surf);
	D3D11_TEXTURE2D_DESC desc = {};
	texture->GetDesc(&desc);
	if(desc.Format == DXGI_FORMAT_NV12) {
		return desc.Height * desc.Width * 3 / 2;
	}
	if(desc.Format == DXGI_FORMAT_R8G8B8A8_UNORM || desc.Format == DXGI_FORMAT_B8G8R8A8_UNORM) {
		return desc.Height * desc.Width * 4;
	}
	printf("ERROR: Texture format not supported!\n");
	return 0;
}

size_t frames_memory_size(std::vector<mfxFrameSurface1*>& surfaces) {
	size_t footprint = 0;
	for(auto surf : surfaces) {
		footprint += frame_memory_size(surf);
	}
	return footprint;
}
// naive example of sharing and processing
void process_frames(DPCPPCompute& dpcpp, std::vector<mfxFrameSurface1*>& surfaces, DPCPPCompute* pdst = nullptr) {
	static passthrough_image_batch_t	shared_batch;
	static void* linear_mem = nullptr;
	static size_t linear_size = 0;

	// assume image size == const
	if (linear_size == 0) {
		linear_size = frames_memory_size(surfaces);
		if (linear_size > 0) linear_mem = pdst==nullptr? malloc_device(linear_size, dpcpp.q): _aligned_malloc(linear_size, 64);
	}
	// use the image for something...
	auto copied = shared_batch.copy(surfaces, &dpcpp, linear_mem, linear_size);
	if (copied != surfaces.size()) {
		printf("WARNING: Not all images were copied to L0!\n");
	}
	static void* linear_mem_dst = nullptr;
	if (pdst != nullptr) {
		if (linear_size > 0 && linear_mem_dst==nullptr) 
			linear_mem_dst = malloc_device(linear_size, pdst->q);
		if (linear_mem_dst != nullptr) {
			pdst->q.memcpy(linear_mem_dst, linear_mem, linear_size);
			pdst->q.wait();
		}
	}
	// DEBUG
	if (false) {
		const int debug_frame = 151;
		static int frame = 0; // current frame if <= debug_frame
		if (frame <= debug_frame) {
			if (frame == debug_frame) {
				uint8_t* linear = reinterpret_cast<uint8_t*>(linear_mem);
				size_t offset = 0;
				std::vector<uint8_t>	test_buffer;
				for (int i = 0; i < surfaces.size(); i++) {
					size_t bytes = frame_memory_size(surfaces[i]);
					test_buffer.resize(bytes);
					// run toy kernel
					dpcpp.test_copy(linear + offset, test_buffer.data(), bytes);
					write_vector(test_buffer, "test_"+ std::to_string(frame) +"_" + std::to_string(i) + ".yuv"); //debug dump to read in matlab
					offset += bytes;
				}
			}
			frame++;
		}
	}
}

// naive example of sharing and processing
void process_frames_sc(DPCPPCompute& dpcpp, std::vector<mfxFrameSurface1*>& surfaces, unsigned int finalW, unsigned int finalH) {
	static shared_image_batch_t	shared_batch;
	static void* linear_mem = nullptr;
	static size_t linear_size = 0;

	// assume image size == const
	if (linear_size == 0) {
		linear_size = finalW * finalH * 3 / 2*surfaces.size();// ATTN: assume NV12 for now //frames_memory_size(surfaces);
		if (linear_size > 0) linear_mem = malloc_device(linear_size, dpcpp.q);
	}
	shared_rect	no_crop = {};
	// use the image for something...
	auto copied = shared_batch.crop_scale(surfaces,
		no_crop, finalW, finalH,
		&dpcpp, 0, linear_mem, linear_size);

	if (copied != surfaces.size()) {
		printf("WARNING: Not all images were copied to L0!\n");
	}

	// DEBUG
	if (false) {
		const int debug_frame = 151;
		static int frame = 0; // current frame if <= debug_frame
		if (frame <= debug_frame) {
			if (frame == debug_frame) {
				uint8_t* linear = reinterpret_cast<uint8_t*>(linear_mem);
				size_t offset = 0;
				std::vector<uint8_t>	test_buffer;
				for (int i = 0; i < surfaces.size(); i++) {
					size_t bytes = frame_memory_size(surfaces[i]);
					test_buffer.resize(bytes);
					// run toy kernel
					dpcpp.test_copy(linear + offset, test_buffer.data(), bytes);
					write_vector(test_buffer, "test_" + std::to_string(frame) + "_" + std::to_string(i) + ".yuv"); //debug dump to read in matlab
					offset += bytes;
				}
			}
			frame++;
		}
	}
}


void cleanup_frame(std::vector<mfxFrameSurface1*>& frame) {
	for (int i = 0; i<frame.size(); i++) {
		if (frame[i] == nullptr) continue;
		frame[i]->FrameInterface->Release(frame[i]);
		frame[i] = nullptr; 
	}
}

#pragma warning (disable : 4996)  // for fopen
int main(int argc, char** argv)
{
	mfxStatus sts = MFX_ERR_NONE;

	std::string img_file_format = "img%04d_dev%02d_cam%02d.jpg";
	std::string stream_file_format = "dev%02d_cam%02d.hevc";
	const cv::String keys =
		"{help h usage ? |      | print this message   }"
		"{@path          |      | path to JPEG files   }"
		"{Nf             |300   | Number of frames to process    }"
		"{encode         |      | If present, Do Transcode (instead of Decode}"
		"{encodeJPEG     |      | If present w/encode, Transcode to JPEG}"
		"{finalW W       |0     | Final With for scaling/cropping} (0=no change in dimension)"
		"{finalH H       |0     | Final Height for scaling/cropping} (0=no change in dimension)"
		"{Crop           |      | If present, do croping (instead of Scaling)}"
		"{adapter        |0     | decode adapter number}"
		"{do_Map         |      | If present, do Map to SYSTEM for decoding}"
		"{outJPEG        |      | if present save JPEG result for decoding}"
		"{inputHEVC      |      | if present input HEVC streams}"
		"{novpp          |      | if present use DX crop/scale if possible}"
		"{padapter       |-1    | processing adapter for multi-GPU}";

	cv::CommandLineParser parser(argc, argv, keys);
	if (parser.has("help"))
	{
		parser.printMessage();
		return 0;
	}
	std::string data_location = parser.get<cv::String>(0);
	const std::string output_location = data_location + "\\" + "output"; // for now use the same location for input and output
	int numFrameSets = parser.get<int>("Nf");
	bool encode = parser.has("encode");
	bool encodeJPEG = parser.has("encodeJPEG");
	unsigned short finalW = parser.get<int>("finalW");
	unsigned short finalH = parser.get<int>("finalH");
	bool Cropping = parser.has("Crop");
	int adapt_number = parser.get<int>("adapter");
	bool do_Map = parser.has("do_Map");
	bool OUTPUTJPEG = parser.has("outJPEG");
	bool InputHEVC = parser.has("inputHEVC");
	bool doScale = (finalW != 0 && finalH != 0);
	bool useVPP = !parser.has("novpp");
	int padapter = parser.get<int>("padapter");

	if(encode || do_Map) { // there will be output, create outptu dir
		if (!std::filesystem::is_directory(output_location) &&
			!std::filesystem::create_directory(output_location)) {
			std::cout << "ERROR: Can't create output directory: "<<output_location<< std::endl;
			exit(-1);
		}
	}
#ifndef TEST_DPCPP_INTEROP
	if (doScale && !useVPP) {
		std::cout << "Non-vpp scale is not available! Using vpp instead." << std::endl;
		useVPP = true;
	}
#endif


	mfxU32 num_adapters_available;

	sts = MFXQueryAdaptersNumber(&num_adapters_available);
	if (sts != MFX_ERR_NONE) {
		printf("Error querying for number of adapters\n");
		return -1;
	}
	else {
		printf("Found %d adapters\n", num_adapters_available);
	}
	if (!num_adapters_available) {
		printf("No adapters found!");
		return -1;
	}
	list_adapters(); // list adapters and displays

	// creates one dxdevice per adapter available
	std::vector<ID3D11Device*> dxdevices;
	for (auto d = 0; d < (int)num_adapters_available; d++)
	{
		ID3D11Device* dxdev = create_device(d);
		if (dxdev == nullptr) {
			std::cout << "Error creating DX11 device (create_device)." << std::endl;
		}
		dxdevices.emplace_back(dxdev);
	}

#ifdef TEST_DPCPP_INTEROP
	ID3D11Device* device = dxdevices[adapt_number]; // create_device(adapt_number);
	if (device == nullptr) {
		printf("Failed to create shared DX11 device!");
		return -1;

	}
	std::vector<ID3D11Device*> devices = { device };
	ID3D11Device* device_ofvi = nullptr;
	DPCPPCompute* pdpcpp_ofvi = nullptr;
	if (padapter >= 0) {
		device_ofvi = dxdevices[padapter]; // create_device(adapt_number);
		pdpcpp_ofvi = new DPCPPCompute(device_ofvi);
	}
	DPCPPCompute dpcpp(device);

	std::vector<mfxFrameSurface1*> decoded_frame(NUM_DEVICES * NUM_CAMS);
	std::vector<mfxFrameSurface1*> discarded_frame(NUM_DEVICES * NUM_CAMS);
	fs_queue< std::vector<mfxFrameSurface1*> > frames_queue(2);
	//std::mutex	dpcpp_mtx; // this is for simple tests
	volatile size_t	dropped_frames = 0;
	volatile size_t	processed_frames = 0;
	volatile double processing_time = 0.0;
	std::barrier frame_sync_point(NUM_DEVICES * NUM_CAMS, [&]() noexcept { // this is for forever spinning decoder threads
		bool discarded = frames_queue.push_discard(decoded_frame, discarded_frame);
		if (discarded) cleanup_frame(discarded_frame);
		dropped_frames += discarded;
		//frames_queue.wait_and_push(decoded_frame);
		//// just in case zero-out decoded images buffer
		for (int i = 0; i < decoded_frame.size(); i++) decoded_frame[i] = nullptr;
		});

	volatile unsigned int ofvi_run = 1;	// stop request
	using recycle_frames_t = void(*)(std::vector<mfxFrameSurface1*>&);

	auto ofvi_simulation = [&](recycle_frames_t recycle) {	// forever looping ofvi processing thread until asked to stop
		std::vector<mfxFrameSurface1*> frame(NUM_DEVICES * NUM_CAMS);
		while (ofvi_run) {
			if (!frames_queue.try_pop(frame)) continue;
			auto start = std::chrono::high_resolution_clock::now();
			if (doScale && !useVPP)
				process_frames_sc(dpcpp, frame, finalW, finalH);
			else
				process_frames(dpcpp, frame, pdpcpp_ofvi);
			recycle(frame);
			auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start);
			processing_time = processing_time + std::chrono::duration<double, std::micro>(duration).count();

			processed_frames += 1;
		}
		// handle last frames in the queue
		while (frames_queue.try_pop(frame)) {
			// comment below if-else out to drop last frames
			auto start = std::chrono::high_resolution_clock::now();
			if (doScale && !useVPP)
				process_frames_sc(dpcpp, frame, finalW, finalH);
			else
				process_frames(dpcpp, frame, pdpcpp_ofvi);
			recycle(frame);
			auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start);
			processing_time = processing_time + std::chrono::duration<double, std::micro>(duration).count();
			processed_frames += 1;
		}
		};
	std::thread ofvi_thread(ofvi_simulation, cleanup_frame);

#endif
	//Time codecs creation
	auto START0 = std::chrono::high_resolution_clock::now();
	// NEEDS ATTENTION: how do we assign specific adapters to each session? for now it will use the adapt_number passed in the command line (or default 0 if none is passed)
	// Use available devices/adapters
	std::vector<std::unique_ptr<VPLCodec>> pipelines;
	std::shared_ptr<VplLoader> loader = std::make_shared<VplLoader>(
		InputHEVC ? (mfxU32)MFX_CODEC_HEVC : (mfxU32)MFX_CODEC_JPEG,
		encode ? (encodeJPEG? (mfxU32)MFX_CODEC_JPEG: (mfxU32)MFX_CODEC_HEVC):0,
		true);
	for (auto i = 0; i < NUM_DEVICES* NUM_CAMS; ++i) {
		auto codec = new VPLCodec(loader);
		unsigned short cropW = Cropping ? finalW : 0;
		unsigned short cropH = Cropping ? finalH : 0;
		codec->init_decoder({ cropW > 0 ? -1 : 0, cropH > 0 ? -1 : 0, cropW, cropH }, { finalW*useVPP, finalH*useVPP }, dxdevices[adapt_number]);
		pipelines.emplace_back(codec);
	}

	auto DURATION0 = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - START0);
	std::cout << "Created " << NUM_DEVICES * NUM_CAMS << " codecs in " << 
		std::chrono::duration<double, std::milli>(DURATION0).count() << " ms" <<std::endl;

	std::vector<std::thread> threads(NUM_DEVICES * NUM_CAMS);
	std::vector<std::thread> hevc_write_threads(NUM_DEVICES * NUM_CAMS);
	float fSumFPS = 0.0f;
	auto START = std::chrono::high_resolution_clock::now();
	std::vector<std::ifstream> in(NUM_DEVICES * NUM_CAMS);
	std::vector<std::ofstream> out(NUM_DEVICES * NUM_CAMS);

	auto refill = [&](mfxBitstreamEx& bs, int frame, int d, int c)->uint32_t
	{
		// HEVC input is not working for now, still WIP
			int pipenum = d * NUM_CAMS + c;
			if (InputHEVC) {
				if (!in[pipenum].is_open()) {
					char str_src_buf[1000];
					std::string format = data_location + "\\" + stream_file_format;
					snprintf(str_src_buf, sizeof(str_src_buf), format.c_str(), d, c);
					in[pipenum].open(str_src_buf, std::ios::in | std::ios::binary);
					if (!in[pipenum]) {
						std::cout << "ERROR opening " << str_src_buf << std::endl;
						return 0;
					} else {
						std::cout << "Succesfully opened " << str_src_buf << std::endl;
					}
				}
				return bs.read_from(in[pipenum]);
			} else { // This is JPEG input
				char str_src_buf[1000];
				std::string format = data_location + "\\" + img_file_format;
				snprintf(str_src_buf, sizeof(str_src_buf), format.c_str(), frame, d, c);
				in[pipenum].open(str_src_buf, std::ios::in | std::ios::binary);
				in[pipenum].seekg(0, std::ios::end);
				auto fsize = in[pipenum].tellg();
				in[pipenum].seekg(0, std::ios::beg);
				in[pipenum].clear();
				bs.rewind();
				bs.resize_buf(fsize);
				auto bytes_read=bs.read_from(in[pipenum],fsize);
				assert(bytes_read == fsize);
				in[pipenum].close();
				in[pipenum].clear();
				return bytes_read;
			}
	};

	auto store = [&](mfxBitstreamEx& bs, int frame, int d, int c)->uint32_t
	{
		const int pipenum = d * NUM_CAMS + c;

		char str_dst_buf[1000];
		if (encode && !encodeJPEG) {
			if (!out[pipenum].is_open()) {
				std::string format = output_location + "\\" + stream_file_format;
				snprintf(str_dst_buf, sizeof(str_dst_buf), format.c_str(), d, c);
				out[pipenum].open(str_dst_buf, std::ios::out | std::ios::binary);
				if (!out[pipenum]) {
					std::cout << "ERROR opening " << str_dst_buf << std::endl;
					return 0;
				}
				else {
					std::cout << "Succesfully opened " << str_dst_buf << std::endl;
				}

			}
			return bs.write_to(out[pipenum]);
		} else { // JPEG encoder output
			std::string filename = output_location + "\\" + img_file_format;//"\\output_enc%04d_dev%02d_cam%02d.jpg";
			snprintf(str_dst_buf, sizeof(str_dst_buf), filename.c_str(), frame, d, c);
			out[pipenum].open(str_dst_buf, std::ios::out | std::ios::binary);
			auto bytes_written = bs.write_to(out[pipenum]);
			out[pipenum].close();
			out[pipenum].clear();
			return bytes_written;
		}
	};

	for (int d = 0; d < NUM_DEVICES; d++)
	{
		for (int c = 0; c < NUM_CAMS; ++c)
		{
			auto f = [&](const mfxU32 d, const mfxU32 c) -> mfxStatus
			{
				const auto pipeNum = d * NUM_CAMS + c;


				auto start = std::chrono::high_resolution_clock::now();

				for (int i = 0; i < numFrameSets; ++i)
				{
					mfxFrameSurface1* output = nullptr;
					uint32_t bytes_read = 0;
					uint32_t bytes_consumed = 0; // incomplete GOP handling
					mfxStatus dec_sts = MFX_ERR_NONE;
					do{
						bytes_read = refill(pipelines[pipeNum]->bs_in(), i, d, c);
						bytes_consumed = pipelines[pipeNum]->bs_in().buffered_length();
						dec_sts = pipelines[pipeNum]->decode_frame(&output, 1);
						bytes_consumed -= pipelines[pipeNum]->bs_in().buffered_length(); // incomplete GOP handling: stream consumed but no output yet - flush decoder
					} while (dec_sts == MFX_ERR_MORE_DATA && (bytes_read || bytes_consumed) && output==nullptr);
					printf("Frame number: %d\r", i);
					fflush(stdout);
					if (output == nullptr) {
						std::cout << "WARNING: Decoded stream is missing frame " << i << " for device " << d << " cam " << c << std::endl;
						break;
					}
					if (encode) {
						auto enc_sts = pipelines[pipeNum]->encode_frame(output);
						auto bytes_written = store(pipelines[pipeNum]->bs_out(), i, d, c);
						(void)enc_sts;
						(void)bytes_written;
					} else {
						if (output) {
 							if (do_Map) {
								mfxStatus sts = output->FrameInterface->Map(output, MFX_MAP_READ);
								if (sts != MFX_ERR_NONE) printf("Error in Map()\n");

								// Data can now be intercepted here in CPU.
								// 
								// TEST: write the data to file to double check is OK
								if (OUTPUTRAW) {
									char str_src_buf[1000];
									std::string filename = output_location + "\\frame%04d_dev%02d_cam%02d.raw";
									snprintf(str_src_buf, sizeof(str_src_buf), filename.c_str(), i, d, c);
									FILE* outfile = fopen(str_src_buf, "wb");

									// TESTS with different chorma formats
									// test if reuesting contiguous memory for Y and UV works:
									//auto szwrite = fwrite(output->Data.Y, output->Data.Pitch, output->Info.Height * 1.5, outfile);
									// In general is not contiguous, there are gaps between the Y plane and UV plane, or between Y, U, V planes. 
									// VPL documentation describes an enumerator called ChromaSiting that specifies where to place the chroma channels
									// (e.g., MFX_CHROMA_SITING_VERTICAL_BOTTOM) but I didn't find in the documentation how to use it...
									switch (output->Info.FourCC) {
									case MFX_FOURCC_YV12:
									    // If m_mfxVppParams.vpp.Out.FourCC = MFX_FOURCC_YV 12; then the output is planar and we need to access Y, U, and V planes separetely 
									    // NOTE: THIS FORMAT DOESN'T SEEM TO BE WORKING PROPERLY, STILL LOOKS THERE IS SOME INTERLEAVING IN THE BUFFERS
									    fwrite(output->Data.Y, output->Data.Pitch, output->Info.Height, outfile);
									    fwrite(output->Data.U, output->Data.Pitch, output->Info.Height / 4, outfile);
									    fwrite(output->Data.V, output->Data.Pitch, output->Info.Height / 4, outfile);
									    break;
									case MFX_FOURCC_NV12:
									    // If m_mfxVppParams.vpp.Out.FourCC = MFX_FOURCC_NV12; then the output is interleaved chorma u1v1u2v2... and we need to access Y, and UV plane separetely 
									    // NOTE: NV12 is the native Gen HW format, so to select something different the IOPattern in VPP needs to use OUT_SYSTEM_MEMORY
									    fwrite(output->Data.Y, output->Data.Pitch, output->Info.Height, outfile);
									    fwrite(output->Data.UV, output->Data.Pitch, output->Info.Height / 2, outfile);
									    break;
									default:
									    printf("output surface FourCC format NOT SUPPORTED\n");
									    exit(-1);
									}
									fclose(outfile);
								}
								if (OUTPUTJPEG) {
									char str_src_buf[1000];
									std::string filename = output_location + "\\output%04d_dev%02d_cam%02d.jpg";
									snprintf(str_src_buf, sizeof(str_src_buf), filename.c_str(), i, d, c);
									switch (output->Info.FourCC) {
									case MFX_FOURCC_YV12:
										cv::imwrite(str_src_buf, YUV420_YV12toRGB(output->Data.Y, output->Data.U, output->Data.V, output->Data.Pitch, output->Info.Height));
										break;
									case MFX_FOURCC_NV12:
										//WhiteBalanceGWNV12(output->Data.UV, output->Info.Width / 2, output->Info.Height / 2, output->Data.Pitch);
										cv::imwrite(str_src_buf, YUV420_NV12toRGB(output->Data.Y, output->Data.UV, output->Data.Pitch, output->Info.Height));
										break;
									default:
										printf("output surface FourCC format NOT SUPPORTED\n");
										exit(-1);
									}
								}
								// after accesing the data we need to Unmap the surface
								sts = output->FrameInterface->Unmap(output);
								if (sts != MFX_ERR_NONE) printf("Error in Unmap()\n");
							}
#ifndef TEST_DPCPP_INTEROP
							// release the surface
							sts = output->FrameInterface->Release(output);
							if (sts != MFX_ERR_NONE) printf("Error Release(output)\n");
							if (false) { //(scaleFactor != 1.0) { // NOT USING decode_VPP with single call for decoding and VPP scaling due to complexity with tratment of surfaces (returns surface array)
								// for decode_VPP we need to release surfaces here (DecoderScaler is reusing output surface)
								sts = outputArray->Surfaces[0]->FrameInterface->Release(outputArray->Surfaces[0]);
								if (sts != MFX_ERR_NONE) printf("Error Release(outputArray->Surfaces[0])\n");
								sts = outputArray->Release(outputArray);
								if (sts != MFX_ERR_NONE) printf("Error Release(outputArray)\n");
							}
#else
							// put the surface into processing buffer
							decoded_frame[pipeNum] = output;
#endif
						}
#ifdef TEST_DPCPP_INTEROP
						// decoded frames ready
						frame_sync_point.arrive_and_wait();
#endif
						printf("Frame number: %d\r", i);
						fflush(stdout);
					}
				}
				if(encode) {
					// need to flush encoder
					mfxStatus enc_sts = MFX_ERR_NONE;
					do {
						enc_sts = pipelines[pipeNum]->encode_frame(nullptr);
						if (enc_sts == MFX_ERR_NONE) {
							auto bytes_written = store(pipelines[pipeNum]->bs_out(), numFrameSets++, d, c);
							(void)bytes_written;
						}
					} while (enc_sts == MFX_ERR_NONE);
				}

				auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - start);
				double delta = std::chrono::duration<double, std::milli>(duration).count();

				float fps = numFrameSets / (delta / 1000.);
				printf("\nExecution time, cam%02d: %3.2f s (%3.2f fps)\n", pipeNum, delta / 1000, fps);
				fSumFPS += fps;
				return MFX_ERR_NONE;
			};
			const auto pipeNum = d * NUM_CAMS + c;
			threads[pipeNum] = std::thread(f, d, c);
		}
	}
	for (int d = 0; d < NUM_DEVICES; d++)
	{
		for (int c = 0; c < NUM_CAMS; ++c)
		{
			const auto pipeNum = d * NUM_CAMS + c;
			threads[pipeNum].join();
		}
	}
	
	auto DURATION = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - START);
	double DELTA = std::chrono::duration<double, std::milli>(DURATION).count();

	for (auto& fs : in) {
		if (fs.is_open()) {
			fs.close();
			fs.clear();
		}
	}
	for (auto& fs : out) {
		if (fs.is_open()) {
			fs.close();
			fs.clear();
		}
	}
#ifdef TEST_DPCPP_INTEROP
	ofvi_run = 0;
	ofvi_thread.join();
	std::cout << "OFVI: processed frames = " << processed_frames << "; dropped frames = " << dropped_frames << std::endl;
	if (processed_frames) std::cout << "Mean frame processing time = " << processing_time/1000 / processed_frames << "ms" << std::endl;

#endif
	float FPS = numFrameSets / (DELTA / 1000.);
	if (encode && doScale)
		printf("\n\nTranscoderScaler test (Nframes=%d, [%d %d])", numFrameSets, finalW, finalH);
	if (encode && !doScale)
		printf("Transcoder test (Nframes=%d)", numFrameSets);
	if (!encode && doScale)
		printf("DecoderScaler test (Nframes=%d, [%d %d])", numFrameSets, finalW, finalH);
	if (!encode && !doScale)
		printf("DecoderOnly test (Nframes=%d)", numFrameSets);
	printf("\n\nAverage transcoding FPS (%3.2f fps) -- TOTAL FPS=(%3.2f fps)\n", fSumFPS / (NUM_DEVICES * NUM_CAMS), FPS);

	return 0;
}




