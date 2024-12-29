# IMX500
- [AI Camera](https://www.raspberrypi.com/documentation/accessories/ai-camera.html#install-the-imx500-firmware)

## 準備
- 更新を行っておく
    ~~~
    $sudo apt update 
    $sudo apt full-upgrade -y
    ~~~
- Firmware をインストール
    ~~~
    $sudo apt install imx500-all -y
    ~~~
    - モデルは /usr/share/imx500-models/ 以下に配置される
- Zero2 の場合
    - /boot/firmware/config.txt に以下のように編集
        ~~~
        # コメントアウト
        #camera_auto_detect=1 
        
        [all]
        # 追加
        dtoverlay=imx500
        ~~~
- 再起動
    ~~~
    $sudo reboot
    ~~~
- インストールの確認
    ~~~
    $sudo dmesg | grep -i imx500
    ~~~

## サンプル
- Object detection サンプルを起動
    ~~~
    $rpicam-hello -t 0s --post-process-file /usr/share/rpi-camera-assets/imx500_mobilenet_ssd.json --viewfinder-width 1920 --viewfinder-height 1080 --framerate 30
    ~~~
    - タイムアウト無し
    - ポストプロセスファイルとして /usr/share/rpicam-assets/imx500_mobilenet_ssd.json を使用 (2 ステージのポストプロセスパイプライン)
        - imx500_object_detection
        - object_detect_draw_cv
    - 解像度 1920 x 1080
    - フレームレート 30

- Pose estimation サンプルを起動
    ~~~
    $rpicam-hello -t 0s --post-process-file /usr/share/rpi-camera-assets/imx500_posenet.json --viewfinder-width 1920 --viewfinder-height 1080 --framerate 30
    ~~~  
    - タイムアウト無し
    - ポストプロセスファイルとして /usr/share/rpicam-assets/imx500_posenet.json を使用 (2 ステージのポストプロセスパイプライン)
        - imx500_posenet
        - plot_pose_cv
    - 解像度 1920 x 1080
    - フレームレート 30


