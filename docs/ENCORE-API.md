# What Encore Studio needs from Encore

Encore Studio (the desktop DAW) can already make a release package for Encore
(File > Export > Send to Encore). Today the artist uploads that package by hand
on encoremusic.studio. This page is the spec for the Encore side, so Encore Studio
can sign in and upload in one click instead.

It's written for whoever builds Encore's backend. Nothing here exists yet on
Encore's side.

## 1. Sign in from a desktop app

Encore Studio is open source, so it can't hold a client secret. Use the standard
flow for desktop apps: **OAuth 2.0 authorization code with PKCE and a loopback
redirect** (RFC 8252).

1. Encore Studio opens the browser at
   `https://encoremusic.studio/oauth/authorize?client_id=basement&response_type=code&code_challenge=...&code_challenge_method=S256&redirect_uri=http://127.0.0.1:<port>/callback&scope=releases:write&state=...`
2. The artist signs in on Encore as usual (existing accounts, existing 2FA) and
   approves "Encore: Studio wants to upload releases to your account".
3. Encore redirects to the loopback address with `?code=...&state=...`.
4. Encore Studio exchanges the code at `POST /oauth/token` (with the PKCE verifier)
   and gets an access token plus a refresh token.

Needs on Encore's side:

- A public client `basement` with loopback redirects allowed on any port.
- A `releases:write` scope that can only create draft releases and upload files
  to them, nothing else on the account.
- Refresh tokens the artist can revoke from their Encore settings
  ("Connected apps").

## 2. Upload a release as a draft

All calls use `Authorization: Bearer <access token>`.

`POST /api/v1/releases` creates a draft. The body is the `release.json` that
Encore Studio already writes:

```json
{
  "format": "encore-release-1",
  "title": "And It Rained",
  "artist": "Sick City",
  "album": "",
  "genre": "Emo",
  "key": "A minor",
  "bpm": 92,
  "timeSignature": "4/4",
  "notes": "",
  "lengthSeconds": 213.4,
  "audio": "And It Rained.wav",
  "loudness": "-14 LUFS",
  "stems": ["stems/Kick.wav", "stems/Vocal.wav"],
  "cover": "cover.jpg",
  "madeWith": "Encore: Studio 0.4.0",
  "madeAt": "2026-10-28T19:02:11.000-04:00"
}
```

It returns `{ "id": "...", "uploads": [ { "name": "And It Rained.wav", "url": "<signed PUT URL>" }, ... ] }`,
with one signed upload URL per file named in the JSON.

- Encore Studio PUTs each file to its URL. A 24-bit WAV of a long song can be over
  100 MB, so the URLs should go straight to storage and allow resumable or
  multipart uploads.
- `POST /api/v1/releases/<id>/complete` tells Encore that every file is up.
  Encore checks them and replies with `{ "editUrl": "https://encoremusic.studio/..." }`.
- Encore Studio then opens `editUrl`, where the artist finishes the release on the
  site: pricing, credits, release date, publishing.

Encore Studio never publishes anything itself. It only creates drafts.

## 3. Errors Encore Studio shows the artist

Any non-2xx response should carry `{ "error": "<code>", "message": "<plain English>" }`.
Encore Studio shows `message` as it is, so it should read well to a musician,
for example "Your Encore plan allows 10 releases; you have 10".

## What's in the package today

The package Encore Studio makes (File > Export > Send to Encore) holds:

- `<title>.wav`: 24-bit at the song's sample rate. When the artist ticks the
  box, it's normalised to -14 LUFS with the master effects on (Encore Master).
- `stems/*.wav`: optional, one file per track or group, before the master effects.
- `cover.jpg` or `cover.png`: optional, whatever the artist picked.
- `release.json`: as above.
