# Track data licence

This applies to the track data in [`data/tracks.json`](data/tracks.json), which the board reads as its track list. Track Rat's code is covered by [LICENSE](LICENSE), and the third-party libraries by [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).

## OpenStreetMap

The Canadian tracks in `data/tracks.json` are built from OpenStreetMap data. © OpenStreetMap contributors.

It is available under the [Open Database Licence (ODbL) 1.0](https://opendatacommons.org/licenses/odbl/1-0/). Details are at [openstreetmap.org/copyright](https://www.openstreetmap.org/copyright).

- The database stays under the ODbL. If you share a changed copy of `tracks.json` (for example, a new file of tracks you've made), share it under the ODbL too, and credit OpenStreetMap.
- The track names, locations and layout maps come from OpenStreetMap. The start/finish points are approximate. Check them before trusting lap times.

## How the file was made

- [`tools/fetch_canada_tracks.py`](tools/fetch_canada_tracks.py) fetches the motorsport tracks in Canada from the Overpass API, which serves OpenStreetMap data.
- [`tools/fill_track_locations.py`](tools/fill_track_locations.py) adds the town and province names. It uses Nominatim and Photon, both run on OpenStreetMap data.
- [`tools/fetch_track_map.py`](tools/fetch_track_map.py) adds a track's layout map from OpenStreetMap.

## Not from OpenStreetMap

- **Bench Test** is a built-in simulated circuit. It isn't in `tracks.json`, and it isn't based on any real track.
- **Your own tracks** are yours. That includes any track you learn or edit on your own unit, and the sessions and logs it records.
- **Seven circuits outside Canada** (Laguna Seca, Nurburgring GP, Silverstone, Circuit of the Americas, Spa-Francorchamps, Autodromo Nazionale Monza and Suzuka Circuit) were typed in by hand when the project started. They have a start/finish point and no map, and don't come from OpenStreetMap.
