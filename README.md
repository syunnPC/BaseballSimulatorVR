# BaseballSimulatorVR

OpenXR を使った Windows 向け野球シミュレーターです。投球軌道の確認、投手との対戦、ティー打撃に対応します。

## ビルド・実行

Visual Studio の C++ ワークロード（MSVC `v145`、Windows SDK、C++20）と OpenXRランタイム(SteamVRなど)を使用します。

## 使い方

主な操作は XR の手元 UI から行えます。

| 操作 | キー |
| --- | --- |
| 軌道確認／対戦開始／ティー打撃 | `F9`／`F10`／`F11` |
| 設定ファイル切り替え | `Ctrl+←/→` |
| 球種選択 | `Ctrl+↑/↓` |
| 投球／打球の軌跡を個別に切り替え | `1`／`4` |
| ストライクゾーンの再校正 | `Ctrl+H` |
| 現在のセッションを CSV 出力 | `Ctrl+E` |

対戦中の `F10` は投球間隔を増やし、`Shift+F10` は減らします。
ゾーンの自動校正は、床に立った自然な直立姿勢で3秒以上静止して行います。
先読み OFF の対戦では、球種・球速・回転数はリリース後に表示します。

## 設定・プリセット

設定とログの保存先は `%LOCALAPPDATA%\BaseballSimulatorVR` です。起動時に不足する同梱設定をコピーし、既存ファイルは上書きしません。

| ファイル | 内容 |
| --- | --- |
| `EnvironmentSettings.user.json` | 投球距離、ゾーン、風、マウンド、バットなど |
| `PlaySettings.user.json` | 投球間隔、再生速度、先読み、バットを持つ手、軌跡表示 |
| `PitchSettings.user.json` | 球速・回転・球種の基本設定 |
| `PitchSettings.Difficulty.*.json` | 4難易度×右投げ（RHP）・左投げ（LHP）の8セット |
| `PitchSettings.Yamamoto.json` | 山本由伸の2025年MLBレギュラーシーズンをもとにした6球種 |

難易度別設定は球種を等確率で選び、左投げは変化方向と投手表示を反転します。フォーム速度はセット内の最速球を基準にします。
難易度別の変化量は練習用の設計値です。山本プロファイルの球速・回転数は公開統計、回転軸・揚力係数はシミュレーター向けの推定値です。
JSON の数値・真偽値は文字列にせず記述し、初期値は `*.sample.json` を参照してください。

履歴は `SessionHistory.sqlite3`、CSV 出力は `SessionHistory.current.csv` に保存します。

## テスト

リポジトリのルートで実行します。テストに VR ランタイムは不要です。

```powershell
msbuild BaseballSimulatorVR.slnx /p:Configuration=Debug /p:Platform=x64 /m
& .\x64\Debug\BaseballSimulatorVR.Tests.exe $PWD.Path
```

Release の確認では `Debug` を `Release` に変更します。

## 出典・ライセンス

- 山本由伸プロファイル：[MLB Baseball Savant](https://baseballsavant.mlb.com/savant-player/yoshinobu-yamamoto-808967)、[FanGraphs](https://www.fangraphs.com/players/yoshinobu-yamamoto/33825/pitch-type-splits?position=P)。球種の特徴は [MLB Glossary](https://www.mlb.com/glossary/pitch-types) を参照。
- 衝突・打球のモデル：Nathan [2000](https://baseball.physics.illinois.edu/AJP-Nov2000.pdf)・[2003](https://baseball.physics.illinois.edu/AJP-Feb2003.pdf)、Kensrud ほか [2017](https://baseball.physics.illinois.edu/ObliqueCollisionsWSU-AJP.pdf)、[打球軌道の分析](https://baseball.physics.illinois.edu/TrajectoryAnalysis.pdf)。
- ゾーンの身長比率：[MLB 2026 ABS資料](https://img.mlbstatic.com/opprops-images/image/upload/opprops/jgdgj1bak2bgiskwpdnm.pdf)。HMD の高さから推定します。

本体は [LICENSE.txt](LICENSE.txt)、同梱の OpenXR・SQLite は [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) を参照してください。
