#include <cassert>
#include <iostream>
#if false
#include <format>
#else
#include <sstream>
#endif
#include <thread>
#include <mutex>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-enum-enum-conversion"
#include <opencv2/opencv.hpp>
#pragma GCC diagnostic pop

#include <hailo/hailort.hpp>

void Infer(std::vector<hailort::InputVStream> &In, [[maybe_unused]]std::vector<hailort::OutputVStream> &Out) {
	auto IsRunning = true;

	auto InFrame = 0;
	auto OutFrame = 0;

	//!< AI への入力
	std::mutex InMutex;
	const auto& InShape = In.front().get_info().shape;
	std::cout << InShape.width << " x " << InShape.height << std::endl;
	cv::Mat OrigColor, Color;
	auto InThread = std::thread([&]() {
#if 0
		//!< 動画を入力
		cv::VideoCapture Capture("instance_segmentation.mp4");
		//cv::VideoCapture Capture("Sample.mp4");
#else
		//!< カメラ画像を入力
		//!< (Gstream の引数に、OpenCV では "format=BGR" を指定する必要がある)	
	#if false
		cv::VideoCapture Capture(std::data(std::format("libcamerasrc ! video/x-raw, width={}, height={}, framerate={}/1, format=BGR, ! appsink", InShape.width, InShape.height, 30)));
	#else
		std::stringstream SS;
		SS << "libcamerasrc ! video/x-raw, width=" << InShape.width  << ", height=" << InShape.height << ", framerate=" << 30 << "/1, format=BGR, ! appsink";	
		cv::VideoCapture Capture(std::data(SS.str()));
	#endif
#endif
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
				Capture >> OrigColor;
			}
			//InMutex.unlock();

			//!< 必要に応じてリサイズ
			if(static_cast<uint32_t>(OrigColor.cols) != InShape.width || static_cast<uint32_t>(OrigColor.rows) != InShape.height) {
            	cv::resize(OrigColor, Color, cv::Size(InShape.width, InShape.height), cv::INTER_AREA);
			} else {
				Color = OrigColor.clone();
			}
  			// if(3 == Color.channels()) {
            // 	cv::cvtColor(Color, Color, cv::COLOR_BGR2RGB);
			// }

			//!< AI への入力 (書き込み)
			In[0].write(hailort::MemoryView(Color.data, InShape.width * InShape.height * InShape.features * Bpp));
		}		
	});

	//!< AI からの出力
	std::mutex OutMutex;
	const auto& OutShape = Out.front().get_info().shape;
	auto Depth = cv::Mat(OutShape.height, OutShape.width, CV_32F, cv::Scalar(0));
	auto OutThread = std::thread([&]() {
		std::vector<uint8_t> Data(Out[0].get_frame_size());

		//!< スレッド自身に終了判断させる
		while (IsRunning) {
			++OutFrame;

			//!< 出力を取得
			Out[0].read(hailort::MemoryView(std::data(Data), std::size(Data)));

			//!< OpenCV 形式へ
			const auto InMat = cv::Mat(OutShape.height, OutShape.width, CV_32F, std::data(Data));
				
			OutMutex.lock();
			//!< 深度マップの調整
			{
				//!< -InMat を指数として自然対数の底 e のべき乗が OutMat に返る
				cv::exp(-InMat, Depth);
    			Depth = 1 / (1 + Depth);
    			Depth = 1 / (Depth * 10 + 0.009);
    
				double Mn, Mx;
    			cv::minMaxIdx(Depth, &Mn, &Mx);
				//!< 手前が黒、奥が白
    			//Depth.convertTo(Depth, CV_8U, 255 / (Mx - Mn), -Mn);
				//!< 手前が白、奥が黒
    			Depth.convertTo(Depth, CV_8U, -255 / (Mx - Mn), -Mn + 255);
			}
			OutMutex.unlock();
		}
	});

	constexpr auto ESC = 27;
	//!< 表示用
	cv::Mat L, R, LR;
	const auto LSize = cv::Size(320, 240);

	//!< ビデオ書き出し
	const auto Fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
	cv::VideoWriter WriterL("RGB.mp4", Fourcc, 30, LSize);
	cv::VideoWriter WriterR("D.mp4", Fourcc, 30, LSize, true);

	//!< メインループ
	while(IsRunning) {
		if(!OrigColor.empty() && !Depth.empty()) {
			//!< 左 : カラーマップ
			//InMutex.lock();
			{
				cv::resize(OrigColor, L, LSize, cv::INTER_AREA);
			}
			//InMutex.unlock();

			//!< 右 : 深度マップ
			OutMutex.lock();
			{
				cv::resize(Depth, R, LSize, cv::INTER_AREA);
			}
			OutMutex.unlock();

			//!< 連結する為に左右のタイプを 8UC3 で合わせる必要がある
			cv::cvtColor(R, R, cv::COLOR_GRAY2BGR);
			R.convertTo(R, CV_8UC3);
			//!< (水平)連結
			cv::hconcat(L, R, LR);

			//!< 表示
			cv::imshow("Color Depth", LR);

			//!< 書き出し
			WriterL << L;
			WriterR << R;
		}
		
		//!< ループを抜けます
		if(ESC == cv::pollKey()) {
			IsRunning = false;
		}
	}

	InThread.join();
	OutThread.join();
}

int main() {
	//!< デバイス
	auto Device = hailort::Device::create_pcie(hailort::Device::scan_pcie().value()[0]);
	std::cout << "Device status = " << Device.status() << std::endl;

	//!< ネットワーク
	auto Hef = hailort::Hef::create("scdepthv3.hef");
	const auto NetworkGroups = Device.value()->configure(Hef.value(), Hef->create_configure_params(HAILO_STREAM_INTERFACE_PCIE).value());
	std::cout << "NetworkGroups status = " << NetworkGroups.status() << ", size = " << NetworkGroups->size() << std::endl;
	const auto NetworkGroup = NetworkGroups->at(0);

	//!< AI 入出力 (入力へ書き込むと AI に処理されて出力に返る)
	auto InputVstreams = hailort::VStreamsBuilder::create_input_vstreams(*NetworkGroup, NetworkGroup->make_input_vstream_params(true, HAILO_FORMAT_TYPE_UINT8, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE).value());
	auto OutputVstreams = hailort::VStreamsBuilder::create_output_vstreams(*NetworkGroup, NetworkGroup->make_output_vstream_params(false, HAILO_FORMAT_TYPE_FLOAT32, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE).value());

	//!< ネットワークのアクティベート
	const auto Activated = NetworkGroup->activate();
	if(!Activated){
		std::cout << "activate failed" << std::endl;
	}

	//!< 推論の起動
	Infer(*InputVstreams, *OutputVstreams);

	return 0;
}
	