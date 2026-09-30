async function getPlatform(): Promise<any> {
    let root: any = null;
    let queue: any[] = [];
    let seen = new Set<any>();
    let walked = 0;

    const own = (object: any, key: PropertyKey) => {
        try {
            let descriptor = Object.getOwnPropertyDescriptor(object, key);
            return descriptor && "value" in descriptor ? descriptor.value : undefined;
        } catch { return undefined; }
    };
    const isObject = (value: any) => value && typeof value === "object";
    const method = (object: any, key: PropertyKey) => {
        for (let depth = 0; isObject(object) && depth < 4; depth++, object = Object.getPrototypeOf(object)) {
            let value = own(object, key);
            if (typeof value === "function") return value;
        }
        return undefined;
    };

    function tryGet() {
        let main = document.querySelector("#main") as any;
        let key = main && Object.keys(main).find(k => k.startsWith("__reactContainer$"));
        let current = key ? own(main, key) : null;
        if (current !== root || !queue.length || walked >= 10000) {
            root = current;
            queue = current ? [current] : [];
            seen = new Set();
            walked = 0;
        }

        while (queue.length && walked < 10000) {
            let object = queue.pop();
            if (!isObject(object) || seen.has(object)) continue;
            seen.add(object);
            walked++;
            if (method(object, "getPlayerAPI")) return object;
            let getState = method(object, "getState");
            if (getState && method(object, "getEvents")) {
                try {
                    let state = getState.call(object);
                    if (state && (state.item || state.playbackId !== undefined)) {
                        let noOpSetting = { setValue() {} };
                        return {
                            getPlayerAPI: () => object,
                            getUserAPI: () => ({ getUser: async () => ({ username: "" }) }),
                            getSettingsAPI: () => ({ quality: {
                                streamingQuality: noOpSetting,
                                autoAdjustQuality: noOpSetting
                            } }),
                            getAdManagers: () => undefined,
                            getClipboardAPI: () => ({ copy: (value: string) => navigator.clipboard?.writeText(value) })
                        };
                    }
                } catch {}
            }

            try {
                let count = 0;
                for (let value of Map.prototype.values.call(object)) {
                    if (++count > 80 || queue.length >= 65536) break;
                    if (isObject(value)) queue.push(value);
                }
            } catch {}

            for (let key of Object.keys(object).slice(0, 120)) {
                let value = own(object, key);
                if (isObject(value) && queue.length < 65536) queue.push(value);
            }
        }
        return null;
    }
    function callback(resolve) {
        let apis = tryGet();
        if (apis) {
            resolve(apis);
            return;
        }
        setTimeout(callback, 50, resolve);
    }
    return new Promise(callback);
}

export const
    Platform = await getPlatform(),
    Player = Platform.getPlayerAPI() as PlayerAPI,
    CosmosAsync = Player._cosmos,
    WebAPI = Platform?.getAdManagers()?.hpto?.hptoApi?.webApi; //TODO: this api reports telemetry, is it a good idea to use it?

let user = await Platform.getUserAPI().getUser().catch(() => ({ username: "" }));

export class SpotifyUtils {
    /** Resets the current track (this method creates a new playback id) */
    static async resetCurrentTrack(preservePosition = true) {
        let state = Player.getState();
        let position = (Date.now() - state.timestamp) * state.speed + state.positionAsOfTimestamp;

        let queue = Player._queue;
        let queuedTracks = queue.getQueue().queued;

        let tracks = [{ uri: state.item.uri }];
        if (queuedTracks.length > 0) {
            await queue.insertIntoQueue(tracks, { before: queuedTracks[0] });
        } else {
            await queue.addToQueue(tracks);
        }
        await Player.skipToNext();

        if (preservePosition) {
            await Player.seekTo(position);
        }
    }
    static getLocalStorageItem(key: string, prependUsername = true) {
        if (prependUsername) {
            key = `${user.username}:${key}`;
        }
        return JSON.parse(localStorage.getItem(key));
    }
    static getPlaylistSortState(uri: string) {
        const SortOrders = {
            0: "NONE",
            1: "ASC",
            2: "DESC",
            3: "SECONDARY_ASC",
            4: "SECONDARY_DESC"
        };
        let state = this.getLocalStorageItem("sortedState")?.[uri];
        return !state ? undefined : {
            field: state.column as string,
            order: SortOrders[state.order] as string
        };
    }
}

export interface PlayerAPI {
    _cosmos: any;
    _events: any;
    _client: any;
    _queue: any;

    getEvents();
    getState(): PlayerState;
    skipToNext(): Promise<void>;
    skipToPrevious(): Promise<void>;
    seekTo(position: number): Promise<void>;

    removeFromQueue(tracks: { uri: string, uid?: string }[]): Promise<void>;
}
export interface PlayerState {
    playbackId: string;
    timestamp: number,
    positionAsOfTimestamp: number,
    speed: number,
    context: {
        uri: string;
        metadata: any;
    },
    item: TrackInfo;
    index: {
        pageURI?: any,
        pageIndex?: number,
        itemIndex?: number;
    }
}
export interface TrackInfo {
    type: "track" | "episode",
    uri: string,
    isLocal: boolean,
    isExplicit: boolean,
    is19PlusOnly: boolean,
    metadata: {
        album_title: string,
        duration: string,
        popularity: string,
        image_url: string,
        image_large_url: string,
        image_small_url: string,
        image_xlarge_url: string,
        context_uri: string,
        album_track_count: string,
        entity_uri: string,
        album_artist_name: string,
        album_disc_number: string,
        artist_uri: string,
        artist_name: string,
        album_disc_count: string,
        title: string,
        album_track_number: string,
        has_lyrics: string,
        album_uri: string,
        is_explicit?: string;
    }
}
