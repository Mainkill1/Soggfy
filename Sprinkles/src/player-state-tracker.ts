import Utils from "./utils";
import { Player, PlayerState, TrackInfo, SpotifyUtils, Platform } from "./spotify-apis";
import Resources from "./resources";
import config, { isTrackIgnored } from "./config";
import { PathTemplate, TemplatedSearchTree } from "./path-template";
import { Connection, MessageType } from "./connection";

export default class PlayerStateTracker {
    private playbacks = new Map<string, PlayerState>();
    private conn: Connection;

    constructor(conn: Connection) {
        this.conn = conn;

        let lastPlaybackId = null as string;
        Player.getEvents().addListener("update", ({ data }) => {
            if (data.playbackId !== lastPlaybackId && lastPlaybackId != null) {
                conn.send(MessageType.PLAYER_STATE, { event: "trackend", playbackId: lastPlaybackId });
            }
            lastPlaybackId = data.playbackId;

            if (!data.playbackId) return;

            if (!this.playbacks.has(data.playbackId)) {
                conn.send(MessageType.PLAYER_STATE, {
                    event: "trackstart",
                    playbackId: data.playbackId,
                    durationMs: Number(data.item?.metadata?.duration || 0)
                });
                conn.send(MessageType.DOWNLOAD_STATUS, { playbackId: data.playbackId, ignore: isTrackIgnored(data.item) });
            }
            this.playbacks.set(data.playbackId, data);
        });

        let queueStatusCache = new Map<string, boolean>();
        Player.getEvents().addListener("queue_update", async ({ data }) => {
            if ((config.skipDownloadedTracks && conn.isConnected) || config.skipIgnoredTracks) {
                this.skipIgnoredTracks(data, queueStatusCache);
            }
        });
        //Player stops sometimes when speed is too high (>= 30)
        Player.getEvents()._client.getError({}, err => {
            if (err.message === "playback_stuck" && err.data.playback_id === Player.getState().playbackId) {
                SpotifyUtils.resetCurrentTrack(false);
            }
        });

        // Force highest audio quality (4=very high, automatically clamped to 3 for free users)
        let settings = Platform.getSettingsAPI();
        settings.quality.streamingQuality.setValue(4);
        settings.quality.autoAdjustQuality.setValue(false);
    }

    /** Returns limited track metadata for a given playbackId, or null. */
    getTrackInfo(playbackId: string) {
        return this.playbacks.get(playbackId)?.item;
    }
    remove(playbackId: string) {
        return this.playbacks.delete(playbackId);
    }
    /** Returns complete metadata for a given playback id. */
    async getMetadata(playbackId: string) {
        let playback = this.playbacks.get(playbackId);

        // Server may request metadata for a playback before we even know
        // about it when playspeed is too high. Keep retrying for up to 10 secs.
        for (let i = 0; playback == null && i < 10; i++) {
            await new Promise(r => setTimeout(r, 1000));
            playback = this.playbacks.get(playbackId);
        }

        let track = playback.item;
        let type = Resources.getUriType(track.uri);

        let meta: any = type === "track"
            ? await this.getTrackMetaProps(track)
            : await this.getPodcastMetaProps(track);
        let paths = this.getSavePaths(type, meta, playback);
        
        let data = {
            type: type,
            playbackId: playback.playbackId,
            trackUri: track.uri,
            metadata: meta,
            trackPath: paths.track,
            coverPath: paths.cover,
            coverTempPath: track.metadata.image_xlarge_url.replaceAll(":", "_")
        };
        this.fixMetadata(track, data.metadata, config.outputFormat.ext || "ogg");
        return { info: data, coverData: new ArrayBuffer(0) };
    }
    private getSavePaths(type: string, meta: any, playback: PlayerState) {
        let template = config.savePaths[type] as string;
        if (!template) throw Error("Unknown track type " + type);

        let path = config.savePaths.basePath;
        if (!/[\/\\]$/.test(path)) path += '/';

        let vars = PathTemplate.getVarsFromMetadata(meta, playback);

        let coverPath = "";
        if (config.saveCoverArt) {
            let parts = template.split(/[\/\\]/);
            let albumDirIdx = parts.findIndex(d => d.includes("{album_name}"));
            let hasAlbumDir = albumDirIdx >= 0 && albumDirIdx + 1 < parts.length;

            if (hasAlbumDir) {
                let coverTemplate = parts.slice(0, albumDirIdx + 1).join('/') + "/cover.jpg";
                coverPath = path + PathTemplate.render(coverTemplate, vars);
            }
        }
        return {
            track: path + PathTemplate.render(template, vars),
            cover: coverPath,
            canvas: path + PathTemplate.render(config.savePaths.canvas, vars)
        };
    }
    private fixMetadata(track: TrackInfo, meta: any, format: string): any {
        //https://wiki.multimedia.cx/index.php?title=FFmpeg_Metadata
        //https://help.mp3tag.de/main_tags.html
        //https://github.com/yarrm80s/orpheusdl/blob/fed108e978083f92d96efcacdf09fe2dc8082bc3/orpheus/tagging.py

        if (["mp3", "mp4", "m4a"].includes(format)) {
            meta.track = `${meta.track}/${meta.totaltracks}`;
            meta.disc = `${meta.disc}/${meta.totaldiscs}`;
            delete meta.totaltracks;
            delete meta.totaldiscs;
        }
        let albumArtist = track.metadata.album_artist_name;
        if (albumArtist) {
            meta.album_artist = albumArtist;
        }
    }
    private async getTrackMetaProps(track: TrackInfo) {
        let meta = track.metadata;
        let date = meta["release_date"] || meta["album_release_date"] || meta["year"];
        
        return {
            title:          meta.title,
            album_artist:   meta.artist_name,
            album:          meta.album_title,
            artist:         meta.artist_name,
            track:          meta.album_track_number,
            totaltracks:    meta.album_track_count,
            disc:           meta.album_disc_number,
            totaldiscs:     meta.album_disc_count,
            date:           date,
            publisher:      meta["label"],
            language:       meta["language"],
            isrc:           meta["isrc"],
            comment:        Resources.getOpenTrackURL(track.uri),
            explicit:       meta.is_explicit ? "1" : undefined
        };
    }
    private async getPodcastMetaProps(track: TrackInfo) {
        let meta: any = track.metadata;

        return {
            title:          meta.title || meta.name,
            album:          meta.album_title || meta.show_name,
            album_artist:   meta.artist_name || meta.publisher,
            description:    meta.description,
            podcastdesc:    meta.show_description,
            podcasturl:     meta.external_url,
            publisher:      meta.publisher || meta.artist_name,
            date:           meta.release_date || meta.year,
            language:       meta.language,
            comment:        Resources.getOpenTrackURL(track.uri),
            podcast:        "1",
            explicit:       meta.explicit ? "1" : undefined
        };
    }
    private async getLyrics(track: TrackInfo) {
        let lyrics;

        try {
            if (track.metadata.has_lyrics === "true") {
                let resp = await Resources.getColorAndLyricsWG(track.uri, track.metadata.image_url);
                lyrics = resp.lyrics;
            }
        } catch (ex) {
            //has_lyrics seems to be wrong sometimes
            console.error("Failed to fetch lyrics for %s: %s", track.uri, ex);
        }
        if (!lyrics) return null;

        let isSynced = ["LINE_SYNCED", "SYLLABLE_SYNCED"].includes(lyrics.syncType);

        let text = "";
        for (let line of lyrics.lines) {
            //skip empty lines
            if (!isSynced && /^(|♪)$/.test(line.words)) continue;

            if (isSynced) {
                //https://en.wikipedia.org/wiki/LRC_(file_format)#Simple_format
                //https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/lrcdec.c#L88
                //(number of digits doesn't seem to matter)
                let time = parseInt(line.startTimeMs);
                let mm = Utils.padInt(time / 1000 / 60, 2);
                let ss = Utils.padInt(time / 1000 % 60, 2);
                let cs = Utils.padInt(time % 1000 / 10, 2);
                text += `[${mm}:${ss}.${cs}]`;
            }
            text += line.words;
            text += '\n';
        }
        return { text: text, rawData: lyrics, isSynced: isSynced };
    }

    private async skipIgnoredTracks(queue: any, statusCache: Map<string, boolean>) {
        if (config.skipDownloadedTracks) {
            let tree = new TemplatedSearchTree(config.savePaths.track);
            let queuedTracks = new Set<string>();
        
            for (let track of queue.nextUp) {
                if (!statusCache.has(track.uri)) {
                    statusCache.set(track.uri, false);

                    let vars = {
                        track_name: track.name,
                        artist_name: track.artists[0].name,
                        all_artist_names: track.artists.map(v => v.name).join(", "),
                        album_name: track.album.name
                    };
                    tree.add(track.uri, vars);
                }
                queuedTracks.add(track.uri);
            }
            //remove junk from cache
            for (let track of statusCache.keys()) {
                if (!queuedTracks.has(track)) {
                    statusCache.delete(track);
                }
            }
            if (!tree.isEmpty) {
                let statusResp = await this.conn.request(MessageType.DOWNLOAD_STATUS, {
                    searchTree: tree.root,
                    basePath: config.savePaths.basePath
                });
                for (let track in statusResp.results) {
                    statusCache.set(track, true);
                }
            }
        }
        let tracksToRemove = queue.nextUp.filter(v => isTrackIgnored(v) || statusCache.get(v.uri) === true);
        if (tracksToRemove.length > 0) {
            await Player.removeFromQueue(tracksToRemove);
        }
    }
}
