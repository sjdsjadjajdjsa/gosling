import std;
import ui;
import player;
import search;
import lib;
import os;

let BG: u32 = 0x121214ff;
let BAR: u32 = 0x121214ff;
let SURFACE: u32 = 0x1c1c1fff;
let RAISED: u32 = 0x29292dff;
let TEXT: u32 = 0xf3f3f5ff;
let MUTED: u32 = 0xa2a2a8ff;
let FAINT: u32 = 0x6e6e74ff;
let ACCENT: u32 = 0xececeeff;
let ON_ACCENT: u32 = 0x111113ff;
let LOVE: u32 = 0xff5f7aff;
let SHADE: u32 = 0x00000066;

let PAD: f32 = 24.0;
let ROW: f32 = 56.0;
let BAR_H: f32 = 80.0;
let SIDE: f32 = 200.0;
let TOP: f32 = 80.0;
let HEAD: f32 = 64.0;
let MENU_W: f32 = 216.0;
let ITEM: f32 = 32.0;

var query: u8[..512];
var name: u8[..512];
var renaming: i64;
var scroll: f32;
var selected: i32 = -1;
var volume: f64 = 80.0;
var drag: i32;
var drag_value: f64;
var hand: bool;
var muted: bool;
var typing: bool;
var menu_row: i32 = -1;
var menu_x: f32;
var menu_y: f32;

fn hit(mx: f32, my: f32, x: f32, y: f32, w: f32, h: f32) -> bool {
    mx >= x && mx < x + w && my >= y && my < y + h
}

fn type_char(buf: u8[..512]&, c: i32) {
    let n = if c < 0x80 { 1 } else if c < 0x800 { 2 } else if c < 0x10000 { 3 } else { 4 };
    guard buf.len + n <= buf.cap;
    if n == 1 {
        buf.push(c as u8);
    } else if n == 2 {
        buf.push((0xC0 | (c >> 6)) as u8);
        buf.push((0x80 | (c & 0x3F)) as u8);
    } else if n == 3 {
        buf.push((0xE0 | (c >> 12)) as u8);
        buf.push((0x80 | ((c >> 6) & 0x3F)) as u8);
        buf.push((0x80 | (c & 0x3F)) as u8);
    } else {
        buf.push((0xF0 | (c >> 18)) as u8);
        buf.push((0x80 | ((c >> 12) & 0x3F)) as u8);
        buf.push((0x80 | ((c >> 6) & 0x3F)) as u8);
        buf.push((0x80 | (c & 0x3F)) as u8);
    }
}

fn backspace(buf: u8[..512]&) {
    while buf.len > 0 {
        let b = buf.pop();
        if (b & 0xC0) != 0x80 { break; }
    }
}

fn start() {
    var url: u8[1024] = [0; 1024];
    let n = lib::current_url(url);
    guard n > 0;
    player::load(url[..n]);
    lib::played();
}

fn play(i: i32) {
    lib::play_row(i);
    start();
}

fn play_path(target: const u8[:]) {
    lib::play_url(target);
    start();
}

fn next() {
    if lib::next() { start(); }
}

fn prev() {
    if player::time() > 3.0 && !player::idle() {
        player::seek(0.0);
    } else if lib::prev() {
        start();
    }
}

fn list_top() -> f32 {
    if lib::view() == lib::SEARCH { TOP } else { TOP + HEAD }
}

fn show(kind: i32, playlist: i64) {
    lib::show(kind, playlist);
    scroll = 0.0;
    selected = -1;
    menu_row = -1;
    renaming = 0;
}

fn start_rename(id: i64) {
    renaming = id;
    name.clear();
    for i: i32 in 0..lib::playlists() {
        if lib::playlist_id(i) == id {
            var buf: u8[256] = [0; 256];
            let n = lib::playlist_name(i, buf);
            name.append(buf[..n]);
        }
    }
}

fn finish_rename() {
    let n = trim(name);
    if n.len > 0 { lib::playlist_rename(renaming, n); }
    renaming = 0;
}

fn reveal(i: i32) {
    let view = ui::height() - BAR_H - list_top();
    let y = i * ROW;
    if y < scroll { scroll = y; }
    if y + ROW > scroll + view { scroll = y + ROW - view; }
}

fn submit() {
    let q = trim(query);
    guard q.len > 0;
    if starts_with(q, "http://") || starts_with(q, "https://") || q[0] == '/' {
        play_path(q);
    } else {
        show(lib::SEARCH, 0);
        search::start(q);
    }
}

fn dropped(path: const u8[:]) {
    if lib::is_dir(path) {
        lib::add_folder(path);
        show(lib::LOCAL, 0);
    } else {
        play_path(path);
    }
}

fn media() {
    let c = lib::np_command();
    if c == lib::NP_TOGGLE && !player::idle() { player::toggle(); }
    if c == lib::NP_PLAY { player::pause(false); }
    if c == lib::NP_PAUSE { player::pause(true); }
    if c == lib::NP_NEXT { next(); }
    if c == lib::NP_PREV { prev(); }
    if c == lib::NP_SEEK { player::seek(lib::np_value()); }
}

fn input() {
    loop {
        let c = ui::next_char();
        if c == 0 { break; }
        if renaming != 0 {
            type_char(name, c);
            continue;
        }
        if c == 32 && query.len == 0 { continue; }
        type_char(query, c);
        selected = -1;
    }
    if renaming != 0 {
        if ui::key(ui::KEY_BACKSPACE) { backspace(name); }
        if ui::key(ui::KEY_ESCAPE) { renaming = 0; }
        if ui::key(ui::KEY_ENTER) { finish_rename(); }
        return;
    }
    if ui::key(ui::KEY_BACKSPACE) {
        backspace(query);
        selected = -1;
    }
    if ui::key(ui::KEY_ESCAPE) {
        if menu_row >= 0 { menu_row = -1; } else { query.clear(); }
        selected = -1;
    }
    let n = lib::count();
    if ui::key(ui::KEY_DOWN) && n > 0 {
        selected = min(selected + 1, n - 1);
        reveal(selected);
    }
    if ui::key(ui::KEY_UP) && selected > 0 {
        selected--;
        reveal(selected);
    }
    if ui::key(ui::KEY_ENTER) {
        if selected >= 0 && selected < n && query.len == 0 { play(selected); } else if query.len > 0 { submit(); } else if selected >= 0 && selected < n { play(selected); }
    }
    if ui::command() && ui::key(ui::KEY_V) {
        var pasted: u8[1024] = [0; 1024];
        let pn = ui::paste(pasted);
        for b in pasted[..pn] {
            if b >= 32 && query.len < query.cap { query.push(b); }
        }
    }
    if ui::command() && ui::key(ui::KEY_RIGHT) { next(); }
    if ui::command() && ui::key(ui::KEY_LEFT) { prev(); }
    if query.len == 0 && !ui::command() {
        if ui::key(ui::KEY_SPACE) && !player::idle() { player::toggle(); }
        if ui::key(ui::KEY_LEFT) { player::seek(player::time() - 5.0); }
        if ui::key(ui::KEY_RIGHT) { player::seek(player::time() + 5.0); }
    }
    var drop: u8[1024] = [0; 1024];
    let dn = ui::dropped(drop);
    if dn > 0 { dropped(drop[..dn]); }
    if player::ended() { next(); }
}

fn fmt_time(out: u8[>..]&, secs: f64) {
    let s = if secs > 0.0 { secs as i64 } else { 0 };
    format(out, s / 60, ":");
    if s % 60 < 10 { out.push('0'); }
    format(out, s % 60);
}

fn heart_button(cx: f32, cy: f32, on: bool, color: u32) {
    ui::heart(cx, cy, 16.0, on, if on { LOVE } else { color });
}

fn button(label: const u8[:], x: f32, y: f32, primary: bool, mx: f32, my: f32, click: bool) -> f32, bool {
    let w = ui::text_width(label, 13.0) + 28.0;
    let over = hit(mx, my, x, y, w, 32.0);
    let f = ui::fade(50000 + (x as i32), over);
    ui::rect(x, y, w, 32.0, 16.0, if primary { ui::mix(ACCENT, 0xffffffff, f) } else { ui::mix(SURFACE, RAISED, f) });
    ui::text(label, x + 14.0, y + 8.0, 13.0, if primary { ON_ACCENT } else { TEXT });
    if over { hand = true; }
    return w, over && click;
}

fn message(text: const u8[:], x: f32, w: f32, h: f32) {
    let top = list_top();
    let cy = top + (h - BAR_H - top) / 2.0 - 24.0;
    ui::icon(ui::I_BIRD, x + w / 2.0, cy - 30.0, 44.0, 0, RAISED);
    ui::text(text, x + (w - ui::text_width(text, 14.0)) / 2.0, cy + 6.0, 14.0, FAINT);
}

fn search_box(x0: f32, w: f32, mx: f32, my: f32, click: bool) {
    let x = x0 + PAD;
    let y = 20.0;
    let bw = min(w - PAD * 2.0, 560.0);
    let bh = 40.0;
    if hit(mx, my, x, y, bw, bh) {
        typing = true;
        if click && renaming != 0 { finish_rename(); }
    }
    ui::rect(x, y, bw, bh, 20.0, SURFACE);
    ui::icon(ui::I_SEARCH, x + 22.0, y + 20.0, 18.0, 0, FAINT);
    let tx = x + 44.0;
    let room = bw - 60.0;
    let caret = renaming == 0;
    if query.len == 0 {
        ui::text("Search songs, or paste a link", tx, y + 11.0, 15.0, FAINT);
        if caret { ui::rect(tx - 1.0, y + 10.0, 2.0, 20.0, 1.0, MUTED); }
    } else {
        ui::text_fit(query, tx, y + 11.0, 15.0, TEXT, room);
        let cw = min(ui::text_width(query, 15.0), room);
        if caret { ui::rect(tx + cw + 1.0, y + 10.0, 2.0, 20.0, 1.0, MUTED); }
    }
}

fn nav(label: const u8[:], icon: i32, y: f32, on: bool, mx: f32, my: f32, click: bool) -> bool {
    let x = 12.0;
    let w = SIDE - 24.0;
    let over = hit(mx, my, x, y, w, ITEM);
    let f = ui::fade(60000 + (y as i32), over);
    if on {
        ui::rect(x, y, w, ITEM, 8.0, SURFACE);
    } else if f > 0.01 {
        ui::rect(x, y, w, ITEM, 8.0, ui::mix(BG, SURFACE, f * 0.6));
    }
    let color = if on { TEXT } else { ui::mix(MUTED, TEXT, f) };
    var tx: f32 = x + 12.0;
    if icon >= -1 {
        let ix = x + 20.0;
        let iy = y + ITEM / 2.0;
        if icon == -1 { ui::heart(ix, iy + 0.5, 15.0, false, color); } else { ui::icon(icon, ix, iy, 18.0, 0, color); }
        tx = x + 38.0;
    }
    ui::text_fit(label, tx, y + 8.0, 13.0, color, w - (tx - x) - 8.0);
    if over { hand = true; }
    over && click
}

fn sidebar(h: f32, mx: f32, my: f32, click: bool) {
    ui::rect(0.0, 0.0, SIDE, h - BAR_H, 0.0, BAR);
    let v = lib::view();
    var y: f32 = TOP;
    if nav("Search", ui::I_SEARCH, y, v == lib::SEARCH, mx, my, click) { show(lib::SEARCH, 0); }
    y += ITEM + 2.0;
    if nav("Liked songs", -1, y, v == lib::LIKED, mx, my, click) { show(lib::LIKED, 0); }
    y += ITEM + 2.0;
    if nav("History", ui::I_CLOCK, y, v == lib::HISTORY, mx, my, click) { show(lib::HISTORY, 0); }
    y += ITEM + 2.0;
    if nav("Local files", ui::I_FOLDER, y, v == lib::LOCAL, mx, my, click) { show(lib::LOCAL, 0); }
    y += ITEM + 2.0;
    if nav("Up next", ui::I_LIST, y, v == lib::QUEUE, mx, my, click) { show(lib::QUEUE, 0); }
    y += ITEM + 22.0;
    ui::text("Playlists", 24.0, y, 12.0, FAINT);
    let px = SIDE - 36.0;
    let over_new = hit(mx, my, px - 4.0, y - 8.0, 28.0, 28.0);
    ui::icon(ui::I_PLUS, px + 10.0, y + 7.0, 16.0, 0, if over_new { TEXT } else { FAINT });
    if over_new {
        hand = true;
        if click {
            let id = lib::playlist_new();
            show(lib::PLAYLIST, id);
            start_rename(id);
        }
    }
    y += 24.0;
    let list_bottom = if lib::discord_available() { h - BAR_H - ITEM - 16.0 } else { h - BAR_H };
    if lib::discord_available() {
        let dy = h - BAR_H - ITEM - 12.0;
        let on = lib::discord_on();
        let over = hit(mx, my, 12.0, dy, SIDE - 24.0, ITEM);
        if over { hand = true; }
        ui::text("Discord status", 24.0, dy + 9.0, 13.0, if over { TEXT } else { MUTED });
        let sx = SIDE - 52.0;
        let sy = dy + 8.0;
        ui::rect(sx, sy, 28.0, 16.0, 8.0, if on { ACCENT } else { RAISED });
        ui::circle(if on { sx + 20.0 } else { sx + 8.0 }, sy + 8.0, 6.0, if on { ON_ACCENT } else { MUTED });
        if over && click { lib::discord_toggle(); }
    }
    ui::clip(0.0, y, SIDE, list_bottom - y);
    var buf: u8[256] = [0; 256];
    for i: i32 in 0..lib::playlists() {
        let id = lib::playlist_id(i);
        let n = lib::playlist_name(i, buf);
        if nav(buf[..n], -2, y, v == lib::PLAYLIST && lib::view_playlist() == id, mx, my, click && my < h - BAR_H) { show(lib::PLAYLIST, id); }
        y += ITEM + 2.0;
    }
    ui::unclip();
}

fn header(x0: f32, w: f32, mx: f32, my: f32, click: bool) {
    let v = lib::view();
    guard v != lib::SEARCH;
    let x = x0 + PAD;
    let y = TOP + 4.0;
    var title: u8[>..] = [];
    var buf: u8[256] = [0; 256];
    if v == lib::LIKED { title.append("Liked songs"); }
    if v == lib::HISTORY { title.append("History"); }
    if v == lib::LOCAL { title.append("Local files"); }
    if v == lib::QUEUE { title.append("Up next"); }
    if v == lib::PLAYLIST {
        for i: i32 in 0..lib::playlists() {
            if lib::playlist_id(i) == lib::view_playlist() { title.append(buf[..lib::playlist_name(i, buf)]); }
        }
    }
    let editing = v == lib::PLAYLIST && renaming == lib::view_playlist();
    let shown = if editing { name[..] } else { title[..] };
    let tw = max(ui::text_width(shown, 20.0), 40.0);
    if editing {
        ui::rect(x - 8.0, y - 6.0, tw + 24.0, 36.0, 8.0, SURFACE);
        ui::rect(x + ui::text_width(shown, 20.0) + 2.0, y + 1.0, 2.0, 22.0, 1.0, MUTED);
        typing = true;
    }
    ui::text(shown, x, y, 20.0, TEXT);
    if v == lib::PLAYLIST && !editing && hit(mx, my, x, y, tw, 30.0) {
        typing = true;
        if click { start_rename(lib::view_playlist()); }
    }
    let n = lib::count();
    var count: u8[>..] = [];
    format(count, n, if n == 1 { " song" } else { " songs" });
    if v == lib::LOCAL && lib::scanning() > 0 { count.append("  ·  scanning…"); }
    if n > 0 || lib::scanning() > 0 { ui::text(count, x, y + 30.0, 12.0, FAINT); }

    var bx = x0 + w - PAD;
    if v == lib::PLAYLIST {
        let dw = ui::text_width("Delete", 13.0) + 28.0;
        bx -= dw;
        let _, del = button("Delete", bx, y + 4.0, false, mx, my, click);
        if del {
            lib::playlist_delete(lib::view_playlist());
            show(lib::LIKED, 0);
            return;
        }
        bx -= 8.0;
    }
    if v == lib::LOCAL {
        let aw = ui::text_width("Add folder", 13.0) + 28.0;
        bx -= aw;
        let _, add = button("Add folder", bx, y + 4.0, false, mx, my, click);
        if add { lib::pick_folder(); }
        bx -= 8.0;
    }
    if n > 0 && v != lib::QUEUE {
        let pw = ui::text_width("Play", 13.0) + 28.0;
        bx -= pw;
        let _, go = button("Play", bx, y + 4.0, true, mx, my, click);
        if go { play(0); }
    }
}

fn menu_items() -> i32 {
    let extra: i32 = if lib::view() == lib::PLAYLIST { 1 } else { 0 };
    2 + extra + lib::playlists()
}

fn menu_item(label: const u8[:], y: f32, mx: f32, my: f32, color: u32) -> bool {
    let over = hit(mx, my, menu_x, y, MENU_W, ITEM);
    if over {
        ui::rect(menu_x + 4.0, y, MENU_W - 8.0, ITEM, 6.0, RAISED);
        hand = true;
    }
    ui::text_fit(label, menu_x + 14.0, y + 9.0, 13.0, color, MENU_W - 28.0);
    over
}

fn menu(mx: f32, my: f32, click: bool) -> bool {
    guard menu_row >= 0 else { return false; }
    let mh = menu_items() * ITEM + 12.0;
    let inside = hit(mx, my, menu_x, menu_y, MENU_W, mh);
    ui::rect(menu_x + 2.0, menu_y + 4.0, MENU_W, mh, 10.0, SHADE);
    ui::rect(menu_x, menu_y, MENU_W, mh, 10.0, RAISED);
    ui::rect(menu_x + 1.0, menu_y + 1.0, MENU_W - 2.0, mh - 2.0, 9.0, SURFACE);
    var y = menu_y + 6.0;
    var chosen: i32 = -2;
    if menu_item("Play next", y, mx, my, TEXT) { chosen = -1; }
    y += ITEM;
    if lib::view() == lib::PLAYLIST {
        if menu_item("Remove from this playlist", y, mx, my, TEXT) { chosen = -3; }
        y += ITEM;
    }
    if menu_item("New playlist", y, mx, my, MUTED) { chosen = -4; }
    y += ITEM;
    var buf: u8[256] = [0; 256];
    for i: i32 in 0..lib::playlists() {
        var label: u8[>..] = [];
        label.append("Add to ");
        label.append(buf[..lib::playlist_name(i, buf)]);
        if menu_item(label, y, mx, my, TEXT) { chosen = i; }
        y += ITEM;
    }
    guard click else { return true; }
    let row = menu_row;
    menu_row = -1;
    if !inside { return true; }
    if chosen == -1 {
        let idle = !lib::has_current();
        lib::enqueue(row);
        if idle { next(); }
    }
    if chosen == -3 { lib::playlist_remove(row); }
    if chosen == -4 {
        let id = lib::playlist_new();
        lib::playlist_add(id, row);
        show(lib::PLAYLIST, id);
        start_rename(id);
    }
    if chosen >= 0 { lib::playlist_add(lib::playlist_id(chosen), row); }
    true
}

fn list(x0: f32, w: f32, h: f32, mx: f32, my: f32, click: bool) {
    let v = lib::view();
    let n = lib::count();
    let top = list_top();
    if v == lib::SEARCH {
        let state = search::state();
        if state == search::IDLE { message("Search for a song, or drop a music file or folder here.", x0, w, h); return; }
        if state == search::BUSY { message("Searching…", x0, w, h); return; }
        if state == search::FAILED { message("Couldn't reach YouTube Music. Check your connection.", x0, w, h); return; }
        if n == 0 { message("No songs found.", x0, w, h); return; }
    }
    if n == 0 {
        if v == lib::LIKED { message("Songs you like show up here. Tap the heart on any song.", x0, w, h); }
        if v == lib::HISTORY { message("Songs you play show up here.", x0, w, h); }
        if v == lib::QUEUE { message("Nothing up next. Use Play next on any song.", x0, w, h); }
        if v == lib::PLAYLIST { message("Add songs with the + on any song.", x0, w, h); }
        if v == lib::LOCAL {
            if lib::scanning() > 0 { message("Looking for music…", x0, w, h); }
            else { message("Add a folder, or drop one here, to play your own music.", x0, w, h); }
        }
        return;
    }
    let bottom = h - BAR_H;
    let view = bottom - top;
    if hit(mx, my, x0, top, w, view) { scroll -= ui::wheel() * 56.0; }
    scroll = clamp(scroll, 0.0, max(0.0, n * ROW + 12.0 - view));
    let x = x0 + PAD - 8.0;
    let rw = w - PAD * 2.0 + 16.0;
    var buf: u8[256] = [0; 256];
    let first = max(0, (scroll / ROW) as i32);
    ui::clip(x0, top, w, view);
    for i: i32 in first..n {
        let y = top + i * ROW - scroll;
        if y > bottom { break; }
        let over = hit(mx, my, x, y, rw, ROW) && my >= top && my < bottom && menu_row < 0;
        let current = lib::current_row(i);
        let f = ui::fade(1000 + i, over);
        let held = i == selected || i == menu_row;
        let t: f32 = if held { 1.0 } else { f };
        let bg = ui::mix(BG, SURFACE, t);
        if t > 0.01 { ui::rect(x, y + 2.0, rw, ROW - 4.0, 10.0, bg); }
        let lx = x + 22.0;
        let cy0 = y + ROW / 2.0;
        if current && !player::paused() && !player::idle() {
            ui::icon(ui::I_BARS, lx, cy0, 16.0, 0, ACCENT);
        } else if over {
            ui::icon(ui::I_PLAY, lx, cy0, 14.0, 0, TEXT);
        } else {
            var num: u8[>..] = [];
            format(num, i + 1);
            ui::text(num, lx - ui::text_width(num, 12.0) / 2.0, cy0 - 7.0, 12.0, if current { ACCENT } else { FAINT });
        }
        lib::art(i, x + 44.0, y + 8.0, 40.0, RAISED, FAINT);
        let tx = x + 98.0;
        let room = rw - 98.0 - 150.0;
        let tn = lib::field(i, lib::TITLE, buf);
        ui::text_fit(buf[..tn], tx, y + 10.0, 15.0, if current { ACCENT } else { TEXT }, room);
        var sub: u8[>..] = [];
        let an = lib::field(i, lib::ARTIST, buf);
        sub.append(buf[..an]);
        let ln = lib::field(i, lib::ALBUM, buf);
        if ln > 0 {
            if sub.len > 0 { sub.append("  ·  "); }
            sub.append(buf[..ln]);
        }
        ui::text_fit(sub, tx, y + 31.0, 13.0, MUTED, room);
        var d: u8[>..] = [];
        let secs = lib::duration(i);
        if secs > 0.0 { fmt_time(d, secs); }
        ui::text(d, x + rw - 16.0 - ui::text_width(d, 12.0), y + 21.0, 12.0, FAINT);
        let liked = lib::liked(i);
        let hx = x + rw - 112.0;
        let mxb = x + rw - 80.0;
        let cy = y + ROW / 2.0;
        let over_heart = over && hit(mx, my, hx - 14.0, cy - 14.0, 28.0, 28.0);
        let over_more = over && hit(mx, my, mxb - 14.0, cy - 14.0, 28.0, 28.0);
        if liked { heart_button(hx, cy, true, LOVE); } else if t > 0.01 { heart_button(hx, cy, false, ui::mix(bg, if over_heart { TEXT } else { MUTED }, t)); }
        if t > 0.01 { ui::icon(ui::I_PLUS, mxb, cy, 18.0, 0, ui::mix(bg, if over_more || i == menu_row { TEXT } else { MUTED }, t)); }
        if over {
            hand = true;
            if click {
                if over_heart {
                    lib::like(i);
                } else if over_more {
                    menu_row = i;
                    menu_x = min(mxb - MENU_W + 14.0, x0 + w - MENU_W - 8.0);
                    let mh = menu_items() * ITEM + 12.0;
                    menu_y = if cy + 18.0 + mh > bottom { cy - 18.0 - mh } else { cy + 18.0 };
                } else {
                    play(i);
                }
            }
        }
    }
    ui::unclip();
}

fn bar(w: f32, h: f32, mx: f32, my: f32, click: bool) {
    let y = h - BAR_H;
    let cy = y + BAR_H / 2.0;
    ui::rect(0.0, y, w, BAR_H, 0.0, BAR);
    let active = !player::idle() || player::loading();
    let has = lib::has_current();

    lib::current_art(PAD, cy - 26.0, 52.0, RAISED, FAINT);
    let tx = PAD + 66.0;
    let tw = max(140.0, w * 0.2);
    var title: u8[512] = [0; 512];
    var buf: u8[256] = [0; 256];
    let ctn = lib::current_field(lib::TITLE, buf);
    if ctn == 0 && player::title(title) > 0 && !player::loading() {
        lib::current_title(title[..player::title(title)]);
    }
    let shown = if ctn > 0 { buf[..ctn] } else if player::loading() { "Opening link…" } else { title[..player::title(title)] };
    var artist: u8[256] = [0; 256];
    let artn = lib::current_field(lib::ARTIST, artist);
    let sub = if player::failed() { "Couldn't play this song" } else { artist[..artn] };
    if !has || (!active && !player::failed() && shown.len == 0) {
        ui::text("Nothing playing", tx, cy - 9.0, 15.0, FAINT);
    } else if sub.len > 0 {
        ui::text_fit(shown, tx, cy - 19.0, 15.0, TEXT, tw);
        ui::text_fit(sub, tx, cy + 3.0, 13.0, if player::failed() { LOVE } else { MUTED }, tw);
    } else {
        ui::text_fit(shown, tx, cy - 9.0, 15.0, TEXT, tw);
    }

    let hx = tx + tw + 22.0;
    let can_like = has && ctn > 0;
    if can_like {
        let over_heart = hit(mx, my, hx - 14.0, cy - 14.0, 28.0, 28.0);
        heart_button(hx, cy, lib::current_liked(), if over_heart { TEXT } else { MUTED });
        if over_heart {
            hand = true;
            if click { lib::current_like(); }
        }
    }

    let px = hx + 92.0;
    let prev_x = px - 44.0;
    let next_x = px + 44.0;
    let can_prev = has && (lib::has_prev() || player::time() > 3.0);
    let can_next = lib::has_next();
    let over_prev = can_prev && hit(mx, my, prev_x - 16.0, cy - 16.0, 32.0, 32.0);
    let over_next = can_next && hit(mx, my, next_x - 16.0, cy - 16.0, 32.0, 32.0);
    let fp = ui::fade(70001, over_prev);
    let fn_ = ui::fade(70002, over_next);
    ui::icon(ui::I_PREV, prev_x, cy, 22.0, 0, if can_prev { ui::mix(MUTED, TEXT, fp) } else { RAISED });
    ui::icon(ui::I_NEXT, next_x, cy, 22.0, 0, if can_next { ui::mix(MUTED, TEXT, fn_) } else { RAISED });
    if over_prev || over_next { hand = true; }
    if over_prev && click { prev(); }
    if over_next && click { next(); }

    let over_play = active && hit(mx, my, px - 22.0, cy - 22.0, 44.0, 44.0);
    let fpl = ui::fade(70003, over_play);
    ui::circle(px, cy, 20.0 + fpl, if active { ui::mix(ACCENT, 0xffffffff, fpl) } else { RAISED });
    if over_play {
        hand = true;
        if click { player::toggle(); }
    }
    let icon = if active { ON_ACCENT } else { FAINT };
    if player::loading() {
        let turn = (ui::time() * 360.0) as f32;
        ui::ring(px, cy, 7.0, 9.5, turn, turn + 270.0, icon);
    } else if player::paused() || !active {
        ui::icon(ui::I_PLAY, px + 1.5, cy, 22.0, 0, icon);
    } else {
        ui::icon(ui::I_PAUSE, px, cy, 22.0, 0, icon);
    }

    let vol_w = 100.0;
    let vx = w - PAD - vol_w;
    let track_x = next_x + 72.0;
    let track_w = vx - 84.0 - track_x;
    let ready = active && !player::loading() && !player::failed();
    let dur = if ready { player::duration() } else { 0.0 };
    if track_w > 40.0 {
        let t = if drag == 1 { drag_value } else if ready { player::time() } else { 0.0 };
        if ready {
            var left: u8[>..] = [];
            fmt_time(left, t);
            var right: u8[>..] = [];
            fmt_time(right, dur);
            ui::text(left, track_x - 12.0 - ui::text_width(left, 12.0), cy - 7.0, 12.0, FAINT);
            ui::text(right, track_x + track_w + 12.0, cy - 7.0, 12.0, FAINT);
        }
        let f = if dur > 0.0 { clamp(t / dur, 0.0, 1.0) as f32 } else { 0.0 };
        ui::rect(track_x, cy - 2.0, track_w, 4.0, 2.0, RAISED);
        ui::rect(track_x, cy - 2.0, track_w * f, 4.0, 2.0, ACCENT);
        let over = dur > 0.0 && hit(mx, my, track_x - 6.0, cy - 12.0, track_w + 12.0, 24.0);
        if over || drag == 1 {
            ui::circle(track_x + track_w * f, cy, 6.0, TEXT);
            hand = true;
        }
        if over && click { drag = 1; }
        if drag == 1 { drag_value = clamp((mx - track_x) / track_w, 0.0, 1.0) * dur; }
    }

    let shown_vol = if muted { 0.0 } else { volume };
    let level: i32 = if shown_vol <= 0.0 { -1 } else if shown_vol < 34.0 { 1 } else if shown_vol < 67.0 { 2 } else { 3 };
    let sx = vx - 22.0;
    let over_speaker = hit(mx, my, sx - 14.0, cy - 14.0, 28.0, 28.0);
    let fs = ui::fade(70004, over_speaker);
    ui::icon(ui::I_SPEAKER, sx, cy, 20.0, level, ui::mix(MUTED, TEXT, fs));
    if over_speaker {
        hand = true;
        if click {
            muted = !muted;
            if !muted && volume <= 0.0 { volume = 50.0; }
            player::volume(if muted { 0.0 } else { volume });
        }
    }
    let vf = (shown_vol / 100.0) as f32;
    ui::rect(vx, cy - 2.0, vol_w, 4.0, 2.0, RAISED);
    ui::rect(vx, cy - 2.0, vol_w * vf, 4.0, 2.0, MUTED);
    let over_vol = hit(mx, my, vx - 6.0, cy - 12.0, vol_w + 12.0, 24.0);
    if over_vol || drag == 2 {
        ui::circle(vx + vol_w * vf, cy, 6.0, TEXT);
        hand = true;
    }
    if over_vol && click { drag = 2; }
    if drag == 2 {
        volume = clamp((mx - vx) / vol_w, 0.0, 1.0) as f64 * 100.0;
        muted = false;
        player::volume(volume);
    }
    if ui::mouse_released() {
        if drag == 1 { player::seek(drag_value); }
        if drag == 2 { lib::set("volume", volume); }
        drag = 0;
    }
}

fn draw() {
    let w = ui::width() as f32;
    let h = ui::height() as f32;
    let mx = ui::mouse_x();
    let my = ui::mouse_y();
    let click = ui::mouse_pressed();
    hand = false;
    typing = false;
    ui::anim_begin();
    ui::frame(BG);
    let mw = w - SIDE;
    search_box(SIDE, mw, mx, my, click && menu_row < 0);
    if click && menu_row < 0 && renaming != 0 && !hit(mx, my, SIDE + PAD, TOP, mw, HEAD) && !hit(mx, my, SIDE, 20.0, mw, 44.0) { finish_rename(); }
    let menu_click = click && menu_row >= 0;
    let list_click = click && menu_row < 0;
    header(SIDE, mw, mx, my, list_click);
    list(SIDE, mw, h, mx, my, list_click);
    sidebar(h, mx, my, list_click);
    bar(w, h, mx, my, list_click);
    menu(mx, my, menu_click);
    ui::cursor(if hand { ui::CURSOR_HAND } else if typing { ui::CURSOR_TEXT } else { ui::CURSOR_DEFAULT });
    ui::present();
}

fn tell_system() {
    lib::now_playing(player::duration(), player::time(), !player::idle() && !player::paused());
}

fn main() {
    ui::open("Gosling", 1240, 780, 820, 520);
    guard player::open() else {
        print("gosling: mpv did not start");
        ui::close();
        return;
    }
    search::init();
    lib::open();
    lib::np_init();
    lib::discord_init();
    volume = lib::get("volume", 80.0);
    player::volume(volume);
    if lib::folders() > 0 { lib::rescan(); }
    if arg_count() > 1 {
        var first: u8[>..] = [];
        arg(1, first);
        dropped(first);
    }
    var want: u8[>..] = [];
    if env("GOSLING_QUERY", want) && want.len > 0 {
        query.append(want[..min(want.len, query.cap)]);
        submit();
    }
    var autoplay = env("GOSLING_AUTOPLAY", want);
    var view_name: u8[>..] = [];
    if env("GOSLING_VIEW", view_name) {
        if view_name[..] == "liked" { show(lib::LIKED, 0); }
        if view_name[..] == "history" { show(lib::HISTORY, 0); }
        if view_name[..] == "local" { show(lib::LOCAL, 0); }
        if view_name[..] == "queue" { show(lib::QUEUE, 0); }
        if view_name[..] == "playlist" && lib::playlists() > 0 { show(lib::PLAYLIST, lib::playlist_id(0)); }
    }
    while ui::running() {
        if autoplay && (lib::view() != lib::SEARCH || search::state() == search::DONE) && lib::count() > 0 {
            autoplay = false;
            play(0);
        }
        player::update();
        lib::refresh();
        ui::input();
        media();
        input();
        if ui::visible() { draw(); }
        tell_system();
        let playing = !player::idle() && !player::paused();
        ui::wait(if drag != 0 { 0.0 } else if ui::animating() { 1.0 / 60.0 } else if player::loading() { 1.0 / 30.0 } else if search::state() == search::BUSY { 0.25 } else if playing && ui::visible() { 1.0 } else { -1.0 });
    }
    player::close();
    lib::close();
    ui::close();
}
