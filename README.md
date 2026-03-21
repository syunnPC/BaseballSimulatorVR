# BaseballSimulatorVR

OpenXR を使って動作する、野球の投球・打撃シミュレーション用の Windows アプリです。  
設定 JSON を切り替えながら挙動を調整でき、セッションログは SQLite に保存されます。

## 動作環境

- Windows
- Visual Studio 2022
- `x64` ビルド
- OpenXR ランタイムが利用可能な環境

## ビルド

1. `BaseballSimulatorVR.slnx` を Visual Studio で開きます。
2. 構成を `Debug|x64` または `Release|x64` に設定します。
3. ビルドして実行します。

`openxr_loader.dll` と `sqlite3.dll` はビルド後に出力先へコピーされます。

## 設定ファイル

以下の JSON を編集して挙動を調整します。

- `EnvironmentSettings.user.json`
- `PitchSettings.user.json`
- `PlaySettings.user.json`

初期値の参考として `*.sample.json` も含まれています。

## ライセンス

このプロジェクト本体のライセンスは [LICENSE.txt](./LICENSE.txt) を参照してください。

同梱している主要なサードパーティーコンポーネント:

- OpenXR loader / headers
  - Version: `1.1.57`
  - License: `Apache-2.0`
- SQLite
  - Version: `3.51.3`

詳細は [THIRD_PARTY_NOTICES.md](./THIRD_PARTY_NOTICES.md) を参照してください。
