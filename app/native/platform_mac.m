#import <AppKit/AppKit.h>
#import <MediaPlayer/MediaPlayer.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include "native.h"

static atomic_int command;
static double command_value;
static char shown_key[1024], shown_art[1100];

void platform_paths(char *data, char *cache, size_t cap) {
    const char *forced = getenv("GOSLING_HOME");
    if (forced && *forced) {
        snprintf(data, cap, "%s", forced);
        snprintf(cache, cap, "%s/cache", forced);
        return;
    }
    const char *home = getenv("HOME");
    snprintf(data, cap, "%s/Library/Application Support/Gosling", home ? home : ".");
    snprintf(cache, cap, "%s/Library/Caches/Gosling", home ? home : ".");
}

bool platform_pick_folder(char *out, size_t cap) {
    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];
        panel.canChooseDirectories = YES;
        panel.canChooseFiles = NO;
        panel.allowsMultipleSelection = NO;
        panel.prompt = @"Add folder";
        panel.message = @"Choose a folder with music in it";
        if ([panel runModal] != NSModalResponseOK || !panel.URL) return false;
        snprintf(out, cap, "%s", panel.URL.path.fileSystemRepresentation);
        return true;
    }
}

static void on(MPRemoteCommand *c, int code) {
    c.enabled = YES;
    [c addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *e) {
        (void)e;
        command = code;
        ui_wake();
        return MPRemoteCommandHandlerStatusSuccess;
    }];
}

void np_init(void) {
    @autoreleasepool {
        MPRemoteCommandCenter *cc = [MPRemoteCommandCenter sharedCommandCenter];
        on(cc.togglePlayPauseCommand, 1);
        on(cc.playCommand, 2);
        on(cc.pauseCommand, 3);
        on(cc.nextTrackCommand, 4);
        on(cc.previousTrackCommand, 5);
        cc.changePlaybackPositionCommand.enabled = YES;
        [cc.changePlaybackPositionCommand addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *e) {
            command_value = ((MPChangePlaybackPositionCommandEvent *)e).positionTime;
            command = 6;
            ui_wake();
            return MPRemoteCommandHandlerStatusSuccess;
        }];
    }
}

void np_update(const track *t, double duration, double position, bool playing) {
    @autoreleasepool {
        MPNowPlayingInfoCenter *center = [MPNowPlayingInfoCenter defaultCenter];
        if (!t) {
            if (shown_key[0]) {
                center.nowPlayingInfo = nil;
                center.playbackState = MPNowPlayingPlaybackStateStopped;
                shown_key[0] = 0;
            }
            return;
        }
        NSMutableDictionary *info = [NSMutableDictionary dictionary];
        info[MPMediaItemPropertyTitle] = [NSString stringWithUTF8String:t->title[0] ? t->title : "Gosling"];
        if (t->artist[0]) info[MPMediaItemPropertyArtist] = [NSString stringWithUTF8String:t->artist];
        if (t->album[0]) info[MPMediaItemPropertyAlbumTitle] = [NSString stringWithUTF8String:t->album];
        info[MPMediaItemPropertyPlaybackDuration] = @(duration);
        info[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(position);
        info[MPNowPlayingInfoPropertyPlaybackRate] = @(playing ? 1.0 : 0.0);
        char path[1100];
        static MPMediaItemArtwork *artwork;
        if (strcmp(shown_key, t->key)) {
            artwork = nil;
            shown_art[0] = 0;
        }
        if (!artwork && art_file(t->thumb, path, sizeof path) && strcmp(path, shown_art)) {
            NSImage *img = [[NSImage alloc] initWithContentsOfFile:[NSString stringWithUTF8String:path]];
            if (img) {
                artwork = [[MPMediaItemArtwork alloc] initWithBoundsSize:img.size requestHandler:^NSImage *(CGSize size) { (void)size; return img; }];
                snprintf(shown_art, sizeof shown_art, "%s", path);
            }
        }
        if (artwork) info[MPMediaItemPropertyArtwork] = artwork;
        snprintf(shown_key, sizeof shown_key, "%s", t->key);
        center.nowPlayingInfo = info;
        center.playbackState = playing ? MPNowPlayingPlaybackStatePlaying : MPNowPlayingPlaybackStatePaused;
    }
}

int32_t np_command(double *value) {
    int c = atomic_exchange(&command, 0);
    if (c == 6) *value = command_value;
    return c;
}
