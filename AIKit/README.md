# [AI Kit](https://www.raspberrypi.com/documentation/accessories/ai-kit.html)

## 準備
- 更新、ファームウェア更新を行っておくs
- PCIe Gen 3.0 を有効にしておく
- AI Kit インストール
    ~~~
    $sudo apt install hailo-all
    $sudo reboot
    ~~~
- AI Kit インストールの確認
    ~~~
    $hailortcli fw-control identify
    or
	$dmesg | grep -i hailo
    ~~~

## サンプル
- [サンプル](https://github.com/hailo-ai/hailo-rpi5-examples) をクローンする
    ~~~
    $git clone https://github.com/hailo-ai/hailo-rpi5-examples.git
    ~~~
- セットアップ
    ~~~
    $pip install -r requirements.txt
    $./download_resources.sh
    ~~~
- サンプル起動
    ~~~
    $. setup_env.sh
    $python basic_pipelines/detection.py --input resources/detection0.mp4
    ~~~
   - detection0.mp4 は download_resources.sh でダウンロードされる

## サンプル
- [サンプル](https://github.com/raspberrypi/rpicam-apps.git) をクローンする
    ~~~
    $cd rpicam-apps/
    $libcamera-hello -t 0 --post-process-file assets/hailo_XXX.json --lores-width 640 --lores-height 640
    ~~~
<!--
    - デフォルトの入力は /dev/video0 (USB カメラ)、--input オプションで明示的に指定可能
        ~~~
        ... --input /dev/vide o2
        ... --input rpi
        ... --input XXX.mp4
        ~~~
    - カメラの確認
        ~~~
        $ffplay -f v4l2 /dev/videoXX
        ~~~
-->

## HailoRT
- [参考](https://www.macnica.co.jp/business/semiconductor/articles/hailo/145098/) 
    - hef ファイルをデバイスに読み込んで推論する 
- hef ファイルのパフォーマンス確認
    ~~~
    $hailortcli run XXX.hef
    ~~~
- hef ファイルのレイテンシ確認
    ~~~
    $hailortcli run XXX.hef --measure-latency
    ~~~
- hef ファイルの電力確認
    ~~~
    $hailortcli run XXX.hef --measure-power
    ~~~

### ライブラリ
- ヘッダ
    ~~~
    /usr/include/hailo
    ~~~
- ライブラリ
    ~~~
    /usr/lib/libhailort.so
    ~~~
- ビルド例
    ~~~
    #include <hailo/hailort.h>
    ~~~
    ~~~
    $g++ main.cpp -lhailort
    ~~~

### C/C++ サンプル
- [参考 (入出力部分)](https://github.com/hailo-ai/hailort/tree/master/hailort/libhailort/examples)
- [参考 (組み込み)](https://github.com/hailo-ai/Hailo-Application-Code-Examples/tree/main/runtime/cpp)

### [Hef ファイル](https://github.com/hailo-ai/hailo_model_zoo/blob/master/docs/PUBLIC_MODELS.rst)
- Hailo-8L の Compiled をダウンロードする
    - scdepthv3.hef
    - yolov5m-seg.hef

### トラブルシューティング
- ドライバのバージョンで怒られる場合
	- 更新後はリブートをしてみる
	- .hef を DL しなおしてみる

### HailoRT (Windowsの場合)
- [HailoRT](https://hailo.ai/developer-zone/software-downloads/) Windows 版を選択してダウンロード
	- C:\Program Files\HailoRT 等にインストールされるので、ここでは (インストール先フォルダ) 環境変数 HAILORT_SDK_PATH を作成する
	- HAILORT_SDK_PATH/bin は環境変数 Path に通しておく
- ヘッダ、ライブラリ、DLL
	~~~
	HAILORT_SDK_PATH/include/hailo/*.h
	HAILORT_SDK_PATH/lib/libhailort.lib
	HAILORT_SDK_PATH/bin/*.dll
	~~~
	- Windows 版だと、hailort.hpp はビルドが通らないので hailort.h の方を使う

### 自作プログラム
#### Depth Estimation
- scdepthv3.hef を [ここ](https://github.com/hailo-ai/hailo_model_zoo/blob/master/docs/PUBLIC_MODELS.rst) からダウンロードする
    ~~~
    $wget https://hailo-model-zoo.s3.eu-west-2.amazonaws.com/ModelZoo/Compiled/v2.14.0/hailo8l/scdepthv3.hef
    ~~~

#### Segmentation
- yolov5m-seg.hef を [ここ](https://github.com/hailo-ai/hailo_model_zoo/blob/master/docs/PUBLIC_MODELS.rst) からダウンロードする
    ~~~
    wget https://hailo-model-zoo.s3.eu-west-2.amazonaws.com/ModelZoo/Compiled/v2.14.0/hailo8l/yolov5m_seg.hef
    ~~~

<!--
## 自前のポストプロセスを書く場合
- [参考](https://github.com/hailo-ai/tappas/blob/master/docs/write_your_own_application/write-your-own-postprocess.rst)

### 準備
- [tappas](https://github.com/hailo-ai/tappas.git) をクローン
    ~~~
    $git clone https://github.com/hailo-ai/tappas.git
    ~~~
- インストール
    ~~~
    $cd tappas/
    $./install.sh
    ~~~
- core - hailo - libs - postprocess へ移動
    ~~~
    $cd core/hailo/libs/postprocess/
    ~~~ 
### コード
- ヘッダファイルを作成 (ここでは my_post.hpp とする)
    ~~~
    #pragma once
    #include "hailo_objects.hpp"
    #include "hailo_common.hpp"
            
    __BEGIN_DECLS
    void filter(HailoROIPtr roi);
    __END_DECLS
    ~~~
- ソースファイルを作成 (ここでは my_post.cpp とする)
    ~~~
    #include <iostream>
    #include "my_post.hpp"
    
    void filter(HailoROIPtr roi) {
        std::cout << "My first postprocess!" << std::endl;
    }
    ~~~
### ビルド
- meson.build を作成 
    ~~~
    my_post_sources = [
        'my_post.cpp',
    ]

    shared_library('my_post',
        my_post_sources,
        cpp_args : hailo_lib_args,
        include_directories: [hailo_general_inc, include_directories('./')] + xtensor_inc,
        dependencies : post_deps + [tracker_dep],
        gnu_symbol_visibility : 'default',
        install: true,
        install_dir: post_proc_install_dir,
    )
    ~~~
- scripts/gstreamer/install_hailo_gstreamer.sh を実行
    - apps/h8/gstreamer/libs/post_processes/libmy_post.so が作成される

### 実行
- 以下のコマンドを実行
    ~~~
    $gst-launch-1.0 videotestsrc 
    ! hailofilter so-path=$TAPPAS_WORKSPACE/apps/h8/gstreamer/libs/post_processes/libmy_post.so 
    ! fakesink
    ~~~
    - "My first postprocess!" が出力されたら成功
        ~~~
        $My first postprocess!
        $My first postprocess!
        ...
        ~~~
## 実装例
- テンソル
    ~~~
    auto Tensors = roi->get_tensors();
    auto Tensor = roi->get_tensor("XXX");

    const auto Name = Tensor->name();
    const auto Width = Tensor->shaoe()[0]; 
    const auto Height = Tensor->shape()[1]; 
    const auto Channels = Tensor->shape()[2];

    auto Data = Tensor->data();
    ~~~
- 検出
    ~~~
    //!< HailoBBox(X, Y, W, H) 引数はピクセルではなく画像に対する比率
    const auto Label = "person";
    const std::vector Detections = {
        //!< 指定のボックス内で、99% 以上 "person" であるもの
        HailoDetection(HailoBBox(0.2, 0.2, 0.2, 0.2), Label, 0.99),
        HailoDetection(HailoBBox(0.6, 0.6, 0.2, 0.2), Label, 0.89),
    }
    hailo_common::add_detections(roi, Detections);
    ~~~
    ~~~
    $gst-launch-1.0 filesrc location=$TAPPAS_WORKSPACE/apps/h8/gstreamer/general/detection/resources/detection.mp4 name=src_0 
    ! decodebin 
    ! videoscale 
    ! video/x-raw, pixel-aspect-ratio=1/1 
    ! videoconvert 
    ! queue 
    ! hailonet hef-path=$TAPPAS_WORKSPACE/apps/h8/gstreamer/general/detection/resources/yolov5m_wo_spp_60p.hef is-active=true 
    ! queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 
    ! hailofilter so-path=$TAPPAS_WORKSPACE/apps/h8/gstreamer/libs/post_processes/libmy_post.so qos=false 
    ! videoconvert 
    ! fpsdisplaysink video-sink=ximagesink name=hailo_display sync=true text-overlay=false
    ~~~

- 描画
    ~~~
    gst-launch-1.0 filesrc location=$TAPPAS_WORKSPACE/apps/h8/gstreamer/general/detection/resources/detection.mp4 name=src_0 
    ! decodebin 
    ! videoscale 
    ! video/x-raw, pixel-aspect-ratio=1/1 
    ! videoconvert 
    ! queue 
    ! hailonet hef-path=$TAPPAS_WORKSPACE/apps/h8/gstreamer/general/detection/resources/yolov5m_wo_spp_60p.hef is-active=true 
    ! queue leaky=no max-size-buffers=30 max-size-bytes=0 max-size-time=0 
    ! hailofilter so-path=$TAPPAS_WORKSPACE/apps/h8/gstreamer/libs/post_processes/libmy_post.so qos=false 
    ! queue 
    ! hailooverlay 
    ! videoconvert 
    ! fpsdisplaysink video-sink=ximagesink name=hailo_display sync=true text-overlay=false
    ~~~

## サンプル
- core/hailo/libs/postprocesses/ 以下を参考に自前で作成する
    - 深度推定のサンプルなら core/hailo/libs/postprocesses/depth_estimation
-->