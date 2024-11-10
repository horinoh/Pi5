//#include <cassert>
#include <iostream>
#include <mutex>
#include <span>

#include "../Hailo.h"
#include "../CV.h"

class Segmentation : public Hailo
{
private:
	using Super = Hailo;

public:
#ifdef USE_HAILOCPP
	virtual void Inference(std::vector<hailort::InputVStream>& In, std::vector<hailort::OutputVStream>& Out, std::string_view CapturePath) override {
		//!< AI ���̓X���b�h
		Threads.emplace_back([&]() {
			cv::VideoCapture Capture(std::data(CapturePath));
			std::cout << Capture.get(cv::CAP_PROP_FRAME_WIDTH) << " x " << Capture.get(cv::CAP_PROP_FRAME_HEIGHT) << " @ " << Capture.get(cv::CAP_PROP_FPS) << std::endl;

			auto& Front = In.front();
			const auto& Shape = Front.get_info().shape;
			cv::Mat InAI;
			//!< �X���b�h���g�ɏI�����f������
			while (Flags.all()) {
				//!< ���͂Əo�͂��Y���Ă����Ȃ��悤�ɓ�������
				if (InFrameCount > OutFrameCount) { continue; }
				++InFrameCount;

				//!< �t���[�����擾
				Capture >> ColorMap;
				if (ColorMap.empty()) {
					std::cout << "Lost input" << std::endl;
					Flags.reset(static_cast<size_t>(FLAGS::HasInput));
					break;
				}

				//!< ���T�C�Y
				if (static_cast<uint32_t>(ColorMap.cols) != Shape.width || static_cast<uint32_t>(ColorMap.rows) != Shape.height) {
					cv::resize(ColorMap, InAI, cv::Size(Shape.width, Shape.height), cv::INTER_AREA);
				}
				else {
					InAI = ColorMap.clone();
				}

				//!< AI �ւ̓��� (��������)
				Front.write(hailort::MemoryView(InAI.data, Front.get_frame_size()));
			}
			});

		//!< AI �o�̓X���b�h
		Threads.emplace_back([&]() {
			auto& Front = Out.front();
			const auto& Shape = Front.get_info().shape;
			std::vector<uint8_t> OutAI(Front.get_frame_size());
			//!< �X���b�h���g�ɏI�����f������
			while (Flags.all()) {
				++OutFrameCount;

				//!< �o�͂��擾
				Front.read(hailort::MemoryView(std::data(OutAI), std::size(OutAI)));

				//!< �擪 uint16_t �Ɂu���o���v���i�[����Ă���
				const auto Count = *reinterpret_cast<uint16_t*>(std::data(OutAI));
				//!< �u���o���v���I�t�Z�b�g
				auto Offset = sizeof(Count);

				//!< ���o�i�[��
				std::vector<hailo_detection_with_byte_mask_t> Detections;
				Detections.reserve(Count);
				//!< ���o���i�[
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

						//!< �{�b�N�X�T�C�Y�̓s�N�Z���ł͂Ȃ��A��ʂɑ΂���䗦�Ŋi�[����Ă���
						const int BoxL = i.box.x_min * Shape.width;
						const int BoxT = i.box.y_min * Shape.height;
						const int BoxW = ceil((i.box.x_max - i.box.x_min) * Shape.width);
						const int BoxH = ceil((i.box.y_max - i.box.y_min) * Shape.height);
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
			}
			});
	}
#else
	virtual void Inference(hailo_input_vstream& In, hailo_output_vstream& Out, std::string_view CapturePath) override {
		//!< AI ���̓X���b�h
		Threads.emplace_back([&]() {
			cv::VideoCapture Capture(std::data(CapturePath));
			std::cout << Capture.get(cv::CAP_PROP_FRAME_WIDTH) << " x " << Capture.get(cv::CAP_PROP_FRAME_HEIGHT) << " @ " << Capture.get(cv::CAP_PROP_FPS) << std::endl;

			hailo_vstream_info_t Info;
			hailo_get_input_vstream_info(In, &Info);
			const auto& Shape = Info.shape;
			size_t FrameSize;
			hailo_get_input_vstream_frame_size(In, &FrameSize);

			cv::Mat InAI;
			//!< �X���b�h���g�ɏI�����f������
			while (Flags.all()) {
				//!< ���͂Əo�͂��Y���Ă����Ȃ��悤�ɓ�������
				if (InFrameCount > OutFrameCount) { continue; }
				++InFrameCount;

				//!< �t���[�����擾
				Capture >> ColorMap;
				if (ColorMap.empty()) {
					std::cout << "Lost input" << std::endl;
					Flags.reset(static_cast<size_t>(FLAGS::HasInput));
					break;
				}

				//!< ���T�C�Y
				if (static_cast<uint32_t>(ColorMap.cols) != Shape.width || static_cast<uint32_t>(ColorMap.rows) != Shape.height) {
					cv::resize(ColorMap, InAI, cv::Size(Shape.width, Shape.height), cv::INTER_AREA);
				}
				else {
					InAI = ColorMap.clone();
				}

				//!< AI �ւ̓��� (��������)
				hailo_vstream_write_raw_buffer(In, InAI.data, InAI.total() * InAI.elemSize());
			}
			});

		//!< AI �o�̓X���b�h
		Threads.emplace_back([&]() {
			hailo_vstream_info_t Info;
			hailo_get_output_vstream_info(Out, &Info);
			const auto& Shape = Info.shape;
			size_t FrameSize;
			hailo_get_output_vstream_frame_size(Out, &FrameSize);
			std::vector<uint8_t> OutAI(FrameSize);
			//!< �X���b�h���g�ɏI�����f������
			while (Flags.all()) {
				++OutFrameCount;

				//!< �o�͂��擾
				hailo_vstream_read_raw_buffer(Out, std::data(OutAI), std::size(OutAI));

				//!< �擪 uint16_t �Ɂu���o���v���i�[����Ă���
				const auto Count = *reinterpret_cast<uint16_t*>(std::data(OutAI));
				//!< �u���o���v���I�t�Z�b�g
				auto Offset = sizeof(Count);

				//!< ���o�i�[��
				std::vector<hailo_detection_with_byte_mask_t> Detections;
				Detections.reserve(Count);
				//!< ���o���i�[
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

						//!< �{�b�N�X�T�C�Y�̓s�N�Z���ł͂Ȃ��A��ʂɑ΂���䗦�Ŋi�[����Ă���
						const int BoxL = i.box.x_min * Shape.width;
						const int BoxT = i.box.y_min * Shape.height;
						const int BoxW = ceil((i.box.x_max - i.box.x_min) * Shape.width);
						const int BoxH = ceil((i.box.y_max - i.box.y_min) * Shape.height);
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
			}
			});
	}
#endif

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
	//!< ������
	//for(int Index = 0; auto i : Args){ std::cout << "Args[" << Index++ << "] " << i << std::endl; }

	//!< ���̓L���v�`���t�@�C������������擾�A�����I�Ɏw�肪�����ꍇ�̓J�����摜���g�p
	//!< (�J�����摜�� VideoCapture() �� Gstreamer �̈�����n���`�ō쐬)
	const auto Cam = Hailo::GetLibCameGSTStr(1280, 960, 30);
	auto CapturePath = std::string_view(std::size(Args) > 1 ? Args[1] : std::data(Cam));
	std::cout << "Capturing : \"" << CapturePath << "\"" << std::endl;

	//!< �Z�O�����e�[�V�����N���X
	Segmentation Seg;

	//!< �\���p
	cv::Mat L, R, LR;
	const auto LSize = cv::Size(320, 240); //!< �� (�E������) �̃T�C�Y 

	//!< ����J�n�A���[�v	
	Seg.Start("yolov5m_seg.hef", CapturePath,
		[&]() {
			//!< �[�x����N���X����J���[�}�b�v�A�[�x�}�b�v���擾
			const auto& CM = Seg.GetColorMap();
			const auto& DM = Seg.GetDetectionMap();
			if (CM.empty() || DM.empty()) { return true; }

			//!< �� : �J���[�}�b�v
			cv::resize(CM, L, LSize, cv::INTER_AREA);

			//!< �E : ���o�}�b�v (AI �̏o�͂�ʃX���b�h�Ő[�x�}�b�v�։��H���Ă���̂ŁA���b�N����K�v������)
			{
				std::lock_guard Lock(Seg.GetMutex());

				cv::resize(DM, R, LSize, cv::INTER_AREA);
			}

#if false
			//!< �����A��
			cv::hconcat(L, R, LR);
#else
			//!< �u�����h
			cv::addWeighted(L, 1, R, 0.7, 0.0, LR);
#endif

			//!< �\��
			cv::imshow("Color & Detection Map", LR);

			constexpr auto ESC = 27;
			if (ESC == cv::pollKey()) {
				return false;
			}
			return true;
		});

	exit(EXIT_SUCCESS);
}
