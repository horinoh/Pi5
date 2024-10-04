#include <cassert>
#include <iostream>
#if false
#include <format>
#else
#include <sstream>
#endif
#include <thread>
#include <mutex>
#include <span>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-enum-enum-conversion"
#include <opencv2/opencv.hpp>
#pragma GCC diagnostic pop

#include <hailo/hailort.hpp>

//#define OUTPUT_VIDEO

void Infer(std::vector<hailort::InputVStream> &In, [[maybe_unused]]std::vector<hailort::OutputVStream> &Out, const char* InFile) {
	auto IsRunning = true;

	//!< 同期用
	auto InFrame = 0;
	auto OutFrame = 0;

	constexpr auto Fps = 30;

	//!< AI への入力
	//std::mutex InMutex;
	const auto& InShape = In.front().get_info().shape;
	cv::Mat ColorMap;
	auto InThread = std::thread([&]() {
		cv::Mat InAI;

		//!< カメラ : VideoCapture() へ Gstreamer 引数を渡す形で作成すれば良い (OpenCV では "format=BGR" を追加指定する必要があるので注意)
#if false
		const auto LibCam = std::format("libcamerasrc ! video/x-raw, width={}, height={}, framerate={}/1, format=BGR, ! appsink", InShape.width, InShape.height, Fps);
#else
		std::stringstream SS;
		SS << "libcamerasrc ! video/x-raw, width=" << InShape.width  << ", height=" << InShape.height << ", framerate=" << Fps << "/1, format=BGR, ! appsink";	
		const auto LibCam = SS.str();
#endif

		//!< キャプチャ : 入力ファイルの指定が無い場合はカメラ
		cv::VideoCapture Capture(nullptr != InFile ? InFile : std::data(LibCam));

		constexpr auto Bpp = 1;

		//!< スレッド自身に終了判断させる
		while (IsRunning) {
			//!< 入力と出力がズレていかないように同期する
			if(InFrame > OutFrame) {
				continue;
			}
			++InFrame;

			//!< フレームを取得
			//InMutex.lock();
			{
				Capture >> ColorMap;
			}
			//InMutex.unlock();

			//!< リサイズ
			if(static_cast<uint32_t>(ColorMap.cols) != InShape.width || static_cast<uint32_t>(ColorMap.rows) != InShape.height) {
            	cv::resize(ColorMap, InAI, cv::Size(InShape.width, InShape.height), cv::INTER_AREA);
			} else {
				InAI = ColorMap.clone();
			}

			//!< AI への入力 (書き込み)
			In[0].write(hailort::MemoryView(InAI.data, InShape.width * InShape.height * InShape.features * Bpp));
		}		
	});

	//!< AI からの出力
	std::mutex OutMutex;
	const auto& OutShape = Out.front().get_info().shape;
	auto DepthMap = cv::Mat(OutShape.height, OutShape.width, CV_32F, cv::Scalar(0));
	auto OutThread = std::thread([&]() {
		std::vector<uint8_t> OutAI(Out[0].get_frame_size());

		//!< スレッド自身に終了判断させる
		while (IsRunning) {
			++OutFrame;

			//!< 出力を取得
			Out[0].read(hailort::MemoryView(std::data(OutAI), std::size(OutAI)));
			//!< OpenCV 形式へ
			const auto CVOutAI = cv::Mat(OutShape.height, OutShape.width, CV_32F, std::data(OutAI));
				
			OutMutex.lock();
			//!< 深度マップの調整
			{
				//!< -CVOutAI を指数として自然対数の底 e のべき乗が DepthMap に返る
				cv::exp(-CVOutAI, DepthMap);
    			DepthMap = 1 / (1 + DepthMap);
    			DepthMap = 1 / (DepthMap * 10 + 0.009);
    
				double Mn, Mx;
    			cv::minMaxIdx(DepthMap, &Mn, &Mx);
				//!< 手前が黒、奥が白
    			//DepthMap.convertTo(DepthMap, CV_8U, 255 / (Mx - Mn), -Mn);
				//!< 手前が白、奥が黒 (逆)
    			DepthMap.convertTo(DepthMap, CV_8U, -255 / (Mx - Mn), -Mn + 255);
			}
			OutMutex.unlock();
		}
	});

	constexpr auto ESC = 27;
	//!< 表示用
	cv::Mat L, R, LR;
	const auto LSize = cv::Size(320, 240); //!< 左のサイズ (右も同じ)

#ifdef OUTPUT_VIDEO
	//!< ビデオ書き出し
	const auto Fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
	cv::VideoWriter WriterL("RGB.mp4", Fourcc, Fps, LSize);
	cv::VideoWriter WriterR("D.mp4", Fourcc, Fps, LSize, true);
#endif

	//!< メインループ
	while(IsRunning) {
		if(!ColorMap.empty() && !DepthMap.empty()) {
			//!< 左 : カラーマップ
			//InMutex.lock();
			{
				cv::resize(ColorMap, L, LSize, cv::INTER_AREA);
			}
			//InMutex.unlock();

			//!< 右 : 深度マップ
			OutMutex.lock();
			{
				cv::resize(DepthMap, R, LSize, cv::INTER_AREA);
			}
			OutMutex.unlock();

			//!< 連結する為に左右のタイプを 8UC3 に合わせる
			cv::cvtColor(R, R, cv::COLOR_GRAY2BGR);
			R.convertTo(R, CV_8UC3);
			//!< (水平) 連結
			cv::hconcat(L, R, LR);

			//!< 表示
			cv::imshow("Color Depth", LR);

#ifdef OUTPUT_VIDEO
			//!< 書き出し
			WriterL << L;
			WriterR << R;
#endif
		}
		
		//!< ループを抜けます
		if(ESC == cv::pollKey()) {
			IsRunning = false;
		}
	}

	InThread.join();
	OutThread.join();
}

int main(int argc, char* argv[]) {
	//!< 引数
	const auto Args = std::span<char*>(argv, argc);
	for(int Index = 0; auto i : Args){
		std::cout << "Args[" << Index++ << "] " << i << std::endl;
	}

	//!< デバイス
	auto Device = hailort::Device::create_pcie(hailort::Device::scan_pcie().value()[0]);

	//!< ネットワーク
	auto Hef = hailort::Hef::create("scdepthv3.hef"); //!< 引数にする?
	const auto NetworkGroups = Device.value()->configure(Hef.value(), Hef->create_configure_params(HAILO_STREAM_INTERFACE_PCIE).value());
	const auto NetworkGroup = NetworkGroups->at(0);

	//!< AI 入出力 (入力へ書き込むと AI に処理されて出力に返る)
	auto InputVstreams = hailort::VStreamsBuilder::create_input_vstreams(*NetworkGroup, NetworkGroup->make_input_vstream_params(true, HAILO_FORMAT_TYPE_UINT8, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE).value());
	auto OutputVstreams = hailort::VStreamsBuilder::create_output_vstreams(*NetworkGroup, NetworkGroup->make_output_vstream_params(false, HAILO_FORMAT_TYPE_FLOAT32, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE).value());

	//!< AI ネットワークのアクティベート
	auto Activated = NetworkGroup->activate();

	//!< 入力ファイルパス(引数から取得)、指定が無い場合はカメラ画像を使用
	const auto InFile = std::size(Args) > 1 ? Args[1] : nullptr;
	//!< 推論の起動
	Infer(*InputVstreams, *OutputVstreams, InFile);

	return 0;
}
	