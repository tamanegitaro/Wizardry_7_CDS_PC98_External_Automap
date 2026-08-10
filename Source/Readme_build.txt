Wizardry7Automap - ビルド手順
===============================

この文書は、Wizardry7Automapをソースコードからビルドする人向けです。
通常プレイだけの場合は、配布されているWizardry7Automap.exeを使用してください。


1. 描画方式
------------

Wizardry6Automapと同様に、Windows標準のWin32 APIとGDIで動作します。

マップ描画は次の構成です。

・CPU側の32bitフレームバッファへ壁、床、タイル、ノート、カーソルを描画
・W7内蔵タイル画像を最近傍法で拡大、反転、暗色化して転送
・完成したフレームをStretchDIBitsでAutomapウィンドウへ表示
・ウィンドウ、マウス、クリップボード、ノート入力、ツールチップはWin32 APIで処理

このためSDL2.dllは不要です。SDL2のヘッダーやライブラリもビルド環境へ導入する必要はありません。


2. 対象環境
------------

・64ビット版Windows
・MSYS2
・MSYS2 MINGW64シェル
・MinGW-w64 64-bit GCC
・MinGW-w64 binutils

UCRT64、CLANG64、MINGW32、MSYSシェルではなく、スタートメニューから
「MSYS2 MINGW64」を開くことを推奨します。

本ソースは64ビットビルド専用です。
32ビットでビルドすると、ソース内のstatic_assertで停止します。


3. MSYS2の準備
---------------

MSYS2 MINGW64シェルを起動し、必要に応じてMSYS2を更新します。

pacman -Syu

更新の途中でシェルを閉じるよう案内された場合は、一度閉じてMINGW64を再起動し、
次を実行します。

pacman -Su

ビルドに必要なパッケージを導入します。

pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-binutils

使用する主なツール：

・g++.exe     C++のコンパイルとリンク
・windres.exe 管理者権限要求マニフェストのリソース化
・objdump.exe 完成EXEのDLL依存確認


4. 必要なファイル
------------------

ビルドにはSourceフォルダ内の次の構成が必要です。

ExternalAutomapMain.cpp
Wizardry7Automap.exe.manifest
Wizardry7Automap.rc
build_msys2_mingw64.bat
src\wizardry_am\am_wiz7_external.cpp
src\wizardry_am\am_wiz7_res.cpp
src\wizardry_am\external_compat.h

Readme.txt、LICENSE.txt、Configフォルダは配布には必要ですが、
EXEのコンパイルそのものには使用しません。


5. ビルド方法
--------------

5.1 MSYS2 MINGW64シェルから実行

例として、プロジェクトを次へ置いた場合：

C:\DOSBox-X_work\Wiz7_PC98_Automap

MSYS2 MINGW64で次を実行します。

cd /c/DOSBox-X_work/Wiz7_PC98_Automap/Source
./build_msys2_mingw64.bat

5.2 エクスプローラーから実行

必要なMSYS2ツールがC:\msys64\mingw64\binに導入されていれば、
build_msys2_mingw64.batをダブルクリックしてもビルドできます。


6. 成功時の出力
----------------

Sourceフォルダへ次が作成されます。

Wizardry7Automap.exe

配布用ZIPを更新する場合は、完成したEXEをプロジェクト直下の
Wizardry7Automap.exeへコピーしてください。

成功時の表示例：

Building Wizardry7Automap.exe with Win32 API/GDI and a static MinGW runtime...
Built Wizardry7Automap.exe.
Verified: no SDL2 or MinGW runtime DLL dependency.
Administrator privileges are embedded in the EXE manifest.

途中生成されるWizardry7Automap.res.oとDLL確認用レポートは、正常終了時に削除されます。


7. ビルド設定
--------------

主な設定：

・64ビット強制：-m64
・C++17 GNU拡張：-std=gnu++17
・最適化：-O2
・リリースビルド：-DNDEBUG
・警告：-Wall -Wextra
・Unicode：UNICODE / _UNICODE
・Windows GUIアプリ：-municode -mwindows
・MinGWランタイムの静的リンク
・未使用関数、未使用データセクションの削除
・デバッグシンボルの削除
・管理者権限要求マニフェストの埋め込み

主要なリンク先はWindows標準ライブラリだけです。

-lcomdlg32 -luser32 -lgdi32 -lshell32 -ladvapi32

SDL2、OpenGL、DirectInput、HID、SetupAPI、WinMMはリンクしません。


8. EXEサイズについて
---------------------

W7版には内蔵タイル画像とW7固有のマップ処理が含まれるため、W6版と完全に同じサイズにはなりません。
実際のサイズは使用するGCC、binutils、最適化、静的ランタイムの版によって変わります。


9. DLL依存確認
---------------

MinGWランタイムを静的リンクするため、正常なビルドでは次を別途配布する必要はありません。

SDL2.dll
libgcc_s_*.dll
libstdc++-6.dll
libwinpthread-1.dll

Windows標準DLLへの依存は残ります。
ビルドBATはobjdump -pで完成EXEを調査し、上記DLLへの依存が残っていないか確認します。


10. 管理者権限要求マニフェスト
------------------------------

Wizardry7Automap.exe.manifestにはrequireAdministratorが指定されています。
Wizardry7Automap.rcをwindresでコンパイルし、EXEへリンクします。

対象エミュレーターのメモリを読み取るため、Automapと対象プロセスの権限レベルを揃える必要があります。


11. ビルドに失敗する場合
-------------------------

「g++.exe not found」
  mingw-w64-x86_64-gccを導入してください。

  pacman -S --needed mingw-w64-x86_64-gcc

「windres.exe not found」または「objdump.exe not found」
  mingw-w64-x86_64-binutilsを導入してください。

  pacman -S --needed mingw-w64-x86_64-binutils

「Wizardry7Automap must be built as a 64-bit executable」
  MINGW32ではなくMSYS2 MINGW64を使用してください。
  g++.exeがC:\msys64\mingw64\bin\g++.exeであることを確認してください。

C++のerrorまたはwarningが表示される
  表示されたファイル名、行番号、全文を確認してください。
  BATはwarningを表示しますが、-Werrorは指定していません。

MinGWランタイムDLLが検出される
  -static -static-libgcc -static-libstdc++が有効であることを確認してください。

リソースビルドに失敗する
  Wizardry7Automap.rcとWizardry7Automap.exe.manifestが同じフォルダに存在するか確認してください。


12. 配布ZIPの推奨構成
----------------------

Wizardry7Automap.exe
Readme.txt
LICENSE.txt
Config\Wizardry7Automap.conf
Source\ExternalAutomapMain.cpp
Source\Wizardry7Automap.exe.manifest
Source\Wizardry7Automap.rc
Source\build_msys2_mingw64.bat
Source\Readme_build.txt
Source\src\wizardry_am\am_wiz7_external.cpp
Source\src\wizardry_am\am_wiz7_res.cpp
Source\src\wizardry_am\external_compat.h

次のファイルは利用者ごとのデータなので配布ZIPへ入れません。

Config\Wizardry7Automap_notes.bin
Config\Wizardry7Automap_notes.bin.tmp
Config\Wizardry7Automap_notes.bin.bad


13. ライセンス
---------------

本プロジェクトはGNU General Public License version 2またはそれ以降の条件で公開されています。
改変または再配布する場合はLICENSE.txtを確認してください。
