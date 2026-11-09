import std;
import ui;
import player;
import search;
import os;

let BG: u32 = 0x141416ff;
let BAR: u32 = 0x0f0f11ff;
let SURFACE: u32 = 0x1d1d20ff;
let RAISED: u32 = 0x2a2a2eff;
let BORDER: u32 = 0x232326ff;
let TEXT: u32 = 0xf3f3f5ff;
let MUTED: u32 = 0xa2a2a8ff;
let FAINT: u32 = 0x6e6e74ff;
let ACCENT: u32 = 0xececeeff;
let ON_ACCENT: u32 = 0x111113ff;

let PAD: f32 = 24.0;
let ROW: f32 = 56.0;
let BAR_H: f32 = 84.0;
let LIST_TOP: f32 = 84.0;

var query: u8[..512];
var scroll: f32;
var now_id: u8[..32];
var now_title: u8[..256];
var now_artist: u8[..256];
var now_index: i32 = -1;
var selected: i32 = -1;
var volume: f64 = 80.0;
var drag: i32;
var drag_value: f64;
var hand: bool;
var typing: bool;

fn hit(mx: f32, my: f32, x: f32, y: f32, w: f32, h: f32) -> bool {
    mx >= x && mx < x + w && my >= y && my < y + h
}

fn type_char(c: i32) {
    let n = if c < 0x80 { 1 } else if c < 0x800 { 2 } else if c < 0x10000 { 3 } else { 4 };
    guard query.len + n <= query.cap;
    if n == 1 {
        query.push(c as u8);
    } else if n == 2 {
        query.push((0xC0 | (c >> 6)) as u8);
        query.push((0x80 | (c & 0x3F)) as u8);
    } else if n == 3 {
        query.push((0xE0 | (c >> 12)) as u8);
        query.push((0x80 | ((c >> 6) & 0x3F)) as u8);
        query.push((0x80 | (c & 0x3F)) as u8);
    } else {
        query.push((0xF0 | (c >> 18)) as u8);
        query.push((0x80 | ((c >> 12) & 0x3F)) as u8);
        query.push((0x80 | ((c >> 6) & 0x3F)) as u8);
        query.push((0x80 | (c & 0x3F)) as u8);
    }
}

fn backspace() {
    while query.len > 0 {
        let b = query.pop();
        if (b & 0xC0) != 0x80 { break; }
    }
}

fn set_now(id: const u8[:], title: const u8[:], artist: const u8[:]) {
    now_id.clear();
    now_id.append(id[..min(id.len, now_id.cap)]);
    now_title.clear();
    now_title.append(title[..min(title.len, now_title.cap)]);
    now_artist.clear();
    now_artist.append(artist[..min(artist.len, now_artist.cap)]);
}

fn play(i: i32) {
    var id: u8[256] = [0; 256];
    let n = search::field(i, search::ID, id);
    guard n > 0;
    var title: u8[256] = [0; 256];
    var artist: u8[256] = [0; 256];
    let tn = search::field(i, search::TITLE, title);
    let an = search::field(i, search::ARTIST, artist);
    set_now(id[..n], title[..tn], artist[..an]);
    now_index = i;
    var url: u8[>..] = [];
    url.append("https://music.youtube.com/watch?v=");
    url.append(id[..n]);
    player::load(url);
}

fn play_path(target: const u8[:]) {
    set_now("", "", "");
    now_index = -1;
    player::load(target);
}

fn reveal(i: i32) {
    let view = ui::height() - BAR_H - LIST_TOP;
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
        search::start(q);
        scroll = 0.0;
        selected = -1;
    }
}

fn input() {
    loop {
        let c = ui::next_char();
        if c == 0 { break; }
        if c == 32 && query.len == 0 { continue; }
        type_char(c);
        selected = -1;
    }
    if ui::key(ui::KEY_BACKSPACE) {
        backspace();
        selected = -1;
    }
    if ui::key(ui::KEY_ESCAPE) {
        query.clear();
        selected = -1;
    }
    let n = search::count();
    if ui::key(ui::KEY_DOWN) && n > 0 {
        selected = min(selected + 1, n - 1);
        reveal(selected);
    }
    if ui::key(ui::KEY_UP) && selected > 0 {
        selected--;
        reveal(selected);
    }
    if ui::key(ui::KEY_ENTER) {
        if selected >= 0 && selected < n { play(selected); } else { submit(); }
    }
    if ui::command() && ui::key(ui::KEY_V) {
        var pasted: u8[1024] = [0; 1024];
        let n = ui::paste(pasted);
        for b in pasted[..n] {
            if b >= 32 && query.len < query.cap { query.push(b); }
        }
    }
    if query.len == 0 {
        if ui::key(ui::KEY_SPACE) && !player::idle() { player::toggle(); }
        if ui::key(ui::KEY_LEFT) { player::seek(player::time() - 5.0); }
        if ui::key(ui::KEY_RIGHT) { player::seek(player::time() + 5.0); }
    }
    var dropped: u8[1024] = [0; 1024];
    let dn = ui::dropped(dropped);
    if dn > 0 { play_path(dropped[..dn]); }
    if player::ended() && now_index >= 0 && now_index + 1 < search::count() { play(now_index + 1); }
}

fn fmt_time(out: u8[>..]&, secs: f64) {
    let s = if secs > 0.0 { secs as i64 } else { 0 };
    format(out, s / 60, ":");
    if s % 60 < 10 { out.push('0'); }
    format(out, s % 60);
}

fn message(text: const u8[:], w: f32, h: f32) {
    let y = LIST_TOP + (h - BAR_H - LIST_TOP) / 2.0 - 30.0;
    ui::text(text, (w - ui::text_width(text, 15.0)) / 2.0, y, 15.0, FAINT);
}

fn search_box(w: f32, mx: f32, my: f32) {
    let x = PAD;
    let y = 20.0;
    let bw = w - PAD * 2.0;
    let bh = 44.0;
    if hit(mx, my, x, y, bw, bh) { typing = true; }
    ui::rect(x, y, bw, bh, 12.0, SURFACE);
    ui::circle(x + 24.0, y + 21.0, 6.5, FAINT);
    ui::circle(x + 24.0, y + 21.0, 4.5, SURFACE);
    ui::triangle(x + 27.5, y + 25.0, x + 30.0, y + 29.5, x + 31.5, y + 27.5, FAINT);
    let tx = x + 44.0;
    let room = bw - 60.0;
    if query.len == 0 {
        ui::text("Search songs, or paste a link", tx, y + 13.0, 15.0, FAINT);
        ui::rect(tx - 1.0, y + 12.0, 2.0, 20.0, 1.0, MUTED);
    } else {
        ui::text_fit(query, tx, y + 13.0, 15.0, TEXT, room);
        let cw = min(ui::text_width(query, 15.0), room);
        ui::rect(tx + cw + 1.0, y + 12.0, 2.0, 20.0, 1.0, MUTED);
    }
}

fn list(w: f32, h: f32, mx: f32, my: f32, click: bool) {
    let state = search::state();
    let n = search::count();
    if state == search::IDLE { message("Search for a song, or drop a music file here.", w, h); return; }
    if state == search::BUSY { message("Searching…", w, h); return; }
    if state == search::FAILED { message("Couldn't reach YouTube Music. Check your connection.", w, h); return; }
    if n == 0 { message("No songs found.", w, h); return; }
    let bottom = h - BAR_H;
    let view = bottom - LIST_TOP;
    if hit(mx, my, 0.0, LIST_TOP, w, view) { scroll -= ui::wheel() * 56.0; }
    scroll = clamp(scroll, 0.0, max(0.0, n * ROW + 12.0 - view));
    let x = PAD - 8.0;
    let rw = w - x * 2.0;
    var buf: u8[256] = [0; 256];
    ui::clip(0.0, LIST_TOP, w, view);
    for i: i32 in 0..n {
        let y = LIST_TOP + i * ROW - scroll;
        if y + ROW < LIST_TOP || y > bottom { continue; }
        let over = hit(mx, my, x, y, rw, ROW) && my >= LIST_TOP && my < bottom;
        let idn = search::field(i, search::ID, buf);
        let current = idn > 0 && now_id.len > 0 && buf[..idn] == now_id[..];
        if over || current || i == selected { ui::rect(x, y + 2.0, rw, ROW - 4.0, 10.0, SURFACE); }
        if over {
            hand = true;
            if click { play(i); }
        }
        let tn = search::field(i, search::TITLE, buf);
        ui::text_fit(buf[..tn], PAD + 8.0, y + 10.0, 15.0, if current { ACCENT } else { TEXT }, rw * 0.7);
        var sub: u8[>..] = [];
        let an = search::field(i, search::ARTIST, buf);
        sub.append(buf[..an]);
        let ln = search::field(i, search::ALBUM, buf);
        if ln > 0 {
            sub.append("  ·  ");
            sub.append(buf[..ln]);
        }
        ui::text_fit(sub, PAD + 8.0, y + 31.0, 13.0, MUTED, rw * 0.7);
        var d: u8[>..] = [];
        fmt_time(d, search::duration(i));
        ui::text(d, x + rw - 16.0 - ui::text_width(d, 13.0), y + 21.0, 13.0, FAINT);
    }
    ui::unclip();
}

fn bar(w: f32, h: f32, mx: f32, my: f32, click: bool) {
    let y = h - BAR_H;
    let cy = y + BAR_H / 2.0;
    ui::rect(0.0, y, w, BAR_H, 0.0, BAR);
    ui::rect(0.0, y, w, 1.0, 0.0, BORDER);
    let active = !player::idle() || player::loading();

    let px = PAD + 22.0;
    ui::circle(px, cy, 22.0, if active { ACCENT } else { RAISED });
    if active && hit(mx, my, px - 22.0, cy - 22.0, 44.0, 44.0) {
        hand = true;
        if click { player::toggle(); }
    }
    let icon = if active { ON_ACCENT } else { FAINT };
    if player::loading() {
        let turn = (ui::time() * 360.0) as f32;
        ui::ring(px, cy, 7.0, 9.5, turn, turn + 270.0, icon);
    } else if player::paused() || !active {
        ui::triangle(px - 6.0, cy - 9.0, px - 6.0, cy + 9.0, px + 10.0, cy, icon);
    } else {
        ui::rect(px - 7.0, cy - 8.0, 5.0, 16.0, 1.5, icon);
        ui::rect(px + 2.0, cy - 8.0, 5.0, 16.0, 1.5, icon);
    }

    let tx = PAD + 60.0;
    let tw = max(140.0, w * 0.26);
    var title: u8[512] = [0; 512];
    let shown = if now_title.len > 0 { now_title[..] } else if player::loading() { "Opening link…" } else { title[..player::title(title)] };
    let sub = if player::failed() { "Couldn't play this song" } else { now_artist[..] };
    if !active && !player::failed() {
        ui::text("Nothing playing", tx, cy - 9.0, 15.0, FAINT);
    } else if sub.len > 0 {
        ui::text_fit(shown, tx, cy - 19.0, 15.0, TEXT, tw);
        ui::text_fit(sub, tx, cy + 3.0, 13.0, MUTED, tw);
    } else {
        ui::text_fit(shown, tx, cy - 9.0, 15.0, TEXT, tw);
    }

    let vol_w = 100.0;
    let vx = w - PAD - vol_w;
    let track_x = tx + tw + 80.0;
    let track_w = vx - 84.0 - track_x;
    let ready = active && !player::loading() && !player::failed();
    let dur = if ready { player::duration() } else { 0.0 };
    if track_w > 40.0 {
        let t = if drag == 1 { drag_value } else if ready { player::time() } else { 0.0 };
        var left: u8[>..] = [];
        fmt_time(left, t);
        var right: u8[>..] = [];
        fmt_time(right, dur);
        ui::text(left, track_x - 12.0 - ui::text_width(left, 12.0), cy - 7.0, 12.0, FAINT);
        ui::text(right, track_x + track_w + 12.0, cy - 7.0, 12.0, FAINT);
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

    ui::rect(vx - 26.0, cy - 4.0, 5.0, 8.0, 1.0, MUTED);
    ui::triangle(vx - 22.0, cy - 4.0, vx - 22.0, cy + 4.0, vx - 15.0, cy + 9.0, MUTED);
    ui::triangle(vx - 22.0, cy - 4.0, vx - 15.0, cy + 9.0, vx - 15.0, cy - 9.0, MUTED);
    let vf = (volume / 100.0) as f32;
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
        player::volume(volume);
    }
    if ui::mouse_released() {
        if drag == 1 { player::seek(drag_value); }
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
    ui::frame(BG);
    search_box(w, mx, my);
    list(w, h, mx, my, click);
    bar(w, h, mx, my, click);
    ui::cursor(if hand { ui::CURSOR_HAND } else if typing { ui::CURSOR_TEXT } else { ui::CURSOR_DEFAULT });
    ui::present();
}

fn main() {
    ui::open("Gosling", 1240, 780, 760, 480);
    guard player::open() else {
        print("gosling: mpv did not start");
        ui::close();
        return;
    }
    search::init();
    player::volume(volume);
    if arg_count() > 1 {
        var first: u8[>..] = [];
        arg(1, first);
        play_path(first);
    }
    while ui::running() {
        player::update();
        ui::input();
        input();
        draw();
        let playing = !player::idle() && !player::paused();
        ui::wait(if drag != 0 { 0.0 } else if player::loading() { 1.0 / 30.0 } else if search::state() == search::BUSY { 0.25 } else if playing { 0.5 } else { -1.0 });
    }
    player::close();
    ui::close();
}
