# AI Kit

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