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
			while (Flags.all()) {
				//!< 入力と出力がズレていかないように同期する
				if(InFrameCount > OutFrameCount) { continue; }
				++InFrameCount;

				//!< フレームを取得
				Capture >> ColorMap;
				if(ColorMap.empty()) {
					std::cout << "Lost input" << std::endl;
					Flags.reset(static_cast<size_t>(FLAGS::HasInput));
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
			while (Flags.all()) {
				++OutFrameCount;

				//!< 出力を取得
				Front.read(hailort::MemoryView(std::data(OutAI), std::size(OutAI)));

				//!< 先頭 uint16_t に「検出数」が格納されている
				const auto Count = *reinterpret_cast<uint16_t*>(std::data(OutAI));
				//!< 「検出数」分オフセット
				auto Offset = sizeof(Count);

				//!< 検出格納先
				std::vector<hailo_detection_with_byte_mask_t> Detections;
				Detections.reserve(Count);
				//!< 検出を格納
				for(auto i = 0; i<Count;++i){
					const auto Detection = reinterpret_cast<hailo_detection_with_byte_mask_t*>(std::data(OutAI) + Offset);
					Detections.emplace_back(*Detection);
					Offset += sizeof(*Detection) + Detection->mask_size;
				}

				OutMutex.lock(); {
					DetectionMap = cv::Mat(Shape.height, Shape.width, CV_8UC3, cv::Vec3b(0, 0, 0));
					for(auto& i : Detections) {
						//i.class_id;
						//i.score;	

						//!< ボックスサイズはピクセルではなく、画面に対する比率で格納されている
						const int BoxL = i.box.x_min * Shape.width;
						const int BoxT = i.box.y_min * Shape.height;
 						const int BoxW = ceil((i.box.x_max - i.box.x_min) * Shape.width);
        				const int BoxH = ceil((i.box.y_max - i.box.y_min) * Shape.height);
						for(auto h = 0;h < BoxH;++h) {
							for(auto w = 0;w < BoxW;++w) {
								if(i.mask[h * BoxW + w]) {
									//!< ROI (Region Of Interest)
								}
							}
						}
						cv::rectangle(DetectionMap, cv::Rect(BoxL, BoxT, BoxW, BoxH), cv::Vec3b(0, 255, 0), 1);
					}	
				} OutMutex.unlock();
			}
		});
	}

	virtual int GetFps() const { return 30; }

	std::mutex& GetMutex() { return OutMutex; }

	const cv::Mat& GetColorMap() const { return ColorMap; }
	const cv::Mat& GetDetectionMap() const { return DetectionMap; }

protected:
	int InFrameCount = 0;
	int OutFrameCount = 0;

	std::mutex OutMutex;
	
	cv::Mat ColorMap;
	cv::Mat DetectionMap;
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
		const auto& DM = Seg.GetDetectionMap();
		if(CM.empty() || DM.empty()) { return true; }

		//!< 左 : カラーマップ
		cv::resize(CM, L, LSize, cv::INTER_AREA);

		//!< 右 : 検出マップ (AI の出力を別スレッドで深度マップへ加工しているので、ロックする必要がある)
		Seg.GetMutex().lock(); {
			cv::resize(DM, R, LSize, cv::INTER_AREA);
		} Seg.GetMutex().unlock();

#if false
		//!< 水平連結
		cv::hconcat(L, R, LR);
#else
		//!< ブレンド
 		cv::addWeighted(L, 1, R, 0.7, 0.0, LR);
#endif

		//!< 表示
		cv::imshow("Color & Detection Map", LR);

		constexpr auto ESC = 27;
		if(ESC == cv::pollKey()) {
			return false;
		}
		return true;
	});

	exit(EXIT_SUCCESS);
}
	