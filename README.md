
[torabo-tsuki LP](https://github.com/sekigon-gonnoc/torabo-tsuki-lp)用のZMKファームウェア

* _centralがついているuf2をトラックボールがついている方に、_peripheralを反対側に書き込んでください
* キーマップはkeymap-editorおよびzmk-studioで編集できます

## Sサイズ専用ファームウェア

このブランチはSサイズの44キー専用です。Lサイズの配列と、Sサイズに存在しない22キーの定義を除外しています。

**旧ファームウェアからの移行前に、[移行手順と注意事項](docs/s-size-only.md#移行時の注意)を確認してください。** Studioの保存済みキーマップは新しいキー番号と互換性がありません。確実に消去するためのsettings_resetはBluetoothのペアリング情報なども消去します。

キー番号の対応表、変更範囲、静的検証方法は[Sサイズ専用化の説明](docs/s-size-only.md)を参照してください。ルートのkey-positions.jpgは旧66キー番号の参考画像で、新しい44キー番号には対応していません。
