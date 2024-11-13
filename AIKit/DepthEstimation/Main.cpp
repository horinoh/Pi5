//#include <cassert>
#include <iostream>
#include <mutex>
#include <span>

#include "../Hailo.h"
#include "../CV.h"

class DepthEstimation : public Hailo
{
private:
	using Super = Hailo;

public:
#ifdef USE_HAILOCPP
	virtual void Inference(std::vector<hailort::InputVStream>& In, std::vector<hailort::OutputVStream>& Out, std::string_view VideoPath) override {
		//!< AI 入力スレッド
		Threads.emplace_back([&]() {
			cv::VideoCapture Capture(std::data(VideoPath));
			std::cout << Capture.get(cv::CAP_PROP_FRAME_WIDTH) << " x " << Capture.get(cv::CAP_PROP_FRAME_HEIGHT) << " @ " << Capture.get(cv::CAP_PROP_FPS) << std::endl;

			auto& Front = In.front();
			const auto& Shape = Front.get_info().shape;
			cv::Mat InAI;
			//!< スレッド自身に終了判断させる
			while (Flags.all()) {
				//!< 入力と出力がズレていかないように同期する
				if (InFrameCount > OutFrameCount) { continue; }
				++InFrameCount;

				//!< フレームを取得
				Capture >> ColorMap;
				if (ColorMap.empty()) {
					std::cout << "Lost input" << std::endl;
					Flags.reset(static_cast<size_t>(FLAGS::HasInput));
					break;
				}

				//!< リサイズ
				if (static_cast<uint32_t>(ColorMap.cols) != Shape.width || static_cast<uint32_t>(ColorMap.rows) != Shape.height) {
					cv::resize(ColorMap, InAI, cv::Size(Shape.width, Shape.height), cv::INTER_AREA);
				}
				else {
					InAI = ColorMap.clone();
				}

				//!< AI への入力 (書き込み)
				Front.write(hailort::MemoryView(InAI.data, Front.get_frame_size()));
			}
			});

		//!< AI 出力スレッド
		Threads.emplace_back([&]() {
			auto& Front = Out.front();
			const auto& Shape = Front.get_info().shape;
			std::vector<uint8_t> OutAI(Front.get_frame_size());
			//!< スレッド自身に終了判断させる
			while (Flags.all()) {
				++OutFrameCount;

				//!< 出力を取得
				Front.read(hailort::MemoryView(std::data(OutAI), std::size(OutAI)));

				//!< OpenCV 形式へ
				const auto CVOutAI = cv::Mat(Shape.height, Shape.width, CV_32F, std::data(OutAI));

				{
					std::lock_guard Lock(OutMutex);

					//!< 深度マップの調整
					DepthMap = cv::Mat(Shape.height, Shape.width, CV_32F, cv::Scalar(0));
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
			}
			});
	}
#else
	virtual void Inference(std::vector<hailo_input_vstream>& InVS, std::vector<hailo_output_vstream>& OutVS, std::string_view VideoPath) override {
		std::cout << "InVS[" << std::size(InVS) << "]" << std::endl;
		std::cout << "OutVS[" << std::size(OutVS) << "]" << std::endl;
		
		//!< AI 入力スレッド
		Threads.emplace_back([&]() {
			cv::VideoCapture Capture(std::data(VideoPath));
			std::cout << Capture.get(cv::CAP_PROP_FRAME_WIDTH) << " x " << Capture.get(cv::CAP_PROP_FRAME_HEIGHT) << " @ " << Capture.get(cv::CAP_PROP_FPS) << std::endl;

			auto& In = InVS[0];

			hailo_vstream_info_t Info;
			VERIFY_HAILO_SUCCESS(hailo_get_input_vstream_info(In, &Info));
			const auto& Shape = Info.shape;
			size_t FrameSize;
			VERIFY_HAILO_SUCCESS(hailo_get_input_vstream_frame_size(In, &FrameSize));

			cv::Mat InAI;
			//!< スレッド自身に終了判断させる
			while (Flags.all()) {
				//!< 入力と出力がズレていかないように同期する
				if (InFrameCount > OutFrameCount) { continue; }
				++InFrameCount;

				//!< フレームを取得
				Capture >> ColorMap;
				if (ColorMap.empty()) {
					std::cout << "Lost input" << std::endl;
					Flags.reset(static_cast<size_t>(FLAGS::HasInput));
					break;
				}

				//!< リサイズ
				if (static_cast<uint32_t>(ColorMap.cols) != Shape.width || static_cast<uint32_t>(ColorMap.rows) != Shape.height) {
					cv::resize(ColorMap, InAI, cv::Size(Shape.width, Shape.height), cv::INTER_AREA);
				}
				else {
					InAI = ColorMap.clone();
				}

				//!< AI への入力 (書き込み)
				VERIFY_HAILO_SUCCESS(hailo_vstream_write_raw_buffer(In, InAI.data, FrameSize));
				std::cout << "In Write Size = " << FrameSize << std::endl;
			}
			});

		//!< AI 出力スレッド
		Threads.emplace_back([&]() {
			auto& Out = OutVS[0];

			hailo_vstream_info_t Info;
			VERIFY_HAILO_SUCCESS(hailo_get_output_vstream_info(Out, &Info));
			const auto& Shape = Info.shape;
			size_t FrameSize;
			VERIFY_HAILO_SUCCESS(hailo_get_output_vstream_frame_size(Out, &FrameSize));

			std::vector<uint8_t> OutAI(FrameSize);
			//!< スレッド自身に終了判断させる
			while (Flags.all()) {
				++OutFrameCount;

				//!< 出力を取得
				VERIFY_HAILO_SUCCESS(hailo_vstream_read_raw_buffer(Out, std::data(OutAI), FrameSize));
				std::cout << "Out Read Size = " << FrameSize << std::endl;

				//!< OpenCV 形式へ
				const auto CVOutAI = cv::Mat(Shape.height, Shape.width, CV_32F, std::data(OutAI));

				{
					std::lock_guard Lock(OutMutex);

					//!< 深度マップの調整
					DepthMap = cv::Mat(Shape.height, Shape.width, CV_32F, cv::Scalar(0));
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
			}
			});
	}
#endif

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
	auto VideoPath = std::string_view(std::size(Args) > 1 ? Args[1] : std::data(Cam));
	std::cout << "Capturing : \"" << VideoPath << "\"" << std::endl;

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
	DepEst.Start("scdepthv3.hef", VideoPath,
		[&]() {
			//!< 深度推定クラスからカラーマップ、深度マップを取得
			const auto& CM = DepEst.GetColorMap();
			const auto& DM = DepEst.GetDepthMap();
			if (CM.empty() || DM.empty()) { return true; }

			//!< 左 : カラーマップ
			cv::resize(CM, L, LSize, cv::INTER_AREA);

			//!< 右 : 深度マップ (AI の出力を別スレッドで深度マップへ加工しているので、ロックする必要がある)
			{
				std::lock_guard Lock(DepEst.GetMutex());

				cv::resize(DM, R, LSize, cv::INTER_AREA);
			}

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
			if (ESC == cv::pollKey()) {
				return false;
			}
			return true;
		});

	exit(EXIT_SUCCESS);
}
