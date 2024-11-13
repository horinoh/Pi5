//#include <cassert>
#include <iostream>
#include <mutex>
#include <span>

#include "../Hailo.h"
#include "../CV.h"

class InstanceSegmentation : public Hailo
{
private:
	using Super = Hailo;

public:
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
					//!< 比率を維持したままモデルサイズを満たすようにするファクタ
					const auto Factor = std::max(static_cast<float>(ColorMap.cols) / Shape.width, static_cast<float>(ColorMap.rows) / Shape.height);
					std::cout << "\tFactor = " << Factor << std::endl;

					//!< 比率を維持したリサイズ
					cv::resize(ColorMap, ColorMap, cv::Size(static_cast<int>(ColorMap.cols / Factor), static_cast<int>(ColorMap.rows / Factor)), cv::INTER_AREA);
					std::cout << "\tResize = " << ColorMap.cols << " x " << ColorMap.rows << std::endl;

					//!< モデルサイズになるように黒帯で埋める
					cv::Mat Padded;
					cv::copyMakeBorder(ColorMap, InAI,
						0, std::max<int>(ColorMap.rows - Shape.height, Shape.height - ColorMap.rows),
						0, std::max<int>(ColorMap.cols - Shape.width, Shape.width - ColorMap.cols),
						cv::BORDER_CONSTANT, cv::Scalar(0, 0, 0));
					std::cout << "\tPad = " << ColorMap.cols << " x " << ColorMap.rows << std::endl;
				}
				else {
					InAI = ColorMap.clone();
				}
				std::cout << "\tIn = " << InAI.cols << " x " << InAI.rows << std::endl;

				std::cout << "\tMat Size = " << InAI.total() * InAI.elemSize() << std::endl;

				//!< AI への入力 (書き込み)
				VERIFY_HAILO_SUCCESS(hailo_vstream_write_raw_buffer(In, InAI.data, FrameSize));
				std::cout << "[" << InFrameCount << "] In Write Size = " << FrameSize << std::endl;
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
				std::cout << "[" << OutFrameCount << "] Out Read Size = " << FrameSize << std::endl;

				//!< 先頭 uint16_t に「検出数」が格納されている
				const auto Count = *reinterpret_cast<uint16_t*>(std::data(OutAI));
				std::cout << "\tDetection Count = " << Count << std::endl;
				//!< 「検出数」分オフセット
				auto Offset = sizeof(Count);

#if 0
				//!< 検出格納先
				std::vector<hailo_detection_with_byte_mask_t> Detections;
				Detections.reserve(Count);
				//!< 検出を格納
				for (auto i = 0; i < Count; ++i) {
					const auto Detection = reinterpret_cast<hailo_detection_with_byte_mask_t*>(std::data(OutAI) + Offset);
					Detections.emplace_back(*Detection);
					Offset += sizeof(*Detection) + Detection->mask_size;
				}
				{
					std::lock_guard Lock(OutMutex);

					DetectionMap = cv::Mat(Shape.height, Shape.width, CV_8UC3, cv::Vec3b(0, 0, 0));
					for (auto& i : Detections) {
						//i.class_id;
						//i.score;	

						//!< ボックスサイズはピクセルではなく、画面に対する比率で格納されている
						const auto BoxL = static_cast<int>(i.box.x_min * Shape.width);
						const auto BoxT = static_cast<int>(i.box.y_min * Shape.height);
						const auto BoxW = static_cast<int>(ceil((i.box.x_max - i.box.x_min) * Shape.width));
						const auto BoxH = static_cast<int>(ceil((i.box.y_max - i.box.y_min) * Shape.height));
						for (auto h = 0; h < BoxH; ++h) {
							for (auto w = 0; w < BoxW; ++w) {
								if (i.mask[h * BoxW + w]) {
									//!< ROI (Region Of Interest)
								}
							}
						}
						cv::rectangle(DetectionMap, cv::Rect(BoxL, BoxT, BoxW, BoxH), cv::Vec3b(0, 255, 0), 1);
					}
				}
#endif
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
	auto VideoPath = std::string_view(std::size(Args) > 1 ? Args[1] : std::data(Cam));
	std::cout << "Capturing : \"" << VideoPath << "\"" << std::endl;

	//!< セグメンテーションクラス
	InstanceSegmentation InstSeg;

	//!< 表示用
	cv::Mat L, R, LR;
	const auto LSize = cv::Size(320, 240); //!< 左 (右も同じ) のサイズ 

	//!< 推定開始、ループ	
	InstSeg.Start("yolov5m_seg.hef", VideoPath,
		[&]() {
			//!< 深度推定クラスからカラーマップ、深度マップを取得
			const auto& CM = InstSeg.GetColorMap();
			const auto& DM = InstSeg.GetDetectionMap();
			if (CM.empty() || DM.empty()) { return true; }

			//!< 左 : カラーマップ
			cv::resize(CM, L, LSize, cv::INTER_AREA);

			//!< 右 : 検出マップ (AI の出力を別スレッドで深度マップへ加工しているので、ロックする必要がある)
			{
				std::lock_guard Lock(InstSeg.GetMutex());

				cv::resize(DM, R, LSize, cv::INTER_AREA);
			}

#if true
			//!< 水平連結
			cv::hconcat(L, R, LR);
#else
			//!< ブレンド
			cv::addWeighted(L, 1, R, 0.7, 0.0, LR);
#endif

			//!< 表示
			cv::imshow("Color & Detection Map", LR);

			constexpr auto ESC = 27;
			if (ESC == cv::pollKey()) {
				return false;
			}
			return true;
		});

	exit(EXIT_SUCCESS);
}
