#pragma once

#include <bitset>
#if false
#include <format>
#else
#include <sstream>
#endif
#include <thread>
#include <functional>
#include <string_view>
#include <source_location>

//#define USE_HAILOCPP
#ifdef USE_HAILOCPP
#include <hailo/hailort.hpp>
#else
#include <hailo/hailort.h>
#endif

#ifdef _WIN64
#pragma comment(lib, "libhailort.lib")
#endif

#define BREAKPOINT()
#if true
#define VERIFY_HAILO_SUCCESS(X) { const auto HS = (X); if(hailo_status::HAILO_SUCCESS != HS) { std::cerr << std::source_location::current().function_name() << " hailo_status = " << HS << " (0x" << std::hex << static_cast<uint32_t>(HS) << std::dec << ")" << std::dec << std::endl; BREAKPOINT(); } }
#else
#define VERIFY_HAILO_SUCCESS(X) (X) 
#endif


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
		SS << "libcamerasrc ! video/x-raw, width=" << Width << ", height=" << Height << ", framerate=" << FPS << "/1, format=BGR, ! appsink";
		return SS.str();
#endif
	}

	virtual void Start(std::string_view HefFile, std::string_view CapturePath, std::function<bool()> Loop) {
		Flags.set(static_cast<size_t>(FLAGS::IsRunning));
		Flags.set(static_cast<size_t>(FLAGS::HasInput));

#ifdef USE_HAILOCPP
		//!< デバイス
		const auto Device = hailort::Device::create_pcie(hailort::Device::scan_pcie().value()[0]);

		//!< ネットワーク
		auto Hef = hailort::Hef::create(std::data(HefFile));
		if (!Hef) {
			std::cerr << "Hef file (" << HefFile << ") create failed, status = " << Hef.status() << std::endl;
		}
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
#else
		//!< デバイス
		hailo_device Device;
		VERIFY_HAILO_SUCCESS(hailo_create_pcie_device(nullptr, &Device));

		//!< ネットワーク
		hailo_hef Hef;
		VERIFY_HAILO_SUCCESS(hailo_create_hef_file(&Hef, std::data(HefFile)));
		hailo_configure_params_t ConfigureParams;
		VERIFY_HAILO_SUCCESS(hailo_init_configure_params(Hef, HAILO_STREAM_INTERFACE_PCIE, &ConfigureParams));

		std::vector<hailo_configured_network_group> ConfiguredNetworkGroups(HAILO_MAX_NETWORK_GROUPS);
		size_t Count = std::size(ConfiguredNetworkGroups);
		VERIFY_HAILO_SUCCESS(hailo_configure_device(Device, Hef, &ConfigureParams, std::data(ConfiguredNetworkGroups), &Count));
		ConfiguredNetworkGroups.resize(Count);

		auto& CNG = ConfiguredNetworkGroups.front();

		//!< 入出力パラメータ
		std::vector<hailo_input_vstream_params_by_name_t> InputVstreamParamsByName(HAILO_MAX_STREAM_NAME_SIZE);
		Count = std::size(InputVstreamParamsByName);
		VERIFY_HAILO_SUCCESS(hailo_make_input_vstream_params(CNG, true, HAILO_FORMAT_TYPE_UINT8, std::data(InputVstreamParamsByName), &Count));
		InputVstreamParamsByName.resize(Count);

		std::vector<hailo_output_vstream_params_by_name_t> OutputVstreamParamsByName(HAILO_MAX_STREAM_NAME_SIZE);
		Count = std::size(OutputVstreamParamsByName);
		VERIFY_HAILO_SUCCESS(hailo_make_output_vstream_params(CNG, false, HAILO_FORMAT_TYPE_FLOAT32, std::data(OutputVstreamParamsByName), &Count));
		OutputVstreamParamsByName.resize(Count);

		//!< 入出力 (入力へ書き込むと AI に処理されて出力に返る)
		hailo_input_vstream InputVstreams;
		VERIFY_HAILO_SUCCESS(hailo_create_input_vstreams(CNG, std::data(InputVstreamParamsByName), std::size(InputVstreamParamsByName), &InputVstreams));

		hailo_output_vstream OutputVstreams;
		VERIFY_HAILO_SUCCESS(hailo_create_output_vstreams(CNG, std::data(OutputVstreamParamsByName), std::size(OutputVstreamParamsByName), &OutputVstreams));

		//!< AI ネットワークのアクティベート
		hailo_activated_network_group ActivatedNetworkGroup;
		VERIFY_HAILO_SUCCESS(hailo_activate_network_group(CNG, nullptr, &ActivatedNetworkGroup));

		//!< 推論開始
		Inference(InputVstreams, OutputVstreams, CapturePath);
#endif
		//!< ループ
		do {
			Flags[static_cast<size_t>(FLAGS::IsRunning)] = Loop();
		} while (Flags.all());

		//!< 終了、スレッド同期
		Join();
	}

	//!< 継承クラスでオーバーライド
#ifdef USE_HAILOCPP
	virtual void Inference([[maybe_unused]] std::vector<hailort::InputVStream>& In, [[maybe_unused]] std::vector<hailort::OutputVStream>& Out, [[maybe_unused]] std::string_view CapturePath) {}
#else
	virtual void Inference([[maybe_unused]] hailo_input_vstream& In, [[maybe_unused]] hailo_output_vstream& Out, [[maybe_unused]] std::string_view CapturePath) {}
#endif

	//!< スレッド同期
	void Join() {
		for (auto& i : Threads) {
			i.join();
		}
	}

protected:
	enum class FLAGS : size_t {
		IsRunning,
		HasInput,
	};
	std::bitset<2> Flags;
	std::vector<std::thread> Threads;
};

