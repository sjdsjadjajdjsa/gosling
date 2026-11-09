namespace search;

let IDLE: i32 = 0;
let BUSY: i32 = 1;
let DONE: i32 = 2;
let FAILED: i32 = 3;

let TITLE: i32 = 0;
let ARTIST: i32 = 1;
let ALBUM: i32 = 2;
let ID: i32 = 3;

extern fn init() = "search_init";
extern fn start(query: const u8[:]) = "search_start";
extern fn state() -> i32 = "search_state";
extern fn count() -> i32 = "search_count";
extern fn field(i: i32, which: i32, out: u8[256]&) -> i32 = "search_field";
extern fn duration(i: i32) -> f64 = "search_duration";
