# BaseballSimulatorVR

OpenXR を使った Windows 向け野球シミュレーターです。投球軌道の確認、投手との対戦、ティー打撃に対応します。

> スクリーンショットは準備中です。

## 動作環境

- Windows 10 / 11（x64）
- AVX2 に対応した CPU（Release ビルドは `/arch:AVX2` でコンパイルしています）
- Direct3D 12（機能レベル 11.0 以上）に対応した GPU とドライバー
- `XR_KHR_D3D12_enable` に対応した OpenXR ランタイムと HMD（SteamVR、Meta Quest Link、Windows Mixed Reality など）
- Visual C++ 再頒布可能パッケージ（x64）。実行ファイルは動的 CRT（`/MD`）でビルドしています。

## ビルド

Visual Studio 2026 の「C++ によるデスクトップ開発」ワークロード（MSVC `v145`、C++20）と Windows SDK を使用します。Windows SDK はインストール済みの最新版（10.0 系）が自動で選ばれます。

1. `BaseballSimulatorVR.slnx` を Visual Studio で開きます。
2. 構成を `Release`、プラットフォームを `x64` にしてビルドします。スタートアッププロジェクトは `BaseballSimulatorVR` です。
3. `x64\Release\` に実行ファイルが出力されます。OpenXR ローダーと SQLite の DLL、同梱のプリセット JSON、`LICENSE.txt`、`THIRD_PARTY_NOTICES.md` も同じフォルダーにコピーされます。

コマンドラインでは Developer PowerShell などから次のように実行します。

```
msbuild BaseballSimulatorVR.slnx /m /p:Configuration=Release /p:Platform=x64
```

Visual Studio 2022 しかない環境では `/p:PlatformToolset=v143` を追加してください（CI も `v145` がないランナーではこの指定でビルドします）。

ソリューションは次の 3 プロジェクトで構成されています。共通のコンパイラー・リンカー設定はルートの `Directory.Build.props` にまとめています。

- `BaseballSimulatorVR.Lib`：`Main.cpp` 以外の本体コードをまとめた静的ライブラリ
- `BaseballSimulatorVR`：アプリ本体（`Main.cpp` とリソース）
- `BaseballSimulatorVR.Tests`：テスト。アプリと同じライブラリをリンクするので、アプリと同じコード生成設定（AVX2、LTCG）のまま検証できます。

## テスト

`BaseballSimulatorVR.Tests` をビルドすると `x64\<構成>\BaseballSimulatorVR.Tests.exe` が出力されます。

```
x64\Release\BaseballSimulatorVR.Tests.exe [オプション] [リポジトリのルート]
```

リポジトリのルートを省略すると、実行ファイルのフォルダー、次にカレントディレクトリから親へたどり、`BaseballSimulatorVR.slnx` のあるフォルダーを使います。

| オプション | 内容 |
| --- | --- |
| `--list` | スイートの一覧を表示します。GPU を使うスイートには `[gpu]` が付きます。 |
| `--filter <文字列>` | 名前にその文字列を含むスイートだけを実行します（大文字・小文字は区別しません）。複数指定すると、いずれかに一致するものを実行します。 |
| `--skip-gpu` | D3D12 / DXGI を使うスイートを実行せず、SKIP として報告します。 |
| `--seed <数値\|random>` | ランダムなゾーン・球種選択の乱数シードです。各スイートの開始前に設定し直します。省略時は `BASEBALLSIM_RANDOM_SEED`、それもなければ 5489 を使います。`random` は毎回新しいシードを選んで表示します。 |
| `--help` | ヘルプを表示します。 |

| 環境変数 | 内容 |
| --- | --- |
| `BASEBALLSIM_TEST_TIMEOUT_SCALE` | GPU テストのウォッチドッグ・待ち時間の倍率（既定値 1、最大 1000）。遅い CI や AddressSanitizer ビルドで指定します。 |
| `BASEBALLSIM_RANDOM_SEED` | 乱数シードの既定値。アプリ本体もこの値を使うので、ランダム選択を再現できます。 |

- 結果はスイートごとに `PASS` / `FAIL` / `SKIP` で表示され、失敗メッセージには `ファイル名:行番号` が付きます。
- 終了コードは 0（失敗なし。SKIP は含んでもよい）、1（失敗したスイートがある）、2（引数・設定のエラー）です。
- GPU テストは WARP（ソフトウェアラスタライザー）で動くため、GPU のない環境でも実行できます。D3D12 を使えない環境（Wine など）では `--skip-gpu` を指定してください。

GitHub Actions（`.github/workflows/build.yml`）では、Windows ランナーで Release / Debug のビルドと全スイートのテストを実行します。AddressSanitizer ビルドと `/analyze` のジョブは参考情報として結果を報告し、失敗してもワークフロー全体は失敗にしません。

## 使い方

主な操作は XR の手元 UI から行えます。デスクトップのウィンドウにはミラー表示が出て、キーボードでも操作できます。

### モード

- 投球軌道（F9）：球種ごとの軌道や変化量を確認します。
- 対戦（F10）：投手が一定間隔で投げる球を打ちます。ストライク・ボールはゾーン通過時に自動で記録されます。
- ティー打撃（F11）：ティーに置いたボールを打ちます。

ストライクゾーンの高さは HMD の高さから推定した身長に合わせます。未設定の場合は起動後に自動で調整するので、3 秒ほどまっすぐ立ってください。Ctrl+H または手元 UI からやり直せます。

### VR の操作

- UI を持つ手のコントローラーを HMD のほうへ向けると、その手の近くに UI パネルが表示されます。パネルの向きはその手のレイに追従します。
- 対戦とティー打撃では、バットを持つ手と反対の手が UI の手になり、バットを持つ手のレーザーで操作します。それ以外のモードでは右手が UI、左手がレーザーです。
- レーザーを項目に向けてトリガーを引くと選択します。バットはコントローラーのグリップ姿勢に追従します。
- 左スティックで視点を回転（ヨー・ピッチ）、右スティックで移動（前後・左右）します。
- メインパネル：モードの切り替えと各パネルの表示（見失ったパネルの呼び戻しを含む）
- 投球パネル：25 分割ゾーンの複数選択、球種と設定ファイルの切り替え、再生・一時停止、再生速度のスライダー
- 計測パネル：軌道とストライクゾーンの表示、マウンドの高さ、投球予告マーカー、打席（右・左）、バットの素材

### 対応コントローラー

OpenXR の次のインタラクションプロファイルに入力を割り当てています。どのプロファイルでも aim ポーズ（レーザー）と grip ポーズ（バット）を使います。

| インタラクションプロファイル | 選択 | 視点の回転 | 移動 |
| --- | --- | --- | --- |
| `/interaction_profiles/oculus/touch_controller`（Meta Quest Touch など） | トリガー | 左スティック | 右スティック |
| `/interaction_profiles/valve/index_controller`（Valve Index） | トリガー | 左スティック | 右スティック |
| `/interaction_profiles/htc/vive_controller`（HTC Vive） | トリガー | 左トラックパッド | 右トラックパッド |
| `/interaction_profiles/microsoft/motion_controller`（Windows Mixed Reality） | トリガー | 左スティック | 右スティック |
| `/interaction_profiles/khr/simple_controller`（Khronos Simple Controller） | select | なし | なし |

### キーボード

デスクトップのウィンドウがアクティブなときに使えます。記号キーは US 配列での表記です（JIS 配列では位置が異なります）。一覧はメニューの「ヘルプ」→「バージョン情報」（Alt+?）でも確認できます。

| キー | 操作 |
| --- | --- |
| F9 / F10 / F11 | モード：投球軌道 / 対戦 / ティー打撃 |
| F10（対戦中）/ Shift+F10 | 投球間隔を 0.25 秒長く / 短く |
| F1 / F2 / F3 / F4 | 球種プリセット：ストレート / スライダー / カーブ / チェンジアップ |
| Ctrl+← / Ctrl+→ | 前 / 次の設定ファイル |
| Ctrl+↑ / Ctrl+↓ | 前 / 次の球種 |
| R | 現在の設定で投球をやり直す |
| Q | 投球予告マーカーの表示切り替え |
| ↑ / ↓、← / → | 初速、横方向の速度を調整 |
| PgUp / PgDn | 回転数を ±100 rpm |
| I | 積分法の切り替え（RK4 / 半陰的オイラー法） |
| X、Y / H、U / J | 縫い目による揺らぎの切り替え、その振幅、周波数 |
| W / S、A / D | 前後の風、横風を調整 |
| Z、T / G | 高さに応じた風の切り替え、そのシア係数 |
| [ / ] | 投球距離を ±0.1 m |
| F12 | マウンドの高さを視点に反映するかを切り替え |
| 1 / 2 / 3 / 4 | 投球軌道（なし / 全体 / ゾーンのみ）、ストライクゾーン（なし / 面 / 箱）、地面、打球軌道の表示 |
| F5 / F6（5 / 6） | FXAA を弱く / 強く |
| F7 / F8（7 / 8） | 軌跡の不透明度を下げる / 上げる |
| E / 9 | 打席（右・左）/ バットの素材（木製・金属・ウレタン） |
| , / . 、; / ' | バットの角度、バットのヨーを 2.5° ずつ調整 |
| テンキー 4 / 6、8 / 2、9 / 3、5 | ティーの位置を左右、前後、上下に 5 cm ずつ移動、5 で元に戻す |
| + / − / 0 | 再生速度を 5% ずつ変更（0〜200%）、0 で 100% に戻す |
| C / V / B / N / M | 直近の投球に結果を記録：ストライク / ボール / インプレー / ファウル / 保留 |
| O / P | 記録した結果のスコアを ±0.1 |
| K / L | 現在の投球設定を JSON に保存 / JSON から読み込み |
| Ctrl+E | 現在のセッションを CSV に出力 |
| Ctrl+H | ストライクゾーンを再調整（3 秒間まっすぐ立つ） |

### データの保存場所

- 初回起動時に、同梱の設定ファイル（プリセット JSON）を `%LOCALAPPDATA%\BaseballSimulatorVR` にコピーし、以降はこのフォルダーのファイルを読み書きします。既にあるファイルは上書きしません。
- 投球の記録は同じフォルダーの `SessionHistory.sqlite3` に保存され、Ctrl+E で `SessionHistory.current.csv` に出力できます。
- 設定を初期状態に戻すには、アプリを終了してこのフォルダーのファイルを削除します。次回起動時に同梱の設定ファイルがコピーし直されます。

### 同梱プリセット

右投げ（RHP）・左投げ（LHP）の両方を用意したものが多く、手元 UI や Ctrl+← / → で切り替えられます。ファイルの配置は今後変更される可能性があります。

- `Difficulty.*`：難易度別のトレーニング用（VeryEasy / Easy / Normal / Hard / Expert）。難易度が上がるほど球種が増え、Expert は 9 球種です。
- `Challenge*`：チャレンジ用（Challenge1：ストレート・カーブ・シンカー、Challenge2：ツーシーム）。
- `Beginner.*Softball*`：初心者向けのソフトボール（通常の変化と、変化の大きい BigBreak）。
- `SlowFastball.*`：70〜100 km/h のストレート（野球ボール・ソフトボール）。
- `Toss.*`：9 m の距離からの下手投げトス（野球ボール・ソフトボール）。
- `Yamamoto`：山本由伸投手の 2025 年 MLB レギュラーシーズンのデータに基づくプロファイル。

## 既知の制限

- OpenXR ランタイムと HMD が必須です。ランタイムが起動していない、または `XR_KHR_D3D12_enable` に対応していない場合は、エラーを表示して終了します。デスクトップだけで遊ぶモードはありません。
- Windows x64 と Direct3D 12 専用です。Release ビルドは AVX2 に対応していない CPU では動作しません。
- ハンドトラッキングには対応していません。コントローラーが必要です。
- キーボード操作は US 配列を前提にしています。
- 打球・投球のモデルは近似を含みます。球種の回転軸や揚力の多くはトレーニング用にフィッティングしたパラメーターです（下の「出典・ライセンス」も参照してください）。

## 出典・ライセンス

- 山本由伸プロファイル：[MLB Baseball Savant](https://baseballsavant.mlb.com/savant-player/yoshinobu-yamamoto-808967)、[FanGraphs](https://www.fangraphs.com/players/yoshinobu-yamamoto/33825/pitch-type-splits?position=P)。球種の特徴は [MLB Glossary](https://www.mlb.com/glossary/pitch-types) を参照。
- 衝突・打球のモデル：Nathan [2000](https://baseball.physics.illinois.edu/AJP-Nov2000.pdf)・[2003](https://baseball.physics.illinois.edu/AJP-Feb2003.pdf)、Kensrud ほか [2017](https://baseball.physics.illinois.edu/ObliqueCollisionsWSU-AJP.pdf)、[打球軌道の分析](https://baseball.physics.illinois.edu/TrajectoryAnalysis.pdf)。
- 芯の範囲：Russell [2004](https://www.acs.psu.edu/drussell/bats/sweetspot.html)・[2013](https://www.acs.psu.edu/drussell/bats/sting-damp.html)、Cross [1998](https://baseball.physics.illinois.edu/CrossSweetSpotBat.pdf)。34インチの先端から5〜7インチを全長比に換算した近似で、個別バットの形状・振動特性は計算しません。軟式球とウレタンの反発は [JSME 2017](https://www.jstage.jst.go.jp/article/jsmeshd/2017/0/2017_C-16/_article/-char/ja) を参照。
- ゾーンの身体位置：[AIST人体寸法統計](https://www.airc.aist.go.jp/dhrt/97-98_Statistics.pdf)。HMDから身長を推定し、膝付近と胸骨上縁・胴囲高の中間を近似します。個人の関節位置は測定しません。
- ベース寸法と球の接触判定：[MLB Field Dimensions](https://www.mlb.com/glossary/rules/field-dimensions)、[MLB Strike Zone](https://www.mlb.com/glossary/rules/strike-zone)。

本体は [LICENSE.txt](LICENSE.txt)、同梱の OpenXR・SQLite は [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) を参照してください。
