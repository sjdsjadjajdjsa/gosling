import std;
import ui;

fn main() {
    let background: u32 = 0x141416ff;
    let text: u32 = 0xf3f3f5ff;
    let faint: u32 = 0x6e6e74ff;
    ui::open("Gosling", 1240, 780, 940, 520);
    var frames = 0;
    while ui::running() {
        frames++;
        ui::frame(background);
        let w = ui::width() as f32;
        let h = ui::height() as f32;
        let title = "Gosling";
        ui::text(title, (w - ui::text_width(title, 28.0)) / 2.0, h / 2.0 - 18.0, 28.0, text);
        var info: u8[>..] = [];
        format(info, "frames drawn ", frames);
        ui::text(info, 16.0, h - 28.0, 12.0, faint);
        ui::present();
    }
    ui::close();
}
