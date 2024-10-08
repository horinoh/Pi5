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

class DepthEstimation : public Hailo
{
private:
	using Super = Hailo;

public:
	virtual void Inference(std::vector<hailort::InputVStream> &In, std::vector<hailort::OutputVStream> &Out, std::string_view CapturePath) override {
		//!< AI 入力スレッド
		Threads.emplace_back([&]() {
			cv::VideoCapture Capture(std::data(CapturePath));
			std::cout << Capture.get(cv::CAP_PROP_FRAME_WIDTH) << " x " << Capture.get(cv::CAP_PROP_FRAME_HEIGHT) << " @ " << Capture.get(cv::CAP_PROP_FPS) << std::endl;

			const auto& InShape = In.front().get_info().shape;
			cv::Mat InAI;
			//!< スレッド自身に終了判断させる
			while (IsRunning) {
				//!< 入力と出力がズレていかないように同期する
				if(InFrameCount > OutFrameCount) { continue; }
				++InFrameCount;

				//!< フレームを取得
				Capture >> ColorMap;

				//!< リサイズ
				if(static_cast<uint32_t>(ColorMap.cols) != InShape.width || static_cast<uint32_t>(ColorMap.rows) != InShape.height) {
            		cv::resize(ColorMap, InAI, cv::Size(InShape.width, InShape.height), cv::INTER_AREA);
				} else {
					InAI = ColorMap.clone();
				}

				//!< AI への入力 (書き込み)
				constexpr auto Bpp = 1;
				In[0].write(hailort::MemoryView(InAI.data, InShape.width * InShape.height * InShape.features * Bpp));
			}
		});

		//!< AI 出力スレッド
		Threads.emplace_back([&](){
			const auto& OutShape = Out.front().get_info().shape;
			std::vector<uint8_t> OutAI(Out[0].get_frame_size());
			//!< スレッド自身に終了判断させる
			while (IsRunning) {
				++OutFrameCount;

				//!< 出力を取得
				Out[0].read(hailort::MemoryView(std::data(OutAI), std::size(OutAI)));
				//!< OpenCV 形式へ
				const auto CVOutAI = cv::Mat(OutShape.height, OutShape.width, CV_32F, std::data(OutAI));

				OutMutex.lock(); {
					//!< 深度マップの調整
					DepthMap = cv::Mat(OutShape.height, OutShape.width, CV_32F, cv::Scalar(0));
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
				} OutMutex.unlock();
			}
		});
	}

	virtual int GetFps() const { return 30; }

	std::mutex& GetMutex() { return OutMutex; }

	const cv::Mat& GetColorMap() const { return ColorMap; }
	const cv::Mat& GetDepthMap() const { return DepthMap; }

protected:
	int InFrameCount = 0;
	int OutFrameCount = 0;

	std::mutex OutMutex;
	
	cv::Mat ColorMap;
	cv::Mat DepthMap;
};

//#define OUTPUT_VIDEO

int main(int argc, char* argv[]) {
	const auto Args = std::span<char*>(argv, argc);
	//!< 引数列挙
	//for(int Index = 0; auto i : Args){ std::cout << "Args[" << Index++ << "] " << i << std::endl; }

	//!< 入力キャプチャファイルを引数から取得、明示的に指定が無い場合はカメラ画像を使用
	//!< (カメラ画像は VideoCapture() へ Gstreamer の引数を渡す形で作成)
	const auto Cam = Hailo::GetLibCameGSTStr(1280, 960, 30);
	auto CapturePath = std::string_view(std::size(Args) > 1 ? Args[1] : std::data(Cam));
	std::cout << "Capturing : \"" << CapturePath << "\"" << std::endl;

	//!< 深度推定クラス
	DepthEstimation DepEst;
	
	//!< 表示用
	cv::Mat L, R, LR;
	const auto LSize = cv::Size(320, 240); //!< 左 (右も同じ) のサイズ 
#ifdef OUTPUT_VIDEO
	//!< ビデオ書き出し
	const auto Fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
	cv::VideoWriter WriterL("RGB.mp4", Fourcc, DepEst.GetFps(), LSize);
	cv::VideoWriter WriterR("D.mp4", Fourcc, DepEst.GetFps(), LSize, true);
#endif

	//!< 推定開始、ループ
	DepEst.Start("scdepthv3.hef", CapturePath, 
	[&]() {
		//!< 深度推定クラスからカラーマップ、深度マップを取得
		const auto& CM = DepEst.GetColorMap();
		const auto& DM = DepEst.GetDepthMap();
		if(CM.empty() || DM.empty()) { return true; }

		//!< 左 : カラーマップ
		cv::resize(CM, L, LSize, cv::INTER_AREA);

		//!< 右 : 深度マップ (AI の出力を別スレッドで深度マップへ加工しているので、ロックする必要がある)
		DepEst.GetMutex().lock(); {
			cv::resize(DM, R, LSize, cv::INTER_AREA);
		} DepEst.GetMutex().unlock();

#ifdef OUTPUT_VIDEO
		//!< 書き出し
		WriterL << L;
		WriterR << R;
#endif	

		//!< 連結する為に左右のタイプを 8UC3 に合わせる必要がある
		cv::cvtColor(R, R, cv::COLOR_GRAY2BGR);
		R.convertTo(R, CV_8UC3);
		//!< 水平連結
		cv::hconcat(L, R, LR);

		//!< 表示
		cv::imshow("Color & Depth Map", LR);

		constexpr auto ESC = 27;
		if(ESC == cv::pollKey()) {
			return false;
		}
		return true;
	});

	exit(EXIT_SUCCESS);
}
	