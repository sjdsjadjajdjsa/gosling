import os

app = defines["app"]
files = [app]
symlinks = {"Applications": "/Applications"}
icon = "packaging/Gosling.icns"
format = "ULMO"
background = "packaging/dmg.tiff"
window_rect = ((200, 160), (640, 400))
default_view = "icon-view"
show_status_bar = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
icon_size = 112
text_size = 13
icon_locations = {os.path.basename(app): (170, 190), "Applications": (470, 190)}
