# MenuCustom 仕様

最終更新: 2026-02-18  
TClock-Win11（`[MenuCustom]`）を利用した右クリックメニューカスタム化の解説

## できること

tclock-win11.iniの`[MenuCustom]`以下の設定で右クリックメニューを設定できます。

カスタマイズ可能なプログラムランチャーのような使い方を想定しています。

注:以下は固定項目になります。

- 言語切り替えメニュー
- TClockのプロパティ
- TClockフォルダを開く
- TClockの再起動
- TClockの終了

## グローバル設定キーについて

- `[MenuCustom]`直下に配置されます。
- `MenuCustomEnabled=<int>`
- `ItemCount=<int>`
- `LabelFormatUpdateSec=<int>`

`MenuCustomEnabled`が1で右クリックのカスタムが動作します。

`ItemCount`は各メニューでItem識別番号として設定される数値の上限値です。上限値以上の数値が名前にあるメニューは読み込まれません。(後述)

`LabelFormatUpdateSec`は、TClock書式を名前に利用したアイテムで設定されます。個別に`ItemNLabelUpdateSec`が未指定の場合にこの値で更新間隔（秒）が設定されます。

## Item番号のルールについて

このドキュメント内例示の`ItemN****`の`N`に当たる箇所には数値を入れて管理してください。`Item1Type`, `Item2Label`等アイテム設定値の個別識別番号になります。一致した番号の項目郡で設定が有効になります。

- 連番出なくても良いです。欠番があったらスキップされます。

例:

- `Item1`と`Item3`だけ定義しても有効
- ただし、グローバル設定での`ItemCount=3`のとき`Item4`以降は無視されます

## 個別メニューの設定

### ItemTypeについて

- `ItemNType=command` 各種機能の実行
- `ItemNType=separator` 区切り線の追加
- `ItemNType=passive` テキストのみの表示
- `ItemNType=alarm` アラーム機能

### command の項目について

`ItemNType`が`command`時に使用するキーの解説

基本キー:

- `ItemNType=command`
- `ItemNEnabled=0|1`
- `ItemNLabel=<menu_text>`
- `ItemNExecType=builtin|shell|commandline`
- `ItemNParam=<execution parameter>`

任意キー:

- `ItemNAction=<action_name>`
- `ItemNLabelFormat=<format_text>`
- `ItemNLabelUpdateSec=<int>`
- `ItemNArgs=<optional args for shell target>`
- `ItemNWorkDir=<optional working directory>`
- `ItemNShow=<SW_*>`（既定 `1` = `SW_SHOWNORMAL`）

### command キーの各項目一覧と解説

- `ItemNEnabled`
    - `1`: 表示
    - `0`: 非表示
- `ItemNExecType`
    - `builtin`: TClock内部コマンド（設定には有効な builtin 名が必要、後述）
    - `shell`: ShellExecute 形式で起動（`ItemNParam`で設定）
      - PATHが通ったコマンド名だけでなく、フルパス実行ファイル、URI（`ms-settings:`）、URLも指定可能
    - `commandline`: `cmd.exe`経由で実行（`ItemNParam`で設定、通常 `/c` から開始）
- `ItemNAction`
    - `builtin`で必須
    - `shell` / `commandline`では任意（メタ情報）
- `ItemNLabel`
    - ラベル表示名(プレーンな表示テキスト)
    - TClockの書式を使わない場合はこちらで設定
- `ItemNLabelFormat`
    - TClockフォーマットで動的にラベル名を表示する場合の設定
    - 値があれば`ItemNLabel`より優先
- `ItemNLabelUpdateSec`
    - 動的ラベルの更新間隔（右クリックメニュー表示中のみ動作）
    - `0`: 起動時のみ更新で表示は固定
    - `1+`: 指定秒間隔で更新
- `ItemNShow`
    - 起動時のウィンドウ状態の指定
    - よく使う値:
        - `0` = `SW_HIDE` 非表示起動
        - `1` = `SW_SHOWNORMAL` 通常起動
        - `3` = `SW_SHOWMAXIMIZED` 最大化して起動
        - `7` = `SW_SHOWMINNOACTIVE` 最小化して起動

- メモ帳を開くサンプル例:

```ini
Item1Type=command
Item1Enabled=1
Item1Action=launch_notepad
Item1Label=Notepad
Item1ExecType=shell
Item1Param=notepad.exe
```

- TClock書式ラベルを利用したサンプル例:

```ini
Item2Type=command
Item2Enabled=1
Item2Action=clock_label
Item2Label=Clock
Item2LabelFormat=日付と時計 yyyy/mm/dd ddd tt hh:nn:ss
Item2LabelUpdateSec=1
Item2ExecType=shell
Item2Param=ms-settings:dateandtime
```

- Explorer 再起動のサンプル例:

```ini
Item24Type=command
Item24Enabled=1
Item24Action=Restart Explorer
Item24Label=Restart Explorer
Item24ExecType=commandline
Item24Param=/c taskkill /f /im explorer.exe & start explorer.exe
Item24Show=0
```

- コマンドプロンプト（管理者権限）のサンプル例:

```ini
Item4Type=command
Item4Enabled=1
Item4Action=cmd_admin
Item4Label=Commandline (Admin)
Item4ExecType=commandline
Item4Param=/c powershell -NoProfile -Command "Start-Process cmd.exe -Verb RunAs -ArgumentList '/k cd /d C:\'"
```

- フォルダを開く

```ini
Item11Type=command
Item11Enabled=1
Item11Label=Open TClock Folder
Item11ExecType=shell
Item11Param=C:\TClock-Win11
```

- URL を開く

```ini
Item13Type=command
Item13Enabled=1
Item13Label=Open GitHub
Item13ExecType=shell
Item13Param=https://github.com/
```

### ExecType builtinで利用できるにActionについて(ItemNAction)

- `taskmgr`: タスクマネージャーを開く
- `cmd`: コマンドプロンプトを開く
- `alarm_clock`: Windows のアラーム/時計を開く
- `pullback`: カスタム項目を折りたたむ/戻す
- `control_panel`: コントロールパネルを開く
- `power_options`: 電源オプションを開く
- `network_connections`: ネットワーク接続を開く
- `settings_home`: Windows 設定ホームを開く
- `settings_network`: ネットワーク設定を開く
- `settings_datetime`: 日時設定を開く
- `remove_drive_dynamic`: ドライブ取り外し項目（動的）

### separator の項目

`ItemNType=separator`で区切り線を設けることができます。

注:

- 先頭/末尾/連続セパレータは自動で正規化されます。
- 区切り線のサンプル:

```ini
Item5Type=separator
Item5Enabled=1
```

### passive の項目

`ItemNType=passive` テキスト文字やTClockの書式を表示のみで利用できます。該当箇所をクリックしても右クリックメニューは非表示になりません。

```ini
Item7Type=passive
Item7Enabled=1
Item7Label=Now
Item7LabelFormat=yyyy/mm/dd ddd tt hh:nn:ss
Item7LabelUpdateSec=1
```

## ItemNType=alarm 

`ItemNType=alarm`でアラームが設定できます。

タイマー設定の値をもとにカウントダウンを行い、設定時間後に音声を鳴らしたり、ダイアログでメッセージ表示ができます。最初の左クリックで開始、次の左クリックで一時停止と再開、右クリックで初期化する動作になります。

### alarm の状態についての解説

- `idle`: 初期秒で待機
- `running`: カウントダウン中
- `paused`: 一時停止中
- `finished`: 0到達

### alarm のクリック動作についての解説

- 左クリック遷移:
    - `idle -> running`
    - `running -> paused`
    - `paused -> running`
    - `finished -> running`（再スタート）
- 右クリック: 常に`idle`（初期秒）へリセット

### alarm キーについて

必須:

- `ItemNType=alarm`
- `ItemNEnabled=0|1`
- `ItemNAlarmInitialSec=<int>`

推奨:

- `ItemNLabel=<text>`
- `ItemNAlarmLabelIdle=<text>`
- `ItemNAlarmLabelRun=<text>`
- `ItemNAlarmLabelPause=<text>`
- `ItemNAlarmLabelDone=<text>`

任意:

- `ItemNAlarmUpdateSec=<int>`
- `ItemNAlarmKeepMenuOpen=0|1`
- `ItemNAlarmNotifyFlags=<0..3>`
- `ItemNAlarmMessage=<text>`
- `ItemNAlarmSoundFile=<path>`
- `ItemNAlarmSoundVolume=<0..100>`
- `ItemNAlarmSoundLoop=0|1`

### alarm のキー解説

`ItemNAlarmNotifyFlags`について`

- `0`: 通知なし
- `1`: メッセージのみ
- `2`: サウンドのみ
- `3`: メッセージ + サウンド

`ItemNAlarmKeepMenuOpen`について

- `0`: クリックしたらメニュー表示が消える
- `1`: クリックしてもメニュー表示を維持する

### alarm のLabelで利用できる専用書式

- `%REMAIN_SEC%` : 残り 秒
- `%REMAIN_MMSS%` : 残り 分:秒
- `%REMAIN_HHMMSS%` : 残り 時:分:秒
- `%STATE%`（`idle`, `running`, `paused`, `finished`） : 状態の表示

- サンプル
```ini
Item3Type=alarm
Item3Enabled=1
Item3Label=Timer
Item3AlarmInitialSec=300
Item3AlarmLabelIdle=%REMAIN_MMSS% タイマー
Item3AlarmLabelRun=%REMAIN_MMSS% 実行中
Item3AlarmLabelPause=%REMAIN_MMSS% 一時停止
Item3AlarmLabelDone=%REMAIN_MMSS% 完了
Item3AlarmUpdateSec=1
Item3AlarmKeepMenuOpen=1
Item3AlarmNotifyFlags=2
Item3AlarmMessage=Timer finished
Item3AlarmSoundFile=C:\Windows\Media\notify.wav
Item3AlarmSoundVolume=70
Item3AlarmSoundLoop=0
```
