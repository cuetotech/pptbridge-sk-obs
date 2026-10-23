# PPTBridge Control - Operator Dock v1 (experimental Windows build)

This is a fork test build, not an upstream stable release. It includes the
Windows live-frame-hold patch and a native OBS operator dock. The dock does not
require a separate app, browser server, cloud account, Python installation, or
changes to your PowerPoint file. Build target: OBS Studio 32.2.2, Windows x64.
Other OBS/Qt versions have not been verified. Desktop PowerPoint is still needed
for PowerPoint Live Mode. No Qt runtime is installed over the one in OBS.

## Install and open

1. Close OBS. Back up the existing `obs-plugins/64bit/pptbridge-obs.dll` and
   `data/obs-plugins/pptbridge-obs` directory from your OBS installation.
2. Extract the entire installer artifact. Run `INSTALL.cmd` and approve elevation
   only for the OBS installation you intend to update. The installer can also be
   invoked with an explicit path: `INSTALL.cmd "D:\PortableOBS"`.
3. Open OBS, then **Docks > PPTBridge Control**. Dock it next to Program or float
   it on the operator monitor. It is not a scene source.
4. Choose the existing **PPTBridge SK Slide** source in the dock's source selector.
   The dock does not automatically switch targets when OBS scenes change.
5. For PowerPoint, start Live Mode using the existing source Properties controls.
   The dock intentionally does not start, restart, or stop a live show on opening.

## Operation

Click a thumbnail to select it PRIVATELY. Inspect its cached image and notes,
then press **Show selected** to send one direct jump to the running slideshow.
A double-click does not bypass that safety step. Previous and Next / animation
act on the explicitly selected presentation. **Back to last shown** returns to
the previous observed slide (including clicker changes); it toggles between the
last two observed slides, rather than navigating a persistent history stack.

Search matches existing slide titles and speaker notes, including Korean text.
It is not full-slide lyric OCR or automatic song detection. A selected slide
hidden by a search is cleared so it cannot be sent accidentally. Named
song/verse bookmarks are not included in v1.

Current, next, and selected images are CACHED previews, not live capture. Use the
OBS Program preview to verify actual audience output, animations and embedded
media. The dock displays the reported current index, capture attachment/hold
status, pending commands, errors, and whether the target deck is in Program.
Different sources using the same deck share one presentation; another scene is
not an independent rehearsal copy. Existing program-routed clickers keep their
existing behavior. OBS PPTBridge keyboard hotkeys are suppressed while the dock
has keyboard focus so typing in its search field cannot change slides.

Blackout remains available during an outstanding navigation command. Live-mode
navigation is disabled while PowerPoint is unavailable; it does not silently
navigate only the exported cache. Deck replacement/reload resets selection and
back history. Requests revalidate source UUID and deck asset generation.

During a brief capture interruption the prior patch holds the previous frame
for up to 2.5 seconds. This build changes the expired-hold behavior to draw opaque
black instead of exposing unrelated sources below the presentation. Capture
readiness is hook/dimension-based and is not proof of a fresh captured frame.
The precise cause of the original first-slide flash remains unconfirmed.

## Engineering / testing

The UI and backend are separate. The real Qt panel can be exercised against an
injected fake backend without PowerPoint. Tests cover private selection, exact
jumps, external navigation/back, stale identity, filtered selection, command
busy state, blackout, source removal and background thumbnail loading. Model
checks cover history/reset rules and navigation availability. These are not
proof of real OBS/PowerPoint end-to-end behavior.

Thumbnail decoding uses one worker, a bounded queue and a 24 MiB pixmap cache;
offscreen grid icons are released. Hidden panels stop polling and scheduling new
work. Cached assets are read without navigating or exporting the live deck.
All note/title labels are plain text. No network listener is added.

CI first commits the small, anchor-checked integration edits and then compiles
that exact source SHA on Windows using matching official OBS and Qt SDKs. The
read-only build job verifies feature markers, runs CTest, exercises the existing
installer against a fixture (including a locked-DLL failure), and uploads the
installer, checksums and test evidence. It does not have Microsoft PowerPoint.
Source and build identity are included in BUILD-INFO.txt and the OBS log. An
integration bootstrap script remains for reproducibility; after source
integration its normal execution is a no-op.

## Required private rehearsal before service

Use the real .ppt and a .pptx copy. Verify that selecting/searching thumbnails
never changes Program; Show selected jumps correctly; back, clicker and
animations work; blackout/restore works; a second deck is never controlled by
mistake; scene changes do not silently retarget the dock; capture recovery has
no title-slide flash; repeated dock open/close and OBS shutdown are clean.
Test Korean filenames/titles, missing assets, a long deck, Stop/Start and Reload,
then run a sustained rehearsal with the usual OBS encoding and audio workload.
None of these workstation acceptance checks is claimed complete by CI.

Do not include the operator monitor in a Display Capture sent to Program.
A native dock is not automatically broadcast, but a screen capture can still
show the entire OBS application.

## Rollback

Close OBS. Restore the backed-up DLL and matching data directory, or reinstall
the previous official/test package. Keep this experimental installer separate
from the known-working package. The installer rolls back failed activation;
that does not replace your backup for reverting after a successful install.

Source: https://github.com/cuetotech/pptbridge-sk-obs
See BUILD-INFO.txt for the exact source commit. Original upstream attribution
and license are preserved; see LICENSE.txt in the package.
