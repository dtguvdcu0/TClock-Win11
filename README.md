# TClock-Win11（β）

Tclock-Win10からフォークしたプロジェクトです。

■ Windows11 25H2でタスクバーを他のツールで変更していない環境向け（現状は人柱向け）

## 2026/02/18 主な変更点（v0.1.6.0）

- TClock右クリックメニューのカスタマイズ機能追加
- `MenuCustom` ドキュメントを参照
    - `additional_files/MenuCustom.md`
    - `additional_files/MenuCustom_jp.md`

## 2026/02/16 主な変更点（v0.1.5.0）

- Windows11 25H2のスタートメニュー左寄せ、純正時計の非表示化（レジストリ変更で対応）
- 元ソースにあったTClock書式互換エリアの安定化調整
- TClock表示エリア背景色をタスクバーから自動採取する動作を追加（プロパティのWindows11対応）
- iniのUTF-8化を段階的に改善
- ソースをShift-JISからUTF-8に変更
- A APIからW APIへ一部移行

## その他

- ドキュメント整理
- 不具合つぶし
- Tclock左クリック動作の自由カスタム化
- 特定テキストファイル読み込み書式の追加
- マルチディスプレイスクリーンショットツール
- タスクスケジューラーツール
- クリップボードツール
- アナログ時計表示

などを予定

## License（ライセンス）

TClockおよびTClock Light、TClock-Win10はGNU GPLで配布されています。  
本ソフトウェアもGNU GPLに従って再配布または改変できます。