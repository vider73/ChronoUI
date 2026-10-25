#!/usr/bin/env python3
# =============================================================================
# make_icon_font.py — build assets/fonts/ChronoUIIcons.ttf for the Linux port.
#
# The widgets name their icons by Segoe MDL2 Assets code point (L"\xE80F" =
# home). Segoe MDL2 ships with Windows and is not redistributable, so on Linux
# the same code points are served by a small font cut from Microsoft's Fluent
# UI System Icons (MIT licence): each MDL2 code point below gets the Fluent
# glyph of the same meaning. No code changes, no runtime remapping.
#
#     curl -LO https://raw.githubusercontent.com/microsoft/fluentui-system-icons/main/fonts/FluentSystemIcons-Regular.ttf
#     curl -LO https://raw.githubusercontent.com/microsoft/fluentui-system-icons/main/fonts/FluentSystemIcons-Regular.json
#     curl -LO https://raw.githubusercontent.com/microsoft/fluentui-system-icons/main/fonts/FluentSystemIcons-Filled.ttf
#     curl -LO https://raw.githubusercontent.com/microsoft/fluentui-system-icons/main/fonts/FluentSystemIcons-Filled.json
#     python3 tools/make_icon_font.py <folder with those four files>
#
# Adding an icon to a widget = a row here (MDL2 name from the Segoe MDL2
# Assets table on learn.microsoft.com) and a rerun. Needs fontTools.
# =============================================================================

import json, os, sys
from fontTools.ttLib import TTFont
from fontTools import subset

SIZE = 20   # Fluent draws each icon per size; 20 px is closest to MDL2 at UI sizes

# MDL2 code point: (MDL2 name, Fluent name, style)
ICONS = {
    0xE701: ("Wifi",               "wifi_1",               "regular"),
    0xE708: ("QuietHours",         "weather_moon",         "regular"),
    0xE70F: ("Edit",               "edit",                 "regular"),
    0xE710: ("Add",                "add",                  "regular"),
    0xE711: ("Cancel",             "dismiss",              "regular"),
    0xE712: ("More",               "more_horizontal",      "regular"),
    0xE713: ("Setting",            "settings",             "regular"),
    0xE715: ("Mail",               "mail",                 "regular"),
    0xE716: ("People",             "people",               "regular"),
    0xE721: ("Search",             "search",               "regular"),
    0xE724: ("Send",               "send",                 "regular"),
    0xE725: ("SendFill",           "send",                 "filled"),
    0xE72A: ("Forward",            "arrow_right",          "regular"),
    0xE72B: ("Back",               "arrow_left",           "regular"),
    0xE72D: ("Share",              "share",                "regular"),
    0xE72E: ("Lock",               "lock_closed",          "regular"),
    0xE734: ("FavoriteStar",       "star",                 "regular"),
    0xE73A: ("CheckboxComposite",  "checkbox_checked",     "regular"),
    0xE73E: ("CheckMark",          "checkmark",            "regular"),
    0xE749: ("Print",              "print",                "regular"),
    0xE74D: ("Delete",             "delete",               "regular"),
    0xE74E: ("Save",               "save",                 "regular"),
    0xE767: ("Volume",             "speaker_2",            "regular"),
    0xE774: ("Globe",              "globe",                "regular"),
    0xE777: ("UpdateRestore",      "arrow_clockwise",      "regular"),
    0xE77B: ("Contact",            "person",               "regular"),
    0xE77F: ("Paste",              "clipboard_paste",      "regular"),
    0xE787: ("Calendar",           "calendar_ltr",         "regular"),
    0xE790: ("Color",              "color",                "regular"),
    0xE792: ("SaveAs",             "save_edit",            "regular"),
    0xE7A6: ("Redo",               "arrow_redo",           "regular"),
    0xE7A7: ("Undo",               "arrow_undo",           "regular"),
    0xE7B3: ("RedEye",             "eye",                  "regular"),
    0xE7B8: ("Package",            "box",                  "regular"),
    0xE7BA: ("Warning",            "warning",              "regular"),
    0xE7C3: ("Page",               "document",             "regular"),
    0xE7EC: ("DrivingMode",        "vehicle_car",          "regular"),
    0xE7F4: ("TVMonitor",          "desktop",              "regular"),
    0xE80F: ("Home",               "home",                 "regular"),
    0xE823: ("Recent",             "clock",                "regular"),
    0xE825: ("Bank",               "building_bank",        "regular"),
    0xE838: ("FolderOpen",         "folder_open",          "regular"),
    0xE895: ("Sync",               "arrow_sync",           "regular"),
    0xE8A1: ("PreviewLink",        "document_search",      "regular"),
    0xE8A5: ("Document",           "document",             "regular"),
    0xE8AC: ("Rename",             "rename",               "regular"),
    0xE8B3: ("SelectAll",          "select_all_on",        "regular"),
    0xE8B5: ("Import",             "arrow_import",         "regular"),
    0xE8B7: ("Folder",             "folder",               "regular"),
    0xE8BD: ("Message",            "chat",                 "regular"),
    0xE8C6: ("Cut",                "cut",                  "regular"),
    0xE8C8: ("Copy",               "copy",                 "regular"),
    0xE8D2: ("Font",               "text_font",            "regular"),
    0xE8D6: ("Audio",              "music_note_2",         "regular"),
    0xE8DB: ("Italic",             "text_italic",          "regular"),
    0xE8DC: ("Underline",          "text_underline",       "regular"),
    0xE8DD: ("Bold",               "text_bold",            "regular"),
    0xE8DE: ("MoveToFolder",       "folder_arrow_right",   "regular"),
    0xE8E5: ("OpenFile",           "open",                 "regular"),
    0xE8F4: ("NewFolder",          "folder_add",           "regular"),
    0xE8FD: ("BulletedList",       "text_bullet_list_ltr", "regular"),
    0xE945: ("LightningBolt",      "flash",                "regular"),
    0xE946: ("Info",               "info",                 "regular"),
    0xE9D9: ("Diagnostic",         "pulse",                "regular"),
    0xEA39: ("ErrorBadge",         "error_circle",         "regular"),
    0xEA8F: ("Ringer",             "alert",                "regular"),
    0xEB52: ("HeartFill",          "heart",                "filled"),
    0xEB6C: ("(pool, Booking)",    "swimming_pool",        "regular"),   # not in Microsoft's table; Booking's amenity row
    0xEB9F: ("Photo2",             "image",                "regular"),
    0xEC32: ("Cafe",               "drink_coffee",         "regular"),
    0xECAD: ("Calories",           "food",                 "regular"),
    0xED1A: ("Hide",               "eye_off",              "regular"),
}

FAMILY = "ChronoUI Icons"


def main(src, out):
    fonts, maps = {}, {}
    for style, tag in (("regular", "Regular"), ("filled", "Filled")):
        fonts[style] = TTFont(os.path.join(src, f"FluentSystemIcons-{tag}.ttf"))
        maps[style] = json.load(open(os.path.join(src, f"FluentSystemIcons-{tag}.json")))

    # Glyph name of every requested icon in its source font.
    picks = {}
    for cp, (mdl2, fluent, style) in ICONS.items():
        key = f"ic_fluent_{fluent}_{SIZE}_{style}"
        if key not in maps[style]:
            sys.exit(f"missing {key} for {mdl2} (U+{cp:04X})")
        picks[cp] = (style, fonts[style].getBestCmap()[maps[style][key]])

    # Cut the regular font down to its picked glyphs...
    base = fonts["regular"]
    keep = sorted({g for s, g in picks.values() if s == "regular"})
    opts = subset.Options()
    opts.layout_features = []
    opts.notdef_outline = True
    opts.name_IDs = ["*"]
    opts.glyph_names = True
    sub = subset.Subsetter(opts)
    sub.populate(glyphs=keep)
    sub.subset(base)

    # ...copy the filled ones in (simple glyphs, same em)...
    filled = fonts["filled"]
    order = base.getGlyphOrder()   # glyf.__setitem__ below appends to this same list
    for cp, (style, g) in picks.items():
        if style != "filled":
            continue
        name = f"{g}.filled"
        base["glyf"][name] = filled["glyf"][g]
        base["hmtx"][name] = filled["hmtx"][g]
        picks[cp] = ("regular", name)
    base.setGlyphOrder(order)
    base["glyf"].glyphOrder = order
    base["maxp"].numGlyphs = len(order)

    # ...and answer the MDL2 code points with them.
    for table in base["cmap"].tables:
        if table.isUnicode():
            table.cmap = {cp: g for cp, (s, g) in picks.items()}

    for rec in base["name"].names:
        if rec.nameID in (1, 16):
            rec.string = FAMILY
        elif rec.nameID == 4:
            rec.string = FAMILY + " Regular"
        elif rec.nameID == 6:
            rec.string = FAMILY.replace(" ", "") + "-Regular"
    base.save(out)
    print(f"{out}: {len(picks)} icons")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__ or "usage: make_icon_font.py <fluent fonts folder> [out.ttf]")
    here = os.path.dirname(os.path.abspath(__file__))
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, "..", "assets", "fonts", "ChronoUIIcons.ttf"))
