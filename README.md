# Pi5

## 初期設定

### SD 作成 (PC)
- [Disk Imager](https://www.raspberrypi.com/software/) をダウンロードし、インストール、起動
- デバイス (Pi5)、OS (64bit)、ストレージ (SD) を選択
- カスタマイズでは「設定を編集する」
    - 「一般」タブ
        - 「ホスト名」にチェック
            ~~~~
            ホスト名    raspberrypi.local
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