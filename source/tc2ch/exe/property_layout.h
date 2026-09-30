#pragma once

// Property pages use resource dialog coordinates; this file only builds grouped navigation.
static HTREEITEM pl_insert(HWND tree, HTREEITEM parent, int value, const WCHAR* text)
{
 TVINSERTSTRUCTW item = { 0 };
 item.hParent = parent; item.hInsertAfter = TVI_LAST;
 item.item.mask = TVIF_TEXT | TVIF_PARAM | TVIF_STATE;
 item.item.pszText = (LPWSTR)text; item.item.lParam = value;
 item.item.state = TVIS_EXPANDED | (value < 0 ? TVIS_BOLD : 0);
 item.item.stateMask = TVIS_EXPANDED | TVIS_BOLD;
 return (HTREEITEM)SendMessageW(tree, TVM_INSERTITEMW, 0, (LPARAM)&item);
}

static void pl_tree(HWND tree, HTREEITEM roots[], HTREEITEM children[])
{
 HTREEITEM group;
 group = pl_insert(tree, TVI_ROOT, -1, b_EnglishMenu ? L"Display" : L"\u8868\u793a");
 children[0] = pl_insert(tree, group, 100, MyStringW(IDS_PROP_COLOR));
 children[1] = pl_insert(tree, group, 101, MyStringW(IDS_PROP_FORMAT));
 children[3] = pl_insert(tree, group, 103, MyStringW(IDS_PROP_GRAPH));
 children[5] = pl_insert(tree, group, 105, MyStringW(IDS_BARMETER));
 children[4] = pl_insert(tree, group, 104, MyStringW(IDS_PROP_ANALOG));
 roots[1] = pl_insert(tree, group, 1, MyStringW(IDS_TOOLTIP));
 group = pl_insert(tree, TVI_ROOT, -1, b_EnglishMenu ? L"Interaction" : L"\u64cd\u4f5c\u30fb\u901a\u77e5");
 roots[6] = pl_insert(tree, group, 6, MyStringW(IDS_PROP_MOUSE));
 roots[9] = pl_insert(tree, group, 9, MyStringW(IDS_PROP_RCLICK_MENU));
 children[7] = pl_insert(tree, group, 107, MyStringW(IDS_PROP_CHIME));
 group = pl_insert(tree, TVI_ROOT, -1, b_EnglishMenu ? L"Integration / system" : L"\u9023\u643a\u30fb\u30b7\u30b9\u30c6\u30e0");
 roots[8] = pl_insert(tree, group, 8, MyStringW(IDS_PROP_CUSTOMVARS));
 roots[2] = pl_insert(tree, group, 2, MyStringW(IDS_PROP_KEYWORDS));
 roots[7] = pl_insert(tree, group, 7, MyStringW(IDS_PROP_ETC));
 roots[5] = pl_insert(tree, group, 5, MyStringW(IDS_PROP_WIN11));
 group = pl_insert(tree, TVI_ROOT, -1, b_EnglishMenu ? L"Information" : L"\u60c5\u5831");
 roots[4] = pl_insert(tree, group, 4, MyStringW(IDS_MISC));
}
