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
		//!< AI ���̓X���b�h
		Threads.emplace_back([&]() {
			cv::VideoCapture Capture(std::data(VideoPath));
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

				//!< OpenCV �`����
				const auto CVOutAI = cv::Mat(Shape.height, Shape.width, CV_32F, std::data(OutAI));

				{
					std::lock_guard Lock(OutMutex);

					//!< �[�x�}�b�v�̒���
					DepthMap = cv::Mat(Shape.height, Shape.width, CV_32F, cv::Scalar(0));
					//!< -CVOutAI ���w���Ƃ��Ď��R�ΐ��̒� e �ׂ̂��悪 DepthMap �ɕԂ�
					cv::exp(-CVOutAI, DepthMap);
					DepthMap = 1 / (1 + DepthMap);
					DepthMap = 1 / (DepthMap * 10 + 0.009);

					double Mn, Mx;
					cv::minMaxIdx(DepthMap, &Mn, &Mx);
					//!< ��O�����A������
					//DepthMap.convertTo(DepthMap, CV_8U, 255 / (Mx - Mn), -Mn);
					//!< ��O�����A������ (�t)
					DepthMap.convertTo(DepthMap, CV_8U, -255 / (Mx - Mn), -Mn + 255);
				}
			}
			});
	}
#else
	virtual void Inference(std::vector<hailo_input_vstream>& InVS, std::vector<hailo_output_vstream>& OutVS, std::string_view VideoPath) override {
		std::cout << "InVS[" << std::size(InVS) << "]" << std::endl;
		std::cout << "OutVS[" << std::size(OutVS) << "]" << std::endl;
		
		//!< AI ���̓X���b�h
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
				VERIFY_HAILO_SUCCESS(hailo_vstream_write_raw_buffer(In, InAI.data, FrameSize));
				std::cout << "In Write Size = " << FrameSize << std::endl;
			}
			});

		//!< AI �o�̓X���b�h
		Threads.emplace_back([&]() {
			auto& Out = OutVS[0];

			hailo_vstream_info_t Info;
			VERIFY_HAILO_SUCCESS(hailo_get_output_vstream_info(Out, &Info));
			const auto& Shape = Info.shape;
			size_t FrameSize;
			VERIFY_HAILO_SUCCESS(hailo_get_output_vstream_frame_size(Out, &FrameSize));

			std::vector<uint8_t> OutAI(FrameSize);
			//!< �X���b�h���g�ɏI�����f������
			while (Flags.all()) {
				++OutFrameCount;

				//!< �o�͂��擾
				VERIFY_HAILO_SUCCESS(hailo_vstream_read_raw_buffer(Out, std::data(OutAI), FrameSize));
				std::cout << "Out Read Size = " << FrameSize << std::endl;

				//!< OpenCV �`����
				const auto CVOutAI = cv::Mat(Shape.height, Shape.width, CV_32F, std::data(OutAI));

				{
					std::lock_guard Lock(OutMutex);

					//!< �[�x�}�b�v�̒���
					DepthMap = cv::Mat(Shape.height, Shape.width, CV_32F, cv::Scalar(0));
					//!< -CVOutAI ���w���Ƃ��Ď��R�ΐ��̒� e �ׂ̂��悪 DepthMap �ɕԂ�
					cv::exp(-CVOutAI, DepthMap);
					DepthMap = 1 / (1 + DepthMap);
					DepthMap = 1 / (DepthMap * 10 + 0.009);

					double Mn, Mx;
					cv::minMaxIdx(DepthMap, &Mn, &Mx);
					//!< ��O�����A������
					//DepthMap.convertTo(DepthMap, CV_8U, 255 / (Mx - Mn), -Mn);
					//!< ��O�����A������ (�t)
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
	//!< ������
	//for(int Index = 0; auto i : Args){ std::cout << "Args[" << Index++ << "] " << i << std::endl; }

	//!< ���̓L���v�`���t�@�C������������擾�A�����I�Ɏw�肪�����ꍇ�̓J�����摜���g�p
	//!< (�J�����摜�� VideoCapture() �� Gstreamer �̈�����n���`�ō쐬)
	const auto Cam = Hailo::GetLibCameGSTStr(1280, 960, 30);
	auto VideoPath = std::string_view(std::size(Args) > 1 ? Args[1] : std::data(Cam));
	std::cout << "Capturing : \"" << VideoPath << "\"" << std::endl;

	//!< �[�x����N���X
	DepthEstimation DepEst;

	//!< �\���p
	cv::Mat L, R, LR;
	const auto LSize = cv::Size(320, 240); //!< �� (�E������) �̃T�C�Y 
#ifdef OUTPUT_VIDEO
	//!< �r�f�I�����o��
	const auto Fourcc = cv::VideoWriter::fourcc('m', 'p', '4', 'v');
	cv::VideoWriter WriterL("RGB.mp4", Fourcc, DepEst.GetFps(), LSize);
	cv::VideoWriter WriterR("D.mp4", Fourcc, DepEst.GetFps(), LSize, true);
#endif

	//!< ����J�n�A���[�v
	DepEst.Start("scdepthv3.hef", VideoPath,
		[&]() {
			//!< �[�x����N���X����J���[�}�b�v�A�[�x�}�b�v���擾
			const auto& CM = DepEst.GetColorMap();
			const auto& DM = DepEst.GetDepthMap();
			if (CM.empty() || DM.empty()) { return true; }

			//!< �� : �J���[�}�b�v
			cv::resize(CM, L, LSize, cv::INTER_AREA);

			//!< �E : �[�x�}�b�v (AI �̏o�͂�ʃX���b�h�Ő[�x�}�b�v�։��H���Ă���̂ŁA���b�N����K�v������)
			{
				std::lock_guard Lock(DepEst.GetMutex());

				cv::resize(DM, R, LSize, cv::INTER_AREA);
			}

#ifdef OUTPUT_VIDEO
			//!< �����o��
			WriterL << L;
			WriterR << R;
#endif	

			//!< �A������ׂɍ��E�̃^�C�v�� 8UC3 �ɍ��킹��K�v������
			cv::cvtColor(R, R, cv::COLOR_GRAY2BGR);
			R.convertTo(R, CV_8UC3);
			//!< �����A��
			cv::hconcat(L, R, LR);

			//!< �\��
			cv::imshow("Color & Depth Map", LR);

			constexpr auto ESC = 27;
			if (ESC == cv::pollKey()) {
				return false;
			}
			return true;
		});

	exit(EXIT_SUCCESS);
}
