# TCycle 仕様書（実装準拠）

この文書は、`source/tcyc` の**現在実装**に基づく仕様です。  
実装変更時は本書も更新してください。

## 1. 概要
- 実行ファイル: `TCycle.exe`
- 役割:
  - INI (`TCycle.ini`) を読み込み、タスクを定期評価して起動する
  - ホットキー起動を受け付ける
  - 非起動監視（watchdog）を行う
  - 実行状態を `tcycle.state.ini` に保存する
- 常駐:
  - `--settings` 以外は常駐ループで動作
  - 同時多重起動はミューテックスで抑止（2重起動時は即終了）

## 2. コマンドライン引数
`main.cpp` 実装で受け付ける引数は以下のみです。

- `--validate-config`
  - 設定読込とゲート状態を確認して終了（常駐しない）
- `--settings`
  - 設定ウィンドウのみ起動して終了
- `--lang=ja` / `--lang=en`
- `--lang ja` / `--lang en`

未定義引数は無視されます。

## 3. 設定ファイル配置
- 実行時に参照する INI:
  - `exeDir\\TCycle.ini`
- 初回起動時:
  - `TCycle.ini` が無ければ自動生成
- 相対パス解決基準:
  - 基本は `exeDir` 基準

## 4. グローバル INI 仕様
### 4.1 `[TCycle]`
- `PollSec` (既定: `1`, 範囲: `1..60`)
- `GraceSec` (既定: `60`, 実行時補正: `<=0` は `60`, `>300` は `300`)
- `LogLevel` (既定: `0`, 範囲: `0..3`)
- `LogFile` (既定: `tcycle.log`, 相対は `exeDir` 基準)
- `StateEnabled` (既定: `0`)
- `StateFile` (既定: `tcycle.state.ini`, 相対は `exeDir` 基準)

### 4.2 `[Debug]`
- `ForceCmdlineReadFail` (既定: `0`)
  - テスト用途。単一起動判定時のコマンドライン読取失敗を擬似化

### 4.3 `[Integration]`
- `TClockIniPath` (既定: `..\\tclock-win11.ini`, 相対は `exeDir` 基準)
  - ここにある `[TCycle] Enabled=0` のとき、TCycle全体を実行抑止

## 5. タスク INI 仕様（`[Task.N]`, N=1..128）
### 5.1 実行時に読み込む主キー
- `Enabled`: タスク有効/無効 (`0/1`)
- `Name`: タスク表示名（文字列）
- `TriggerType`: 単一トリガー互換キー（`interval|datetime_interval_limited|weekly_time|startup|hotkey_only|non_running`）
- `TriggerTypes`: 複数トリガー指定（カンマ区切り）
- `IntervalSec`: intervalトリガー間隔秒（整数）
- `ActionMode`: 実行モード（`program|command|shell`）
- `ActionPath`: 実行対象（programは実行ファイル、command/shellはコマンド本体）
- `ActionArgs`: 追加引数（文字列）
- `ActionCwd`: 作業フォルダ（文字列、相対可）
- `SingleInstance`: 二重起動抑止 (`0/1`)
- `WatchdogEnabled`: 非起動監視有効 (`0/1`)
- `WatchdogRetrySec`: 監視再試行間隔秒（`10..3600` にクランプ）
- `WatchdogMaxRetry`: 監視再試行上限（`<0` で無制限）
- `WatchdogRequireArgsMatch`: 単一起動判定時の引数一致要求 (`0/1`)
- `StartDateTime`: datetime_interval_limited の開始日時（`YYYY-MM-DD HH:MM[:SS]`）
- `RepeatEverySec`: datetime_interval_limited の繰返し秒（`0..86400`）
- `RepeatCount`: datetime_interval_limited の上限回数（`0..1000000`）
- `DateEnabled`: 週次条件での日付固定条件有効 (`0/1`)
- `Date`: 日付固定値（`YYYY-MM-DD`）
- `WeekdayEnabled`: 週次条件での曜日条件有効 (`0/1`)
- `EveryDay`: 曜日を毎日扱いにする (`0/1`)
- `Weekdays`: 曜日マスク文字列（例: `mon,wed,fri` / `everyday`）
- `Weekday`: 単一曜日互換キー（`sun..sat`）
- `TimeEnabled`: 週次条件での時刻条件有効 (`0/1`)
- `TimeOfDay`: 実行時刻（`HH:MM[:SS]`）
- `Hotkey`: ホットキー文字列（例: `Ctrl+Alt+9`）

### 5.2 補正・フォールバック規則
- `ActionMode` 不正値は `program`
- `TriggerTypes` が空なら `TriggerType` を使用
- `TriggerTypes` が有効かつ `TriggerType` 不明なら、マスクから先頭トリガーを採用
- `WatchdogEnabled=1` の場合、`non_running` ビットを強制付与
- `EveryDay=1` または `Weekdays=everyday` の場合、曜日個別指定は無効化
- セクションスキップ条件:
  - `ActionPath` 空、`Name` 空、`TriggerType` 不明、`Enabled=0` のとき読み飛ばし

### 5.3 パス解決規則
- `ActionPath`:
  - `ActionMode=program` のときのみ相対パスを `exeDir` 基準で解決
  - `command/shell` ではコマンド文字列として扱い、`exeDir` 前置きしない
- `ActionCwd`:
  - 非空なら全モードで `exeDir` 基準相対解決

## 6. トリガー仕様
`TriggerType` / `TriggerTypes` の対応:
- `interval`
- `datetime_interval_limited`
- `weekly_time`
- `startup`
- `hotkey_only`
- `non_running`

### 6.1 `startup`
- 状態ファイルの `StartupDone` を見て、未実行なら1回だけ起動

### 6.2 `interval`
- `IntervalSec` 周期で評価
- `GraceSec` を使って取りこぼし許容ウィンドウ判定

### 6.3 `datetime_interval_limited`
- `StartDateTime` を起点に `RepeatEverySec` 間隔で評価
- `RepeatCount` 上限まで起動

### 6.4 `weekly_time`
- `DateEnabled/Date`、`WeekdayEnabled/Weekdays|Weekday|EveryDay`、`TimeEnabled/TimeOfDay` の組み合わせで判定
- 日次キャッシュを使って当日対象時刻を評価

### 6.5 `hotkey_only`
- `Hotkey` が有効ならグローバルホットキー登録
- 受理キー:
  - 修飾: `Ctrl`, `Shift`, `Alt`, `Win`
  - 本体: `A-Z`, `0-9`, `F1-F24`, `PrintScreen`, `Insert`, `Delete`, `Home`, `End`, `PgUp`, `PgDn`, `Space`, `Enter`

### 6.6 `non_running`（watchdog）
- `WatchdogEnabled=1` かつ `non_running` トリガー時に有効
- 監視対象プロセスが不在なら `WatchdogRetrySec` 後に再試行
- `WatchdogMaxRetry` 到達で停止（`<0` は無制限）

## 7. 実行モード仕様（`ActionMode`）
### 7.1 `program`
- `CreateProcessW(app=ActionPath, cmdLine="ActionPath" + ActionArgs)`

### 7.2 `command`
- `app=cmd.exe`
- `cmdLine=cmd.exe /C <ActionPath + ActionArgs>`

### 7.3 `shell`
- `app=powershell.exe`
- `cmdLine=powershell.exe -NoProfile -Command <ActionPath + ActionArgs>`

### 7.4 作業フォルダ
- `ActionCwd` 非空時のみ `lpCurrentDirectory` に設定

## 8. 単一起動判定（`SingleInstance`）
- `SingleInstance=1` のとき、起動前に既存プロセス確認
- 注意:
  - 判定は `ActionMode=program` のみ実施
  - `command/shell` は既存実行判定対象外
- `WatchdogRequireArgsMatch=1` かつ `ActionArgs` 非空時は、パス一致に加えて引数一致まで確認

## 9. ゲート制御（TClock連携）
- `TClockIniPath` の `[TCycle] Enabled` を参照
- `Enabled=0` の場合:
  - due/watchdog/hotkey いずれも起動抑止
  - ループ自体は継続（設定再読込待ち）

## 10. 状態ファイル仕様（`StateFile`）
- `StateEnabled=1` の場合のみ読み書きします（既定は `0`）。
- セクション `[Runtime]`
  - `BootCount`
  - `LastEvalUnix`
  - `LastSaveUnix`
- セクション `[Task.N]`
  - `FiredCount`
  - `LastCheckUnix`
  - `LastFireUnix`
  - `StartupDone`
  - `WatchdogRetryCount`
  - `WatchdogNextRetryUnix`
- 保存タイミング:
  - 起動直後
  - 以後 10 秒ごと

## 11. ログ仕様
- `LogFile` に UTF-8 (BOM付き) で出力
- ローテーション:
  - 1MB 超でローテート、最大 `.<1..5>` 世代
- レベル:
  - `0`: エラー
  - `1`: 通常イベント
  - `2`: 詳細（Heartbeat含む）
  - `3`: 予約（現在実装で実質未使用）

## 12. 設定再読込
- `Local\\TCycle_Reload_Config_Event` 受信で即時再読込
- 設定再読込時:
  - スケジューラキャッシュ再構築
  - ホットキー構成差分があれば再登録

## 13. 注意点（現行実装）
- `StartupDelaySec` は現行 `source/tcyc` 実装では未使用（INIにあっても無視）
- `command/shell` は `ActionPath` を「実行ファイルパス」ではなく「コマンド文字列」として扱う
