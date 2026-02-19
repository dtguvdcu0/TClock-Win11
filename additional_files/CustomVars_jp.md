# CustomVars 解説
最終更新: 2026-02-19  
tclock-win11.iniにカスタムの書式機能を追加しました。
 `[CustomVars]` 以下で設定し、外部ファイルを読み取り表示する機能になります。

- 基本: `CustomNMode=line` : プレーンなテキストの表示
- 上級者向け: `CustomNMode=json` ; JSONからの情報取得と、カスタマイズされた表示

## サンプル


```ini
[CustomVars]
Custom1Path=custom\custom_test.txt
Custom1Mode=line
Custom1FailValue=N/A
Custom1RefreshSec=10
Custom1MaxChars=20
```
`custom\custom_test.txt` の1行目が `CUSTOM1` として表示されます。
`[Format]` などの表示書式で `CUSTOM1` を挿入すると動作します。

## 設定キー一覧

`N` は `1..32`（例: `Custom4Path`）。

- `CustomNPath=<path>`: 読み込み元のファイルのパス
- `CustomNMode=line|json`: 取得するモード
- `CustomNFailValue=<text>`: 失敗時の表示テキスト
- `CustomNRefreshSec=<int>`: 更新間隔（秒）
- `CustomNMaxChars=<int>`: 最大の文字数

共通キー:
- `PreloadOnStartup=<0|1>`
- `RefreshSec=<int>`
- `MaxChars=<int>`

## JSON モード（上級者向け）

JSON モードは、JSONを理解している人向けで、スクリプト等で出力したJSONから情報を表示します。

主なキー:
- `CustomNJsonPath`
- `CustomNJsonType`
- `CustomNJsonDefault`
- `CustomNJsonValue`

例:
```ini
[CustomVars]
Custom4Path=custom\weather_tokyo.json
Custom4Mode=json
Custom4JsonValue=東京 {$.weather.desc} {$.weather.temp_c}℃ {$.weather.humidity_pct}% 風{$.weather.wind_mps}
Custom4JsonType=auto
Custom4FailValue=N/A
Custom4RefreshSec=60
Custom4MaxChars=20
```
