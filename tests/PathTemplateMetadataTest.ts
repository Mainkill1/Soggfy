import { getReleaseYear } from "../Sprinkles/src/metadata-values";

if (getReleaseYear(undefined) !== "") {
    throw new Error("missing cached release dates must produce an empty path value");
}

if (getReleaseYear("2026-09-30") !== "2026") {
    throw new Error("complete cached release dates must produce their year");
}
