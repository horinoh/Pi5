#include <cassert>
#include <iostream>
#include <thread>
#include <mutex>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-enum-enum-conversion"
#include <opencv2/opencv.hpp>
#pragma GCC diagnostic pop

#include <hailo/hailort.hpp>

void Infer(std::vector<hailort::InputVStream> &In, [[maybe_unused]]std::vector<hailort::OutputVStream> &Out) {
	auto IsRunning = true;

	//!< 入力 (AI への書き込み)
	std::mutex InMutex;
	const auto& InShape = In.front().get_info().shape;
	cv::Mat Color;
	auto InThread = std::thread([&]() {
		cv::VideoCapture Capture("instance_segmentation.mp4");
		constexpr auto Bpp = 1;

		//!< スレッド自身に終了判断させる
		while (IsRunning) {
			//!< キャプチャからフレームを取得
			InMutex.lock();
			{
				Capture >> Color;
				//!< 必要に応じてリサイズ
				if(static_cast<uint32_t>(Color.cols) != InShape.width || static_cast<uint32_t>(Color.rows) != InShape.height) {
            		cv::resize(Color, Color, cv::Size(InShape.width, InShape.height), cv::INTER_AREA);
				}
  				// if(3 == Color.channels()) {
            	// 	cv::cvtColor(Color, Color, cv::COLOR_BGR2RGB);
				// }
			}
			InMutex.unlock();

			//!< AI の入力へ書き込み
			In[0].write(hailort::MemoryView(Color.data, InShape.width * InShape.height * InShape.features * Bpp));
		}		
	});

	//!< 出力 (AI からの読み込み)
	std::mutex OutMutex;
	const auto& OutShape = Out.front().get_info().shape;
	auto Depth = cv::Mat(OutShape.height, OutShape.width, CV_32F, cv::Scalar(0));
	auto OutThread = std::thread([&]() {
		std::vector<uint8_t> Data(Out[0].get_frame_size());

		//!< スレッド自身に終了判断させる
		while (IsRunning) {
			//!< AI からの出力を取得
			Out[0].read(hailort::MemoryView(std::data(Data), std::size(Data)));

			//!< AI からの出力を CV 形式へ
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
    			Depth.convertTo(Depth, CV_8U, 255 / (Mx - Mn), -Mn);

				//!< 白黒を反転 (手前が白)
				Depth = 255 - Depth;
			}
			OutMutex.unlock();
		}
	});

	constexpr auto ESC = 27;
	while(IsRunning) {
		//!< カラー画像を表示
		InMutex.lock();
		if(!Color.empty()) {
			cv::imshow("Color", Color);
		}
		InMutex.unlock();

		//!< 深度画像を表示
		OutMutex.lock();
		if(!Depth.empty()) {
			cv::imshow("Depth", Depth);
		}
		OutMutex.unlock();

		//!< ループ脱出
		if(ESC == cv::pollKey()) {
			IsRunning=false;
		}
	}

	InThread.join();
	OutThread.join();
}

int main() {
#if 0
	cv::VideoCapture Cap("instance_segmentation.mp4");
	cv::Mat Frame;

	while(true){
		Cap >> Frame;
		cv::imshow("Win", Frame);
		cv::pollKey ();
	}

	//auto Output = cv::Mat::zeros( 120, 350, CV_8UC3 );
    cv::waitKey(0);
#else
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
#endif

	return 0;
}
	