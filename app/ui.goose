namespace ui;

extern fn open(title: const u8[:], w: i32, h: i32, min_w: i32, min_h: i32) = "ui_open";
extern fn running() -> bool = "ui_running";
extern fn frame(bg: u32) = "ui_frame";
extern fn present() = "ui_present";
extern fn width() -> i32 = "ui_width";
extern fn height() -> i32 = "ui_height";
extern fn text(s: const u8[:], x: f32, y: f32, size: f32, color: u32) = "ui_text";
extern fn text_width(s: const u8[:], size: f32) -> f32 = "ui_text_width";
extern fn close() = "ui_close";
