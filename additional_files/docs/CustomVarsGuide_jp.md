# CustomVars ユーザーガイド

最終更新: 2026-10-10

CustomVarsは外部ファイルのテキストまたはJSONを読み込み、TClockの書式で`CUSTOM1`から`CUSTOM32`として利用できるようにします。

## まず試す

1. `tclock-win11.ini`と同じ場所に`customvars`フォルダーを作り、`custom1_test.txt`と`custom2_static_sample.json`を置きます。どちらも静的サンプルで、スクリプトは不要です。
2. `[CustomVars]`に項目を追加します。

```ini
[CustomVars]
Custom1Mode=line
Custom1Path=customvars\custom1_test.txt
Custom1FailValue=N/A
Custom1RefreshSec=10
Custom1MaxChars=20
```

3. 時計の書式に`CUSTOM1`を追加します。

```ini
[Format]
Custom=1
CustomFormat=yyyy/mm/dd(ddd) hh:nn:ss CUSTOM1
```

時計には`CUSTOM1`の代わりに、`custom1_test.txt`の1行目（`カスタム書式用テキスト`）が表示されます。

## 設定の基本

変数番号は`1`から`32`までです。番号は各キー名の一部になります。

```ini
CustomNMode=...
CustomNPath=...
```

`N`を変数番号に置き換えます。たとえば4番目の変数なら`Custom4Mode`です。

相対パスは`tclock-win11.ini`があるフォルダーを基準に解決されます。絶対パスも使用できます。

入力ファイルは空でない必要があり、サイズは64 KiB（65,536バイト）以下でなければなりません。UTF-8、UTF-16、従来のShift-JIS形式を読み込めます。

## 読み込みモード

### `line`モード

`CustomNMode=line`はテキストファイルの先頭行だけを読み込みます。スクリプトが書き出す状態表示などに適しています。

```ini
[CustomVars]
Custom1Mode=line
Custom1Path=customvars\custom1_test.txt
Custom1RefreshSec=10
Custom1MaxChars=20
Custom1FailValue=offline
Custom1Whitespace=trim_edges
```

### `json`モード

`CustomNMode=json`はJSONファイルから値を読み込みます。`{...}`の中にJSONパスを記述して、表示テキストを組み立てます。

```ini
[CustomVars]
Custom2Mode=json
Custom2Path=customvars\custom2_static_sample.json
Custom2JsonValue={$.display.headline} {$.status.value} {$.status.count}
Custom2RefreshSec=60
Custom2MaxChars=48
Custom2FailValue=N/A
```

JSONパスはドット区切りのオブジェクト名と、0始まりの配列番号を使用します。例: `$.display.headline`、`$.items.0.name`

JSON用の追加設定:

- `CustomNJsonStringify=0|1`: JSONオブジェクトや配列をJSONテキストとして出力する
- `CustomNJsonNullAsEmpty=0|1`: JSONの`null`を空文字として出力する。`0`の場合は失敗時の値を使用する

テンプレート内で波括弧をそのまま表示するには`{{`と`}}`を使います。JSONパスが存在しない場合や値の型が合わない場合は、`CustomNFailValue`が使われます。

JSONモードの更新間隔を5秒未満に設定した場合は、5秒に引き上げられます。

## 項目別キー

各`CustomN`項目で使用できるキーです。

| キー | 意味 |
| --- | --- |
| `CustomNPath` | 入力ファイルのパス。値を読み込むには必須 |
| `CustomNMode` | `line`または`json`。既定値は`line` |
| `CustomNRefreshSec` | ファイルの更新間隔。1～86400秒 |
| `CustomNMaxChars` | 出力する最大文字数。1～4096文字 |
| `CustomNFailValue` | 読み込みまたは抽出に失敗した場合の表示文字列 |
| `CustomNWhitespace` | `trim_edges`または`keep` |

`trim_edges`は先頭と末尾の半角スペース、タブ、全角スペースを取り除きます。`keep`は空白を保持します。

## グローバル既定値

以下のキーは`[CustomVars]`直下に置き、項目別キーがない場合の既定値として使用します。

```ini
[CustomVars]
RefreshSec=60
MaxChars=20
FailValue=N/A
Whitespace=trim_edges
PreloadOnStartup=1
```

- `RefreshSec`: ファイル更新間隔の既定値。1～86400秒
- `MaxChars`: 最大出力文字数の既定値。1～4096文字
- `FailValue`: 失敗時の表示文字列の既定値
- `Whitespace`: 空白処理の既定値。`trim_edges`または`keep`
- `PreloadOnStartup=0|1`: 通常の最初の更新前に、起動時に設定済みファイルを読み込む

項目別の値がある場合は、グローバル値より優先されます。

## 外部スクリプトによる更新

CustomVarsは、入力ファイルを更新するコマンドを実行できます。天気、為替、その他の外部データの取得に利用できます。

更新用スクリプトは同梱していません。データを自動取得する場合は、別途スクリプトを用意してください。

スクリプト関連キー:

- `CustomNExecEnable=0|1`: スクリプト実行を有効／無効にする
- `CustomNExecType=command|shell`: `command`は`cmd.exe`、`shell`はPowerShell経由で実行する
- `CustomNExecStart=startup|interval|both|time`: 実行タイミング
- `CustomNExecIntervalSec`: `interval`または`both`での間隔。1～86400秒
- `CustomNExecTime=HH:MM`: `time`で使用する毎日の実行時刻
- `CustomNExecCommand`: 実行するコマンド文字列
- `CustomNExecCwd`: 省略可能な作業フォルダー。相対パスはINIフォルダー基準

`both`を指定すると、起動時に1回実行した後、指定間隔で実行します。スクリプトは非表示で実行され、プロセスの終了を待たずに処理が続行されます。

## 完成したJSON例

```ini
[CustomVars]
PreloadOnStartup=1
RefreshSec=60
MaxChars=48
FailValue=N/A
Whitespace=trim_edges

Custom2Mode=json
Custom2Path=customvars\custom2_static_sample.json
Custom2JsonValue={$.display.headline} {$.status.value} {$.status.count}
Custom2RefreshSec=60
Custom2MaxChars=48
Custom2FailValue=N/A
Custom2JsonStringify=0
Custom2JsonNullAsEmpty=0
Custom2ExecEnable=0
```

## トラブルシューティング

- `CUSTOMn`が失敗時の値になる場合は、パス、ファイルの存在、ファイルサイズ、JSONパスを確認してください。
- テキストの一部しか表示されない場合は、`CustomNMaxChars`を確認してください。
- 空白が消える場合は、`CustomNWhitespace=keep`を設定してください。
- JSONが表示されない場合は、`CustomNJsonValue`に正しい`{JSON path}`式が入っているか確認してください。
- スクリプトでファイルが更新されない場合は、`CustomNExecEnable`、`CustomNExecCommand`、`CustomNExecType`、`CustomNExecCwd`を確認してください。
