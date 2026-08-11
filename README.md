# Wizardry 7: Crusaders of the Dark Savant PC-9801版 External AutoMap v1.1

## 概要

このリポジトリは、**Wizardry 7: Crusaders of the Dark Savant PC-9801版**をWindows上のPC-98エミュレーターで遊ぶための、非公式External AutoMapです。

`Wizardry7Automap.exe`は、エミュレーター上で動作しているWizardry 7のメモリを外部から読み取り、ゲーム画面とは別のウィンドウにマップを表示します。

標準設定ではAnex86の`anex86.exe`を対象プロセスとして検索しますが、`Config\Wizardry7Automap.conf`の`target`を変更することで、別の実行ファイル名も指定できます。

本ツールは、**PC-9801版 Wizardry 7**向けです。  
海外MS-DOS版、DOS/V版、Wizardry 6、その他の機種版向けAutoMap機能は含まれていません。

現在のPC-98版メモリ解析は未完了のため、本リリースは**Experimental**です。

まだゲームを進められておらず、Wizard EyeとLocate Objectの呪文を覚えていないので試せないためです。

今後遊び始めたら改善していきます。

## できること

このツールでは、主に以下のことができます。

- Wizardry 7 PC-9801版のプレイ中に、別ウィンドウでAutoMapを表示
- ゲーム側の踏破情報に合わせたマップ表示
- 現在位置と向きの表示
- 壁、扉、床、階段、泉、穴、梯子など、解析済み固定要素の表示
- 暗闇エリアでのマップ表示制御
- AutoMapウィンドウのマウスによるサイズ変更
- マウスドラッグによるマップ移動
- 中クリックで現在位置へ表示を戻す
- マスをダブルクリックしてノートを追加・編集・削除
- ノート上へマウスを重ねると内容を即時表示
- ノートの右クリックでの色変更
- ノート内の座標リンクによるマップ移動
- 対象プロセスやDS.EXEの自動検出・再検索

## 含まれていないもの

このリポジトリおよびRelease ZIPには、以下のものは含まれていません。

- Wizardry 7のゲームデータ
- PC-98エミュレーター本体
- Wizardry 7本体に由来するゲームプログラムやデータファイル

ゲームを遊ぶには、利用者自身が正規に所有している **Wizardry 7: Crusaders of the Dark Savant PC-9801版** が必要です。

## 対応対象

対象は以下です。

```text
Wizardry 7: Crusaders of the Dark Savant
PC-9801版
```

動作対象は64ビット版Windowsです。  
標準設定ではAnex86の`anex86.exe`を検索します。

## 導入方法

### 1. Wizardry 7 PC-9801版を遊べる状態にする

最初に、Anex86などのWindows上のPC-98エミュレーターで、Wizardry 7 PC-9801版を通常どおり起動できる状態にしてください。

このAutoMapにはゲーム本体やエミュレーターは含まれていません。

### 2. External AutoMapを導入する

このリポジトリの**Releases**からRelease ZIPをダウンロードして、任意のフォルダへ展開してください。

主な構成は以下です。

```text
Wizardry7Automap.exe
README.md
LICENSE.txt

Config\
  Wizardry7Automap.conf

Source\
  ...
```

### 3. Wizardry7Automap.exeを起動する

`Wizardry7Automap.exe`を実行します。

対象エミュレーターのメモリを読み取るため、Windowsのユーザーアカウント制御（UAC）で管理者権限を要求します。表示された場合は内容を確認して許可してください。

AutoMapとエミュレーターは、どちらを先に起動しても構いません。

標準設定では、AutoMapは次のプロセスを待機します。

```text
anex86.exe
```

対象プロセスを検出すると、Wizardry 7の`DS.EXE`常駐データをメモリ上から検索します。

正常に検出できれば、ゲーム内の移動に合わせてAutoMapウィンドウの表示が更新されます。

## AutoMap設定

`Config\Wizardry7Automap.conf`の`[automap]`セクションで設定できます。

標準設定は以下です。

```ini
[automap]
target="anex86.exe"
enable=true
show_tooltips=true
hide_in_dark_zones=true
width=512
height=512
position_x=-1
position_y=-1
wiz7_sns_mode=false
```

### target

```ini
target="anex86.exe"
```

接続対象として検索するWindowsプロセスの実行ファイル名です。

標準値は`anex86.exe`です。大文字と小文字は区別しません。

次の両形式を使用できます。

```ini
target="My Emulator.exe"
target=anex86.exe
```

空白を含む実行ファイル名は二重引用符で囲んでください。

### enable

```ini
enable=true
```

AutoMap機能を有効にするかどうかを指定します。

`false`にするとRAM監視とマップ表示を無効にします。

通常は`true`のままで使用してください。

### show_tooltips

```ini
show_tooltips=true
```

ノートがあるマスへマウスを重ねたとき、ノート本文を表示するかどうかを指定します。

### hide_in_dark_zones

```ini
hide_in_dark_zones=true
```

`true`の場合、ゲーム内の暗闇エリアでAutoMap表示を制限します。

快適性を優先する場合は`false`にしてください。

### width / height

```ini
width=512
height=512
```

AutoMapウィンドウの初期地図領域サイズを指定します。

```text
width  : 256～2048
height : 256～2048
```

標準では512×512ピクセルです。

起動後はウィンドウ枠をマウスでドラッグしてサイズを変更できます。

### position_x / position_y

```ini
position_x=-1
position_y=-1
```

AutoMapウィンドウ左上の初期位置を指定します。

```text
position_x : 画面左端からの横位置
position_y : 画面上端からの縦位置
-1         : Windows側の自動配置
```

### wiz7_sns_mode

```ini
wiz7_sns_mode=false
```

旧AutoMapコードとの互換用設定です。

`true`にするとノートツールチップを抑制します。通常は`false`のままで使用してください。

## ノート機能

### ノートを追加・編集する

ノートを付けたいマスを左ダブルクリックします。

- ノートがないマス：新しいノートを追加
- ノートがあるマス：既存の内容を編集
- 入力内容を空欄にしてOK：ノートを削除
- Cancel：変更せずに閉じる

ノートは1件につき最大4096文字です。

### ノートを読む

ノートがあるマスへマウスカーソルを重ねると、待ち時間なしでノート本文を表示します。

### ノート色を変更する

ノートがあるマスを右クリックすると、Windowsの色選択画面が開きます。

### 座標リンクを使用する

Altキーを押しながらマスを左クリックすると、次の形式の座標をクリップボードへコピーします。

```text
{level:quadrant:x:y}
```

この文字列をノート本文へ書き込み、そのノートをCtrlキーを押しながら左クリックすると、座標リンクの場所へマップ表示を移動します。

ノートは以下へ保存されます。

```text
Config\Wizardry7Automap_notes.bin
```

ノートはゲームのセーブスロットごとではなく、すべてのセーブデータで共通です。

## ウィンドウ配置

AutoMapウィンドウは標準で512×512です。

初期位置を固定したい場合は、`Config\Wizardry7Automap.conf`の以下を変更してください。

```ini
position_x=0
position_y=0
```

起動後はウィンドウを通常のWindowsアプリケーションと同じように移動・サイズ変更できます。

## DS.EXEの検出と再接続

AutoMapは、対象エミュレーターのWindowsプロセスへ接続した後、メモリ上からWizardry 7の`DS.EXE`常駐データを自動検索します。

ゲームウィンドウを終了するなどして、検出済みのDS.EXEメモリが無効になった場合は、DS.EXEを再検索します。

待機中には、状態に応じて次のような表示が出ます。

```text
Waiting for anex86.exe
anex86.exe found - scanning for Wizardry 7
Waiting for DS.EXE...
```

`target`を変更している場合、表示されるプロセス名も設定内容に応じて変わります。

## 現在の制限

PC-98版の動的オブジェクト領域は、まだ完全には解析できていません。

現在、次の要素には未対応です。

- 宝箱
- 隠し物
- その他、動的オブジェクトテーブルに保存されている要素
- 取得済み・使用済み状態を必要とする一部表示
- Mapping、Locate Objectなどの呪文イベントによる表示効果

現在は、PC-98版でアドレスと構造を確認できたマップ情報のみを使用しています。

そのため、本リリースは**Experimental**です。

## 注意事項

このツールは非公式です。

本ツールの導入、動作、不具合などについて、Anex86、Wizardryの権利者・販売元、AutoMap Modの原作者・refactor作者、その他の公式サポート窓口へ問い合わせないでください。

また、ゲーム本体の著作物は一切含めていません。利用者自身が正規に所有しているゲームデータを使用してください。

本ツールの使用は利用者自身の責任で行ってください。利用前にセーブデータと`Config`フォルダをバックアップすることを推奨します。

## Sourceフォルダについて

このリポジトリの`Source`フォルダには、Wizardry7Automap.exeに対応するソースコードとビルド用ファイルを格納しています。

主な構成は以下です。

```text
Source\
  ExternalAutomapMain.cpp
  Wizardry7Automap.exe.manifest
  Wizardry7Automap.rc
  build_msys2_mingw64.bat
  Readme_build.txt
  src\wizardry_am\
    am_wiz7_external.cpp
    am_wiz7_res.cpp
    external_compat.h
```

現在の外部版は、Wizardry 6外部AutoMapと同様にWindows標準のWin32 APIとGDIを使用して描画します。

SDL2、OpenGL、およびMinGWランタイムDLLの別途配布は必要ありません。

ビルド方法の詳細は`Source\Readme_build.txt`を参照してください。

## License

This project is distributed under the **GNU General Public License version 2 or later**.

This repository contains code derived from the Wizardry 6 & 7 Automap Mod.

See [`LICENSE.txt`](LICENSE.txt) and the copyright/license notices in the source files for details.

## Acknowledgements

This project is based on the work of the **Wizardry 6 & 7 Automap Mod**.

I would like to express my deepest gratitude to the original author and the refactor author.  
Without their work, this PC-98 External AutoMap adaptation would not have been possible.

Original: Copyright (C) 2014 KoriTama  
Wizardry 6 Automap Mod:  
https://www.moddb.com/mods/wizardry-6-automap-mod

Wizardry 7 Automap Mod:  
https://www.moddb.com/mods/wizardry-7-automap-mod

Refactor: Copyright (C) 2025 DungeonCrawl-Classics.com  
Wizardry 7 Map Details:  
https://dungeoncrawl-classics.com/wizardry-series/7-crusaders-of-the-dark-savant/wizardry-7-map-details/

This project is an unofficial adaptation for the PC-9801 version of Wizardry 7.
