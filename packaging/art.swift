import AppKit

func color(_ hex: UInt32) -> CGColor {
    CGColor(red: CGFloat(hex >> 16 & 255) / 255, green: CGFloat(hex >> 8 & 255) / 255, blue: CGFloat(hex & 255) / 255, alpha: 1)
}

func render(_ w: Int, _ h: Int, _ path: String, _ draw: (CGContext) -> Void) {
    let ctx = CGContext(data: nil, width: w, height: h, bitsPerComponent: 8, bytesPerRow: 0, space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
    ctx.translateBy(x: 0, y: CGFloat(h))
    ctx.scaleBy(x: 1, y: -1)
    draw(ctx)
    let rep = NSBitmapImageRep(cgImage: ctx.makeImage()!)
    try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: path))
}

func bird(_ g: CGContext, x: CGFloat, y: CGFloat, size: CGFloat) {
    let k = size / 64
    g.saveGState()
    g.translateBy(x: x, y: y)
    g.scaleBy(x: k, y: k)
    let ink = color(0x2b2106)
    g.setLineJoin(.round)
    g.setFillColor(color(0xfff3c4))
    g.setStrokeColor(ink)
    g.setLineWidth(4)
    g.addEllipse(in: CGRect(x: 18, y: 14, width: 36, height: 36))
    g.drawPath(using: .fillStroke)
    g.move(to: CGPoint(x: 18, y: 30))
    g.addLine(to: CGPoint(x: 6, y: 34))
    g.addLine(to: CGPoint(x: 18, y: 38))
    g.closePath()
    g.setFillColor(color(0xe8762b))
    g.setLineWidth(3)
    g.drawPath(using: .fillStroke)
    g.setFillColor(ink)
    g.fillEllipse(in: CGRect(x: 27.5, y: 23.5, width: 7, height: 7))
    g.restoreGState()
}

let out = CommandLine.arguments[1]

render(1024, 1024, out + "/icon_1024.png") { g in
    let tile = CGRect(x: 100, y: 100, width: 824, height: 824)
    let shape = CGPath(roundedRect: tile, cornerWidth: 185, cornerHeight: 185, transform: nil)
    g.saveGState()
    g.setShadow(offset: CGSize(width: 0, height: 10), blur: 28, color: CGColor(gray: 0, alpha: 0.3))
    g.addPath(shape)
    g.setFillColor(color(0xffd84a))
    g.fillPath()
    g.restoreGState()
    bird(g, x: 100 + 824 * 0.1, y: 100 + 824 * 0.1, size: 824 * 0.8)
}

for scale in [1, 2] {
    let s = CGFloat(scale)
    render(640 * scale, 400 * scale, out + "/dmg_\(scale)x.png") { g in
        g.scaleBy(x: s, y: s)
        g.setFillColor(color(0xffd84a))
        g.fill(CGRect(x: 0, y: 0, width: 640, height: 400))
        let ink = color(0x2b2106)
        g.setStrokeColor(ink)
        g.setLineWidth(5)
        g.setLineCap(.round)
        g.setLineJoin(.round)
        g.move(to: CGPoint(x: 262, y: 190))
        g.addLine(to: CGPoint(x: 378, y: 190))
        g.move(to: CGPoint(x: 360, y: 174))
        g.addLine(to: CGPoint(x: 380, y: 190))
        g.addLine(to: CGPoint(x: 360, y: 206))
        g.strokePath()
        let text = NSAttributedString(string: "Drag Gosling into Applications", attributes: [
            .font: NSFont.systemFont(ofSize: 15, weight: .semibold),
            .foregroundColor: NSColor(cgColor: ink)!,
        ])
        NSGraphicsContext.saveGraphicsState()
        g.saveGState()
        g.translateBy(x: 0, y: 400)
        g.scaleBy(x: 1, y: -1)
        NSGraphicsContext.current = NSGraphicsContext(cgContext: g, flipped: false)
        let size = text.size()
        text.draw(at: NSPoint(x: (640 - size.width) / 2, y: 400 - 336))
        g.restoreGState()
        NSGraphicsContext.restoreGraphicsState()
    }
}
