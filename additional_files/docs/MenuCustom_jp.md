# MenuCustom ユーザーガイド

最終更新: 2026-08-30

MenuCustomを使うと、TClockの右クリックメニューをカスタマイズ可能なランチャー／ユーティリティメニューにできます。

## まず試す

MenuCustomは`[MenuCustom]`セクションで設定します。項目番号は`1`から始まり、`ItemCount`で処理する項目数を指定します。

以下はメモ帳を追加する最小例です。

```ini
[MenuCustom]
MenuCustomEnabled=1
ItemCount=1

Item1Mode=shell
Item1Enabled=1
Item1Param=notepad.exe
Item1LabelFormat=メモ帳
```

## できること

- よく使うアプリを起動する
- フォルダー、URL、URIを開く
- 日付・時刻などを含む動的なラベルを表示する
- 区切り線や表示専用の行を追加する
- メニュー内に簡単なカウントダウンタイマーを置く

## 固定項目

以下の項目は常に表示され、MenuCustomの項目には置き換えられません。

- 言語切り替えサブメニュー
- TClockのプロパティ
- TClockフォルダーを開く
- TClockを再起動
- 終了

## 項目の種類

`ItemNMode`で項目の動作を指定します。

- `builtin`: TClock内蔵アクション
- `shell`: Windowsシェルでファイル、フォルダー、URL、URIを開く
- `commandline`: コマンドラインを実行する
- `separator`: 区切り線
- `passive`: クリックしてもメニューを閉じない表示専用行
- `alarm`: カウントダウンタイマー

## 共通の設定

`[MenuCustom]`直下のグローバルキーです。

- `MenuCustomEnabled=0|1`: カスタム右クリックメニューを無効／有効にする
- `ItemCount=0..64`: 処理する項目数
- `LabelFormatUpdateSec=<整数>`: 項目別の更新秒がない場合の既定値

`N`は項目番号です。たとえば`Item4Mode`の`N`は`4`を表します。

- 欠番は使用できます。
- `ItemCount=3`の場合、`Item1`から`Item3`までが処理されます。
- `Item4`以降は無視されます。

## `ItemNMode=builtin`

TClock内蔵アクションを実行します。`ItemNAction`でアクション名を指定します。

- `ItemNEnabled=0|1`
- `ItemNAction=<アクション名>`
- `ItemNLabelFormat=<表示書式>`
- `ItemNLabelUpdateSec=<整数>`
- `ItemNShow=<整数>`: Windowsの`SW_*`表示値

## `ItemNMode=shell`

Windowsシェルを使って、ファイル、フォルダー、URL、URIを開きます。

- `ItemNEnabled=0|1`
- `ItemNParam=<対象>`
- `ItemNArgs=<省略可能な引数>`
- `ItemNWorkDir=<省略可能な作業フォルダー>`
- `ItemNLabelFormat=<表示書式>`
- `ItemNLabelUpdateSec=<整数>`
- `ItemNShow=<SW_*>`

例:

```ini
Item2Mode=shell
Item2Enabled=1
Item2Param=ms-settings:dateandtime
Item2LabelFormat=日付と時刻 yyyy/mm/dd ddd tt hh:nn:ss
Item2LabelUpdateSec=1
```

## `ItemNMode=commandline`

`ItemNParam`をコマンドラインとして実行します。このモードでは`ItemNArgs`は追加されません。

- `ItemNEnabled=0|1`
- `ItemNParam=<コマンドライン>`
- `ItemNWorkDir=<省略可能な作業フォルダー>`
- `ItemNLabelFormat=<表示書式>`
- `ItemNLabelUpdateSec=<整数>`
- `ItemNShow=<SW_*>`

## `ItemNMode=separator`

区切り線を追加します。

```ini
Item5Mode=separator
Item5Enabled=1
```

## `ItemNMode=passive`

クリックしてもメニューを閉じない表示専用行です。日付や時刻などの表示に使用できます。

- `ItemNEnabled=0|1`
- `ItemNLabelFormat=<表示書式>`
- `ItemNLabelUpdateSec=<整数>`

```ini
Item7Mode=passive
Item7Enabled=1
Item7LabelFormat=現在時刻 yyyy/mm/dd ddd tt hh:nn:ss
Item7LabelUpdateSec=1
```

## `ItemNMode=alarm`

メニュー内にカウントダウンタイマーを追加します。

- `ItemNEnabled=0|1`
- `ItemNLabelFormat=<基本ラベル>`
- `ItemNAlarmInitialSec=1..86400`
- `ItemNAlarmUpdateSec=<整数>`
- `ItemNAlarmKeepMenuOpen=0|1`
- `ItemNAlarmNotifyFlags=0..3`
- `ItemNAlarmSoundFile=<WAVファイルのパス>`
- `ItemNAlarmSoundVolume=0..100`
- `ItemNAlarmSoundLoop=0|1`
- `ItemNAlarmLabelIdle=<書式>`
- `ItemNAlarmLabelRun=<書式>`
- `ItemNAlarmLabelPause=<書式>`
- `ItemNAlarmLabelDone=<書式>`
- `ItemNAlarmMessage=<通知メッセージ>`

タイマーのラベルでは、`%REMAIN_HHMMSS%`、`%REMAIN_MMSS%`、`%REMAIN_SEC%`、`%STATE%`を使用できます。

例:

```ini
Item3Mode=alarm
Item3Enabled=1
Item3LabelFormat=%REMAIN_MMSS% タイマー
Item3AlarmInitialSec=300
Item3AlarmLabelIdle=%REMAIN_MMSS% タイマー
Item3AlarmLabelRun=%REMAIN_MMSS% 実行中
Item3AlarmLabelPause=%REMAIN_MMSS% 一時停止
Item3AlarmLabelDone=%REMAIN_MMSS% 完了
Item3AlarmUpdateSec=1
Item3AlarmKeepMenuOpen=1
Item3AlarmNotifyFlags=3
Item3AlarmMessage=タイマー終了
Item3AlarmSoundFile=C:\Windows\Media\notify.wav
Item3AlarmSoundVolume=70
Item3AlarmSoundLoop=0
```

## 内蔵アクション

`ItemNMode=builtin`で`ItemNAction`に指定できる代表的なアクションです。

- `taskmgr`
- `cmd`
- `alarm_clock`
- `pullback`
- `control_panel`
- `power_options`
- `network_connections`
- `settings_home`
- `settings_network`
- `settings_datetime`
- `remove_drive_dynamic`
