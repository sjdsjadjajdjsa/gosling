namespace player;

extern fn open() -> bool = "player_open";
extern fn update() = "player_update";
extern fn load(url: const u8[:]) = "player_load";
extern fn toggle() = "player_toggle";
extern fn seek(secs: f64) = "player_seek";
extern fn volume(v: f64) = "player_volume";
extern fn time() -> f64 = "player_time";
extern fn duration() -> f64 = "player_duration";
extern fn paused() -> bool = "player_paused";
extern fn idle() -> bool = "player_idle";
extern fn loading() -> bool = "player_loading";
extern fn failed() -> bool = "player_failed";
extern fn ended() -> bool = "player_ended";
extern fn title(out: u8[512]&) -> i32 = "player_title";
extern fn close() = "player_close";
