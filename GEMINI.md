# GEMINI.md - torabo_tsuki_lp ZMKファームウェア

このファイルはGeminiを使用して `torabo_tsuki_lp` キーボードファームウェアの開発進捗を管理・追跡するためのものです。ビルドプロセスには含まれません。

## プロジェクト概要

このリポジトリには、分割キーボード `torabo_tsuki_lp` のZMKファームウェア設定が含まれています。

### 主要コンポーネント

*   **キーボード名:** `torabo_tsuki_lp`
*   **タイプ:** シールド (`bmp_boost` のようなコントローラーボードと共に使用)
*   **特徴:**
    *   分割キーボード (`torabo_tsuki_lp_left` と `torabo_tsuki_lp_right`)
    *   トラックボール (`paw3222` ドライバ)
    *   ステータスLED
    *   USB & BLE 出力
    *   複数の物理レイアウト (`S`, `M`, `L`) が `torabo_tsuki_lp.dtsi` で定義されています。デフォルトは `L Layout` のようです。
*   **コントローラーボード:** `bmp_boost` (`build.yaml` で指定)
*   **ZMKバージョン:** `v0.2` (`config/west.yml` より)

### 重要ファイル

*   `build.yaml`: 左右およびcentral/peripheralの異なる構成のためのビルドマトリクスを定義します。
*   `config/west.yml`: ZMKのバージョンと外部モジュール/ドライバを管理します。
*   `boards/shields/torabo_tsuki_lp/`: このシールドに関するすべてのハードウェア固有ファイルを含むディレクトリ。
    *   `torabo_tsuki_lp.dtsi`: キーマトリックス、トラックボール、その他のハードウェア機能のGPIO割り当てを定義します。
    *   `torabo_tsuki_lp.keymap`: メインのキーマップファイル。現在は `config/keymap.keymap` をインクルードしているだけです。
    *   `*.conf`: 各半身用の設定ファイル。
    *   `torabo_tsuki_lp.zmk.yml`: シールド定義ファイル。

## 開発ログ

*   **2025-08-16:** リポジトリ構造の初期分析。将来の変更を追跡するためにこの `GEMINI.md` ファイルを作成。ファームウェアコードへの変更はまだ行われていません。

## 次のステップ & ToDo

*   `config/keymap.keymap` の実際のキーマップロジックを確認する。
*   異なる物理レイアウト (`S`, `M`, `L`) を分析し、違いを理解する。
*   (ユーザーが次の目標を定義)