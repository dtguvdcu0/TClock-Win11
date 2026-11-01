#ifndef TCLOCK_PLUGIN_TRANSPORT_H
#define TCLOCK_PLUGIN_TRANSPORT_H
#include <windows.h>

/* WM_COPYDATA is only a bounded byte transport, not authentication or cancellation. */
#define TPI_MAX_BYTES 65536u
typedef enum TPI_SEND_RESULT {
    TPI_NOT_SENT = 0,
    TPI_REPLIED = 1,
    TPI_UNKNOWN = 2
} TPI_SEND_RESULT;
typedef enum TPI_RECEIVE_RESULT {
    TPI_NOT_MINE = 0,
    TPI_REJECTED = 1,
    TPI_ACCEPTED = 2
} TPI_RECEIVE_RESULT;

/* The caller owns a pointer-free payload for the duration of the send. */
static __inline TPI_SEND_RESULT tpi_send(HWND target, HWND sender, ULONG_PTR tag,
    const void* payload, DWORD bytes, UINT timeout_ms, DWORD_PTR* reply) {
    COPYDATASTRUCT data;
    if (!reply) return TPI_NOT_SENT;
    *reply = 0;
    if (!target || !payload || !bytes || bytes > TPI_MAX_BYTES || !timeout_ms) return TPI_NOT_SENT;
    data.dwData = tag;data.cbData = bytes;data.lpData = (void*)payload;
    if (!SendMessageTimeoutW(target, WM_COPYDATA, (WPARAM)sender, (LPARAM)&data,
            SMTO_ABORTIFHUNG | SMTO_BLOCK, timeout_ms, reply)) {
        *reply = 0;return TPI_UNKNOWN;
    }
    return TPI_REPLIED;
}

/* The received bytes are borrowed only until the window procedure returns. */
static __inline TPI_RECEIVE_RESULT tpi_receive(UINT message, LPARAM parameter, ULONG_PTR tag,
    DWORD expected_bytes, const void** payload) {
    const COPYDATASTRUCT* data;
    if (payload) *payload = NULL;
    if (message != WM_COPYDATA || !parameter) return TPI_NOT_MINE;
    data = (const COPYDATASTRUCT*)parameter;
    if (data->dwData != tag) return TPI_NOT_MINE;
    if (!payload || !expected_bytes || expected_bytes > TPI_MAX_BYTES ||
        data->cbData != expected_bytes || !data->lpData) return TPI_REJECTED;
    *payload = data->lpData;return TPI_ACCEPTED;
}
#endif
