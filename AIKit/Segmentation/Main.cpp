//#include <cassert>
#include <iostream>
#include <mutex>
#include <span>

#include "../Hailo.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-enum-enum-conversion"
#include <opencv2/opencv.hpp>
#pragma GCC diagnostic pop

class Segmentation : public Hailo
{
private:
	using Super = Hailo;

public:
	virtual void Inference(std::vector<hailort::InputVStream> &In, std::vector<hailort::OutputVStream> &Out, std::string_view CapturePath) override {
		//!< AI 入力スレッド
		Threads.emplace_back([&]() {
			cv::VideoCapture Capture(std::data(CapturePath));
			std::cout << Capture.get(cv::CAP_PROP_FRAME_WIDTH) << " x " << Capture.get(cv::CAP_PROP_FRAME_HEIGHT) << " @ " << Capture.get(cv::CAP_PROP_FPS) << std::endl;

			auto& Front = In.front();
			const auto& Shape = Front.get_info().shape;
			cv::Mat InAI;
			//!< スレッド自身に終了判断させる
			while (IsRunning) {
				//!< 入力と出力がズレていかないように同期する
				if(InFrameCount > OutFrameCount) { continue; }
				++InFrameCount;

				//!< フレームを取得
				Capture >> ColorMap;
				if(ColorMap.empty()) {
					std::cout << "Lost input" << std::endl;
					HasInput = false;
					break;
				}

				//!< リサイズ
				if(static_cast<uint32_t>(ColorMap.cols) != Shape.width || static_cast<uint32_t>(ColorMap.rows) != Shape.height) {
            		cv::resize(ColorMap, InAI, cv::Size(Shape.width, Shape.height), cv::INTER_AREA);
				} else {
					InAI = ColorMap.clone();
				}

				//!< AI への入力 (書き込み)
				Front.write(hailort::MemoryView(InAI.data, Front.get_frame_size()));
			}
		});

		//!< AI 出力スレッド
		Threads.emplace_back([&](){
			auto& Front = Out.front();
			const auto& Shape = Front.get_info().shape;
			std::vector<uint8_t> OutAI(Front.get_frame_size());
			//!< スレッド自身に終了判断させる
			while (IsRunning) {
				++OutFrameCount;

				//!< 出力を取得
				Front.read(hailort::MemoryView(std::data(OutAI), std::size(OutAI)));

				{
					//!< 先頭 uint16_t に検出数が格納されている
					const auto Count = *reinterpret_cast<uint16_t*>(std::data(OutAI));
					//!< その分オフセット
					auto Offset = sizeof(uint16_t);

					//!< 検出格納先
					std::vector<hailort::hailo_detection_with_byte_mask_t> Detections;
					Detectons.reserve(Count);
					//!< 検出を格納
					for(auto i = 0; i<Count;++i){
						const auto Detection = reuinterpret_cast<hailort::hailo_detection_with_byte_mask_t*>(std::data(OutAI) + Offset);
						Detections.emplace_back(*Detection);
						Offset += sizeof(*Detection) + Detection->mask_size;
					}

					for(auto& i : Detections) {
						//!< 幅、高さはピクセルではなく、画面に対する比率で格納されている
						const auto WidthRate = i.box.x_max - i.box.x_min;
						const auto HeightRate = i.box.y_max - i.box.y_min;
						//!< 検出物毎にクラスが異なる
						i.class_id;

						
					}
				}


				//!< OpenCV 形式へ
				const auto CVOutAI = cv::Mat(Shape.height, Shape.width, CV_32F, std::data(OutAI));

				OutMutex.lock(); {
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

int main(int argc, char* argv[]) {
	const auto Args = std::span<char*>(argv, argc);
	//!< 引数列挙
	//for(int Index = 0; auto i : Args){ std::cout << "Args[" << Index++ << "] " << i << std::endl; }

	//!< 入力キャプチャファイルを引数から取得、明示的に指定が無い場合はカメラ画像を使用
	//!< (カメラ画像は VideoCapture() へ Gstreamer の引数を渡す形で作成)
	const auto Cam = Hailo::GetLibCameGSTStr(1280, 960, 30);
	auto CapturePath = std::string_view(std::size(Args) > 1 ? Args[1] : std::data(Cam));
	std::cout << "Capturing : \"" << CapturePath << "\"" << std::endl;

	//!< セグメンテーションクラス
	Segmentation Seg;
	
	//!< 表示用
	cv::Mat L, R, LR;
	const auto LSize = cv::Size(320, 240); //!< 左 (右も同じ) のサイズ 

	//!< 推定開始、ループ
	Seg.Start("yolov5m-seg.hef", CapturePath, 
	[&]() {
		//!< 深度推定クラスからカラーマップ、深度マップを取得
		const auto& CM = Seg.GetColorMap();
		const auto& DM = Seg.GetDepthMap();
		if(CM.empty() || DM.empty()) { return true; }

		//!< 左 : カラーマップ
		cv::resize(CM, L, LSize, cv::INTER_AREA);

		//!< 右 : 深度マップ (AI の出力を別スレッドで深度マップへ加工しているので、ロックする必要がある)
		DepEst.GetMutex().lock(); {
			cv::resize(DM, R, LSize, cv::INTER_AREA);
		} DepEst.GetMutex().unlock();

		//!< 連結する為に左右のタイプを 8UC3 に合わせる必要がある
		cv::cvtColor(R, R, cv::COLOR_GRAY2BGR);
		R.convertTo(R, CV_8UC3);
		//!< 水平連結
		cv::hconcat(L, R, LR);

		//!< 表示
		cv::imshow("", LR);

		constexpr auto ESC = 27;
		if(ESC == cv::pollKey()) {
			return false;
		}
		return true;
	});

	exit(EXIT_SUCCESS);
}
	