# BaseballSimulatorVR

OpenXR を使った Windows 向け野球シミュレーターです。投球軌道の確認、投手との対戦、ティー打撃に対応します。

## ビルド・実行

Visual Studio の C++ ワークロード（MSVC `v145`、Windows SDK、C++20）と OpenXRランタイム(SteamVRなど)を使用します。

## 使い方

主な操作は XR の手元 UI から行えます。

## 投球の空力モデル

投球プロファイル（`PitchProfile`）のマグヌス力は、回転軸と空気に対する進行方向の外積から求めます。係数表またはプロファイルから選んだ揚力係数に、進行方向に垂直な回転成分（両方向のなす角を θ としたときの `|sin θ|`）を反映し、軸が進行方向に近づくほど力が連続して弱くなります。[Nathan 2021 の prescription a](https://baseball.physics.illinois.edu/HawkeyeSpinAnalysis-Part%20I-rev1.pdf) に相当する角度補正で、実測軌道の再現精度は各プロファイルの係数にも依存します。

縫い目による追加の横力は、周期的に変化する既存の近似モデルです。進行方向に垂直な回転軸の投影を使い、平行に近い軸でも補正が急変しないようにしています。この補正は、実測から求めた seam-shifted wake のモデルではありません。

## 出典・ライセンス

- 山本由伸プロファイル：[MLB Baseball Savant](https://baseballsavant.mlb.com/savant-player/yoshinobu-yamamoto-808967)、[FanGraphs](https://www.fangraphs.com/players/yoshinobu-yamamoto/33825/pitch-type-splits?position=P)。球種の特徴は [MLB Glossary](https://www.mlb.com/glossary/pitch-types) を参照。
- 衝突・打球のモデル：Nathan [2000](https://baseball.physics.illinois.edu/AJP-Nov2000.pdf)・[2003](https://baseball.physics.illinois.edu/AJP-Feb2003.pdf)、Kensrud ほか [2017](https://baseball.physics.illinois.edu/ObliqueCollisionsWSU-AJP.pdf)、[打球軌道の分析](https://baseball.physics.illinois.edu/TrajectoryAnalysis.pdf)。
- 芯の範囲：Russell [2004](https://www.acs.psu.edu/drussell/bats/sweetspot.html)・[2013](https://www.acs.psu.edu/drussell/bats/sting-damp.html)、Cross [1998](https://baseball.physics.illinois.edu/CrossSweetSpotBat.pdf)。34インチの先端から5〜7インチを全長比に換算した近似で、個別バットの形状・振動特性は計算しません。軟式球とウレタンの反発は [JSME 2017](https://www.jstage.jst.go.jp/article/jsmeshd/2017/0/2017_C-16/_article/-char/ja) を参照。
- ゾーンの身体位置：[AIST人体寸法統計](https://www.airc.aist.go.jp/dhrt/97-98_Statistics.pdf)。HMDから身長を推定し、膝付近と胸骨上縁・胴囲高の中間を近似します。個人の関節位置は測定しません。
- ベース寸法と球の接触判定：[MLB Field Dimensions](https://www.mlb.com/glossary/rules/field-dimensions)、[MLB Strike Zone](https://www.mlb.com/glossary/rules/strike-zone)。

本体は [LICENSE.txt](LICENSE.txt)、同梱の OpenXR・SQLite は [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) を参照してください。
