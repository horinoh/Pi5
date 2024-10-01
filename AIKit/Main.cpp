#include <cassert>
#include <iostream>
#include <thread>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-enum-enum-conversion"
#include <opencv2/opencv.hpp>
#pragma GCC diagnostic pop

#include <hailo/hailort.hpp>

void Infer(std::vector<hailort::InputVStream> &In, [[maybe_unused]]std::vector<hailort::OutputVStream> &Out) {
	auto IsRunning = true;

	auto InThread = std::thread([&]() {
		cv::VideoCapture Capture("instance_segmentation.mp4");
		cv::Mat Frame;

		//!< 入力の情報
		const auto& Shape = In.front().get_info().shape;
		constexpr auto Bpp = 1;

		//!< スレッド自身に終了判断させる
		while (IsRunning) {
			//!< キャプチャからフレームを取得
			Capture >> Frame;
			if(Frame.empty()) {
				break;
			}

  			if(3 == Frame.channels()) {
            	cv::cvtColor(Frame, Frame, cv::COLOR_BGR2RGB);
			}
        	if(static_cast<uint32_t>(Frame.cols) != Shape.width || static_cast<uint32_t>(Frame.rows) != Shape.height) {
				//!< 必要に応じてリサイズ
            	cv::resize(Frame, Frame, cv::Size(Shape.width, Shape.height), cv::INTER_AREA);
			}

			//cv::imshow("In", Frame);
			//cv::pollKey ();

			//!< AI の入力へ書き込み
			const auto Result = In[0].write(hailort::MemoryView(Frame.data, Shape.width * Shape.height * Shape.features * Bpp));
			if(HAILO_SUCCESS != Result) {
				std::cout << "write failed" << std::endl;
			}
		}		
	});

	auto OutThread = std::thread([&]() {
		std::vector<uint8_t> Data(Out[0].get_frame_size());

		const auto& Shape = Out.front().get_info().shape;

		//!< スレッド自身に終了判断させる
		while (IsRunning) {
			//!< AI からの出力を取得
			const auto Result = Out[0].read(hailort::MemoryView(std::data(Data), std::size(Data)));
			if(HAILO_SUCCESS != Result) {
				std::cout << "read failed" << std::endl;
			}

			//!< AI からの出力を CV 形式へ
			const auto InMat = cv::Mat(Shape.height, Shape.width, CV_32F, std::data(Data));
			//!< CV 形式の出力先
			auto OutMat = cv::Mat(Shape.height, Shape.width, CV_32F, cv::Scalar(0));

			//!< 深度マップの調整
			{
				//!< -InMat を指数として自然対数の底 e のべき乗が OutMat に返る
				cv::exp(-InMat, OutMat);
    			OutMat = 1 / (1 + OutMat);
    			OutMat = 1 / (OutMat * 10 + 0.009);
    
				double Mn, Mx;
    			cv::minMaxIdx(OutMat, &Mn, &Mx);
    			OutMat.convertTo(OutMat, CV_8U, 255 / (Mx - Mn), -Mn);

				//!< 白黒を反転 (手前が白)
				OutMat = 255 - OutMat;

				cv::imshow("Out", OutMat);
				cv::pollKey ();
			}
		}
	});

	while(IsRunning) {
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
	