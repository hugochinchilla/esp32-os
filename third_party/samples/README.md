# Sample pictures from outside the project

The firmware puts these photographs in C:\pictures at start-up, next to the
pictures that `tools/gen_pictures.py` draws. `tools/gen_photos.py` crops and
scales the originals kept here and encodes them with the project's own
writers; `tools/gen_pictures.py embed core/src/fs/sample_pictures.cpp` puts
the results in the firmware.

| File here | On the board | Source | Status |
| --- | --- | --- | --- |
| `as08-14-2383.jpg` (1280x1280, "medium" size) | `earthrise.jpg`, 640x480 baseline JPEG | NASA Image and Video Library, nasa_id AS08-14-2383, "Apollo 8 Mission image, Earth over the horizon of the moon", taken by the Apollo 8 crew, 24 December 1968: https://images.nasa.gov/details/as08-14-2383 (file: https://images-assets.nasa.gov/image/as08-14-2383/as08-14-2383~medium.jpg) | Public domain (work of the U.S. Government) |
| `as17-148-22727.jpg` (1280x1280, "medium" size) | `blue marble.png`, 320x320 PNG | NASA Image and Video Library, nasa_id AS17-148-22727, "View of the Earth seen by the Apollo 17 crew traveling toward the moon", 7 December 1972: https://images.nasa.gov/details/as17-148-22727 (file: https://images-assets.nasa.gov/image/as17-148-22727/as17-148-22727~medium.jpg) | Public domain (work of the U.S. Government) |

Licence statement, from NASA's Media Usage Guidelines
(https://www.nasa.gov/nasa-brand-center/images-and-media/, read 2026-11-16):
"NASA content - images, audio, video, and media files ... generally are not
subject to copyright in the United States." The guidelines ask that NASA be
acknowledged as the source, that the material not be used to imply NASA's
endorsement, and they exclude the NASA insignia and logotype (neither
photograph shows them). Neither photograph is marked as third-party
copyrighted material. Source: NASA.

Not included: the Kodak Lossless True Color Image Suite. The page it is
usually taken from (https://r0k.us/graphics/kodak/) says only "It is my
understanding they have been released by the Eastman Kodak Company for
unrestricted usage"; I found no licence statement from Kodak itself, so its
status could not be confirmed.
