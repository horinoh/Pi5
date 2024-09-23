#include <cassert>
#include <iostream>

#include <hailo/hailort.hpp>

int main() {
	auto Device = hailort::Device::create_pcie(hailort::Device::scan_pcie().value()[0]);
	std::cout << "Device status = " << Device.status() << std::endl;

	auto Hef = hailort::Hef::create("scdepthv3.hef");
	const auto ConfigureParams = Hef->create_configure_params(HAILO_STREAM_INTERFACE_PCIE);
	const auto NetworkGroups = Device.value()->configure(Hef.value(), ConfigureParams.value());
	const auto NetworkGroup = std::move(NetworkGroups->at(0));

	const auto InputVstreamParams = NetworkGroup->make_input_vstream_params(true, HAILO_FORMAT_TYPE_UINT8, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
	const auto InputVstreams = hailort::VStreamsBuilder::create_input_vstreams(*NetworkGroup, InputVstreamParams.value());

	const auto OutputVstreamParams = NetworkGroup->make_output_vstream_params(false, HAILO_FORMAT_TYPE_FLOAT32, HAILO_DEFAULT_VSTREAM_TIMEOUT_MS, HAILO_DEFAULT_VSTREAM_QUEUE_SIZE);
	const auto OutputVstreams = hailort::VStreamsBuilder::create_output_vstreams(*NetworkGroup, OutputVstreamParams.value());

	//"instance_segmentation.mp4";

	return 0;
}
	