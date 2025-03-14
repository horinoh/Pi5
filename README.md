# Pi5

## 初期設定 (ヘッドレス)

### SD 作成 (PC)
- [Disk Imager](https://www.raspberrypi.com/software/) をダウンロードし、インストール、起動
- デバイス (Pi5)、OS (64bit)、ストレージ (SD) を選択
- カスタマイズでは「設定を編集する」
    - 「一般」タブ
        - 「ホスト名」にチェック、ホスト名を任意に決める
            ~~~~
            raspberrypi.local
            ~~~~
        - 「ユーザ名とパスワードを設定する」にチェック
            ~~~
            ユーザ名    任意 (以下<USER>とする)
            パスワード  任意 (以下<PASS>とする)
            ~~~
        - 「WiFI を設定する」にチェック
            ~~~
            SSID        自環境のSSID
            パスワード   自環境のパスワード
            ~~~
        - 「ロケールを設定する」にチェック
            ~~~
            タイムゾーン            Asia/Tokyo
            キーボードレイアウト     任意
            ~~~
    - 「サービス」タブ
        - 「SSH を有効にする」にチェック
            - パスワード認証を使うにチェック
- 書き込む

### 起動 (Pi5)
- Pi5 に差し込み、電源を入れる
    - 起動するまでしばらく待つ

### SSH 接続 (PC)
#### Power Shell の場合
- Power Shell から以下のようにする
    ~~~
    $ssh <USER>@raspberrypi.local
    $<USER>@raspberrypi.local's password:<PASS>
    ~~~
#### TeraTerm の場合
- [TeraTerm](https://teratermproject.github.io/) をダウンロード、インストール、起動
    - ファイル - 新しい接続 - 以下のようにして OK
        ~~~
        ホスト      raspberrypi.local
        サービス    SSH にチェック
        ~~~
    - ユーザ名とパスワードを求められるので以下のようにする
        ~~~
        ユーザ名    <USER>
        パスワード  <PASS>
        ~~~

### VNC 接続
- VNC を有効にする (Pi5)
    ~~~
    $sudo raspi-config
    ~~~
    - Interface Options - VNC - YES - OK
- VNC 接続 (PC)
    - [RealVNC](https://www.realvnc.com/en/connect/download/viewer/) をダウンロード、インストール、起動
    - File - New Connection
        ~~~
        VNC Server  raspberrypi.local
        Name        任意
        ~~~
    - ユーザ名とパスワードを求められるので以下のようにする
        ~~~
        Username    <USER>
        Password    <PASS>
        ~~~

### X11 (Wayland をやめる)
- 一旦 VNC をオフにする (VNC のオンオフは上記)、下記で X11 を有効にした VNC 後戻す (必要に応じて)
- Wayland をオフにする
    ~~~
    $sudo raspi-config
    ~~~
    - Advanced Options - Wayland - X11 
- 確認
    ~~~
    echo $XDG_SESSION_TYPE
    ~~~
- Wayland バージョンをインストールしていたものがあれば、アンインストールして、X11 バージョンにする
    - glfw3-wayland を glfw3 にするとか

### 解像度 (ヘッドレス時)
- ラズベリーアイコン - Preferences - Raspberry Pi Configuration - Display - Headless Resolution - 再起動

## 更新
- 更新
    ~~~
    $sudo apt update
	$sudo apt full-upgrade -y
    ~~~
- ファームウェア確認
    ~~~
    $sudo rpi-eeprom-update
    ~~~
- ファームウェア更新
    ~~~
	$sudo rpi-eeprom-update -a
    ~~~
- 再起動
    ~~~
	$sudo reboot
    ~~~

## PCIe Gen 3.0 有効化
- /boot/firmware/config.txt に以下の行を追記する
    ~~~
	dtparam=pciex1_gen=3
    ~~~
- 再起動
    ~~~
	$sudo reboot
    ~~~
- コンフィグ設定
    ~~~
	$sudo raspi-config
    ~~~
    - Advanced Options - PCIe Speed - Yes - Finish
- 再起動
    ~~~
	$sudo reboot
    ~~~		

## コマンド等
### apt
#### list
- パッケージ一覧
    ~~~
    $apt list
    ~~~
- インストール済みパッケージ一覧
    ~~~
    $apt list --installed
    ~~~
#### show
- パッケージ情報を表示
    ~~~
    $apt show XXX
    ~~~
#### install
- インストール
    ~~~
    $sudo apt install -y XXX
    ~~~
#### remove
- アンインストール
    ~~~
    $sudo apt remove XXX
    ~~~
#### update
- パッケージのインデックスファイルを更新
    ~~~
    $sudo apt full-upgrade
    ~~~
#### full-upgrade
- インストール済みのパッケージを更新、(必要に応じて)削除
    ~~~
    $sudo apt full-upgrade -y
    ~~~

### DPKG
- インストール先の列挙
    ~~~
    $dpkg -L XXX
    ~~~
    - apt install XXX した名前 XXX

### Git
- ファイルに実行権限を付ける
    ~~~
    $git update-index --chmod=+x XXX
    ~~~

## [状態調査 vcgencmd](https://www.raspberrypi.com/documentation/computers/os.html#vcgencmd)
- 例
    ~~~
    $vcgencmd measure_clock arm
    $vcgencmd measure_clock core
    $vcgencmd measure_temp
    $vcgencmd measure_volts
    $vcgencmd get_config total_mem
    ~~~

## カメラ
- 設定ファイル /boot/firmware/config.txt
    ~~~
    # 純正カメラ (imx500は純正扱いなので 1 で良い)
    camera_auto_detect=1
    ~~~
    ~~~
    # 純正カメラ以外
    camera_auto_detect=0
    dtoverlay=XXX
    ~~~
- カメラをリストアップ
    ~~~
    $libcamera-hello --list-cameras
    ~~~
- プレビュー
    ~~~
    $libcamera-hello -t 5000
    ~~~
    - -t はプレビューの表示時間 (ms)、デフォルトは 5 秒、0 で無期限
- 静止画を保存
    ~~~
    $libcamera-jpeg -o XXX.jpg
    $libcamera-still -o XXX.jpg
    $libcamera-still -e png -o XXX.png
    ~~~
- 動画を保存
    ~~~
    $libcamera-vid -t 5000 -o XXX.mpeg
    $libcamera-vid -t 5000 -o XXX.mp4
    ~~~
    - 再生
        ~~~
        $vlc XXX.mpeg
        ~~~

## インストールしたもの等

### [AI Kit](https://github.com/horinoh/Pi5/tree/master/AIKit)
### [IMX500](https://github.com/horinoh/Pi5/tree/master/IMX500)

### emacs
- インストール
    ~~~
    $sudo apt install -y emacs
    ~~~
- 設定 (~/.emacs.d/init.el を作成)
    ~~~
    ; バックアップを作らない
    (setq make-backup-files nil)
    (setq auto-save-default nil)
    ~~~

### VSCode
- インストール
    ~~~
    $suto apt install -y code
    ~~~
    
### libvulkan-dev
- インストール
    ~~~
    $sudo apt install -y libvulkan-dev
    ~~~
- 確認 (-lvulkan オプションをつけてコマンドが通るか)
    ~~~
    $g++ main.cpp -lvulkan -I/usr/include
    ~~~

### glslang-tools
- インストール
    ~~~
    $sudo apt install -y glslang-tools
    ~~~
- 確認 (コマンドが通るか)
    ~~~
    $glslangValidator --help
    ~~~

### libglfw3, libglfw3-wayland
- インストール
    ~~~
    $sudo apt install -y libglfw3
    ~~~
    or
    ~~~
    $sudo apt install -y libglfw3-wayland
    ~~~
    - 一方をインストールすると他方がアンインストールされる (排他)
    
### libglfw3-dev
- インストール
    ~~~
    $sudo apt install -y libglfw3-dev
    ~~~
- 確認 (-lglfw オプションをつけてコマンドが通るか)
    ~~~
    $g++ main.cpp -lglfw
    ~~~
<!--
- インストール
    ~~~
    $sudo apt install -y libglfw3
    $sudo apt install -y libglfw3-wayland
    ~~~
    - libglfw3 と libglfw3-wayland はどちらか１つで良い?
    - libglfw3-wayland だけ入れれば良い?
-->

### w3m
- インストール
    ~~~
    $suto apt install -y w3m
    ~~~

### samba
- インストール
    ~~~
    $sudo apt install -y samba
    ~~~
- 設定 /etc/samba/smb.conf に以下を追記
    ~~~
    [share]
    path = /home/<USER>/Share
    browsable = yes                                                            
    read only = no
    guest ok = yes
    guest only = yes
    force user = <USER>
    ~~~
    - \<USER\> はインストール時に決めたもの
- samba を再起動
    ~~~
    $sudo systemctl restart smbd
    ~~~
- Windows からアクセス
    ~~~
    \\raspberrypi\share
    ~~~

<!--
### looking glass
- [BridgeSDK](https://lookingglassfactory.com/software/looking-glass-bridge) をダウンロード、解凍、LookingGlassBridge-X.Y.Z-Linux.sh に実行権限を付けて実行する
- バイナリファイルが含まれていて、Pi では動かないっぽい
-->

### cmake
- AI Kit セットアップでインストールされる
    ~~~
    $sudo apt install cmake -y
    ~~~
### opencv
- AI Kit セットアップでインストールされる
    ~~~
    $sudo apt install libopencv-dev -y
    ~~~
### libvulkan1
- デフォルトでインストールされる
### meson
- デフォルトでインストールされる
### ninja
- デフォルトでインストールされる
### git
- デフォルトでインストールされる
- クローン
    ~~~
    $git clone --recursive https://XXX.YYY.ZZZ.git
    ~~~
- 設定
    ~~~
    $git config --global user.email "XXX@YYY"
    $git config --global user.name "ZZZ"
    ~~~
- 他コマンド
    - ステージングへ
        ~~~
        $git add XXX
        $git add .
        ~~~
    - 取り消し
        ~~~
        $git restore XXX
        ~~~
    - ステージングの確認
        ~~~
        $git status
        ~~~
    - コミット
        ~~~
        $git commit -m "YYY"
        ~~~
    - コミットを確認
        ~~~
        $git log
        ~~~
    - リモート (mainブランチ) へ反映
        ~~~
        $git push origin main
        ~~~
### gstreamer
- *src 入力
    - videotestsrc  
        - テスト用入力 (カラーバー)
    - appsrc        
        - cv::VideoWriter()等、アプリケーションからの出力を入力とする
    - filesrc location=XXX
        - ファイル入力
    - libcamerasrc  
        - カメラ入力
- *sink 出力
    - autovideosink 
        - 自動出力 (画面ウインドウ等)
    - appsink       
        - cv::VideoCapture()等、アプリケーションへの入力を出力する
    - filesink location=XXX
        - ファイル出力
#### 例
- カラーバーを画面表示
    ~~~
    $gst-launch-1.0 videotestsrc ! autovideosink
    ~~~
- カメラ画像表示
    ~~~
    $gst-launch-1.0 libcamerasrc ! autovideosink
    $gst-launch-1.0 libcamerasrc ! video/x-raw, width=640, height=320, framerate=30/1 ! autovideosink
    ~~~
- カメラ画像を OpenCV で処理する
    - 基本的に gstreamer コマンド引数の、ケツを "appsink" に変えたものを VideoCapture() へ引数として渡せばよい
    - OpenCV ではフォーマット "format=BGR" を指定する必要がある
    ~~~
    cv::VideoCapture Cap("libcamerasrc ! video/x-raw, format=BGR ! appsink");
    ~~~

## ビルド関連
- Vulkan
    ~~~
    #include <vulkan/vulkan.h>
    ~~~
    ~~~
    $g++ main.cpp -lvulkan
    ~~~
    - meson
        ~~~
        dependency('vulkan')
        ~~~
- Glfw
    ~~~
    #include <GLFW/glfw3.h>
    ~~~
    ~~~
    $g++ main.cpp -lglfw
    ~~~
    - meson
        ~~~
        dependency('glfw3')
        ~~~
- OpenCV
    ~~~
    #include <opencv2/opencv.hpp>
    ~~~
    ~~~
    $g++ main.cpp -lopencv_core -lopencv_highgui -lopencv_imgproc -lopencv_videoio ... -I/usr/include/opencv4
    ~~~
    - -lopencv_XXX は各種あるので、指定の有無は必要に応じて
    - meson
        ~~~
        dependency('opencv4')
        ~~~
- HailoRT
    ~~~
    #include <hailo/hailort.h>
    ~~~
    ~~~
    $g++ main.cpp -lhailort
    ~~~
    - meson
        ~~~
        Compiler = meson.get_compiler('cpp')
        Compiler.find_library('hailort', dirs : '/usr/lib'),
        ~~~
## Warning 等
- 以下が出る場合
    ~~~
    QStandardPaths: wrong permissions on runtime directory /run/user/1000, 0770 instead of 0700
    ~~~
    - 以下のようにすると出なくなる (.bashrc 等に書いておくと良い)
    ~~~
    $chmod 0700 /run/user/1000
    ~~~
- 以下が出る場合
    ~~~
    libpng warning: iCCP: known incorrect sRGB profile
    ~~~
    - ?