export function getReleaseYear(date: unknown): string {
    return typeof date === "string" ? date.split("-")[0] : "";
}
