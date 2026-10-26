/*
 * 現在の日時をクリップボードへコピーする、プラグインのサンプルです。
 * 本体の設定で有効にすると、ビルトイン操作の選択肢に追加されます。
 * このサンプル専用の設定画面やINIファイルはありません。
 *
 * 処理を変えるときは sample_invoke、操作名を変えるときは sample_describe を
 * 編集します。操作を増やすときは sample_count の件数も合わせて変更します。
 * 一覧に表示する名前と説明文は sample_actions.rc にあります。
 */
#include "../../common/plugin_api.h"
#include <wchar.h>

/* このプラグインが提供する操作の数を返します。 */
static UINT WINAPI sample_count(void) { return 1; }

/*
 * 一覧の index 番目の操作について、IDと表示名を本体へ渡します。
 * このサンプルは1件だけなので、受け付ける index は0です。
 */
static HRESULT WINAPI sample_describe(UINT index, LANGID language, TC_PLUGIN_ACTION* out) {
    if (!out || out->size < sizeof(*out) || index) return E_INVALIDARG;

    /* IDは割り当ての保存に使うため、表示名を変えても変更しません。 */
    wcscpy_s(out->id, _countof(out->id), L"copy-iso-time");
    wcscpy_s(out->label, _countof(out->label), PRIMARYLANGID(language) == LANG_JAPANESE ?
        L"\u73fe\u5728\u65e5\u6642\u3092ISO\u5f62\u5f0f\u3067\u30b3\u30d4\u30fc (DLL)" : L"Copy ISO timestamp (DLL)");
    return S_OK;
}

/*
 * 選ばれた操作を実行します。現在の日時を文字列にして、本体のコピー機能へ渡します。
 * 本体の画面操作を待たせないよう、ここでは時間のかかる処理を避けます。
 */
static HRESULT WINAPI sample_invoke(LPCWSTR action, const TC_ACTION_CONTEXT* context) {
    SYSTEMTIME now;
    WCHAR text[32];

    if (!action || wcscmp(action, L"copy-iso-time") || !context || context->size < sizeof(*context) || !context->copy_text) return E_INVALIDARG;

    /* PCのローカル日時を使います。出力例: 2026-10-20T09:30:00 */
    GetLocalTime(&now);
    swprintf_s(text, _countof(text), L"%04u-%02u-%02uT%02u:%02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);

    /* 本体はこの呼び出し中に文字をコピーするので、関数内のtextを渡せます。 */
    return context->copy_text(context->owner, text);
}

/*
 * プラグインの識別情報です。別のプラグインを作るときは sample.actions を
 * 独自のIDへ変更し、.rcの TC_PLUGIN_HEADER_ID も同じIDに揃えます。
 */
static const TC_PLUGIN_INFO sample_info = { sizeof(TC_PLUGIN_INFO), L"sample.actions", L"Sample builtin actions" };

/*
 * 本体はこの関数表で操作の件数と内容を取得し、選ばれた操作を実行します。
 * 本体が使い続けられるよう、関数表は static const で保持します。
 */
static const TC_ACTION_PROVIDER sample_actions = { sizeof(TC_ACTION_PROVIDER), sample_count, sample_describe, sample_invoke };

/*
 * 本体が最初に呼ぶ公開関数です。求められた識別情報または関数表を返します。
 * 共通定義に対応する要求だけを受け付け、それ以外は E_NOINTERFACE を返します。
 * この入口の名前は sample_actions.def と揃えたままにします。
 */
HRESULT WINAPI TClockGetInterface(UINT abi, LPCWSTR id, const void** out) {
    if (!out) return E_POINTER;
    *out = NULL;
    if (abi != TC_PLUGIN_ABI || !id) return E_NOINTERFACE;

    if (wcscmp(id, TC_PLUGIN_INFO_ID) == 0) *out = &sample_info;
    else if (wcscmp(id, TC_PLUGIN_ACTIONS_ID) == 0) *out = &sample_actions;
    /* このサンプルは個別設定を持たないため、設定用の機能は返しません。 */
    else return E_NOINTERFACE;
    return S_OK;
}
