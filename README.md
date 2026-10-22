# BaseballSimulatorVR

OpenXR を使った Windows 向けの VR 野球シミュレーターです。投球軌道の確認、投手との対戦、ティー打撃、捕手としての捕球ができます。

## 動作環境

- Windows 10 / 11（x64）
- AVX2 に対応した CPU
- Direct3D 12（機能レベル 11.0 以上）に対応した GPU
- `XR_KHR_D3D12_enable` に対応した OpenXR ランタイムと HMD（SteamVR、Meta Quest Link、Windows Mixed Reality など）とモーションコントローラー
- Visual C++ 再頒布可能パッケージ（x64）

## ビルド

Visual Studio 2026 の「C++ によるデスクトップ開発」ワークロード（MSVC `v145`）と Windows SDK を使います。

1. `BaseballSimulatorVR.slnx` を Visual Studio で開きます。
2. 構成を `Release`、プラットフォームを `x64` にしてビルドします。
3. `x64\Release\` に実行ファイル、必要な DLL、同梱プリセットが出力されます。

コマンドラインでは次のとおりです。Visual Studio 2022 の場合は `/p:PlatformToolset=v143` を加えます。

```
msbuild BaseballSimulatorVR.slnx /m /p:Configuration=Release /p:Platform=x64
```

## 遊び方

HMD を着けてアプリを起動します。操作は手元の UI パネルとコントローラーで行い、デスクトップのウィンドウにはミラー表示が出ます（キーボードでも操作できます）。

初回は身長に合わせてストライクゾーンを決めるため、起動後 3 秒ほどまっすぐ立ってください。Ctrl+H または手元 UI からやり直せます。

### モード

| モード | キー | 内容 |
| --- | --- | --- |
| 投球軌道 | F9 | 球種ごとの軌道と変化を確認します。 |
| 対戦 | F10 | 投手が一定間隔で投げる球を打ちます。ストライク・ボールは自動で判定されます。 |
| ティー打撃 | F11 | ティーに置いたボールを打ちます。 |
| 捕手 | F | 投手にサインを出し、ミットで構えたところへ来る球を捕ります。 |

### VR の操作

- **UI パネル**：UI の手のコントローラーを顔のほうへ向けると、その手の近くにパネルが出ます。もう一方の手のレーザーを項目に向けてトリガーで選びます。対戦とティー打撃ではバットを持つ手、捕手では投げる手がレーザーの手で、その反対が UI の手です。それ以外のモードでは右手が UI の手です。
- **移動**：左スティックで左右に向きを変え、右スティックで移動します。
- **リセンター**：左スティックの押し込み（Vive は左メニューボタン）で、今の位置と向きを打席・投手方向に合わせ直します。
- **バット**：コントローラーの握りに合わせて持ちます。打つとコントローラーが振動し、芯で捉えると短く締まった手応え、グリップ寄りや先端だとしびれるような長い振動になります。もう一方のコントローラーをグリップに添えている（両手持ち）と、そちらも振動します。

手元 UI のパネル：

- **メイン**：モードの切り替え、各パネルの表示と呼び戻し
- **投球**：狙うゾーンの選択（25 マス、複数可）、球種と設定ファイルの切り替え、再生・一時停止、再生速度
- **計測**：軌道とストライクゾーンの表示、マウンドの高さ、投球予告マーカー、打席（左右）、バットの種類

25 マスのうち中央の 9 マスがストライクゾーン、外周の 16 マスがボールゾーンです。ストライクとボールのマスが両方対象のとき（何も選んでいないときを含む）は、ボールゾーンへは投げにくく、全マスが対象ならおよそ 3 球に 2 球がストライクゾーンに来ます。

バットは木製・金属・ウレタン・初心者の 4 種類です。初心者バット（オレンジ）は、グリップ以外のどこに当たっても芯で捉えたのと同じ打球になり、よく飛びます。

### 捕手モード

利き手と反対の手にミットをはめます。利き手は計測パネル（捕手モードでは「利き手」と表示）または E キーで切り替えます。

1. **サイン**：ミットの上のカードに球種が出ます。A / X で次の球種、B / Y で前の球種（左右どちらのボタンでも可）、トリガーで決定します。
2. **構える**：決定したときのミットの位置を狙って投手が投げます。投手の制球にはばらつきがあり、ときどき大きく外れます。ナックルボールは投手にも行き先がわかりません。
3. **捕る**：ミットをボールに合わせ、届く直前から直後までにミットの手のトリガーを握ると捕球です。早く握りすぎたりミットの縁に当たったりするとはじき、ミットに届かなければパスボールかワイルドピッチになります。
4. **次の球**：トリガーを離すと、投手が準備してサイン待ちに戻ります。

捕球数と結果はスコアボードに表示されます。UI パネルを開いてレーザーを出している間は、レーザーの手のトリガーは UI の操作に使われます。

### 球場と打球の判定

グラウンドは公式規格の寸法で、両翼 330 ft・左中間と右中間 約 380 ft・中堅 400 ft、高さ 8 ft のフェンスに囲まれた球場です（地面の表示（3 キー）で切り替え）。

- ボールはフェンス・壁・バックネットに当たると跳ね返り、芝と土では弾み方と転がり方が変わります。地面にはボールの影が落ちます。
- フェアゾーンでフェンスを越えればホームランです。飛距離は、球場がなかった場合に落ちる地点までの距離です。
- 対戦とティー打撃では、標準的な守備位置の 9 人を相手に打球の結果を判定し、「ショートゴロ」「レフト前ヒット」「左中間二塁打」「センターへホームラン」のように表示します。走者・守備シフト・野手ごとの能力は考慮しません。

### スコアボード

中堅のスコアボードに、直前の投球の球種・球速・回転数・判定・変化量（cm）と、打球の初速・角度・飛距離・最高点・打点（バットのどこに当たったか）を表示します。右側の 2 つの図は投球の軌道です。

- **捕手目線**：本塁の後ろから見た軌道です。ストライクゾーンの枠とホームベースを重ねて表示します。
- **真上から**：上が投手、下が本塁です。横方向の動きを拡大して、どのあたりで曲がったか、ホームベースのどこを通ったかを表示します。

### 対応コントローラー

| コントローラー（インタラクションプロファイル） | 選択 | 向きを変える | 移動 | リセンター | 次の球種（捕手） | 前の球種（捕手） |
| --- | --- | --- | --- | --- | --- | --- |
| Meta Quest Touch など（`oculus/touch_controller`） | トリガー | 左スティック | 右スティック | 左スティック押し込み | X / A | Y / B |
| Valve Index（`valve/index_controller`） | トリガー | 左スティック | 右スティック | 左スティック押し込み | A | B |
| HTC Vive（`htc/vive_controller`） | トリガー | 左トラックパッド | 右トラックパッド | 左メニューボタン | グリップ | 右メニューボタン |
| Windows Mixed Reality（`microsoft/motion_controller`） | トリガー | 左スティック | 右スティック | 左スティック押し込み | トラックパッド押し込み | メニュー |
| Khronos Simple Controller（`khr/simple_controller`） | select | なし | なし | なし | なし | メニュー |

ヘッドセットを外すなどして入力が離れている間は一時停止します。ランタイムとの接続が切れた場合は、再接続を待って再開します。

### キーボード

デスクトップのウィンドウがアクティブなときに使えます。記号キーは US 配列での位置です。一覧はメニューの「ヘルプ」→「バージョン情報」（Alt+?）でも確認できます。

| キー | 操作 |
| --- | --- |
| F9 / F10 / F11 / F | モード：投球軌道 / 対戦 / ティー打撃 / 捕手 |
| F10（対戦中）/ Shift+F10 | 投球間隔を 0.25 秒長く / 短く |
| F1 / F2 / F3 / F4 | 球種：ストレート / スライダー / カーブ / チェンジアップ |
| Ctrl+← / Ctrl+→ | 前 / 次の設定ファイル |
| Ctrl+↑ / Ctrl+↓ | 前 / 次の球種 |
| R | 同じ設定で投げ直す |
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
| F7 / F8（7 / 8） | 軌跡を薄く / 濃く |
| E / 9 | 打席（左右。捕手モードでは利き手）/ バットの種類 |
| , / . 、; / ' | バットの角度、バットの向きを 2.5° ずつ調整 |
| テンキー 4 / 6、8 / 2、9 / 3、5 | ティーの位置を左右、前後、上下に 5 cm 移動、5 で元に戻す |
| + / − / 0 | 再生速度を 5% ずつ変更（0〜200%）、0 で 100% |
| C / V / B / N / M | 直近の投球に結果を記録：ストライク / ボール / インプレー / ファウル / 保留 |
| O / P | 記録した結果のスコアを ±0.1 |
| K / L | 投球設定を JSON に保存 / JSON から読み込み |
| Ctrl+E | セッションの記録を CSV に出力 |
| Ctrl+H | ストライクゾーンを決め直す（3 秒間まっすぐ立つ） |

## 設定とデータ

ユーザーの設定と記録は `%LOCALAPPDATA%\BaseballSimulatorVR` に保存されます。

| ファイル | 内容 |
| --- | --- |
| `PitchSettings.user.json` | 保存した投球設定（K キーなど） |
| `EnvironmentSettings.user.json` | 投球距離・風・バット・環境 |
| `PlaySettings.user.json` | モードや表示の状態 |
| `SessionHistory.sqlite3` | 投球と打球の記録（Ctrl+E で `SessionHistory.current.csv` に出力） |

- 設定を初期状態に戻すには、アプリを終了して `*.user.json` を削除します。次回起動時に作り直されます。
- 読み込めない設定ファイルは `*.broken-<日時>.json` に名前を変えて退避し、既定値で起動します。
- 古いバージョンが設定フォルダーにコピーした同梱プリセットは、`LegacyPresetBackup\<日時>\` に移動されます。自分で調整したものを使う場合は、別の名前で設定フォルダーに戻してください。設定フォルダーに置いた `PitchSettings.<名前>.json` は一覧に表示されます。

`EnvironmentSettings.user.json` では次の項目も指定できます。標高・気温・湿度は空気の密度を通じて投球と打球の飛び方に影響します。

| 項目 | 内容（既定値、範囲） |
| --- | --- |
| `altitudeMeters` | 球場の標高 [m]（0、−500〜5000） |
| `temperatureCelsius` | 気温 [°C]（15、−40〜60） |
| `relativeHumidityPercent` | 相対湿度 [%]（0、0〜100） |
| `batMassKilograms` | バットの質量 [kg]（0.885、0.3〜1.5） |

### 同梱プリセット

手元 UI や Ctrl+← / → で切り替えます。左投げの投手が選べるものもあります。

| プリセット | 内容 |
| --- | --- |
| `Difficulty.*` | 難易度別（VeryEasy / Easy / Normal / Hard / Expert）。上の難易度ほど球種が増え、Expert は 9 球種 |
| `Challenge1` / `Challenge2` | ストレート・カーブ・シンカー / ツーシーム |
| `Beginner.*Softball*` | 初心者向けのソフトボール（通常と、変化の大きい BigBreak） |
| `SlowFastball.*` | 70〜100 km/h のストレート（野球・ソフトボール） |
| `Toss.*` | 9 m からの下手投げトス（野球・ソフトボール） |
| `Yamamoto` | 山本由伸投手の 2025 年 MLB レギュラーシーズンのデータに基づく球種 |
| `Knuckleball` | ナックルだけを投げるナックルボーラー。毎球違う揺れ方をします |

## テスト

`BaseballSimulatorVR.Tests` をビルドすると `x64\<構成>\BaseballSimulatorVR.Tests.exe` が出力されます。

```
x64\Release\BaseballSimulatorVR.Tests.exe [オプション] [リポジトリのルート]
```

| オプション | 内容 |
| --- | --- |
| `--list` | スイートの一覧（GPU を使うものに `[gpu]`） |
| `--filter <文字列>` | 名前にその文字列を含むスイートだけを実行（複数指定可） |
| `--skip-gpu` | D3D12 を使うスイートを SKIP にする |
| `--seed <数値\|random>` | 乱数シード（既定は環境変数 `BASEBALLSIM_RANDOM_SEED`、なければ 5489） |
| `--help` | ヘルプ |

GPU テストの待ち時間は環境変数 `BASEBALLSIM_TEST_TIMEOUT_SCALE` で延ばせます。終了コードは 0 が成功、1 が失敗あり、2 が引数・設定のエラーです。

## 既知の制限

- OpenXR ランタイムと HMD が必要です。デスクトップだけで遊ぶモードはありません。
- ハンドトラッキングには対応していません。
- キーボード操作は US 配列が前提です。
- 打球の判定では、走者・守備シフト・野手ごとの能力を扱いません。

## 出典・ライセンス

- 山本由伸プロファイル：[Baseball Savant](https://baseballsavant.mlb.com/savant-player/yoshinobu-yamamoto-808967)、[FanGraphs](https://www.fangraphs.com/players/yoshinobu-yamamoto/33825/pitch-type-splits?position=P)、[MLB Glossary: Pitch Types](https://www.mlb.com/glossary/pitch-types)
- 投球・打球の空気力学：Nathan「[The Physics of Baseball Trajectories](https://baseball.physics.illinois.edu/TrajectoryAnalysis.pdf)」、Watts & Sawyer「Aerodynamics of a knuckleball」（Am. J. Phys. 43, 1975）
- バットとボールの衝突・芯：Nathan [2000](https://baseball.physics.illinois.edu/AJP-Nov2000.pdf)・[2003](https://baseball.physics.illinois.edu/AJP-Feb2003.pdf)、Kensrud ほか [2017](https://baseball.physics.illinois.edu/ObliqueCollisionsWSU-AJP.pdf)、Russell [2004](https://www.acs.psu.edu/drussell/bats/sweetspot.html)・[2013](https://www.acs.psu.edu/drussell/bats/sting-damp.html)、Cross [1998](https://baseball.physics.illinois.edu/CrossSweetSpotBat.pdf)、[JSME 2017](https://www.jstage.jst.go.jp/article/jsmeshd/2017/0/2017_C-16/_article/-char/ja)
- 地面での弾み：Cross「Grip-slip behavior of a bouncing ball」（Am. J. Phys. 70, 2002）
- ストライクゾーンの身体位置：[AIST 人体寸法データベース](https://www.airc.aist.go.jp/dhrt/97-98_Statistics.pdf)
- 球場と打球の判定：公認野球規則 2.01、[MLB Field Dimensions](https://www.mlb.com/glossary/rules/field-dimensions)、[Strike Zone](https://www.mlb.com/glossary/rules/strike-zone)、[Launch Angle](https://www.mlb.com/glossary/statcast/launch-angle)、[Fielding Alignments](https://www.mlb.com/glossary/statcast/shifts)、[Catch Probability](https://www.mlb.com/glossary/statcast/catch-probability)、[Sprint Speed](https://baseballsavant.mlb.com/sprint_speed_leaderboard)

本体のライセンスは [LICENSE.txt](LICENSE.txt)、同梱の OpenXR ローダーと SQLite については [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) を参照してください。
