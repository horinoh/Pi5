#if false
#include <format>
#else
#include <sstream>
#endif
#include <thread>

#include <hailo/hailort.hpp>

class Hailo 
{
public:
	//!< カメラ画像を取得するのに必要な、cv::VideoCapture() へ引数 (文字列) を作成
	//!< (OpenCV では BGR なので "format=BGR" を指定する必要がある)
	static std::string GetLibCameGSTStr(const int Width, const int Height, const int FPS) {
#if false
		return std::format("libcamerasrc ! video/x-raw, width={}, height={}, framerate={}/1, format=BGR, ! appsink", Width, Height, FPS);
#else
		std::stringstream SS;
		SS << "libcamerasrc ! video/x-raw, width=" << Width  << ", height=" << Height << ", framerate=" << FPS << "/1, format=BGR, ! appsink";	
		return SS.str();
#endif
	}

	virtual void Start(std::string_view HefFile, std::string_view CapturePath, std::function<bool()> Loop) {
		IsRunning = true;

		//!< デバイス
		const auto Device = hailort::Device::create_pcie(hailort::Device::scan_pcie().value()[0]);

		//!< ネットワーク
		auto Hef = hailort::Hef::create(std::data(HefFile));
		const auto ConfigureParams = Hef->create_configure_params(HAILO_STREAM_INTERFACE_PCIE).value();
		auto ConfiguredNetworkGroup = Device.value()->configure(Hef.value(), ConfigureParams)->at(0);
		
		//!< 入出力パラメータ
		const auto InputParams = ConfiguredNetworkGroup->make_input_vstream_params(true, HAILO_FORMAT_TYPE_UINT8, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE).value();
		const auto OutputParams = ConfiguredNetworkGroup->make_output_vstream_params(false, HAILO_FORMAT_TYPE_FLOAT32, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE).value();

		//!< 入出力 (入力へ書き込むと AI に処理されて出力に返る)
		auto InputVstreams = hailort::VStreamsBuilder::create_input_vstreams(*ConfiguredNetworkGroup, InputParams);
		auto OutputVstreams = hailort::VStreamsBuilder::create_output_vstreams(*ConfiguredNetworkGroup, OutputParams);

		//!< AI ネットワークのアクティベート
		const auto ActivatedNetworkGroup = ConfiguredNetworkGroup->activate();

		//!< 推論開始
		Inference(InputVstreams.value(), OutputVstreams.value(), CapturePath);

		//!< ループ
		while((IsRunning = Loop())) { 
		}

		//!< 終了、スレッド同期
		Join();
	}

	//!< 継承クラスでオーバーライド
	virtual void Inference([[maybe_unused]] std::vector<hailort::InputVStream> &In, [[maybe_unused]] std::vector<hailort::OutputVStream> &Out, [[maybe_unused]] std::string_view CapturePath) {
	}
	
	//!< スレッド同期
	void Join() {
		for(auto& i : Threads) { 
			i.join();
		}
	}
	
protected:
	bool IsRunning = false;
	std::vector<std::thread> Threads;
};
