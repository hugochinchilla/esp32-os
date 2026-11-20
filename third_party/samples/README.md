# Sample pictures from outside the project

The firmware puts these photographs in C:\pictures at start-up, next to the
pictures that `tools/gen_pictures.py` draws. `tools/gen_photos.py` crops and
scales the originals kept here and encodes them with the project's own
writers; `tools/flash.py` copies the results to the board, and
`tools/gen_pictures.py embed core/src/fs/sample_pictures.cpp` puts them in
the PC programs and the web page.

| File here | On the board | Source | Status |
| --- | --- | --- | --- |
| `as08-14-2383.jpg` (1280x1280, "medium" size) | `earthrise.jpg`, 640x480 baseline JPEG | NASA Image and Video Library, nasa_id AS08-14-2383, "Apollo 8 Mission image, Earth over the horizon of the moon", taken by the Apollo 8 crew, 24 December 1968: https://images.nasa.gov/details/as08-14-2383 (file: https://images-assets.nasa.gov/image/as08-14-2383/as08-14-2383~medium.jpg) | Public domain (work of the U.S. Government) |
| `as17-148-22727.jpg` (1280x1280, "medium" size) | `blue marble.png`, 320x320 PNG | NASA Image and Video Library, nasa_id AS17-148-22727, "View of the Earth seen by the Apollo 17 crew traveling toward the moon", 7 December 1972: https://images.nasa.gov/details/as17-148-22727 (file: https://images-assets.nasa.gov/image/as17-148-22727/as17-148-22727~medium.jpg) | Public domain (work of the U.S. Government) |

The photographs that fill the screen (backlog item 85, picked by the owner
from fourteen candidates on 2026-11-20). Each is on the board twice, as
`<name> 800x600.jpg` and `<name> 640x480.jpg`, baseline JPEG, cropped to
4:3. All are from the NASA Image and Video Library, the "medium" size
(https://images-assets.nasa.gov/image/ID/ID~medium.jpg), fetched 2026-11-20;
the page of each is https://images.nasa.gov/details/ID with its nasa_id as
ID. Status of all seven: public domain (work of the U.S. Government); none
carries a copyright notice or a photographer's name in its record.

| File here | On the board | Source |
| --- | --- | --- |
| `s89e5400.jpg` (1280x853) | `mir over the ocean` | nasa_id s89e5400, "DTO 1118 - Survey of the Mir Space Station": Mir over clouds and water seen from the Space Shuttle Endeavour after undocking, STS-89, 29 January 1998 |
| `sts076-713-083.jpg` (1280x1274) | `mir and the horizon` | nasa_id sts076-713-083, "Mir Space Station views after undocking and flyaround by shuttle Atlantis": Mir against clouds over the South Pacific, from the aft flight deck of Atlantis, STS-76, March 1996 |
| `sts074-716-021.jpg` (1280x1280) | `mir over clouds` | nasa_id sts074-716-021, "Mir space station as seen from shuttle Atlantis": Mir with the Earth's horizon behind it, soon after the two separated, STS-74, 18 November 1995 |
| `as17-148-22727.jpg` (the same file as above) | `blue marble` | nasa_id AS17-148-22727, as above |
| `as17-148-22718.jpg` (1280x1280) | `africa and arabia` | nasa_id as17-148-22718, "View of Saudi Arabia and north eastern Africa from the Apollo 17 spacecraft", December 1972 |
| `as17-152-23272.jpg` (1280x1280) | `earth behind the moon` | nasa_id as17-152-23272, "Apollo 17, Earthrise over Moon": the crescent Earth above the lunar horizon, from lunar orbit, December 1972 |
| `iss020e024040.jpg` (1280x874) | `clouds from orbit` | nasa_id iss020e024040, "Earth observation taken by the Expedition 20 crew": the horizon and the atmosphere from the International Space Station, 22 July 2009 |

Licence statement, from NASA's Media Usage Guidelines
(https://www.nasa.gov/nasa-brand-center/images-and-media/, read 2026-11-16):
"NASA content - images, audio, video, and media files ... generally are not
subject to copyright in the United States." The guidelines ask that NASA be
acknowledged as the source, that the material not be used to imply NASA's
endorsement, and they exclude the NASA insignia and logotype (no
photograph here shows them). None is marked as third-party
copyrighted material. Source: NASA.

Not included: the Kodak Lossless True Color Image Suite. The page it is
usually taken from (https://r0k.us/graphics/kodak/) says only "It is my
understanding they have been released by the Eastman Kodak Company for
unrestricted usage"; I found no licence statement from Kodak itself, so its
status could not be confirmed.
